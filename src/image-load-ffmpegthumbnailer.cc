/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Tomasz Golinski <tomaszg@math.uwb.edu.pl>
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

#include "image-load-ffmpegthumbnailer.h"

#include <config.h>

#if HAVE_FFMPEGTHUMBNAILER
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <libffmpegthumbnailer/ffmpegthumbnailertypes.h>
#include <libffmpegthumbnailer/imagetypes.h>
#include <libffmpegthumbnailer/videothumbnailerc.h> //TODO Use videothumbnailer.h?

#include "debug.h"
#include "filedata.h"
#include "image-load.h"
#include "options.h"

namespace
{

struct ImageLoaderFT {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;

	video_thumbnailer *vt;

	gpointer data;

	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;

};

#if HAVE_FFMPEGTHUMBNAILER_RGB
void image_loader_ft_log_cb(ThumbnailerLogLevel log_level, const char* msg)
{
	if (log_level == ThumbnailerLogLevelError)
		log_printf("ImageLoaderFFmpegthumbnailer: %s",msg);
	else
		DEBUG_1("ImageLoaderFFmpegthumbnailer: %s",msg);
}
#endif

void image_loader_ft_destroy_image_data(guchar *, gpointer data)
{
	auto image = static_cast<image_data *>(data);

	video_thumbnailer_destroy_image_data (image);
}

gchar* image_loader_ft_get_format_name(gpointer)
{
	return g_strdup("ffmpeg");
}

gchar** image_loader_ft_get_format_mime_types(gpointer)
{
	static const gchar *mime[] = {"video/mp4", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

gpointer image_loader_ft_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderFT, 1);

	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;

	loader->vt = video_thumbnailer_create();
	loader->vt->overlay_film_strip = 1;
	loader->vt->maintain_aspect_ratio = 1;
#if HAVE_FFMPEGTHUMBNAILER_RGB
	video_thumbnailer_set_log_callback(loader->vt, image_loader_ft_log_cb);
#endif

	return loader;
}

void image_loader_ft_set_size(gpointer loader, int width, int height)
{
	auto lft = static_cast<ImageLoaderFT *>(loader);
	lft->requested_width = width;
	lft->requested_height = height;
	DEBUG_1("TG: setting size, w=%d, h=%d", width, height);
}

gboolean image_loader_ft_write (gpointer loader, const guchar *, gsize &chunk_size, gsize count, GError **)
{
	auto lft = static_cast<ImageLoaderFT *>(loader);
	auto il = static_cast<ImageLoader *>(lft->data);

	image_data *image = video_thumbnailer_create_image_data();

#if HAVE_FFMPEGTHUMBNAILER_WH
	video_thumbnailer_set_size(lft->vt, lft->requested_width, lft->requested_height);
#else
	lft->vt->thumbnail_size = MAX(lft->requested_width,lft->requested_height);
#endif

#if HAVE_FFMPEGTHUMBNAILER_METADATA
	lft->vt->prefer_embedded_metadata = options->thumbnails.use_ft_metadata ? 1 : 0;
#endif

#if HAVE_FFMPEGTHUMBNAILER_RGB
	lft->vt->thumbnail_image_type = Rgb;
#else
	lft->vt->thumbnail_image_type = Png;
#endif

	video_thumbnailer_generate_thumbnail_to_buffer (lft->vt, il->fd->path, image);

#if HAVE_FFMPEGTHUMBNAILER_RGB
	lft->pixbuf  = gdk_pixbuf_new_from_data (image->image_data_ptr, GDK_COLORSPACE_RGB, FALSE, 8, image->image_data_width, image->image_data_height,  image->image_data_width*3, image_loader_ft_destroy_image_data, image);
	lft->size_cb(loader, image->image_data_width, image->image_data_height, lft->data);
	lft->area_updated_cb(loader, 0, 0, image->image_data_width, image->image_data_height, lft->data);
#else
	GInputStream *image_stream;
	image_stream = g_memory_input_stream_new_from_data (image->image_data_ptr, image->image_data_size, nullptr);

	if (image_stream == nullptr)
		{
		video_thumbnailer_destroy_image_data (image);
		DEBUG_1("FFmpegthumbnailer: cannot open stream for %s", il->fd->path);
		return FALSE;
		}

	lft->pixbuf  = gdk_pixbuf_new_from_stream (image_stream, nullptr, nullptr);
	lft->size_cb(loader, gdk_pixbuf_get_width(lft->pixbuf), gdk_pixbuf_get_height(lft->pixbuf), lft->data);
	g_object_unref (image_stream);
	video_thumbnailer_destroy_image_data (image);
#endif

	if (!lft->pixbuf)
		{
		DEBUG_1("FFmpegthumbnailer: no frame generated for %s", il->fd->path);
		return FALSE;
		}

	/** See comment in image_loader_area_prepared_cb
	 * Geeqie uses area_prepared signal to fill pixbuf with background color.
	 * We can't do it here as pixbuf already contains the data
	 * lft->area_prepared_cb(loader, lft->data);
	 */
	lft->area_updated_cb(loader, 0, 0, gdk_pixbuf_get_width(lft->pixbuf), gdk_pixbuf_get_height(lft->pixbuf), lft->data);

	chunk_size = count;
	return TRUE;
}

GdkPixbuf* image_loader_ft_get_pixbuf(gpointer loader)
{
	auto lft = static_cast<ImageLoaderFT *>(loader);
	return lft->pixbuf;
}

void image_loader_ft_abort(gpointer)
{
}

gboolean image_loader_ft_close(gpointer, GError **)
{
	return TRUE;
}

void image_loader_ft_free(gpointer loader)
{
	auto lft = static_cast<ImageLoaderFT *>(loader);
	if (lft->pixbuf) g_object_unref(lft->pixbuf);
	video_thumbnailer_destroy (lft->vt);

	g_free(lft);
}

} // namespace

void image_loader_backend_set_ft(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_ft_new;
	funcs->set_size = image_loader_ft_set_size;
	funcs->write = image_loader_ft_write;
	funcs->get_pixbuf = image_loader_ft_get_pixbuf;
	funcs->close = image_loader_ft_close;
	funcs->abort = image_loader_ft_abort;
	funcs->free = image_loader_ft_free;

	funcs->get_format_name = image_loader_ft_get_format_name;
	funcs->get_format_mime_types = image_loader_ft_get_format_mime_types;
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
