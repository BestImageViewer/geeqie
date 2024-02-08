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

#include <config.h>

#include "filedata.h"
#include "image-load.h"


static gchar* image_loader_gdk_get_format_name(gpointer loader)
{
	GdkPixbufFormat *format;

	format = gdk_pixbuf_loader_get_format(GDK_PIXBUF_LOADER(loader));
	if (format)
		{
		return gdk_pixbuf_format_get_name(format);
		}

	return nullptr;
}

static gchar** image_loader_gdk_get_format_mime_types(gpointer loader)
{
	return gdk_pixbuf_format_get_mime_types(gdk_pixbuf_loader_get_format(GDK_PIXBUF_LOADER(loader)));
}

static gpointer image_loader_gdk_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	GdkPixbufLoader *loader;

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
	g_signal_connect(G_OBJECT(loader), "size_prepared", G_CALLBACK(size_cb), data);
	g_signal_connect(G_OBJECT(loader), "area_prepared", G_CALLBACK(area_prepared_cb), data);
	return loader;
}

static void image_loader_gdk_abort(gpointer)
{
}

static void image_loader_gdk_free(gpointer loader)
{
	g_object_unref(G_OBJECT(loader));
}

void image_loader_backend_set_default(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_gdk_new;
	funcs->set_size = reinterpret_cast<ImageLoaderBackendFuncSetSize>(gdk_pixbuf_loader_set_size);
	funcs->load = nullptr;
	funcs->write = reinterpret_cast<ImageLoaderBackendFuncWrite>(gdk_pixbuf_loader_write);
	funcs->get_pixbuf = reinterpret_cast<ImageLoaderBackendFuncGetPixbuf>(gdk_pixbuf_loader_get_pixbuf);
	funcs->close = reinterpret_cast<ImageLoaderBackendFuncClose>(gdk_pixbuf_loader_close);
	funcs->abort = image_loader_gdk_abort;
	funcs->free = image_loader_gdk_free;

	funcs->get_format_name = image_loader_gdk_get_format_name;
	funcs->get_format_mime_types = image_loader_gdk_get_format_mime_types;
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
