/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <cstring>
#include <memory>

#include <glib-object.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <config.h>

#include "filedata.h"
#include "image-load.h"
#include "options.h"
#include "ui-fileops.h"

#if HAVE_JPEG
#include "image-load-jpeg.h"
#endif

namespace
{

class ImageLoaderSource : public testing::TestWithParam<const char *>
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		directory = g_dir_make_tmp("geeqie-loader-source-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image", nullptr);
	}

	void TearDown() override
	{
		if (path) g_remove(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
	}

	gchar *directory = nullptr;
	gchar *path = nullptr;
};

TEST_P(ImageLoaderSource, TruncationDoesNotInvalidatePreparedInput)
{
	g_autoptr(GdkPixbuf) source = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 256, 128);
	gdk_pixbuf_fill(source, 0x204060ff);
	g_autofree gchar *encoded = nullptr;
	gsize length = 0;
	ASSERT_TRUE(gdk_pixbuf_save_to_buffer(source, &encoded, &length, GetParam(), nullptr, nullptr));
	ASSERT_TRUE(g_file_set_contents(path, encoded, length, nullptr));
	auto fd = FileData::new_simple(path);
	std::unique_ptr<ImageLoader, decltype(&g_object_unref)> loader(image_loader_new(fd), g_object_unref);
	ASSERT_TRUE(image_loader_start(loader.get()));
	ASSERT_EQ(loader->bytes_total, length);

	// Truncate the same inode, rather than atomically replacing the path.
	// Reading the original file-backed mapping here would raise SIGBUS even
	// if the worker had already finished decoding before the truncation.
	{
		g_autoptr(FILE) output = g_fopen(path, "wb");
		ASSERT_NE(output, nullptr);
	}
	EXPECT_EQ(std::memcmp(loader->input_buffer, encoded, length), 0);
	const gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (!image_loader_get_is_done(loader.get()) && g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
	ASSERT_TRUE(image_loader_get_is_done(loader.get()));
	g_autoptr(GError) error = image_loader_dup_error(loader.get());
	EXPECT_EQ(error, nullptr);
	GdkPixbuf *decoded = image_loader_get_pixbuf(loader.get());
	ASSERT_NE(decoded, nullptr);
	EXPECT_EQ(gdk_pixbuf_get_width(decoded), 256);
	EXPECT_EQ(gdk_pixbuf_get_height(decoded), 128);
}

INSTANTIATE_TEST_SUITE_P(Formats, ImageLoaderSource, testing::Values("png"
#if HAVE_JPEG
	, "jpeg"
#endif
#if HAVE_TIFF
	, "tiff"
#endif
));

TEST(ImageLoader, EmptySourceReportsReadFailure)
{
	if (!options) options = conf_options_new();
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-loader-empty-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "empty.jpg", nullptr);
	ASSERT_TRUE(g_file_set_contents(path, "", 0, nullptr));
	auto fd = FileData::new_simple(path);
	std::unique_ptr<ImageLoader, decltype(&g_object_unref)> loader(image_loader_new(fd), g_object_unref);
	EXPECT_FALSE(image_loader_start(loader.get()));
	EXPECT_EQ(loader->input_buffer, nullptr);
	g_autoptr(GError) error = image_loader_dup_error(loader.get());
	EXPECT_NE(error, nullptr);
	g_remove(path);
	g_rmdir(directory);
}

TEST(ImageLoader, SourceReadRejectsTruncationAndCancellation)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-loader-read-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "source", nullptr);
	g_autofree auto *data = static_cast<gchar *>(g_malloc0(2 * 1024 * 1024));
	ASSERT_TRUE(g_file_set_contents(path, data, 2 * 1024 * 1024, nullptr));
	gsize length = 99;
	g_autoptr(GError) error = nullptr;
	g_autofree guchar *buffer = read_file_snapshot(path, length, &error, G_MAXSIZE, []() { return true; });
	EXPECT_EQ(buffer, nullptr);
	EXPECT_EQ(length, 0U);
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->domain, G_IO_ERROR);
	EXPECT_EQ(error->code, G_IO_ERROR_CANCELLED);
	g_clear_error(&error);

	// Video input needs only its header, even when the source is much larger.
	buffer = read_file_snapshot(path, length, &error, 4096);
	ASSERT_NE(buffer, nullptr);
	EXPECT_EQ(length, 4096U);
	EXPECT_EQ(std::memcmp(buffer, data, length), 0);
	g_clear_pointer(&buffer, g_free);

	int reads = 0;
	buffer = read_file_snapshot(path, length, &error, G_MAXSIZE, [&]()
		{
		if (++reads == 2)
			{
			g_autoptr(FILE) output = g_fopen(path, "wb");
			EXPECT_NE(output, nullptr);
			}
		return false;
		});
	EXPECT_EQ(buffer, nullptr);
	EXPECT_EQ(length, 0U);
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->domain, G_IO_ERROR);
	EXPECT_EQ(error->code, G_IO_ERROR_BUSY);
	g_remove(path);
	g_rmdir(directory);
}

