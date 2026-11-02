/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <config.h>

#if HAVE_ARCHIVE
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>

#include "filedata.h"
#include "image-load-ora.h"
#include "image-load.h"
#include "options.h"

namespace
{

std::string png_image(bool alpha)
{
	g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alpha, 8, 3, 2);
	gdk_pixbuf_fill(pixels, 0x20406080);
	gchar *buffer = nullptr;
	gsize length = 0;
	if (!gdk_pixbuf_save_to_buffer(pixels, &buffer, &length, "png", nullptr, nullptr)) return {};
	g_autofree gchar *owned = buffer;
	return {buffer, length};
}

std::vector<guchar> ora_archive(const std::vector<std::pair<std::string, std::string>> &entries, bool symlink = false)
{
	std::vector<guchar> bytes(65536);
	size_t used = 0;
	std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
	if (archive_write_set_format_zip(writer.get()) != ARCHIVE_OK ||
	    archive_write_open_memory(writer.get(), bytes.data(), bytes.size(), &used) != ARCHIVE_OK) return {};
	for (const auto &item : entries)
		{
		std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
		archive_entry_set_pathname(entry.get(), item.first.c_str());
		archive_entry_set_filetype(entry.get(), symlink ? AE_IFLNK : AE_IFREG);
		archive_entry_set_perm(entry.get(), 0644);
		archive_entry_set_size(entry.get(), symlink ? 0 : item.second.size());
		if (symlink) archive_entry_set_symlink(entry.get(), "data/layer.png");
		if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK) return {};
		if (!symlink && archive_write_data(writer.get(), item.second.data(), item.second.size()) != static_cast<la_ssize_t>(item.second.size())) return {};
		}
	if (archive_write_close(writer.get()) != ARCHIVE_OK) return {};
	bytes.resize(used);
	return bytes;
}

void initialize(ImageLoaderBackend *backend, bool shrink = false)
{
	backend->init([](gpointer, gint, gint, gint, gint, gpointer) {},
	              [](gpointer loader, gint, gint, gpointer data)
		              {
		              if (data) gdk_pixbuf_loader_set_size(GDK_PIXBUF_LOADER(loader), 1, 1);
		              }, shrink ? backend : nullptr);
}

gboolean decode(ImageLoaderBackend *backend, const std::vector<guchar> &bytes, GError **error)
{
	gsize chunk = 1;
	const auto success = backend->write(bytes.data(), chunk, bytes.size(), error);
	if (success)
		{
		EXPECT_EQ(chunk, bytes.size());
		}
	return success;
}

TEST(OpenRaster, DecodesCompositeColorsAlphaAndThumbnailSize)
{
	for (bool alpha : {false, true})
		for (bool shrink : {false, true})
			{
			auto bytes = ora_archive({{"mimetype", "image/openraster"}, {"data/layer.png", "ignored"}, {"mergedimage.png", png_image(alpha)}});
			ASSERT_FALSE(bytes.empty());
			auto backend = get_image_loader_backend_ora();
			initialize(backend.get(), shrink);
			g_autoptr(GError) error = nullptr;
			ASSERT_TRUE(decode(backend.get(), bytes, &error)) << (error ? error->message : "");
			ASSERT_TRUE(backend->close(&error));
			auto *pixels = backend->get_pixbuf();
			ASSERT_NE(pixels, nullptr);
			EXPECT_EQ(gdk_pixbuf_get_width(pixels), shrink ? 1 : 3);
			EXPECT_EQ(gdk_pixbuf_get_height(pixels), shrink ? 1 : 2);
			EXPECT_EQ(gdk_pixbuf_get_has_alpha(pixels), alpha);
			const auto *rgb = gdk_pixbuf_read_pixels(pixels);
			EXPECT_EQ(rgb[0], 0x20);
			EXPECT_EQ(rgb[1], 0x40);
			EXPECT_EQ(rgb[2], 0x60);
			if (alpha)
				{
				EXPECT_EQ(rgb[3], 0x80);
				}
			}
}

TEST(OpenRaster, RejectsMissingCorruptAndUnsafeComposites)
{
	const auto png = png_image(true);
	for (const auto &bytes : {std::vector<guchar>{'b', 'a', 'd'},
	                         ora_archive({{"data/layer.png", png}}),
	                         ora_archive({{"../mergedimage.png", png}}),
	                         ora_archive({{"mergedimage.png", "not a PNG"}}),
	                         ora_archive({{"mergedimage.png", ""}}),
	                         ora_archive({{"mergedimage.png", ""}}, true)})
		{
		auto backend = get_image_loader_backend_ora();
		initialize(backend.get());
		g_autoptr(GError) error = nullptr;
		EXPECT_FALSE(decode(backend.get(), bytes, &error));
		EXPECT_NE(error, nullptr);
		EXPECT_EQ(backend->get_pixbuf(), nullptr);
		}
}

