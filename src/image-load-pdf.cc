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

#include "main.h"

#include "image-load.h"
#include "image-load-pdf.h"

#ifdef HAVE_PDF
#include <poppler/glib/poppler.h>

struct ImageLoaderPDF {
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

static gboolean image_loader_pdf_load(gpointer loader, const guchar *buf, gsize count, GError **UNUSED(error))
{
	ImageLoaderPDF *ld = static_cast<ImageLoaderPDF *>(loader);
	GError *poppler_error = NULL;
	PopplerPage *page;
	PopplerDocument *document;
	gdouble width, height;
	cairo_surface_t *surface;
	cairo_t *cr;
	gboolean ret = FALSE;
	gint page_total;

	document = poppler_document_new_from_data((gchar *)(buf), count, NULL, &poppler_error);

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
			ld->page_total = page_total;
			}

		page = poppler_document_get_page(document, ld->page_num);
		poppler_page_get_size(page, &width, &height);

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
		cr = cairo_create(surface);
		poppler_page_render(page, cr);

		cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER);
		cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		cairo_paint(cr);

		ld->pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
		ld->area_updated_cb(loader, 0, 0, width, height, ld->data);

		cairo_destroy (cr);
		cairo_surface_destroy(surface);
		g_object_unref(page);
		ret = TRUE;
		}

	g_object_unref(document);

	return ret;
}

static gpointer image_loader_pdf_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	ImageLoaderPDF *loader = g_new0(ImageLoaderPDF, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	loader->page_num = 0;
	return loader;
}

static void image_loader_pdf_set_size(gpointer loader, int width, int height)
{
	ImageLoaderPDF *ld = static_cast<ImageLoaderPDF *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_pdf_get_pixbuf(gpointer loader)
{
	ImageLoaderPDF *ld = static_cast<ImageLoaderPDF *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_pdf_get_format_name(gpointer UNUSED(loader))
{
	return g_strdup("pdf");
}

static gchar** image_loader_pdf_get_format_mime_types(gpointer UNUSED(loader))
{
	static const gchar *mime[] = {"application/pdf", NULL};
	return g_strdupv(const_cast<gchar **>(mime));
}

static void image_loader_pdf_set_page_num(gpointer loader, gint page_num)
{
	ImageLoaderPDF *ld = static_cast<ImageLoaderPDF *>(loader);

	ld->page_num = page_num;
}

static gint image_loader_pdf_get_page_total(gpointer loader)
{
	ImageLoaderPDF *ld = static_cast<ImageLoaderPDF *>(loader);

	return ld->page_total;
}

static gboolean image_loader_pdf_close(gpointer UNUSED(loader), GError **UNUSED(error))
{
	return TRUE;
}

static void image_loader_pdf_abort(gpointer loader)
{
	ImageLoaderPDF *ld = static_cast<ImageLoaderPDF *>(loader);
	ld->abort = TRUE;
}

static void image_loader_pdf_free(gpointer loader)
{
	ImageLoaderPDF *ld = static_cast<ImageLoaderPDF *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_pdf(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_pdf_new;
	funcs->set_size = image_loader_pdf_set_size;
	funcs->load = image_loader_pdf_load;
	funcs->write = NULL;
	funcs->get_pixbuf = image_loader_pdf_get_pixbuf;
	funcs->close = image_loader_pdf_close;
	funcs->abort = image_loader_pdf_abort;
	funcs->free = image_loader_pdf_free;
	funcs->get_format_name = image_loader_pdf_get_format_name;
	funcs->get_format_mime_types = image_loader_pdf_get_format_mime_types;
	funcs->set_page_num = image_loader_pdf_set_page_num;
	funcs->get_page_total = image_loader_pdf_get_page_total;
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
