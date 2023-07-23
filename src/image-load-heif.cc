/*
 * Copyright (C) 20019 - The Geeqie Team
 *
 * Author: Colin Clark
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "main.h"

#include "image-load.h"
#include "image-load-heif.h"
#include <vector>
#ifdef HAVE_HEIF
#include <libheif/heif.h>

struct ImageLoaderHEIF {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
	gint page_num;
	gint page_total;
};

static void free_buffer(guchar *, gpointer data)
{
	heif_image_release(static_cast<const struct heif_image*>(data));
}

static gboolean image_loader_heif_load(gpointer loader, const guchar *buf, gsize count, GError **)
{
	auto ld = static_cast<ImageLoaderHEIF *>(loader);
	struct heif_context* ctx;
	struct heif_image* img;
	struct heif_error error_code;
	struct heif_image_handle* handle;
	guint8* data;
	gint width, height;
	gint stride;
	gboolean alpha;
	gint page_total;

	ctx = heif_context_alloc();

	error_code = heif_context_read_from_memory_without_copy(ctx, buf, count, nullptr);
	if (error_code.code)
		{
		log_printf("warning: heif reader error: %s\n", error_code.message);
		heif_context_free(ctx);
		return FALSE;
		}
	else
		{
		page_total = heif_context_get_number_of_top_level_images(ctx);
		ld->page_total = page_total;

		std::vector<heif_item_id> IDs(page_total);

		/* get list of all (top level) image IDs */
		heif_context_get_list_of_top_level_image_IDs(ctx, IDs.data(), page_total);

		error_code = heif_context_get_image_handle(ctx, IDs[ld->page_num], &handle);
		if (error_code.code)
			{
			log_printf("warning:  heif reader error: %s\n", error_code.message);
			heif_context_free(ctx);
			return FALSE;
			}

		// decode the image and convert colorspace to RGB, saved as 24bit interleaved
		error_code = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_24bit, nullptr);
		if (error_code.code)
			{
			log_printf("warning: heif reader error: %s\n", error_code.message);
			heif_context_free(ctx);
			return FALSE;
			}

		data = heif_image_get_plane(img, heif_channel_interleaved, &stride);

		height = heif_image_get_height(img,heif_channel_interleaved);
		width = heif_image_get_width(img,heif_channel_interleaved);
		alpha = heif_image_handle_has_alpha_channel(handle);
		heif_image_handle_release(handle);

		ld->pixbuf = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, alpha, 8, width, height, stride, free_buffer, img);

		ld->area_updated_cb(loader, 0, 0, width, height, ld->data);
		}

	heif_context_free(ctx);

	return TRUE;
}

static gpointer image_loader_heif_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderHEIF, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	loader->page_num = 0;
	return loader;
}

static void image_loader_heif_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderHEIF *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_heif_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderHEIF *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_heif_get_format_name(gpointer)
{
	return g_strdup("heif");
}

static gchar** image_loader_heif_get_format_mime_types(gpointer)
{
	static const gchar *mime[] = {"image/heic", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

static void image_loader_heif_set_page_num(gpointer loader, gint page_num)
{
	auto ld = static_cast<ImageLoaderHEIF *>(loader);

	ld->page_num = page_num;
}

static gint image_loader_heif_get_page_total(gpointer loader)
{
	auto ld = static_cast<ImageLoaderHEIF *>(loader);

	return ld->page_total;
}

static gboolean image_loader_heif_close(gpointer, GError **)
{
	return TRUE;
}

static void image_loader_heif_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderHEIF *>(loader);
	ld->abort = TRUE;
}

static void image_loader_heif_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderHEIF *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_heif(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_heif_new;
	funcs->set_size = image_loader_heif_set_size;
	funcs->load = image_loader_heif_load;
	funcs->write = nullptr;
	funcs->get_pixbuf = image_loader_heif_get_pixbuf;
	funcs->close = image_loader_heif_close;
	funcs->abort = image_loader_heif_abort;
	funcs->free = image_loader_heif_free;
	funcs->get_format_name = image_loader_heif_get_format_name;
	funcs->get_format_mime_types = image_loader_heif_get_format_mime_types;
	funcs->set_page_num = image_loader_heif_set_page_num;
	funcs->get_page_total = image_loader_heif_get_page_total;
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
