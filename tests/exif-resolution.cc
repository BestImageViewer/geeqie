/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "exif.h"

#include <cstdint>
#include <cstring>
#include <memory>

#include <config.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#if HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include "gtest/gtest.h"

namespace
{

class ImageResolution : public testing::Test
{
protected:
	void SetUp() override
	{
		directory = g_dir_make_tmp("geeqie-resolution-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image", nullptr);
	}

	void TearDown() override
	{
		if (path) g_unlink(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
	}

	void save(const char *format, bool density = true)
	{
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 4, 4);
		gdk_pixbuf_fill(pixels, 0x123456ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, format, nullptr, nullptr));
		if (!density) return;
		if (strcmp(format, "png") == 0)
			{
			/* 1x1 PNG with pHYs recording 11811 x 5906 pixels per meter. */
			gsize size;
			g_autofree guchar *contents = g_base64_decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAACXBIWXMAAC4jAAAXEgGVQubbAAAADElEQVR4nGNgYGAAAAAEAAH2FzhVAAAAAElFTkSuQmCC", &size);
			ASSERT_TRUE(g_file_set_contents(path, reinterpret_cast<const gchar *>(contents), size, nullptr));
			return;
			}
		g_autofree gchar *contents = nullptr;
		gsize size;
		ASSERT_TRUE(g_file_get_contents(path, &contents, &size, nullptr));
		ASSERT_GT(size, 20);
		ASSERT_EQ(memcmp(contents + 6, "JFIF\0", 5), 0);
		contents[13] = 1;
		contents[14] = 1;
		contents[15] = 44;
		contents[16] = 0;
		contents[17] = static_cast<gchar>(150);
		ASSERT_TRUE(g_file_set_contents(path, contents, size, nullptr));
	}

	void expect(const char *value)
	{
		std::unique_ptr<ExifData, decltype(&exif_free)> data(exif_read(path, nullptr, nullptr), exif_free);
		g_autofree gchar *resolution = data ? exif_get_data_as_text(data.get(), "formatted.Resolution") : nullptr;
		EXPECT_STREQ(resolution, value);
	}

	gchar *directory = nullptr;
	gchar *path = nullptr;
};

TEST_F(ImageResolution, JfifDensityPreservesBothAxes)
{
	save("jpeg");
	expect("300 x 150 (dot/inch)");
}

TEST_F(ImageResolution, PngDensityConvertsMetersToInches)
{
	save("png");
	expect("300 x 150 (dot/inch)");
}

TEST_F(ImageResolution, MissingDensityIsEmpty)
{
	save("png", false);
	expect(nullptr);
	save("jpeg", false);
	expect(nullptr);
}

TEST_F(ImageResolution, JfifCentimetersAndAspectRatio)
{
	save("jpeg");
	g_autofree gchar *contents = nullptr;
	gsize size;
	ASSERT_TRUE(g_file_get_contents(path, &contents, &size, nullptr));
	ASSERT_GT(size, 18);
	contents[13] = 2;
	ASSERT_TRUE(g_file_set_contents(path, contents, size, nullptr));
	expect("300 x 150 (dot/cm)");
	contents[13] = 0;
	ASSERT_TRUE(g_file_set_contents(path, contents, size, nullptr));
	expect(nullptr);
}

TEST_F(ImageResolution, PngAspectRatioAndZeroDensityAreEmpty)
{
	/* Valid 1x1 PNGs with pHYs: unitless 300:150, then zero horizontal density. */
	for (const char *encoded : {
	     "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAACXBIWXMAAAEsAAAAlgBsYMVXAAAADElEQVR4nGNgYGAAAAAEAAH2FzhVAAAAAElFTkSuQmCC",
	     "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAACXBIWXMAAAAAAAAuIwFmHwovAAAADElEQVR4nGNgYGAAAAAEAAH2FzhVAAAAAElFTkSuQmCC"})
		{
		gsize size;
		g_autofree guchar *contents = g_base64_decode(encoded, &size);
		ASSERT_TRUE(g_file_set_contents(path, reinterpret_cast<const gchar *>(contents), size, nullptr));
		expect(nullptr);
		}
}

TEST_F(ImageResolution, TruncatedDensityDoesNotProduceAValue)
{
	for (const char *format : {"png", "jpeg"})
		{
		save(format);
		g_autofree gchar *contents = nullptr;
		gsize size;
		ASSERT_TRUE(g_file_get_contents(path, &contents, &size, nullptr));
		ASSERT_GT(size, 17);
		ASSERT_TRUE(g_file_set_contents(path, contents, 17, nullptr));
		expect(nullptr);
		}
}

#if HAVE_EXIV2
TEST_F(ImageResolution, ExifTakesPrecedenceOverJfif)
{
	save("jpeg");
	auto image = Exiv2::ImageFactory::open(path);
	image->readMetadata();
	image->exifData()["Exif.Image.XResolution"] = "72/1";
	image->exifData()["Exif.Image.YResolution"] = "96/1";
	image->exifData()["Exif.Image.ResolutionUnit"] = uint16_t{2};
	image->writeMetadata();
	expect("72 x 96 (dot/inch)");
}

TEST_F(ImageResolution, InvalidExifDensityFallsBackToJfif)
{
	save("jpeg");
	auto image = Exiv2::ImageFactory::open(path);
	image->readMetadata();
	image->exifData()["Exif.Image.XResolution"] = "0/1";
	image->exifData()["Exif.Image.YResolution"] = "96/1";
	image->exifData()["Exif.Image.ResolutionUnit"] = uint16_t{2};
	image->writeMetadata();
	expect("300 x 150 (dot/inch)");
}
#endif

} // namespace
