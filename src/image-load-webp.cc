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

#include <config.h>

#if HAVE_WEBP
#include "image-load-webp.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <webp/decode.h>

#include "debug.h"
#include "image-load.h"

namespace
{

struct ImageLoaderWEBP : public ImageLoaderBackend
{
public:
	~ImageLoaderWEBP() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;

private:
	AreaUpdatedCb area_updated_cb;
	gpointer data;

	GdkPixbuf *pixbuf;
};

void free_buffer(guchar *pixels, gpointer)
{
	g_free(pixels);
}

gboolean ImageLoaderWEBP::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	guint8* pixels;
	gint width;
	gint height;
	gboolean res_info;
	WebPBitstreamFeatures features;
	VP8StatusCode status_code;

	res_info = WebPGetInfo(buf, count, &width, &height);
	if (!res_info)
		{
		log_printf("warning: webp reader error\n");
		return FALSE;
		}

	status_code = WebPGetFeatures(buf, count, &features);
	if (status_code != VP8_STATUS_OK)
		{
		log_printf("warning: webp reader error\n");
		return FALSE;
		}

	if (features.has_alpha)
		{
		pixels = WebPDecodeRGBA(buf, count, &width, &height);
		}
	else
		{
		pixels = WebPDecodeRGB(buf, count, &width, &height);
		}

	pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, features.has_alpha, 8, width, height, width * (features.has_alpha ? 4 : 3), free_buffer, nullptr);

	area_updated_cb(nullptr, 0, 0, width, height, data);

	chunk_size = count;
	return TRUE;
}

void ImageLoaderWEBP::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderWEBP::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderWEBP::get_format_name()
{
	return g_strdup("webp");
}

gchar **ImageLoaderWEBP::get_format_mime_types()
{
	static const gchar *mime[] = {"image/webp", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

ImageLoaderWEBP::~ImageLoaderWEBP()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace


std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_webp()
{
	DEBUG_0("        "     );
	return std::make_unique<ImageLoaderWEBP>();
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
