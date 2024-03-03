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

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <libffmpegthumbnailer/ffmpegthumbnailertypes.h>
#include <libffmpegthumbnailer/imagetypes.h>
#include <libffmpegthumbnailer/videothumbnailerc.h> //TODO Use videothumbnailer.h?

#include <config.h>

#include "debug.h"
#include "filedata.h"
#include "image-load.h"
#include "options.h"

namespace
{

struct ImageLoaderFT : public ImageLoaderBackend
{
public:
	~ImageLoaderFT() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	void set_size(int width, int height) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;

private:
	AreaUpdatedCb area_updated_cb;
	SizePreparedCb size_prepared_cb;
	AreaPreparedCb area_prepared_cb;
	gpointer data;

	video_thumbnailer *vt;

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

gchar *ImageLoaderFT::get_format_name()
{
	return g_strdup("ffmpeg");
}

gchar **ImageLoaderFT::get_format_mime_types()
{
	static const gchar *mime[] = {"video/mp4", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderFT::init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->size_prepared_cb = size_prepared_cb;
	this->area_prepared_cb = area_prepared_cb;
	this->data = data;

	vt = video_thumbnailer_create();
	vt->overlay_film_strip = 1;
	vt->maintain_aspect_ratio = 1;
#if HAVE_FFMPEGTHUMBNAILER_RGB
	video_thumbnailer_set_log_callback(vt, image_loader_ft_log_cb);
#endif
}

void ImageLoaderFT::set_size(int width, int height)
{
	requested_width = width;
	requested_height = height;
	DEBUG_1("TG: setting size, w=%d, h=%d", width, height);
}

gboolean ImageLoaderFT::write(const guchar *, gsize &chunk_size, gsize count, GError **)
{
	auto il = static_cast<ImageLoader *>(data);

	image_data *image = video_thumbnailer_create_image_data();

#if HAVE_FFMPEGTHUMBNAILER_WH
	video_thumbnailer_set_size(vt, requested_width, requested_height);
#else
	vt->thumbnail_size = MAX(requested_width, requested_height);
#endif

#if HAVE_FFMPEGTHUMBNAILER_METADATA
	vt->prefer_embedded_metadata = options->thumbnails.use_ft_metadata ? 1 : 0;
#endif

#if HAVE_FFMPEGTHUMBNAILER_RGB
	vt->thumbnail_image_type = Rgb;
#else
	vt->thumbnail_image_type = Png;
#endif

	video_thumbnailer_generate_thumbnail_to_buffer (vt, il->fd->path, image);

#if HAVE_FFMPEGTHUMBNAILER_RGB
	pixbuf = gdk_pixbuf_new_from_data (image->image_data_ptr, GDK_COLORSPACE_RGB, FALSE, 8, image->image_data_width, image->image_data_height, image->image_data_width*3, image_loader_ft_destroy_image_data, image);
	size_prepared_cb(nullptr, image->image_data_width, image->image_data_height, data);
	area_updated_cb(nullptr, 0, 0, image->image_data_width, image->image_data_height, data);
#else
	GInputStream *image_stream;
	image_stream = g_memory_input_stream_new_from_data (image->image_data_ptr, image->image_data_size, nullptr);

	if (image_stream == nullptr)
		{
		video_thumbnailer_destroy_image_data (image);
		DEBUG_1("FFmpegthumbnailer: cannot open stream for %s", il->fd->path);
		return FALSE;
		}

	pixbuf = gdk_pixbuf_new_from_stream (image_stream, nullptr, nullptr);
	size_prepared_cb(nullptr, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), data);
	g_object_unref (image_stream);
	video_thumbnailer_destroy_image_data (image);
#endif

	if (!pixbuf)
		{
		DEBUG_1("FFmpegthumbnailer: no frame generated for %s", il->fd->path);
		return FALSE;
		}

	/** See comment in image_loader_area_prepared_cb
	 * Geeqie uses area_prepared signal to fill pixbuf with background color.
	 * We can't do it here as pixbuf already contains the data
	 * area_prepared_cb(data);
	 */
	area_updated_cb(nullptr, 0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), data);

	chunk_size = count;
	return TRUE;
}

GdkPixbuf *ImageLoaderFT::get_pixbuf()
{
	return pixbuf;
}

ImageLoaderFT::~ImageLoaderFT()
{
	if (pixbuf) g_object_unref(pixbuf);
	video_thumbnailer_destroy (vt);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_ft()
{
	return std::make_unique<ImageLoaderFT>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
