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

#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <libheif/heif_cxx.h>

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
	heif::Context ctx{};

	try
		{
		ctx.read_from_memory_without_copy(buf, count);

		page_total = ctx.get_number_of_top_level_images();

		/* get list of all (top level) image IDs */
		std::vector<heif_item_id> IDs = ctx.get_list_of_top_level_image_IDs();

		heif::ImageHandle handle = ctx.get_image_handle(IDs[page_num]);

		// decode the image and convert colorspace to RGB, saved as 24bit interleaved
		heif_image *img;
		heif_error error = heif_decode_image(handle.get_raw_image_handle(), &img, heif_colorspace_RGB, heif_chroma_interleaved_24bit, nullptr);
		if (error.code) throw heif::Error(error);

		gint stride;
		guint8* pixels = heif_image_get_plane(img, heif_channel_interleaved, &stride);
		gint width = heif_image_get_width(img,heif_channel_interleaved);
		gint height = heif_image_get_height(img,heif_channel_interleaved);
		gboolean alpha = handle.has_alpha_channel();

		pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, alpha, 8, width, height, stride, free_buffer, img);

		area_updated_cb(nullptr, 0, 0, width, height, data);
		}
	catch (const heif::Error &error)
		{
		log_printf("warning: heif reader error: %s\n", error.get_message().c_str());
		return FALSE;
		}

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

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
