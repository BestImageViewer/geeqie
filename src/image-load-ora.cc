/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "image-load-ora.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>

#include <archive.h>
#include <archive_entry.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "image-load.h"
#include "intl.h"

namespace
{

// Bound both archive expansion and decoded pixels, independently of thumbnail scaling.
constexpr guint64 max_composite_bytes = 512ULL * 1024 * 1024;
constexpr guint64 max_pixels = max_composite_bytes / 4;

class ImageLoaderOra : public ImageLoaderBackend
{
public:
	explicit ImageLoaderOra(GCancellable *cancel) : cancellable(cancel ? G_CANCELLABLE(g_object_ref(cancel)) : nullptr) {}
	~ImageLoaderOra() override
	{
		close(nullptr);
		g_clear_object(&loader);
		g_clear_object(&cancellable);
		g_clear_error(&init_error);
	}

	void init(AreaUpdatedCb updated, SizePreparedCb prepared, gpointer data) override
	{
		area_updated = updated;
		size_prepared = prepared;
		callback_data = data;
		loader = gdk_pixbuf_loader_new_with_type("png", &init_error);
		if (!loader) return;
		g_signal_connect(loader, "size-prepared", G_CALLBACK(+[](GdkPixbufLoader *png, gint width, gint height, gpointer data)
			{
			auto *self = static_cast<ImageLoaderOra *>(data);
			if (width <= 0 || height <= 0 || static_cast<guint64>(width) * height > max_pixels)
				{
				self->oversized = true;
				gdk_pixbuf_loader_set_size(png, 1, 1);
				return;
				}
			self->size_prepared(png, width, height, self->callback_data);
			}), this);
	}

	void set_size(int width, int height) override
	{
		if (loader) gdk_pixbuf_loader_set_size(loader, width, height);
	}

	void abort() override { aborted = true; }
	GdkPixbuf *get_pixbuf() override { return loader && !failed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr; }
	gchar *get_format_name() override { return g_strdup("ora"); }
	gchar **get_format_mime_types() override
	{
		const gchar *mime[] = {"image/openraster", nullptr};
		return g_strdupv(const_cast<gchar **>(mime));
	}

	gboolean close(GError **error) override
	{
		if (!loader || closed) return !failed;
		closed = true;
		return gdk_pixbuf_loader_close(loader, error);
	}

	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;

private:
	bool cancelled(GError **error)
	{
		if (cancellable && g_cancellable_set_error_if_cancelled(cancellable, error)) return true;
		if (!aborted) return false;
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED, _("Image loading cancelled."));
		return true;
	}

	GdkPixbufLoader *loader = nullptr;
	GCancellable *cancellable;
	GError *init_error = nullptr;
	AreaUpdatedCb area_updated = nullptr;
	SizePreparedCb size_prepared = nullptr;
	gpointer callback_data = nullptr;
	std::atomic<bool> aborted{false};
	bool oversized = false;
	bool closed = false;
	bool failed = false;
};

gboolean ImageLoaderOra::write(const guchar *buf, gsize &chunk_size, gsize count, GError **error)
{
	failed = true;
	if (!loader)
		{
		g_propagate_error(error, init_error ? g_error_copy(init_error) :
		                  g_error_new_literal(GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_UNKNOWN_TYPE, _("No PNG loader is available.")));
		return FALSE;
		}
	if (cancelled(error)) return FALSE;
	std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
	archive_read_support_format_zip(reader.get());
	if (archive_read_open_memory(reader.get(), buf, count) != ARCHIVE_OK)
		{
		g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE, _("Invalid OpenRaster archive."));
		return FALSE;
		}
	archive_entry *entry = nullptr;
	while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK)
		{
		if (cancelled(error)) return FALSE;
		if (g_strcmp0(archive_entry_pathname(entry), "mergedimage.png") != 0) continue;
		if (archive_entry_filetype(entry) != AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) break;
		if (archive_entry_size(entry) > static_cast<la_int64_t>(max_composite_bytes))
			{
			g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY, _("OpenRaster composite image is too large."));
			return FALSE;
			}
		std::array<guchar, 32768> buffer;
		std::array<guchar, 24> header;
		gsize header_size = 0;
		guint64 total = 0;
		la_ssize_t bytes;
		while ((bytes = archive_read_data(reader.get(), buffer.data(), buffer.size())) > 0)
			{
			if (cancelled(error)) return FALSE;
			total += bytes;
			if (total > max_composite_bytes)
				{
				g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY, _("OpenRaster composite image is too large."));
				return FALSE;
				}
			gsize offset = 0;
			if (header_size < header.size())
				{
				offset = std::min(static_cast<gsize>(bytes), header.size() - header_size);
				std::copy_n(buffer.data(), offset, header.data() + header_size);
				header_size += offset;
				if (header_size < header.size()) continue;
				// Check dimensions before invoking decoders that defer size-prepared until after decoding.
				if (memcmp(header.data(), "\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16) != 0) // NOTRANSLATE: PNG signature and IHDR chunk.
					{
					g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE, _("Invalid OpenRaster composite PNG."));
					return FALSE;
					}
				const auto dimension = [&header](gsize index) -> guint64
					{
					return (static_cast<guint32>(header[index]) << 24) | (static_cast<guint32>(header[index + 1]) << 16) |
					       (static_cast<guint32>(header[index + 2]) << 8) | header[index + 3];
					};
				const auto width = dimension(16);
				const auto height = dimension(20);
				if (!width || !height || width * height > max_pixels)
					{
					g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY, _("OpenRaster composite image is too large."));
					return FALSE;
					}
				if (!gdk_pixbuf_loader_write(loader, header.data(), header.size(), error)) return FALSE;
				}
			const bool decoded = offset == static_cast<gsize>(bytes) ||
			                     gdk_pixbuf_loader_write(loader, buffer.data() + offset, bytes - offset, error);
			if (oversized)
				{
				g_clear_error(error);
				g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY, _("OpenRaster composite image is too large."));
				return FALSE;
				}
			if (!decoded) return FALSE;
			}
		if (bytes < 0) break;
		if (!close(error)) return FALSE;
		if (cancelled(error)) return FALSE;
		chunk_size = count;
		failed = false;
		// Publish only a complete, validated composite, never partial archive data.
		auto *pixels = get_pixbuf();
		area_updated(loader, 0, 0, gdk_pixbuf_get_width(pixels), gdk_pixbuf_get_height(pixels), callback_data);
		return TRUE;
		}
	g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE, _("OpenRaster archive has no readable mergedimage.png composite."));
	return FALSE;
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_ora(GCancellable *cancellable)
{
	return std::make_unique<ImageLoaderOra>(cancellable);
}
