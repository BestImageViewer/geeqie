/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <initializer_list>
#include <memory>
#include <string>

#include <glib/gstdio.h>

#include "cache.h"
#include "exif.h"
#include "filedata.h"
#include "metadata.h"
#include "options.h"
#include "thumb-standard.h"
#include "ui-fileops.h"

namespace
{

class ThumbnailReplacement : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved_rotate = options->image.exif_rotate_enable;
		options->image.exif_rotate_enable = TRUE;
		directory = g_dir_make_tmp("geeqie-thumbnail-replacement-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image.png", nullptr);
		write_image(path, 0x2040c0ff);
		g_autofree gchar *uri = g_filename_to_uri(path, nullptr, nullptr);
		g_autofree gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_MD5, uri, -1);
		g_autofree gchar *name = g_strconcat(hash, ".png", nullptr);
		cache_path = g_build_filename(get_thumbnails_standard_cache_dir(), THUMB_FOLDER_NORMAL, name, nullptr);
	}

	void TearDown() override
	{
		if (path) cache_remove_thumbnails(path);
		if (path) g_unlink(path);
		if (directory)
			{
			g_autofree gchar *replacement = g_build_filename(directory, "replacement.png", nullptr);
			g_unlink(replacement);
			g_rmdir(directory);
			}
		g_free(cache_path);
		g_free(path);
		g_free(directory);
		options->image.exif_rotate_enable = saved_rotate;
	}

	static void write_image(const gchar *filename, guint32 color)
	{
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 320, 160);
		gdk_pixbuf_fill(pixels, color);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, filename, "png", nullptr, nullptr));
	}

	void seed_cache(const gchar *size)
	{
		struct stat st{};
		ASSERT_EQ(g_stat(path, &st), 0);
		g_autofree gchar *uri = g_filename_to_uri(path, nullptr, nullptr);
		const std::string mtime = std::to_string(st.st_mtime);
		g_autofree gchar *parent = g_path_get_dirname(cache_path);
		ASSERT_EQ(g_mkdir_with_parents(parent, 0700), 0);
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 128, 64);
		gdk_pixbuf_fill(pixels, 0xff0000ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, cache_path, "png", nullptr,
		                           "tEXt::Thumb::URI", uri,
		                           "tEXt::Thumb::MTime", mtime.c_str(),
		                           size ? "tEXt::Thumb::Size" : nullptr, size, nullptr));
	}

	static void load(FileData *fd)
	{
		struct Completion { bool done = false; bool failed = false; } result;
		auto *loader = thumb_loader_std_new(128, 128);
		thumb_loader_std_set_cache(loader, TRUE, FALSE, TRUE);
		thumb_loader_std_set_callbacks(loader,
			[](ThumbLoaderStd *, gpointer data) { static_cast<Completion *>(data)->done = true; },
			[](ThumbLoaderStd *, gpointer data)
				{
				auto *result = static_cast<Completion *>(data);
				result->done = true;
				result->failed = true;
				}, nullptr, &result);
		fd->format_class = FORMAT_CLASS_IMAGE;
		const bool started = thumb_loader_std_start(loader, fd);
		const gint64 deadline = g_get_monotonic_time() + (5 * G_TIME_SPAN_SECOND);
		while (started && !result.done && g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
		EXPECT_TRUE(started);
		EXPECT_TRUE(result.done);
		EXPECT_FALSE(result.failed);
		thumb_loader_std_free(loader);
	}

	gchar *directory = nullptr;
	gchar *path = nullptr;
	gchar *cache_path = nullptr;
	gboolean saved_rotate = FALSE;
};

TEST_F(ThumbnailReplacement, MismatchedSizeRejectsCachedImageAndWritesCurrentSize)
{
	seed_cache("1");
	auto fd = FileData::new_simple(path);
	load(fd);
	ASSERT_NE(fd->thumb_pixbuf, nullptr);
	const auto *pixel = gdk_pixbuf_read_pixels(fd->thumb_pixbuf);
	EXPECT_LT(pixel[0], pixel[2]); // Current blue image, rather than the cached red image.
	g_autoptr(GdkPixbuf) cached = gdk_pixbuf_new_from_file(cache_path, nullptr);
	ASSERT_NE(cached, nullptr);
	EXPECT_STREQ(gdk_pixbuf_get_option(cached, "tEXt::Thumb::Size"), std::to_string(fd->size).c_str());
}

TEST_F(ThumbnailReplacement, LegacyThumbnailWithoutSizeRemainsUsable)
{
	seed_cache(nullptr);
	auto fd = FileData::new_simple(path);
	load(fd);
	ASSERT_NE(fd->thumb_pixbuf, nullptr);
	EXPECT_EQ(gdk_pixbuf_read_pixels(fd->thumb_pixbuf)[0], 255);
}

TEST_F(ThumbnailReplacement, MatchingSizeRemainsUsableAndMalformedSizeIsRejected)
{
	auto fd = FileData::new_simple(path);
	const std::string size = std::to_string(fd->size);
	seed_cache(size.c_str());
	load(fd);
	ASSERT_NE(fd->thumb_pixbuf, nullptr);
	EXPECT_EQ(gdk_pixbuf_read_pixels(fd->thumb_pixbuf)[0], 255);
	for (const gchar *invalid : {"-1", "nonsense", "12junk", "184467440737095516160"})
		{
		g_clear_object(&fd->thumb_pixbuf);
		seed_cache(invalid);
		load(fd);
		ASSERT_NE(fd->thumb_pixbuf, nullptr);
		EXPECT_LT(gdk_pixbuf_read_pixels(fd->thumb_pixbuf)[0], 128);
		}
}

