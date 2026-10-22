/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <cstdint>
#include <memory>

#include <config.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#if HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include "exif.h"
#include "metadata.h"
#include "options.h"

#if HAVE_EXIV2
namespace
{

class MetadataPreservation : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved_iptc = options->metadata.save_legacy_IPTC;
		options->metadata.save_legacy_IPTC = FALSE;
		directory = g_dir_make_tmp("geeqie-metadata-preservation-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image.jpg", nullptr);
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 16, 16);
		gdk_pixbuf_fill(pixels, 0x123456ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "jpeg", nullptr, nullptr));
		auto image = Exiv2::ImageFactory::open(path);
		image->readMetadata();
		image->exifData()["Exif.Image.Orientation"] = uint16_t{1};
		image->exifData()["Exif.Image.Artist"] = "Camera author";
		image->xmpData()["Xmp.exif.DateTimeOriginal"] = "2015-04-03";
		image->xmpData()["Xmp.dc.creator"] = "XMP author";
		image->iptcData()["Iptc.Application2.Caption"] = "Original caption";
		image->writeMetadata();
	}

	void TearDown() override
	{
		options->metadata.save_legacy_IPTC = saved_iptc;
		g_autofree gchar *sidecar = directory ? g_build_filename(directory, "image.xmp", nullptr) : nullptr;
		if (sidecar) g_unlink(sidecar);
		if (path) g_unlink(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
	}

	void edit(const gchar *key, const gchar *value)
	{
		std::unique_ptr<ExifData, decltype(&exif_free)> metadata(exif_read(path, nullptr, nullptr), exif_free);
		ASSERT_NE(metadata, nullptr);
		GList entry{const_cast<gchar *>(value), nullptr, nullptr};
		ASSERT_TRUE(exif_update_metadata(metadata.get(), key, value ? &entry : nullptr));
		ASSERT_TRUE(exif_write(metadata.get()));
	}

	gboolean saved_iptc = FALSE;
	gchar *directory = nullptr;
	gchar *path = nullptr;
};

TEST_F(MetadataPreservation, OrientationPreservesDateOnlyXmpAndUnrelatedMetadata)
{
	edit(ORIENTATION_KEY, "6");
	auto image = Exiv2::ImageFactory::open(path);
	image->readMetadata();
	EXPECT_EQ(image->exifData()["Exif.Image.Orientation"].toString(), "6");
	EXPECT_EQ(image->xmpData()[ORIENTATION_KEY].toString(), "6");
	EXPECT_EQ(image->xmpData()["Xmp.exif.DateTimeOriginal"].toString(), "2015-04-03");
	EXPECT_EQ(image->exifData().findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal")), image->exifData().end());
	EXPECT_EQ(image->exifData()["Exif.Image.Artist"].toString(), "Camera author");
	EXPECT_EQ(image->xmpData()["Xmp.dc.creator"].toString(), "XMP author");
	EXPECT_EQ(image->iptcData()["Iptc.Application2.Caption"].toString(), "Original caption");
}

TEST_F(MetadataPreservation, ExplicitEditsStillUpdateAndDeleteLegacyMappings)
{
	options->metadata.save_legacy_IPTC = TRUE;
	edit(COMMENT_KEY, "New caption");
	auto image = Exiv2::ImageFactory::open(path);
	image->readMetadata();
	EXPECT_EQ(image->iptcData()["Iptc.Application2.Caption"].toString(), "New caption");
	EXPECT_EQ(image->exifData()["Exif.Image.ImageDescription"].toString(), "New caption");
	edit(COMMENT_KEY, nullptr);
	image = Exiv2::ImageFactory::open(path);
	image->readMetadata();
	EXPECT_EQ(image->iptcData().findKey(Exiv2::IptcKey("Iptc.Application2.Caption")), image->iptcData().end());
	EXPECT_EQ(image->exifData().findKey(Exiv2::ExifKey("Exif.Image.ImageDescription")), image->exifData().end());
}

TEST_F(MetadataPreservation, PendingEditsPreserveUnrelatedMetadata)
{
	GList entry{const_cast<gchar *>("8"), nullptr, nullptr};
	g_autoptr(GHashTable) pending = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_insert(pending, const_cast<gchar *>(ORIENTATION_KEY), &entry);
	std::unique_ptr<ExifData, decltype(&exif_free)> metadata(exif_read(path, nullptr, pending), exif_free);
	ASSERT_NE(metadata, nullptr);
	ASSERT_TRUE(exif_write(metadata.get()));
	auto image = Exiv2::ImageFactory::open(path);
	image->readMetadata();
	EXPECT_EQ(image->exifData()["Exif.Image.Orientation"].toString(), "8");
	EXPECT_EQ(image->xmpData()["Xmp.exif.DateTimeOriginal"].toString(), "2015-04-03");
	EXPECT_EQ(image->exifData().findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal")), image->exifData().end());
}

TEST_F(MetadataPreservation, ExistingSidecarPreservesDateOnlyXmp)
{
	g_autofree gchar *sidecar_path = g_build_filename(directory, "image.xmp", nullptr);
	auto sidecar = Exiv2::ImageFactory::create(Exiv2::ImageType::xmp, sidecar_path);
	sidecar->xmpData()["Xmp.exif.DateTimeOriginal"] = "2016-05-04";
	sidecar->xmpData()["Xmp.dc.source"] = "Original source";
	sidecar->writeMetadata();
	std::unique_ptr<ExifData, decltype(&exif_free)> metadata(exif_read(path, sidecar_path, nullptr), exif_free);
	ASSERT_NE(metadata, nullptr);
	GList entry{const_cast<gchar *>("6"), nullptr, nullptr};
	ASSERT_TRUE(exif_update_metadata(metadata.get(), ORIENTATION_KEY, &entry));
	ASSERT_TRUE(exif_write_sidecar(metadata.get(), sidecar_path));
	sidecar = Exiv2::ImageFactory::open(sidecar_path);
	sidecar->readMetadata();
	EXPECT_EQ(sidecar->xmpData()[ORIENTATION_KEY].toString(), "6");
	EXPECT_EQ(sidecar->xmpData()["Xmp.exif.DateTimeOriginal"].toString(), "2016-05-04");
	EXPECT_EQ(sidecar->xmpData()["Xmp.dc.source"].toString(), "Original source");
}

TEST_F(MetadataPreservation, RepeatedWritesCanRestoreTheInitialOrientation)
{
	std::unique_ptr<ExifData, decltype(&exif_free)> metadata(exif_read(path, nullptr, nullptr), exif_free);
	ASSERT_NE(metadata, nullptr);
	for (const gchar *orientation : {"6", "1"})
		{
		GList entry{const_cast<gchar *>(orientation), nullptr, nullptr};
		ASSERT_TRUE(exif_update_metadata(metadata.get(), ORIENTATION_KEY, &entry));
		ASSERT_TRUE(exif_write(metadata.get()));
		auto image = Exiv2::ImageFactory::open(path);
		image->readMetadata();
		EXPECT_EQ(image->exifData()["Exif.Image.Orientation"].toString(), orientation);
		EXPECT_EQ(image->xmpData()["Xmp.exif.DateTimeOriginal"].toString(), "2015-04-03");
		}
}

} // namespace
#endif
