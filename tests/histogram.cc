/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <array>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "filedata.h"
#include "histogram.h"

namespace
{

std::array<guchar, 3> pixel_at(GdkPixbuf *pixbuf, gint x, gint y)
{
	const auto *pixel = gdk_pixbuf_read_pixels(pixbuf) + (y * gdk_pixbuf_get_rowstride(pixbuf)) + (x * 3);
	return {pixel[0], pixel[1], pixel[2]};
}

class HistogramDrawing : public testing::TestWithParam<HistogramMode>
{
protected:
	void SetUp() override
	{
		fd = FileData::new_simple("/histogram-test.png", &context);
		histogram.set_channel(HCHAN_MAX);
		histogram.set_mode(GetParam());
	}

	void set_source(guint32 color)
	{
		g_autoptr(GdkPixbuf) source = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
		gdk_pixbuf_fill(source, color);
		set_source(source);
	}

	void set_source(GdkPixbuf *source)
	{
		g_clear_pointer(&fd->histmap, histmap_free);
		g_set_object(&fd->pixbuf, source);
		ASSERT_TRUE(histmap_start_idle(fd));
		const auto deadline = g_get_monotonic_time() + (5 * G_TIME_SPAN_SECOND);
		while (!histmap_get(fd))
			{
			ASSERT_LT(g_get_monotonic_time(), deadline);
			g_main_context_iteration(nullptr, FALSE);
			}
	}

	void expect_outside_unchanged(GdkPixbuf *canvas, GdkRectangle rect)
	{
		for (gint y = 0; y < gdk_pixbuf_get_height(canvas); y++)
			{
			for (gint x = 0; x < gdk_pixbuf_get_width(canvas); x++)
				{
				if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height) continue;
				EXPECT_EQ(pixel_at(canvas, x, y), background);
				}
			}
	}

	FileDataContext context;
	FileDataRef fd{nullptr};
	Histogram histogram;
	const std::array<guchar, 3> background{17, 34, 51};
};

TEST_P(HistogramDrawing, SingleCountAndClippedPeaksFillTheOffsetRectangleHeight)
{
	for (guint32 color : {0x000000ffU, 0x808080ffU, 0xffffffffU})
		{
		SCOPED_TRACE(color);
		ASSERT_NO_FATAL_FAILURE(set_source(color));
		g_autoptr(GdkPixbuf) canvas = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 276, 25);
		gdk_pixbuf_fill(canvas, 0x112233ff);
		const GdkRectangle rect{7, 3, 256, 17};
		histogram.draw(histmap_get(fd), canvas, rect.x, rect.y, rect.width, rect.height);
		const gint peak_x = rect.x + (color >> 24);
		const std::array<guchar, 3> black{0, 0, 0};
		EXPECT_EQ(pixel_at(canvas, peak_x, rect.y), black);
		EXPECT_EQ(pixel_at(canvas, peak_x, rect.y + rect.height - 1), black);
		expect_outside_unchanged(canvas, rect);
		}
}

TEST_P(HistogramDrawing, NarrowAndSinglePixelAreasRemainVisible)
{
	ASSERT_NO_FATAL_FAILURE(set_source(0x808080ff));
	for (const auto &size : {std::array<int, 2>{1, 1}, std::array<int, 2>{1, 19},
	                        std::array<int, 2>{256, 1}, std::array<int, 2>{127, 5}})
		{
		g_autoptr(GdkPixbuf) canvas = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 276, 25);
		gdk_pixbuf_fill(canvas, 0x112233ff);
		const GdkRectangle rect{7, 3, size[0], size[1]};
		histogram.draw(histmap_get(fd), canvas, rect.x, rect.y, rect.width, rect.height);
		bool found_bar = false;
		const std::array<guchar, 3> black{0, 0, 0};
		for (gint y = rect.y; y < rect.y + rect.height; y++)
			{
			for (gint x = rect.x; x < rect.x + rect.width; x++)
				{
				if (pixel_at(canvas, x, y) == black) found_bar = true;
				}
			}
		EXPECT_TRUE(found_bar);
		expect_outside_unchanged(canvas, rect);
		}
}

TEST_P(HistogramDrawing, InvalidDimensionsLeaveTheDestinationUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(set_source(0x808080ff));
	g_autoptr(GdkPixbuf) canvas = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 12, 12);
	gdk_pixbuf_fill(canvas, 0x112233ff);
	for (const auto &size : {std::array<int, 2>{0, 5}, std::array<int, 2>{5, 0},
	                        std::array<int, 2>{-1, 5}, std::array<int, 2>{5, -1}})
		{
		histogram.draw(histmap_get(fd), canvas, 2, 2, size[0], size[1]);
		}
	histogram.draw(histmap_get(fd), nullptr, 0, 0, 5, 5);
	histogram.draw(nullptr, canvas, 0, 0, 5, 5);
	expect_outside_unchanged(canvas, {0, 0, 0, 0});
}

TEST_P(HistogramDrawing, ClipsPartiallyOffCanvasRectangles)
{
	ASSERT_NO_FATAL_FAILURE(set_source(0x808080ff));
	g_autoptr(GdkPixbuf) canvas = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 256, 20);
	gdk_pixbuf_fill(canvas, 0x112233ff);
	const GdkRectangle rect{-4, -2, 256, 17};
	histogram.draw(histmap_get(fd), canvas, rect.x, rect.y, rect.width, rect.height);
	const std::array<guchar, 3> black{0, 0, 0};
	EXPECT_EQ(pixel_at(canvas, rect.x + 128, 0), black);
	EXPECT_EQ(pixel_at(canvas, rect.x + 128, rect.y + rect.height - 1), black);
	expect_outside_unchanged(canvas, rect);
}

TEST_P(HistogramDrawing, ClippedPeaksAreLimitedToTheInteriorScaleInEveryChannel)
{
	g_autoptr(GdkPixbuf) source = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 6, 1);
	gdk_pixbuf_fill(source, 0x000000ff);
	auto *pixel = gdk_pixbuf_get_pixels(source) + 15;
	pixel[0] = pixel[1] = pixel[2] = 128;
	ASSERT_NO_FATAL_FAILURE(set_source(source));
	for (gint channel = HCHAN_R; channel < HCHAN_COUNT; channel++)
		{
		histogram.set_channel(channel);
		g_autoptr(GdkPixbuf) canvas = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 276, 25);
		gdk_pixbuf_fill(canvas, 0x112233ff);
		const GdkRectangle rect{7, 3, 256, 17};
		histogram.draw(histmap_get(fd), canvas, rect.x, rect.y, rect.width, rect.height);
		const std::array<guchar, 3> expected{static_cast<guchar>(channel == HCHAN_R ? 255 : 0),
		                                     static_cast<guchar>(channel == HCHAN_G ? 255 : 0),
		                                     static_cast<guchar>(channel == HCHAN_B ? 255 : 0)};
		// The existing line blender can round a color component down by one.
		for (guint component = 0; component < expected.size(); component++)
			{
			EXPECT_NEAR(pixel_at(canvas, rect.x, rect.y)[component], expected[component], 1);
			EXPECT_NEAR(pixel_at(canvas, rect.x, rect.y + rect.height - 1)[component], expected[component], 1);
			}
		expect_outside_unchanged(canvas, rect);
		}
}

INSTANTIATE_TEST_SUITE_P(Modes, HistogramDrawing, testing::Values(HMODE_LINEAR, HMODE_LOG));

} // namespace
