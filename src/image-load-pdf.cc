/*
 * Copyright (C) 20018 - The Geeqie Team
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

#include "image-load-pdf.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <poppler.h>

#include "debug.h"
#include "image-load.h"

namespace
{

struct ImageLoaderPDF : public ImageLoaderBackend
{
public:
	~ImageLoaderPDF() override;

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

gboolean ImageLoaderPDF::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	GError *poppler_error = nullptr;
	PopplerPage *page;
	PopplerDocument *document;
	gdouble width;
	gdouble height;
	cairo_surface_t *surface;
	cairo_t *cr;
	gboolean ret = FALSE;
	gint page_total;

#if POPPLER_CHECK_VERSION(0,82,0)
	GBytes *bytes = g_bytes_new_static(buf, count);
	document = poppler_document_new_from_bytes(bytes, nullptr, &poppler_error);
#else
	document = poppler_document_new_from_data((gchar *)(buf), count, nullptr, &poppler_error);
#endif

	if (poppler_error)
		{
		log_printf("warning: pdf reader error: %s\n", poppler_error->message);
		g_error_free(poppler_error);
		}
	else
		{
		page_total = poppler_document_get_n_pages(document);
		if (page_total > 0)
			{
			this->page_total = page_total;
			}

		page = poppler_document_get_page(document, page_num);
		poppler_page_get_size(page, &width, &height);

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
		cr = cairo_create(surface);
		poppler_page_render(page, cr);

		cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER);
		cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		cairo_paint(cr);

		pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
		area_updated_cb(nullptr, 0, 0, width, height, data);

		cairo_destroy (cr);
		cairo_surface_destroy(surface);
		g_object_unref(page);
		chunk_size = count;
		ret = TRUE;
		}

	g_object_unref(document);
#if POPPLER_CHECK_VERSION(0,82,0)
	g_bytes_unref(bytes);
#endif

	return ret;
}

void ImageLoaderPDF::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
	page_num = 0;
}

GdkPixbuf *ImageLoaderPDF::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderPDF::get_format_name()
{
	return g_strdup("pdf");
}

gchar **ImageLoaderPDF::get_format_mime_types()
{
	static const gchar *mime[] = {"application/pdf", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderPDF::set_page_num(gint page_num)
{
	this->page_num = page_num;
}

gint ImageLoaderPDF::get_page_total()
{
	return page_total;
}

ImageLoaderPDF::~ImageLoaderPDF()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_pdf()
{
	return std::make_unique<ImageLoaderPDF>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
