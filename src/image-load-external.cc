/*
 * Copyright (C) 2021 - The Geeqie Team
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

#include "image-load-external.h"

#include <config.h>

#include "filedata.h"
#include "image-load.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"

struct ImageLoaderExternal {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
};

static gboolean image_loader_external_load(gpointer loader, const guchar *, gsize, GError **)
{
	auto ld = static_cast<ImageLoaderExternal *>(loader);
	auto il = static_cast<ImageLoader *>(ld->data);
	gchar *cmd_line;
	gchar *randname;
	gchar *tilde_filename;

	tilde_filename = expand_tilde(options->external_preview.extract);

	randname = g_strdup("/tmp/geeqie_external_preview_XXXXXX");
	g_mkstemp(randname);

	cmd_line = g_strdup_printf("\"%s\" \"%s\" \"%s\"" , tilde_filename, il->fd->path, randname);

	runcmd(cmd_line);

	ld->pixbuf = gdk_pixbuf_new_from_file(randname, nullptr);

	ld->area_updated_cb(loader, 0, 0, gdk_pixbuf_get_width(ld->pixbuf), gdk_pixbuf_get_height(ld->pixbuf), ld->data);

	g_free(cmd_line);
	unlink_file(randname);
	g_free(randname);
	g_free(tilde_filename);

	return TRUE;
}

static gpointer image_loader_external_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderExternal, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return loader;
}

static void image_loader_external_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderExternal *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_external_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderExternal *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_external_get_format_name(gpointer)
{
	return g_strdup("external");
}

static gchar** image_loader_external_get_format_mime_types(gpointer)
{
	static const gchar *mime[] = {"application/octet-stream", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gboolean image_loader_external_close(gpointer, GError **)
{
	return TRUE;
}

static void image_loader_external_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderExternal *>(loader);
	ld->abort = TRUE;
}

static void image_loader_external_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderExternal *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_external(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_external_new;
	funcs->set_size = image_loader_external_set_size;
	funcs->load = image_loader_external_load;
	funcs->write = nullptr;
	funcs->get_pixbuf = image_loader_external_get_pixbuf;
	funcs->close = image_loader_external_close;
	funcs->abort = image_loader_external_abort;
	funcs->free = image_loader_external_free;
	funcs->get_format_name = image_loader_external_get_format_name;
	funcs->get_format_mime_types = image_loader_external_get_format_mime_types;
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
