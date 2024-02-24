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

#include "image-load-heif.h"

#include <config.h>

#if HAVE_HEIF
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <libheif/heif.h>

#include "debug.h"
#include "image-load.h"

namespace
{

struct ImageLoaderHEIF : public ImageLoaderBackend
{
public:
	~ImageLoaderHEIF() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;
	void set_page_num(gint page_num) override;
	gint get_page_total() override;

private:
	AreaUpdatedCb area_updated_cb;
	gpointer data;

	GdkPixbuf *pixbuf;
	gint page_num;
	gint page_total;
};

void free_buffer(guchar *, gpointer data)
{
	heif_image_release(static_cast<const struct heif_image*>(data));
}

gboolean ImageLoaderHEIF::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	struct heif_context* ctx;
	struct heif_image* img;
	struct heif_error error_code;
	struct heif_image_handle* handle;
	guint8* pixels;
	gint width;
	gint height;
	gint stride;
	gboolean alpha;

	ctx = heif_context_alloc();

	error_code = heif_context_read_from_memory_without_copy(ctx, buf, count, nullptr);
	if (error_code.code)
		{
		log_printf("warning: heif reader error: %s\n", error_code.message);
		heif_context_free(ctx);
		return FALSE;
		}

	page_total = heif_context_get_number_of_top_level_images(ctx);

	std::vector<heif_item_id> IDs(page_total);

	/* get list of all (top level) image IDs */
	heif_context_get_list_of_top_level_image_IDs(ctx, IDs.data(), page_total);

	error_code = heif_context_get_image_handle(ctx, IDs[page_num], &handle);
	if (error_code.code)
		{
		log_printf("warning: heif reader error: %s\n", error_code.message);
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

	pixels = heif_image_get_plane(img, heif_channel_interleaved, &stride);

	height = heif_image_get_height(img,heif_channel_interleaved);
	width = heif_image_get_width(img,heif_channel_interleaved);
	alpha = heif_image_handle_has_alpha_channel(handle);
	heif_image_handle_release(handle);

	pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, alpha, 8, width, height, stride, free_buffer, img);

	area_updated_cb(nullptr, 0, 0, width, height, data);

	heif_context_free(ctx);

	chunk_size = count;
	return TRUE;
}

void ImageLoaderHEIF::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
	page_num = 0;
}

GdkPixbuf *ImageLoaderHEIF::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderHEIF::get_format_name()
{
	return g_strdup("heif");
}

gchar **ImageLoaderHEIF::get_format_mime_types()
{
	static const gchar *mime[] = {"image/heic", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderHEIF::set_page_num(gint page_num)
{
	this->page_num = page_num;
}

gint ImageLoaderHEIF::get_page_total()
{
	return page_total;
}

ImageLoaderHEIF::~ImageLoaderHEIF()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_heif()
{
	return std::make_unique<ImageLoaderHEIF>();
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