TEST(OpenRaster, RejectsExcessivePixelDimensionsBeforeAllocation)
{
	auto png = png_image(true);
	ASSERT_GE(png.size(), 33U);
	// Set IHDR to 16384 x 8193 (just over the pixel budget) and update its CRC.
	const guchar dimensions[] = {0, 0, 0x40, 0, 0, 0, 0x20, 1};
	memcpy(png.data() + 16, dimensions, sizeof(dimensions));
	guint32 crc = 0xffffffffU;
	for (int i = 12; i < 29; ++i)
		{
		crc ^= static_cast<guchar>(png[i]);
		for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
		}
	crc ^= 0xffffffffU;
	for (int i = 0; i < 4; ++i) png[29 + i] = (crc >> (24 - i * 8)) & 0xff;
	auto backend = get_image_loader_backend_ora();
	initialize(backend.get());
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(decode(backend.get(), ora_archive({{"mergedimage.png", png}}), &error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(backend->get_pixbuf(), nullptr);
	EXPECT_TRUE(g_error_matches(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY)) << error->message;
}

TEST(OpenRaster, SupportsCancellationAndAbort)
{
	auto bytes = ora_archive({{"mergedimage.png", png_image(false)}});
	for (bool abort : {false, true})
		{
		g_autoptr(GCancellable) cancellable = g_cancellable_new();
		auto backend = get_image_loader_backend_ora(cancellable);
		initialize(backend.get());
		if (abort) backend->abort();
		else g_cancellable_cancel(cancellable);
		g_autoptr(GError) error = nullptr;
		EXPECT_FALSE(decode(backend.get(), bytes, &error));
		ASSERT_NE(error, nullptr);
		EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
		EXPECT_EQ(backend->get_pixbuf(), nullptr);
		}
}

TEST(OpenRaster, RejectsExcessiveArchiveEntrySize)
{
	auto bytes = ora_archive({{"mergedimage.png", png_image(false)}});
	ASSERT_GT(bytes.size(), 30U);
	// Advertise a composite larger than 512 MiB in both ZIP headers.
	for (size_t i = 0; i + 28 < bytes.size(); ++i)
		{
		if (bytes[i] != 'P' || bytes[i + 1] != 'K') continue;
		const bool local = bytes[i + 2] == 3 && bytes[i + 3] == 4;
		const bool central = bytes[i + 2] == 1 && bytes[i + 3] == 2;
		if (!local && !central) continue;
		const size_t offset = i + (local ? 22 : 24);
		bytes[offset] = 1;
		bytes[offset + 1] = 0;
		bytes[offset + 2] = 0;
		bytes[offset + 3] = 0x20;
		}
	auto backend = get_image_loader_backend_ora();
	initialize(backend.get());
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(decode(backend.get(), bytes, &error));
	ASSERT_NE(error, nullptr);
	EXPECT_TRUE(g_error_matches(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY));
	EXPECT_EQ(backend->get_pixbuf(), nullptr);
}

TEST(OpenRaster, CancellationDuringPngDecodeDoesNotPublishPixels)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	auto backend = get_image_loader_backend_ora(cancellable);
	backend->init([](gpointer, gint, gint, gint, gint, gpointer) { ADD_FAILURE() << "Cancelled image was published"; },
	              [](gpointer, gint, gint, gpointer data) { g_cancellable_cancel(G_CANCELLABLE(data)); }, cancellable);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(decode(backend.get(), ora_archive({{"mergedimage.png", png_image(true)}}), &error));
	ASSERT_NE(error, nullptr);
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
	EXPECT_EQ(backend->get_pixbuf(), nullptr);
}

TEST(OpenRaster, UninitializedBackendReportsAnError)
{
	auto backend = get_image_loader_backend_ora();
	backend->set_size(1, 1);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(decode(backend.get(), ora_archive({{"mergedimage.png", png_image(false)}}), &error));
	EXPECT_NE(error, nullptr);
	EXPECT_EQ(backend->get_pixbuf(), nullptr);
}

TEST(OpenRaster, ImageLoaderSelectsOpenRasterForUppercaseExtension)
{
	if (!options) options = conf_options_new();
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-ora-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "image.ORA", nullptr);
	const auto bytes = ora_archive({{"mergedimage.png", png_image(true)}});
	ASSERT_TRUE(g_file_set_contents(path, reinterpret_cast<const gchar *>(bytes.data()), bytes.size(), nullptr));
	auto fd = FileData::new_simple(path);
	g_autoptr(ImageLoader) loader = image_loader_new(fd);
	bool done = false;
	bool failed = false;
	g_signal_connect(loader, "done", G_CALLBACK(+[](ImageLoader *, gpointer data) { *static_cast<bool *>(data) = true; }), &done);
	g_signal_connect(loader, "error", G_CALLBACK(+[](ImageLoader *, gpointer data) { *static_cast<bool *>(data) = true; }), &failed);
	ASSERT_TRUE(image_loader_start(loader));
	const auto deadline = g_get_monotonic_time() + (5 * G_TIME_SPAN_SECOND);
	while (!done && !failed && g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
	EXPECT_TRUE(done);
	EXPECT_FALSE(failed);
	EXPECT_STREQ(fd->format_name, "ora");
	ASSERT_NE(image_loader_get_pixbuf(loader), nullptr);
	EXPECT_EQ(gdk_pixbuf_get_width(image_loader_get_pixbuf(loader)), 3);
	g_unlink(path);
	g_rmdir(directory);
}

} // namespace
#endif
