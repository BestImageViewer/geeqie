/*
 * Copyright (C) 2019 The Geeqie Team
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

#include "image-load-svgz.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>

#include "image-load.h"

namespace
{

struct ImageLoaderSvgz : public ImageLoaderBackend
{
public:
	~ImageLoaderSvgz() override;

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

gchar *ImageLoaderSvgz::get_format_name()
{
	return g_strdup("svg");
}

gchar **ImageLoaderSvgz::get_format_mime_types()
{
	static const gchar *mime[] = {"image/svg", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderSvgz::init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data)
{
	g_autoptr(GError) error = nullptr;

	loader = gdk_pixbuf_loader_new_with_mime_type("image/svg", &error);
	if (error)
		{
		return;
		}

	g_signal_connect(G_OBJECT(loader), "area_updated", G_CALLBACK(area_updated_cb), data);
	g_signal_connect(G_OBJECT(loader), "size_prepared", G_CALLBACK(size_prepared_cb), data);
	g_signal_connect(G_OBJECT(loader), "area_prepared", G_CALLBACK(area_prepared_cb), data);
}

void ImageLoaderSvgz::set_size(int width, int height)
{
	gdk_pixbuf_loader_set_size(loader, width, height);
}

gboolean ImageLoaderSvgz::write(const guchar *buf, gsize &chunk_size, gsize, GError **error)
{
	return gdk_pixbuf_loader_write(loader, buf, chunk_size, error);
}

GdkPixbuf *ImageLoaderSvgz::get_pixbuf()
{
	return gdk_pixbuf_loader_get_pixbuf(loader);
}

gboolean ImageLoaderSvgz::close(GError **error)
{
	return gdk_pixbuf_loader_close(loader, error);
}

ImageLoaderSvgz::~ImageLoaderSvgz()
{
	g_object_unref(G_OBJECT(loader));
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_svgz()
{
	return std::make_unique<ImageLoaderSvgz>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
