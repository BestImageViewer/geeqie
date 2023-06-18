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

#include "main.h"

#include "image-load.h"
#include "image-load-djvu.h"

#ifdef HAVE_DJVU

#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>

struct ImageLoaderDJVU {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
	gint page_num;
	gint page_total;
};

static void free_buffer(guchar *pixels, gpointer UNUSED(data))
{
	g_free (pixels);;
}

static gboolean image_loader_djvu_load(gpointer loader, const guchar *buf, gsize count, GError **UNUSED(error))
{
	auto ld = static_cast<ImageLoaderDJVU *>(loader);
	ddjvu_context_t *ctx;
	ddjvu_document_t *doc;
	ddjvu_page_t *page;
	ddjvu_rect_t rrect;
	ddjvu_rect_t prect;
	ddjvu_format_t *fmt;
	gint width, height;
	gint stride;
	gboolean alpha = FALSE;
	cairo_surface_t *surface;
	guchar *pixels;

	ctx = ddjvu_context_create(nullptr);

	doc = ddjvu_document_create(ctx, nullptr, FALSE);

	ddjvu_stream_write(doc, 0, (char *)buf, count );
	while (!ddjvu_document_decoding_done(doc));

	ld->page_total = ddjvu_document_get_pagenum(doc);

	page = ddjvu_page_create_by_pageno(doc, ld->page_num);
	while (!ddjvu_page_decoding_done(page));

	fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr);

	width = ddjvu_page_get_width(page);
	height = ddjvu_page_get_height(page);
	stride = width * 4;

	pixels = static_cast<guchar *>(g_malloc(height * stride));

	prect.x = 0;
	prect.y = 0;
	prect.w = width;
	prect.h = height;
	rrect = prect;

	surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_RGB24, width, height, stride);

	ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &rrect, fmt, stride, reinterpret_cast<char *>(pixels));

	/**
	 * @FIXME implementation of rotation is not correct */
	GdkPixbuf *tmp1;
	GdkPixbuf *tmp2;
	tmp1 = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, alpha, 8, width, height, stride, free_buffer, nullptr);
	tmp2 = gdk_pixbuf_flip(tmp1, TRUE);
	g_object_unref(tmp1);

	ld->pixbuf = gdk_pixbuf_rotate_simple(tmp2,GDK_PIXBUF_ROTATE_UPSIDEDOWN);

	ld->area_updated_cb(loader, 0, 0, width, height, ld->data);


	cairo_surface_destroy(surface);
	ddjvu_page_release(page);
	ddjvu_document_release(doc);
	ddjvu_context_release(ctx);

	return TRUE;
}

static gpointer image_loader_djvu_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderDJVU, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return loader;
}

static void image_loader_djvu_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderDJVU *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_djvu_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderDJVU *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_djvu_get_format_name(gpointer UNUSED(loader))
{
	return g_strdup("djvu");
}

static gchar** image_loader_djvu_get_format_mime_types(gpointer UNUSED(loader))
{
	static const gchar *mime[] = {"image/vnd.djvu", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

static void image_loader_djvu_set_page_num(gpointer loader, gint page_num)
{
	auto ld = static_cast<ImageLoaderDJVU *>(loader);

	ld->page_num = page_num;
}

static gint image_loader_djvu_get_page_total(gpointer loader)
{
	auto ld = static_cast<ImageLoaderDJVU *>(loader);

	return ld->page_total;
}

static gboolean image_loader_djvu_close(gpointer UNUSED(loader), GError **UNUSED(error))
{
	return TRUE;
}

static void image_loader_djvu_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderDJVU *>(loader);
	ld->abort = TRUE;
}

static void image_loader_djvu_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderDJVU *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_djvu(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_djvu_new;
	funcs->set_size = image_loader_djvu_set_size;
	funcs->load = image_loader_djvu_load;
	funcs->write = nullptr;
	funcs->get_pixbuf = image_loader_djvu_get_pixbuf;
	funcs->close = image_loader_djvu_close;
	funcs->abort = image_loader_djvu_abort;
	funcs->free = image_loader_djvu_free;
	funcs->get_format_name = image_loader_djvu_get_format_name;
	funcs->get_format_mime_types = image_loader_djvu_get_format_mime_types;
	funcs->set_page_num = image_loader_djvu_set_page_num;
	funcs->get_page_total = image_loader_djvu_get_page_total;
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
