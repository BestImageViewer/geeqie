/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "exif.h"

#include <cstring>
#include <memory>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>

#include "gtest/gtest.h"

namespace
{

class JpegColorProfile : public testing::Test
{
protected:
	void SetUp() override
	{
		directory = g_dir_make_tmp("geeqie-icc-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image.jpg", nullptr);
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
		gdk_pixbuf_fill(pixels, 0x123456ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "jpeg", nullptr, nullptr));
		g_autofree gchar *contents = nullptr;
		gsize length;
		ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
		// A minimal EXIF directory also permits reading without Exiv2.
		const std::vector<guchar> header{
			0xff, 0xd8, 0xff, 0xe1, 0, 22,
			'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0,
			8, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		std::vector<guchar> image = header;
		image.insert(image.end(), contents + 2, contents + length);
		ASSERT_TRUE(g_file_set_contents(path, reinterpret_cast<const gchar *>(image.data()), image.size(), nullptr));
		metadata.reset(exif_read(path, nullptr, nullptr));
		ASSERT_NE(metadata, nullptr);
	}

	void TearDown() override
	{
		metadata.reset();
		if (path) g_unlink(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
	}

	void add_chunk(guchar number, guchar total, const std::vector<guchar> &payload)
	{
		const guint length = 16 + payload.size();
		const std::vector<guchar> header{
			0xff, 0xe2, static_cast<guchar>(length >> 8), static_cast<guchar>(length),
			'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0,
			number, total};
		jpeg.insert(jpeg.end(), header.begin(), header.end());
		jpeg.insert(jpeg.end(), payload.begin(), payload.end());
	}

	void expect_profile(const std::vector<guchar> &expected)
	{
		jpeg.insert(jpeg.end(), {0xff, 0xd9});
		ASSERT_TRUE(exif_jpeg_parse_color(metadata.get(), jpeg.data(), jpeg.size()));
		guint length = 0;
		g_autofree guchar *profile = exif_get_color_profile(metadata.get(), &length);
		ASSERT_NE(profile, nullptr);
		ASSERT_EQ(length, expected.size());
		EXPECT_EQ(memcmp(profile, expected.data(), length), 0);
	}

	void expect_rejected()
	{
		jpeg.insert(jpeg.end(), {0xff, 0xd9});
		EXPECT_FALSE(exif_jpeg_parse_color(metadata.get(), jpeg.data(), jpeg.size()));
		guint length = 0;
		g_autofree guchar *profile = exif_get_color_profile(metadata.get(), &length);
		EXPECT_EQ(profile, nullptr);
	}

	std::vector<guchar> jpeg{0xff, 0xd8};
	std::unique_ptr<ExifData, decltype(&exif_free)> metadata{nullptr, exif_free};
	gchar *directory = nullptr;
	gchar *path = nullptr;
};

TEST_F(JpegColorProfile, SingleSegment)
{
	add_chunk(1, 1, {0, 1, 0xfe, 0xff, 4});
	expect_profile({0, 1, 0xfe, 0xff, 4});
}

TEST_F(JpegColorProfile, MultipleSegmentsWithUnrelatedMarker)
{
	add_chunk(1, 3, {0, 1, 2});
	// An unrelated APP2 segment must be skipped without losing the file offset.
	jpeg.insert(jpeg.end(), {0xff, 0xe2, 0, 6, 't', 'e', 's', 't'});
	add_chunk(2, 3, {3, 4, 5, 6, 7});
	add_chunk(3, 3, {8, 9});
	expect_profile({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_F(JpegColorProfile, ReorderedSegments)
{
	add_chunk(3, 3, {8, 9});
	add_chunk(1, 3, {0, 1, 2});
	add_chunk(2, 3, {3, 4, 5, 6, 7});
	expect_profile({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_F(JpegColorProfile, MissingSegment)
{
	add_chunk(1, 3, {0, 1});
	add_chunk(3, 3, {4, 5});
	expect_rejected();
}

TEST_F(JpegColorProfile, DuplicateSegment)
{
	add_chunk(1, 2, {0, 1});
	add_chunk(1, 2, {2, 3});
	add_chunk(2, 2, {4, 5});
	expect_rejected();
}

TEST_F(JpegColorProfile, InconsistentSegmentCounts)
{
	add_chunk(1, 2, {0, 1});
	add_chunk(2, 3, {2, 3});
	expect_rejected();
}

} // namespace
