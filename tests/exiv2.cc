/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple>
#include <vector>

#include <config.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#if HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
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

class GroupedMetadataSidecars : public testing::TestWithParam<bool>
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved_metadata = options->metadata;
		saved_grouping = g_strdup(options->sidecar.ext);
		options->metadata.sidecar_extended_name = TRUE;
		options->metadata.save_in_image_file = TRUE;
		options->metadata.sync_grouped_files = FALSE;
		options->metadata.confirm_after_timeout = FALSE;
		filter_add_defaults();
		for (GList *work = filter_get_list(); work; work = work->next)
			{
			auto *filter = static_cast<FilterEntry *>(work->data);
			if (!strstr(filter->extensions, ".jpg") && !strstr(filter->extensions, ".cr2")) continue;
			filters.emplace_back(filter, filter->writable, filter->allow_sidecar);
			filter->writable = FALSE;
			filter->allow_sidecar = TRUE;
			}
		filter_rebuild();
		sidecar_ext_parse(GetParam() ? ".jpg;.cr2;.xmp" : ".cr2;.jpg;.xmp");
		directory = g_dir_make_tmp("geeqie-grouped-sidecars-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (const gchar *name : {"photo.jpg", "photo.cr2"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 16, 16);
			gdk_pixbuf_fill(pixels, 0x123456ff);
			// The test exercises routing, not RAW decoding; both files contain valid JPEG data.
			ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "jpeg", nullptr, nullptr));
			}
		g_autofree gchar *jpeg_path = g_build_filename(directory, "photo.jpg", nullptr);
		g_autofree gchar *raw_path = g_build_filename(directory, "photo.cr2", nullptr);
		jpeg = FileData::new_group(jpeg_path);
		raw = FileData::new_group(raw_path);
		if (GetParam()) ASSERT_EQ(raw->parent, static_cast<FileData *>(jpeg));
		else ASSERT_EQ(jpeg->parent, static_cast<FileData *>(raw));
	}

	void TearDown() override
	{
		for (auto *fd : {static_cast<FileData *>(jpeg), static_cast<FileData *>(raw)})
			{
			if (!fd) continue;
			if (fd->modified_xmp) metadata_write_queue_remove(fd);
			file_data_free_ci(fd);
			}
		jpeg.reset(nullptr);
		raw.reset(nullptr);
		if (directory)
			{
			for (const gchar *name : {"photo.jpg", "photo.cr2", "photo.jpg.xmp", "photo.cr2.xmp", "photo.xmp"})
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				g_unlink(path);
				}
			g_rmdir(directory);
			}
		g_free(directory);
		for (const auto &[filter, writable, allow_sidecar] : filters)
			{
			filter->writable = writable;
			filter->allow_sidecar = allow_sidecar;
			}
		filter_rebuild();
		sidecar_ext_parse(saved_grouping);
		g_free(saved_grouping);
		options->metadata = saved_metadata;
	}

	static void write_comment(FileData *fd, const gchar *comment, const gchar *expected_path)
	{
		ASSERT_TRUE(metadata_write_string(fd, COMMENT_KEY, comment));
		ASSERT_TRUE(file_data_add_ci_write_metadata(fd));
		EXPECT_EQ(file_data_verify_ci(fd, nullptr) & CHANGE_ERROR_MASK, 0);
		ASSERT_STREQ(fd->change->dest, expected_path);
		ASSERT_TRUE(metadata_write_perform(fd));
		file_data_free_ci(fd);
		metadata_write_queue_remove(fd);
	}

	static void expect_comment(FileData *fd, const gchar *comment)
	{
		g_autofree gchar *actual = metadata_read_string(fd, COMMENT_KEY, METADATA_PLAIN);
		EXPECT_STREQ(actual, comment);
	}

	FileDataRef jpeg{nullptr};
	FileDataRef raw{nullptr};
	gchar *directory = nullptr;
	gchar *saved_grouping = nullptr;
	decltype(options->metadata) saved_metadata{};
	std::vector<std::tuple<FilterEntry *, gboolean, gboolean>> filters;
};

TEST_P(GroupedMetadataSidecars, RepeatedWritesKeepEachImagesOwnExtendedSidecar)
{
	g_autofree gchar *jpeg_sidecar = g_strconcat(jpeg->path, ".xmp", nullptr);
	g_autofree gchar *raw_sidecar = g_strconcat(raw->path, ".xmp", nullptr);
	g_autofree gchar *missing = file_data_get_sidecar_path(raw, TRUE);
	EXPECT_EQ(missing, nullptr);
	write_comment(jpeg, "First JPEG comment", jpeg_sidecar);
	write_comment(raw, "First RAW comment", raw_sidecar);
	expect_comment(jpeg, "First JPEG comment");
	expect_comment(raw, "First RAW comment");
	write_comment(raw, "Second RAW comment", raw_sidecar);
	expect_comment(jpeg, "First JPEG comment");
	expect_comment(raw, "Second RAW comment");
	write_comment(jpeg, "Second JPEG comment", jpeg_sidecar);
	expect_comment(jpeg, "Second JPEG comment");
	expect_comment(raw, "Second RAW comment");
}

TEST_P(GroupedMetadataSidecars, MissingMemberSidecarDoesNotUseTheOtherMembersSidecar)
{
	g_autofree gchar *jpeg_sidecar = g_strconcat(jpeg->path, ".xmp", nullptr);
	write_comment(jpeg, "JPEG only", jpeg_sidecar);
	g_autofree gchar *existing = file_data_get_sidecar_path(raw, TRUE);
	EXPECT_EQ(existing, nullptr);
	g_autofree gchar *new_path = file_data_get_sidecar_path(raw, FALSE);
	g_autofree gchar *expected = g_strconcat(raw->path, ".xmp", nullptr);
	EXPECT_STREQ(new_path, expected);
}

TEST_P(GroupedMetadataSidecars, BasenameModeStillUsesTheSharedSidecar)
{
	g_autofree gchar *extended = g_strconcat(jpeg->path, ".xmp", nullptr);
	write_comment(jpeg, "Extended JPEG", extended);
	options->metadata.sidecar_extended_name = FALSE;
	g_autofree gchar *missing_shared = file_data_get_sidecar_path(raw, TRUE);
	EXPECT_EQ(missing_shared, nullptr);
	g_autofree gchar *shared = g_build_filename(directory, "photo.xmp", nullptr);
	write_comment(jpeg, "Shared first", shared);
	g_autofree gchar *jpeg_sidecar = file_data_get_sidecar_path(jpeg, TRUE);
	g_autofree gchar *raw_sidecar = file_data_get_sidecar_path(raw, TRUE);
	EXPECT_STREQ(jpeg_sidecar, shared);
	EXPECT_STREQ(raw_sidecar, shared);
	write_comment(raw, "Shared second", shared);
	expect_comment(raw, "Shared second");
}

INSTANTIATE_TEST_SUITE_P(GroupLeaders, GroupedMetadataSidecars, testing::Bool());

} // namespace
#endif
