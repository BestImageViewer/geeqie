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

#include "image-load-zxscr.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>

#include "debug.h"
#include "image-load.h"

namespace
{

struct ImageLoaderZXSCR : public ImageLoaderBackend
{
public:
	~ImageLoaderZXSCR() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;

private:
	AreaUpdatedCb area_updated_cb;
	gpointer data;

	GdkPixbuf *pixbuf;
};

constexpr guchar palette[2][8][3] = {
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

void free_buffer(guchar *pixels, gpointer)
{
	g_free(pixels);
}

gboolean ImageLoaderZXSCR::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	guint8 *pixels;
	gint width;
	gint height;
	gint row;
	gint col;
	gint mrow;
	gint pxs;
	gint i;
	guint8 attr;
	guint8 bright;
	guint8 ink;
	guint8 paper;
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

	pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, FALSE, 8, width, height, width * 3, free_buffer, nullptr);

	if (!pixbuf)
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

	area_updated_cb(nullptr, 0, 0, width, height, data);

	chunk_size = count;
	return TRUE;
}

void ImageLoaderZXSCR::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderZXSCR::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderZXSCR::get_format_name()
{
	return g_strdup("zxscr");
}

gchar **ImageLoaderZXSCR::get_format_mime_types()
{
	static const gchar *mime[] = {"application/octet-stream", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

ImageLoaderZXSCR::~ImageLoaderZXSCR()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_zxscr()
{
	return std::make_unique<ImageLoaderZXSCR>();
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
