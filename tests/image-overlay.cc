/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <cstdlib>
#include <functional>
#include <string>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "exif.h"
#include "filedata.h"
#include "image.h"
#include "image-overlay.h"
#include "metadata.h"
#include "options.h"
#include "pixbuf-renderer.h"

namespace
{

class OsdWrapping : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		saved_overlay = options->image_overlay;
		overlay_saved = true;
		options->image_overlay.template_string = g_strdup("");
		options->image_overlay.font = g_strdup("Sans 12");
		options->image_overlay.x = 10;
		options->image_overlay.y = 10;
		directory = g_dir_make_tmp("geeqie-osd-wrapping-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image.png", nullptr);
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 4, 4);
		gdk_pixbuf_fill(pixels, 0x336699ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "png", nullptr, nullptr));
		file = FileData::new_simple(path);
		window = GTK_WINDOW(gtk_window_new());
		imd = image_new(FALSE);
		pr = PIXBUF_RENDERER(imd->pr);
		gtk_window_set_default_size(window, 400, 300);
		gtk_window_set_child(window, imd->widget);
		image_set_fd(imd, file);
		image_change_pixbuf(imd, pixels, 1.0, FALSE);
		// Supply a completed image synchronously instead of using ImageLoader.
		imd->unknown = FALSE;
		gtk_window_present(window);
		ASSERT_TRUE(wait([this]() { return gtk_widget_get_mapped(imd->pr) && pr->viewport_width > 0; }));
		resize(400);
	}

	void TearDown() override
	{
		if (window) gtk_window_destroy(window);
		if (file && file->modified_xmp) metadata_write_queue_remove(file);
		file.reset(nullptr);
		if (path) g_unlink(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
		if (overlay_saved)
			{
			g_free(options->image_overlay.font);
			g_free(options->image_overlay.template_string);
			options->image_overlay = saved_overlay;
			}
	}

	bool wait(const std::function<bool()> &predicate)
	{
		const gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
		do
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (predicate()) return true;
			g_usleep(1000);
			} while (g_get_monotonic_time() < deadline);
		return false;
	}

	void resize(gint width)
	{
		g_signal_emit_by_name(pr, "resize", width, 300);
		while (g_main_context_iteration(nullptr, FALSE)) {}
	}

	GdkPixbuf *overlay()
	{
		GdkPixbuf *pixels = nullptr;
		pr->renderer->overlay_get(pr->renderer, 1, &pixels, nullptr, nullptr);
		return pixels;
	}

	void show(const std::string &text)
	{
		g_free(options->image_overlay.template_string);
		options->image_overlay.template_string = g_strdup(text.c_str());
		image_osd_set(imd, OSD_SHOW_INFO);
		image_osd_update(imd);
		ASSERT_TRUE(wait([this]() { return overlay() != nullptr; }));
	}

	gint width()
	{
		return gdk_pixbuf_get_width(overlay()) / gtk_widget_get_scale_factor(imd->pr);
	}

	gint height()
	{
		return gdk_pixbuf_get_height(overlay()) / gtk_widget_get_scale_factor(imd->pr);
	}

	void expect_fits()
	{
		ASSERT_NE(overlay(), nullptr);
		EXPECT_LE(width(), pr->viewport_width - std::abs(options->image_overlay.x));
	}

	std::string paragraph()
	{
		std::string text;
		for (gint i = 0; i < 30; ++i) text += "A longer description with several words. ";
		return text;
	}

	ConfOptions::ImageOverlay saved_overlay{};
	bool overlay_saved = false;
	GtkWindow *window = nullptr;
	ImageWindow *imd = nullptr;
	PixbufRenderer *pr = nullptr;
	FileDataRef file{nullptr};
	gchar *directory = nullptr;
	gchar *path = nullptr;
};

TEST_F(OsdWrapping, DescriptionWrapsWithoutChangingMetadata)
{
	const auto description = paragraph();
	metadata_write_string(file, COMMENT_KEY, description.c_str());
	ASSERT_NO_FATAL_FAILURE(show("%description%"));
	expect_fits();
	EXPECT_GT(height(), 60);
	g_autofree gchar *stored = metadata_read_string(file, COMMENT_KEY, METADATA_PLAIN);
	EXPECT_STREQ(stored, description.c_str());
}

