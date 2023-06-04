/*
 * Copyright (C) 2021 - The Geeqie Team
 *
 * Author: Dusan Gallo
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
#include "image-load-zxscr.h"

typedef struct _ImageLoaderZXSCR ImageLoaderZXSCR;
struct _ImageLoaderZXSCR {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
};

const guchar palette[2][8][3] = {
	{
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0xbf},
		{0xbf, 0x00, 0x00},
		{0xbf, 0x00, 0xbf},
		{0x00, 0xbf, 0x00},
		{0x00, 0xbf, 0xbf},
		{0xbf, 0xbf, 0x00},
		{0xbf, 0xbf, 0xbf}
	}, {
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0xff},
		{0xff, 0x00, 0x00},
		{0xff, 0x00, 0xff},
		{0x00, 0xff, 0x00},
		{0x00, 0xff, 0xff},
		{0xff, 0xff, 0x00},
		{0xff, 0xff, 0xff}
	}
};

static void free_buffer(guchar *pixels, gpointer UNUSED(data))
{
	g_free(pixels);
}

static gboolean image_loader_zxscr_load(gpointer loader, const guchar *buf, gsize count, GError **UNUSED(error))
{
	auto ld = static_cast<ImageLoaderZXSCR *>(loader);
	guint8 *pixels;
	gint width, height;
	gint row, col, mrow, pxs, i;
	guint8 attr, bright, ink, paper;
	guint8 *ptr;

	if (count != 6144 && count != 6912)
		{
		log_printf("Error: zxscr reader error\n");
		return FALSE;
		}

	width = 256;
	height = 192;

	pixels = static_cast<guint8 *>(g_try_malloc(width * height * 3));

	if (!pixels)
		{
		log_printf("Error: zxscr reader error\n");
		return FALSE;
		}

	ld->pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, FALSE, 8, width, height, width * 3, free_buffer, NULL);

	if (!ld->pixbuf)
		{
		g_free(pixels);
		DEBUG_1("Insufficient memory to open ZXSCR file");
		return FALSE;
		}
	//let's decode screen
	for (row = 0; row < 24; row++)
		for (col = 0; col < 32; col++)
			{
			if (count == 6144)
				{
				//if we have pixels only, make default white ink on black paper
				bright = 0x01;
				ink = 0x07;
				paper = 0x00;
				}
			else
				{
				attr = buf[6144 + row * 32 + col];
				bright = (attr >> 6) & 0x01;
				ink = attr & 0x07;
				paper = ((attr >> 3) & 0x07);
				}
			ptr = pixels + (row * 256 + col) * 8 * 3;

			for (mrow = 0; mrow < 8; mrow ++)
				{
				pxs = buf[(row / 8) * 2048 + mrow * 256 + (row % 8) * 32 + col];
				for (i = 0; i < 8; i++)
					{
					if (pxs & 0x80)
						{
						*ptr++ = palette[bright][ink][0];	//r
						*ptr++ = palette[bright][ink][1];	//g
						*ptr++ = palette[bright][ink][2];	//b
						}
					else
						{
						*ptr++ = palette[bright][paper][0];
						*ptr++ = palette[bright][paper][1];
						*ptr++ = palette[bright][paper][2];
						}
					pxs <<= 1;
					}
				ptr += (31 * 8 * 3);
				}
			}

	ld->area_updated_cb(loader, 0, 0, width, height, ld->data);

	return TRUE;
}

static gpointer image_loader_zxscr_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderZXSCR, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return (gpointer) loader;
}

static void image_loader_zxscr_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderZXSCR *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf *image_loader_zxscr_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderZXSCR *>(loader);
	return ld->pixbuf;
}

static gchar *image_loader_zxscr_get_format_name(gpointer UNUSED(loader))
{
	return g_strdup("zxscr");
}

static gchar **image_loader_zxscr_get_format_mime_types(gpointer UNUSED(loader))
{
	static const gchar *mime[] = {"application/octet-stream", NULL};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gboolean image_loader_zxscr_close(gpointer UNUSED(loader), GError **UNUSED(error))
{
	return TRUE;
}

static void image_loader_zxscr_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderZXSCR *>(loader);
	ld->abort = TRUE;
}

static void image_loader_zxscr_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderZXSCR *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_zxscr(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_zxscr_new;
	funcs->set_size = image_loader_zxscr_set_size;
	funcs->load = image_loader_zxscr_load;
	funcs->write = NULL;
	funcs->get_pixbuf = image_loader_zxscr_get_pixbuf;
	funcs->close = image_loader_zxscr_close;
	funcs->abort = image_loader_zxscr_abort;
	funcs->free = image_loader_zxscr_free;
	funcs->get_format_name = image_loader_zxscr_get_format_name;
	funcs->get_format_mime_types = image_loader_zxscr_get_format_mime_types;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
