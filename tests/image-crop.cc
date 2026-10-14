/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "image-crop.h"

#include <array>
#include <string>
#include <unistd.h>
#include <vector>

#include "gtest/gtest.h"

#include <config.h>
#include <glib/gstdio.h>
#if HAVE_LCMS
#include <lcms2.h>
#endif
#if HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "metadata.h"
#include "options.h"

namespace
{

class ImageCrop : public ::testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		filter_add_defaults();
		filter_rebuild();
		directory = g_dir_make_tmp("geeqie-crop-test-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "source.png", nullptr);
		pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 4, 3);
		gdk_pixbuf_fill(pixels, 0xff000080);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "png", nullptr, nullptr));
		source = file_data_new_group(path);
	}

	void TearDown() override
	{
		file_data_unref(source);
		g_clear_object(&pixels);
		if (directory)
			{
			g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
			while (const gchar *name = g_dir_read_name(dir))
				{
				g_autofree gchar *file = g_build_filename(directory, name, nullptr);
				g_unlink(file);
				}
			g_rmdir(directory);
			}
		g_free(path);
		g_free(directory);
	}

	gchar *directory = nullptr;
	gchar *path = nullptr;
	FileData *source = nullptr;
	GdkPixbuf *pixels = nullptr;
};

TEST_F(ImageCrop, CropsOriginalPixelsAndAppliesEveryOrientation)
{
	// Six distinct pixels in the selected region make rotation and mirroring observable.
	for (gint y = 0; y < 2; ++y)
		for (gint x = 0; x < 3; ++x)
			{
			guchar *pixel = gdk_pixbuf_get_pixels(pixels) + ((y + 1) * gdk_pixbuf_get_rowstride(pixels)) + ((x + 1) * 4);
			pixel[0] = (y * 3) + x + 1;
			}
	const std::array<std::array<guchar, 6>, 8> expected = {{{1, 2, 3, 4, 5, 6}, {3, 2, 1, 6, 5, 4},
	    {6, 5, 4, 3, 2, 1}, {4, 5, 6, 1, 2, 3}, {1, 4, 2, 5, 3, 6}, {4, 1, 5, 2, 6, 3},
	    {6, 3, 5, 2, 4, 1}, {3, 6, 2, 5, 1, 4}}};
	for (gint orientation = 1; orientation <= 8; ++orientation)
		{
		g_autoptr(GdkPixbuf) result = image_crop_pixbuf(pixels, {1, 1, 3, 2}, orientation, nullptr);
		ASSERT_NE(result, nullptr);
		const gint width = orientation <= 4 ? 3 : 2;
		EXPECT_EQ(gdk_pixbuf_get_width(result), width);
		EXPECT_EQ(gdk_pixbuf_get_height(result), orientation <= 4 ? 2 : 3);
		for (gint index = 0; index < 6; ++index)
			EXPECT_EQ(gdk_pixbuf_get_pixels(result)[((index / width) * gdk_pixbuf_get_rowstride(result)) + ((index % width) * 4)],
			          expected[orientation - 1][index]);
		}
}

TEST_F(ImageCrop, RejectsInvalidRectangle)
{
	g_autoptr(GError) error = nullptr;
	EXPECT_EQ(image_crop_pixbuf(pixels, {3, 0, 2, 2}, 1, &error), nullptr);
	ASSERT_NE(error, nullptr);
}

TEST_F(ImageCrop, PreservesOriginalAndRefusesItsLinks)
{
	g_autofree gchar *before = nullptr;
	gsize size;
	ASSERT_TRUE(g_file_get_contents(path, &before, &size, nullptr));
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(image_crop_save(source, pixels, path, TRUE, 90, nullptr, FALSE, &error));
	ASSERT_NE(error, nullptr);
	g_clear_error(&error);
	g_autofree gchar *link_path = g_build_filename(directory, "hardlink.png", nullptr);
	ASSERT_EQ(link(path, link_path), 0);
	EXPECT_FALSE(image_crop_save(source, pixels, link_path, TRUE, 90, nullptr, FALSE, &error));
	g_clear_error(&error);
	g_unlink(link_path);
	ASSERT_EQ(symlink(path, link_path), 0);
	EXPECT_FALSE(image_crop_save(source, pixels, link_path, TRUE, 90, nullptr, FALSE, &error));
	g_autofree gchar *after = nullptr;
	gsize after_size;
	ASSERT_TRUE(g_file_get_contents(path, &after, &after_size, nullptr));
	EXPECT_EQ(std::string(before, size), std::string(after, after_size));
}

TEST_F(ImageCrop, PngKeepsAlphaAndJpegFlattensAgainstWhite)
{
	g_autofree gchar *png = g_build_filename(directory, "crop.png", nullptr);
	g_autofree gchar *jpeg = g_build_filename(directory, "crop.jpg", nullptr);
	g_autoptr(GError) error = nullptr;
	ASSERT_TRUE(image_crop_save(source, pixels, png, TRUE, 90, nullptr, FALSE, &error)) << (error ? error->message : "");
	ASSERT_TRUE(image_crop_save(source, pixels, jpeg, FALSE, 100, nullptr, FALSE, &error)) << (error ? error->message : "");
	g_autoptr(GdkPixbuf) saved_png = gdk_pixbuf_new_from_file(png, nullptr);
	g_autoptr(GdkPixbuf) saved_jpeg = gdk_pixbuf_new_from_file(jpeg, nullptr);
	ASSERT_NE(saved_png, nullptr);
	ASSERT_NE(saved_jpeg, nullptr);
	EXPECT_TRUE(gdk_pixbuf_get_has_alpha(saved_png));
	EXPECT_EQ(gdk_pixbuf_get_pixels(saved_png)[3], 128);
	EXPECT_FALSE(gdk_pixbuf_get_has_alpha(saved_jpeg));
	EXPECT_NEAR(gdk_pixbuf_get_pixels(saved_jpeg)[0], 255, 2);
	EXPECT_NEAR(gdk_pixbuf_get_pixels(saved_jpeg)[1], 127, 2);
	EXPECT_NEAR(gdk_pixbuf_get_pixels(saved_jpeg)[2], 127, 2);
}