TEST_F(OsdWrapping, ShortTextStaysCompactAndExplicitLinesArePreserved)
{
	ASSERT_NO_FATAL_FAILURE(show("Short text"));
	const gint single_line = height();
	EXPECT_LT(width(), 200);
	ASSERT_NO_FATAL_FAILURE(show("Short text\nSecond line"));
	EXPECT_GT(height(), single_line);
	EXPECT_LT(width(), 200);
}

TEST_F(OsdWrapping, UnbrokenUnicodeAndMarkupTextWrap)
{
	std::string unicode;
	for (gint i = 0; i < 12; ++i) unicode += "日本語の説明文は単語間の空白がなくても折り返します。";
	for (const auto &text : {std::string(150, 'W'),
	                        std::string("<b>") + paragraph() + "</b>",
	                        unicode,
	                        std::string("A &lt;beautiful&gt; description &amp; more. ") + paragraph()})
		{
		SCOPED_TRACE(text);
		g_autoptr(PangoLayout) unwrapped = gtk_widget_create_pango_layout(imd->pr, nullptr);
		g_autoptr(PangoFontDescription) font = pango_font_description_from_string(options->image_overlay.font);
		pango_layout_set_font_description(unwrapped, font);
		pango_layout_set_markup(unwrapped, text.c_str(), -1);
		gint unwrapped_width;
		gint unwrapped_height;
		pango_layout_get_pixel_size(unwrapped, &unwrapped_width, &unwrapped_height);
		ASSERT_GT(unwrapped_width, pr->viewport_width);
		ASSERT_NO_FATAL_FAILURE(show(text));
		expect_fits();
		// Compare against this font's single line, including the overlay's padding.
		EXPECT_GT(height(), unwrapped_height + 10);
		}
}

TEST_F(OsdWrapping, ViewportResizeReflowsInBothDirections)
{
	ASSERT_NO_FATAL_FAILURE(show(paragraph()));
	const gint original_height = height();
	resize(220);
	expect_fits();
	EXPECT_GT(height(), original_height);
	resize(400);
	expect_fits();
	EXPECT_EQ(height(), original_height);
}

TEST_F(OsdWrapping, RightAnchoredOverlayRespectsItsMargin)
{
	options->image_overlay.x = -30;
	ASSERT_NO_FATAL_FAILURE(show(paragraph()));
	expect_fits();
}

TEST_F(OsdWrapping, StereoViewportReflowsWithoutChangingWindowSize)
{
	ASSERT_NO_FATAL_FAILURE(show(paragraph()));
	const gint original_height = height();
	const gint original_width = pr->viewport_width;
	pixbuf_renderer_stereo_set(pr, PR_STEREO_HORIZ);
	ASSERT_TRUE(wait([&]() { return pr->viewport_width == original_width / 2; }));
	expect_fits();
	EXPECT_GT(height(), original_height);
}

TEST_F(OsdWrapping, ExplicitTemplateTruncationIsPreserved)
{
	metadata_write_string(file, COMMENT_KEY, paragraph().c_str());
	ASSERT_NO_FATAL_FAILURE(show("%description:12%"));
	EXPECT_LT(width(), 220);
	EXPECT_LT(height(), 50);
}

TEST_F(OsdWrapping, HistogramFitsANarrowViewport)
{
	ASSERT_NO_FATAL_FAILURE(show("Short text"));
	image_osd_set(imd, static_cast<OsdShowFlags>(OSD_SHOW_INFO | OSD_SHOW_HISTOGRAM));
	ASSERT_TRUE(wait([this]() { return overlay() && height() > 140; }));
	resize(180);
	expect_fits();
	EXPECT_GT(height(), 140);
}

TEST_F(OsdWrapping, NoSpaceHidesOverlayAndResizeRestoresIt)
{
	ASSERT_NO_FATAL_FAILURE(show(paragraph()));
	resize(15);
	EXPECT_EQ(overlay(), nullptr);
	resize(400);
	ASSERT_TRUE(wait([this]() { return overlay() != nullptr; }));
	expect_fits();
}

} // namespace