TEST_F(ThumbnailReplacement, ReplacementWithPreservedSizeAndMtimeInvalidatesCachedState)
{
	auto fd = FileData::new_simple(path);
	const auto original_size = fd->size;
	const auto original_inode = fd->inode;
	const auto original_nsec = fd->mtime_nsec;
	g_autofree gchar *replacement = g_build_filename(directory, "replacement.png", nullptr);
	// An identical copy guarantees identical size and mtime while changing file identity.
	g_autofree gchar *contents = nullptr;
	gsize length = 0;
	ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
	ASSERT_TRUE(g_file_set_contents(replacement, contents, length, nullptr));
	const timespec preserved[] = {{fd->date, fd->mtime_nsec}, {fd->date, fd->mtime_nsec}};
	ASSERT_EQ(utimensat(AT_FDCWD, replacement, preserved, 0), 0);
	seed_cache(std::to_string(fd->size).c_str());
	load(fd);
	fd->exif_orientation = 6;
	fd->exifdate = 123;
	fd->media_date = 456;
	ASSERT_EQ(g_rename(replacement, path), 0);
	ASSERT_TRUE(file_data_check_changed_files(fd));
	EXPECT_EQ(fd->size, original_size);
	EXPECT_NE(fd->inode, original_inode);
	EXPECT_EQ(fd->mtime_nsec, original_nsec);
	EXPECT_EQ(fd->thumb_pixbuf, nullptr);
	EXPECT_EQ(fd->exif_orientation, 0);
	EXPECT_EQ(fd->exifdate, 0);
	EXPECT_EQ(fd->media_date, 0);
	EXPECT_FALSE(g_file_test(cache_path, G_FILE_TEST_EXISTS));
	EXPECT_FALSE(file_data_check_changed_files(fd));
}

TEST_F(ThumbnailReplacement, MissingSourceKeepsItsCachedThumbnail)
{
	seed_cache(nullptr);
	auto fd = FileData::new_simple(path);
	load(fd);
	fd->exif_orientation = 6;
	ASSERT_EQ(g_unlink(path), 0);
	EXPECT_TRUE(file_data_check_changed_files(fd));
	EXPECT_TRUE(g_file_test(cache_path, G_FILE_TEST_EXISTS));
	EXPECT_EQ(fd->exif_orientation, 6);
	load(fd);
	EXPECT_NE(fd->thumb_pixbuf, nullptr);
}

TEST_F(ThumbnailReplacement, DirectoryRescanDetectsAllSubsecondChanges)
{
#if !HAVE_STAT_TIM && !HAVE_STAT_TIMESPEC
	GTEST_SKIP() << "Subsecond stat timestamps are unavailable";
#endif
	g_autofree gchar *other_path = g_build_filename(directory, "replacement.png", nullptr);
	write_image(other_path, 0x804020ff);
	const timespec before[] = {{1700000000, 100000000}, {1700000000, 100000000}};
	const timespec after[] = {{1700000000, 200000000}, {1700000000, 200000000}};
	ASSERT_EQ(utimensat(AT_FDCWD, path, before, 0), 0);
	ASSERT_EQ(utimensat(AT_FDCWD, other_path, before, 0), 0);
	auto first = FileData::new_simple(path);
	auto second = FileData::new_simple(other_path);
	if (first->mtime_nsec != before[1].tv_nsec) GTEST_SKIP() << "Filesystem lacks the required timestamp precision";
	const gint first_version = first->version;
	const gint second_version = second->version;
	ASSERT_EQ(utimensat(AT_FDCWD, path, after, 0), 0);
	ASSERT_EQ(utimensat(AT_FDCWD, other_path, after, 0), 0);
	auto directory_fd = FileData::new_simple(directory);
	g_autoptr(FileDataList) files = nullptr;
	ASSERT_TRUE(filelist_read(directory_fd, &files, nullptr));
	EXPECT_EQ(g_list_length(files), 2U);
	EXPECT_GT(first->version, first_version);
	EXPECT_GT(second->version, second_version);
	EXPECT_EQ(first->mtime_nsec, after[1].tv_nsec);
	EXPECT_EQ(second->mtime_nsec, after[1].tv_nsec);
}

#if HAVE_EXIV2
TEST_F(ThumbnailReplacement, ReusedFilenameReloadsTheNewExifOrientation)
{
	const auto set_orientation = [](const gchar *filename, const gchar *value)
		{
		std::unique_ptr<ExifData, decltype(&exif_free)> exif(exif_read(const_cast<gchar *>(filename), nullptr, nullptr), exif_free);
		ASSERT_NE(exif, nullptr);
		GList entry{const_cast<gchar *>(value), nullptr, nullptr};
		ASSERT_TRUE(exif_update_metadata(exif.get(), ORIENTATION_KEY, &entry));
		ASSERT_TRUE(exif_write(exif.get()));
		};
	set_orientation(path, "1");
	auto fd = FileData::new_simple(path);
	load(fd);
	ASSERT_NE(fd->thumb_pixbuf, nullptr);
	EXPECT_GT(gdk_pixbuf_get_width(fd->thumb_pixbuf), gdk_pixbuf_get_height(fd->thumb_pixbuf));
	g_autofree gchar *replacement = g_build_filename(directory, "replacement.png", nullptr);
	write_image(replacement, 0x804020ff);
	set_orientation(replacement, "6");
	ASSERT_TRUE(filetime_set(replacement, fd->date));
	ASSERT_EQ(g_rename(replacement, path), 0);
	ASSERT_TRUE(file_data_check_changed_files(fd));
	load(fd);
	ASSERT_NE(fd->thumb_pixbuf, nullptr);
	EXPECT_EQ(fd->exif_orientation, 6);
	EXPECT_LT(gdk_pixbuf_get_width(fd->thumb_pixbuf), gdk_pixbuf_get_height(fd->thumb_pixbuf));
}
#endif

} // namespace
