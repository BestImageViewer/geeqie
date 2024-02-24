/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#include "image-load-gdk.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>

#include "filedata.h"
#include "image-load.h"

namespace
{

struct ImageLoaderGdk : public ImageLoaderBackend
{
public:
	~ImageLoaderGdk() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	void set_size(int width, int height) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gboolean close(GError **error) override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;

private:
	GdkPixbufLoader *loader;
};

gchar *ImageLoaderGdk::get_format_name()
{
	GdkPixbufFormat *format;

	format = gdk_pixbuf_loader_get_format(loader);
	if (format)
		{
		return gdk_pixbuf_format_get_name(format);
		}

	return nullptr;
}

gchar **ImageLoaderGdk::get_format_mime_types()
{
	return gdk_pixbuf_format_get_mime_types(gdk_pixbuf_loader_get_format(loader));
}

void ImageLoaderGdk::init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);

	/** @FIXME gdk-pixbuf-loader does not recognize .xbm files unless
	 * the mime type is given. There should be a general case */
	if (g_ascii_strncasecmp(il->fd->extension, ".xbm", 4) == 0)
		{
		loader = gdk_pixbuf_loader_new_with_mime_type("image/x-xbitmap", nullptr);
		}
	else
		{
		loader = gdk_pixbuf_loader_new();
		}

	g_signal_connect(G_OBJECT(loader), "area_updated", G_CALLBACK(area_updated_cb), data);
	g_signal_connect(G_OBJECT(loader), "size_prepared", G_CALLBACK(size_prepared_cb), data);
	g_signal_connect(G_OBJECT(loader), "area_prepared", G_CALLBACK(area_prepared_cb), data);
}

void ImageLoaderGdk::set_size(int width, int height)
{
	gdk_pixbuf_loader_set_size(loader, width, height);
}

gboolean ImageLoaderGdk::write(const guchar *buf, gsize &chunk_size, gsize, GError **error)
{
	return gdk_pixbuf_loader_write(loader, buf, chunk_size, error);
}

GdkPixbuf *ImageLoaderGdk::get_pixbuf()
{
	return gdk_pixbuf_loader_get_pixbuf(loader);
}

gboolean ImageLoaderGdk::close(GError **error)
{
	return gdk_pixbuf_loader_close(loader, error);
}

ImageLoaderGdk::~ImageLoaderGdk()
{
	g_object_unref(G_OBJECT(loader));
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_default()
{
	return std::make_unique<ImageLoaderGdk>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