TEST_F(ImageCrop, WrongExtensionLeavesExistingDestinationUnchanged)
{
	g_autofree gchar *output = g_build_filename(directory, "existing.jpg", nullptr);
	ASSERT_TRUE(g_file_set_contents(output, "existing contents", -1, nullptr));
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(image_crop_save(source, pixels, output, TRUE, 90, nullptr, FALSE, &error));
	ASSERT_NE(error, nullptr);
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(output, &contents, nullptr, nullptr));
	EXPECT_STREQ(contents, "existing contents");
}

#if HAVE_LCMS && HAVE_EXIV2
TEST_F(ImageCrop, EmbedsColorProfileInBothFormats)
{
	const auto profile = cmsCreate_sRGBProfile();
	ASSERT_NE(profile, nullptr);
	cmsUInt32Number length = 0;
	ASSERT_TRUE(cmsSaveProfileToMem(profile, nullptr, &length));
	std::vector<guchar> bytes(length);
	ASSERT_TRUE(cmsSaveProfileToMem(profile, bytes.data(), &length));
	cmsCloseProfile(profile);
	g_autofree gchar *encoded = g_base64_encode(bytes.data(), length);
	for (const bool png : {false, true})
		{
		g_autofree gchar *output = g_build_filename(directory, png ? "crop.png" : "crop.jpg", nullptr);
		g_autoptr(GError) error = nullptr;
		ASSERT_TRUE(image_crop_save(source, pixels, output, png, 90, encoded, TRUE, &error)) << (error ? error->message : "");
		auto saved = Exiv2::ImageFactory::open(output);
		saved->readMetadata();
		const auto &saved_profile = saved->iccProfile();
#if EXIV2_TEST_VERSION(0,28,0)
		EXPECT_EQ(saved_profile.size(), length);
		EXPECT_EQ(std::vector<guchar>(saved_profile.c_data(), saved_profile.c_data() + saved_profile.size()), bytes);
#else
		EXPECT_EQ(saved_profile.size_, length);
		EXPECT_EQ(std::vector<guchar>(saved_profile.pData_, saved_profile.pData_ + saved_profile.size_), bytes);
#endif
		}
}
#endif

#if HAVE_EXIV2
TEST_F(ImageCrop, CopiesMetadataAndUpdatesDimensionsAndOrientation)
{
	ExifData *exif = exif_read(path, nullptr, nullptr);
	ASSERT_NE(exif, nullptr);
	GList description{const_cast<gchar *>("Crop test description"), nullptr, nullptr};
	GList orientation{const_cast<gchar *>("6"), nullptr, nullptr};
	GList latitude{const_cast<gchar *>("54,30N"), nullptr, nullptr};
	exif_update_metadata(exif, "Xmp.dc.description", &description);
	exif_update_metadata(exif, "Xmp.tiff.Orientation", &orientation);
	exif_update_metadata(exif, "Xmp.exif.GPSLatitude", &latitude);
	ASSERT_TRUE(exif_write(exif));
	exif_free(exif);
	for (const bool png : {false, true})
		{
		g_autofree gchar *output = g_build_filename(directory, png ? "crop.png" : "crop.jpg", nullptr);
		g_autoptr(GdkPixbuf) crop = image_crop_pixbuf(pixels, {1, 1, 3, 2}, 6, nullptr);
		g_autoptr(GError) error = nullptr;
		ASSERT_TRUE(image_crop_save(source, crop, output, png, 90, nullptr, TRUE, &error)) << (error ? error->message : "");
		ExifData *saved = exif_read(output, nullptr, nullptr);
		ASSERT_NE(saved, nullptr);
		GList *text = exif_get_metadata(saved, "Xmp.dc.description", METADATA_PLAIN);
		ASSERT_NE(text, nullptr);
		EXPECT_STREQ(static_cast<const gchar *>(text->data), "Crop test description");
		g_list_free_full(text, g_free);
		auto *saved_file = file_data_new_group(output);
		const auto gps = metadata_read_GPS_coord(saved_file, "Xmp.exif.GPSLatitude");
		EXPECT_EQ(gps, 54.5);
		file_data_unref(saved_file);
		EXPECT_EQ(exif_item_get_integer(exif_get_item(saved, "Exif.Image.Orientation")), 1);
		EXPECT_EQ(exif_item_get_integer(exif_get_item(saved, "Exif.Photo.PixelXDimension")), 2);
		EXPECT_EQ(exif_item_get_integer(exif_get_item(saved, "Exif.Photo.PixelYDimension")), 3);
		exif_free(saved);
		}
	ExifData *original = exif_read(path, nullptr, nullptr);
	ASSERT_NE(original, nullptr);
	EXPECT_EQ(exif_item_get_integer(exif_get_item(original, "Exif.Image.Orientation")), 6);
	exif_free(original);
}
#endif

} // namespace
