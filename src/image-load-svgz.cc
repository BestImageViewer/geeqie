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

#include "main.h"
#include "image-load.h"
#include "image-load-svgz.h"


static gchar* image_loader_svgz_get_format_name(gpointer UNUSED(loader))
{
	return g_strdup("svg");

}
static gchar** image_loader_svgz_get_format_mime_types(gpointer UNUSED(loader))
{
	static const gchar *mime[] = {"image/svg", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gpointer image_loader_svgz_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	GError *error = nullptr;

	GdkPixbufLoader *loader = gdk_pixbuf_loader_new_with_mime_type("image/svg", &error);
	if (error)
		{
		g_error_free(error);
		return nullptr;
		}

	g_signal_connect(G_OBJECT(loader), "area_updated", G_CALLBACK(area_updated_cb), data);
	g_signal_connect(G_OBJECT(loader), "size_prepared", G_CALLBACK(size_cb), data);
	g_signal_connect(G_OBJECT(loader), "area_prepared", G_CALLBACK(area_prepared_cb), data);
	return loader;
}

static void image_loader_svgz_abort(gpointer UNUSED(loader))
{
}

static void image_loader_svgz_free(gpointer loader)
{
	g_object_unref(G_OBJECT(loader));
}

void image_loader_backend_set_svgz(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_svgz_new;
	funcs->set_size = reinterpret_cast<ImageLoaderBackendFuncSetSize>(gdk_pixbuf_loader_set_size);
	funcs->load = nullptr;
	funcs->write = reinterpret_cast<ImageLoaderBackendFuncWrite>(gdk_pixbuf_loader_write);
	funcs->get_pixbuf = reinterpret_cast<ImageLoaderBackendFuncGetPixbuf>(gdk_pixbuf_loader_get_pixbuf);
	funcs->close = reinterpret_cast<ImageLoaderBackendFuncClose>(gdk_pixbuf_loader_close);
	funcs->abort = image_loader_svgz_abort;
	funcs->free = image_loader_svgz_free;

	funcs->get_format_name = image_loader_svgz_get_format_name;
	funcs->get_format_mime_types = image_loader_svgz_get_format_mime_types;
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
