/*
 * Copyright (C) 2019 - The Geeqie Team
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

#include "image-load-djvu.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <libdjvu/ddjvuapi.h>

#include "image-load.h"

namespace
{

struct ImageLoaderDJVU : public ImageLoaderBackend
{
public:
	~ImageLoaderDJVU() override;

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

void free_buffer(guchar *pixels, gpointer)
{
	g_free (pixels);
}

gboolean ImageLoaderDJVU::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	ddjvu_context_t *ctx;
	ddjvu_document_t *doc;
	ddjvu_page_t *page;
	ddjvu_rect_t rrect;
	ddjvu_rect_t prect;
	ddjvu_format_t *fmt;
	gint width;
	gint height;
	gint stride;
	gboolean alpha = FALSE;
	cairo_surface_t *surface;
	guchar *pixels;

	ctx = ddjvu_context_create(nullptr);

	doc = ddjvu_document_create(ctx, nullptr, FALSE);

	ddjvu_stream_write(doc, 0, reinterpret_cast<const char *>(buf), count );
	while (!ddjvu_document_decoding_done(doc));

	page_total = ddjvu_document_get_pagenum(doc);

	page = ddjvu_page_create_by_pageno(doc, page_num);
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

	pixbuf = gdk_pixbuf_rotate_simple(tmp2, GDK_PIXBUF_ROTATE_UPSIDEDOWN);

	area_updated_cb(nullptr, 0, 0, width, height, data);

	cairo_surface_destroy(surface);
	ddjvu_page_release(page);
	ddjvu_document_release(doc);
	ddjvu_context_release(ctx);

	chunk_size = count;
	return TRUE;
}

void ImageLoaderDJVU::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderDJVU::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderDJVU::get_format_name()
{
	return g_strdup("djvu");
}

gchar **ImageLoaderDJVU::get_format_mime_types()
{
	static const gchar *mime[] = {"image/vnd.djvu", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderDJVU::set_page_num(gint page_num)
{
	this->page_num = page_num;
}

gint ImageLoaderDJVU::get_page_total()
{
	return page_total;
}

ImageLoaderDJVU::~ImageLoaderDJVU()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_djvu()
{
	return std::make_unique<ImageLoaderDJVU>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