#if HAVE_JPEG
TEST(ImageLoader, ConfigurableJpegDecodeMemoryLimit)
{
	if (!options) options = conf_options_new();
	struct RestoreLimit
	{
		gint value = options->image.jpeg_decode_memory_limit;
		~RestoreLimit() { options->image.jpeg_decode_memory_limit = value; }
	} restore_limit;

	g_autoptr(GdkPixbuf) source = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1024, 512);
	gdk_pixbuf_fill(source, 0x204060ff);
	g_autofree gchar *jpeg = nullptr;
	gsize length = 0;
	ASSERT_TRUE(gdk_pixbuf_save_to_buffer(source, &jpeg, &length, "jpeg", nullptr, nullptr));

	// The decoded image requires 1.5 MiB. Changing the limit must affect new loads.
	for (const gint limit : {1, 2, 1})
		{
		options->image.jpeg_decode_memory_limit = limit;
		auto loader = get_image_loader_backend_jpeg();
		loader->init([](gpointer, gint, gint, gint, gint, gpointer) {},
		             [](gpointer, gint, gint, gpointer) {}, nullptr);
		g_autoptr(GError) error = nullptr;
		gsize chunk_size = length;
		const gboolean loaded = loader->write(reinterpret_cast<const guchar *>(jpeg), chunk_size, length, &error);
		if (limit == 1)
			{
			EXPECT_FALSE(loaded);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(error->domain, GDK_PIXBUF_ERROR);
			EXPECT_EQ(error->code, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY);
			}
		else
			{
			EXPECT_TRUE(loaded);
			EXPECT_EQ(error, nullptr);
			ASSERT_NE(loader->get_pixbuf(), nullptr);
			EXPECT_EQ(gdk_pixbuf_get_width(loader->get_pixbuf()), 1024);
			EXPECT_EQ(gdk_pixbuf_get_height(loader->get_pixbuf()), 512);
			}
		}
}
#endif

TEST(ImageLoader, CancelPendingStart)
{
	auto *fd = file_data_new_simple("/nonexistent/geeqie-pending-image.jpg");
	auto *il = image_loader_new(fd);
	file_data_unref(fd);
	ASSERT_TRUE(image_loader_start_deferred(il));
	EXPECT_FALSE(il->thread);
	EXPECT_EQ(il->input_buffer, nullptr);
	const guint source_id = il->idle_id;
	EXPECT_NE(g_main_context_find_source_by_id(nullptr, source_id), nullptr);
	EXPECT_FALSE(image_loader_start_deferred(il));
	g_object_unref(il);
	EXPECT_EQ(g_main_context_find_source_by_id(nullptr, source_id), nullptr);
}

TEST(ImageLoader, DeferredStartReportsFailure)
{
	auto *fd = file_data_new_simple("/nonexistent/geeqie-pending-image.jpg");
	auto *il = image_loader_new(fd);
	file_data_unref(fd);
	gboolean failed = FALSE;
	g_signal_connect(il, "error", G_CALLBACK(+[](ImageLoader *, gpointer data)
		{
		*static_cast<gboolean *>(data) = TRUE;
		}), &failed);
	ASSERT_TRUE(image_loader_start_deferred(il));
	const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	while (!failed && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		}
	EXPECT_TRUE(failed);
	EXPECT_EQ(il->idle_id, 0U);
	g_object_unref(il);
}

} // namespace
