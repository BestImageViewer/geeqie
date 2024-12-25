/*
 * Copyright (C) 1999 Mark Crichton
 * Copyright (C) 1999 The Free Software Foundation
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Mark Crichton <crichton@gimp.org>
 *          Federico Mena-Quintero <federico@gimp.org>
 *          Jonathan Blandford <jrb@redhat.com>
 *          S�ren Sandmann <sandmann@daimi.au.dk>
 *	    Vladimir Nadvornik
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

#include "image-load-tiff.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <tiff.h>
#include <tiffio.h>

#include "debug.h"
#include "image-load.h"

namespace
{

struct ImageLoaderTiff : public ImageLoaderBackend
{
public:
	~ImageLoaderTiff() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	void set_size(int width, int height) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	void abort() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;
	void set_page_num(gint page_num) override;
	gint get_page_total() override;

private:
	AreaUpdatedCb area_updated_cb;
	SizePreparedCb size_prepared_cb;
	AreaPreparedCb area_prepared_cb;
	gpointer data;

	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;

	gboolean aborted;

	gint page_num;
	gint page_total;
};

struct GqTiffContext
{
	const guchar *buffer;
	toff_t used;
	toff_t pos;
};

void free_buffer (guchar *pixels, gpointer)
{
	g_free (pixels);
}

tsize_t tiff_load_read (thandle_t handle, tdata_t buf, tsize_t size)
{
	auto context = static_cast<GqTiffContext *>(handle);

	if (context->pos + size > context->used)
		return 0;

	memcpy (buf, context->buffer + context->pos, size);
	context->pos += size;
	return size;
}

tsize_t tiff_load_write (thandle_t, tdata_t, tsize_t)
{
	return -1;
}

toff_t tiff_load_seek (thandle_t handle, toff_t offset, int whence)
{
	auto context = static_cast<GqTiffContext *>(handle);

	switch (whence)
		{
		case SEEK_SET:
			if (offset > context->used)
				return -1;
			context->pos = offset;
			break;
		case SEEK_CUR:
			if (offset + context->pos >= context->used)
				return -1;
			context->pos += offset;
			break;
		case SEEK_END:
			if (offset + context->used > context->used)
				return -1;
			context->pos = context->used + offset;
			break;
		default:
			return -1;
		}

	return context->pos;
}

int tiff_load_close (thandle_t)
{
	return 0;
}

toff_t tiff_load_size (thandle_t handle)
{
	auto context = static_cast<GqTiffContext *>(handle);
	return context->used;
}

int tiff_load_map_file (thandle_t handle, tdata_t *buf, toff_t *size)
{
	auto context = static_cast<GqTiffContext *>(handle);

	*buf = const_cast<guchar *>(context->buffer);
	*size = context->used;

	return 0;
}

void tiff_load_unmap_file (thandle_t, tdata_t, toff_t)
{
}

gboolean ImageLoaderTiff::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	TIFF *tiff;
	guchar *pixels = nullptr;
	gint width;
	gint height;
	gint rowstride;
	size_t bytes;
	guint32 rowsperstrip;
	gint dircount = 0;

	TIFFSetWarningHandler(nullptr);

	GqTiffContext context{buf, count, 0};
	tiff = TIFFClientOpen (	"libtiff-geeqie", "r", &context,
							tiff_load_read, tiff_load_write,
							tiff_load_seek, tiff_load_close,
							tiff_load_size,
							tiff_load_map_file, tiff_load_unmap_file);
	if (!tiff)
		{
		DEBUG_1("Failed to open TIFF image");
		return FALSE;
		}

	do	{
		dircount++;
		} while (TIFFReadDirectory(tiff));

	page_total = dircount;

	if (!TIFFSetDirectory(tiff, page_num))
		{
		DEBUG_1("Failed to open TIFF image");
		TIFFClose(tiff);
		return FALSE;
		}

	if (!TIFFGetField (tiff, TIFFTAG_IMAGEWIDTH, &width))
		{
		DEBUG_1("Could not get image width (bad TIFF file)");
		TIFFClose(tiff);
		return FALSE;
		}

	if (!TIFFGetField (tiff, TIFFTAG_IMAGELENGTH, &height))
		{
		DEBUG_1("Could not get image height (bad TIFF file)");
		TIFFClose(tiff);
		return FALSE;
		}

	if (width <= 0 || height <= 0)
		{
		DEBUG_1("Width or height of TIFF image is zero");
		TIFFClose(tiff);
		return FALSE;
		}

	rowstride = width * 4;
	if (rowstride / 4 != width)
		{ /* overflow */
		DEBUG_1("Dimensions of TIFF image too large: width %d", width);
		TIFFClose(tiff);
		return FALSE;
		}

	bytes = static_cast<size_t>(height) * rowstride;
	if (bytes / rowstride != static_cast<size_t>(height))
		{ /* overflow */
		DEBUG_1("Dimensions of TIFF image too large: height %d", height);
		TIFFClose(tiff);
		return FALSE;
		}

	requested_width = width;
	requested_height = height;
	size_prepared_cb(nullptr, requested_width, requested_height, data);

	pixels = static_cast<guchar *>(g_try_malloc (bytes));

	if (!pixels)
		{
		DEBUG_1("Insufficient memory to open TIFF file: need %zu", bytes);
		TIFFClose(tiff);
		return FALSE;
		}

	pixbuf = gdk_pixbuf_new_from_data (pixels, GDK_COLORSPACE_RGB, TRUE, 8,
										   width, height, rowstride,
										   free_buffer, nullptr);
	if (!pixbuf)
		{
		g_free (pixels);
		DEBUG_1("Insufficient memory to open TIFF file");
		TIFFClose(tiff);
		return FALSE;
		}

	area_prepared_cb(nullptr, data);

	if (TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &rowsperstrip))
		{
		/* read by strip */
		ptrdiff_t row;
		const size_t line_bytes = width * sizeof(guint32);
		g_autofree auto *wrk_line = static_cast<guchar *>(g_malloc(line_bytes));

		for (row = 0; row < height; row += rowsperstrip)
			{
			int rows_to_write;
			int i_row;

			if (aborted) {
				break;
			}

			/* Read the strip into an RGBA array */
			if (!TIFFReadRGBAStrip(tiff, row, reinterpret_cast<guint32 *>(pixels + (row * rowstride)))) {
				break;
			}

			/*
			 * Figure out the number of scanlines actually in this strip.
			*/
			if (row + static_cast<int>(rowsperstrip) > height)
				rows_to_write = height - row;
			else
				rows_to_write = rowsperstrip;

			/*
			 * For some reason the TIFFReadRGBAStrip() function chooses the
			 * lower left corner as the origin.  Vertically mirror scanlines.
			 */
			for (i_row = 0; i_row < rows_to_write / 2; i_row++)
				{
				guchar *top_line;
				guchar *bottom_line;

				top_line = pixels + (row + i_row) * rowstride;
				bottom_line = pixels + (row + rows_to_write - i_row - 1) * rowstride;

				memcpy(wrk_line, top_line, line_bytes);
				memcpy(top_line, bottom_line, line_bytes);
				memcpy(bottom_line, wrk_line, line_bytes);
				}
			area_updated_cb(nullptr, 0, row, width, rows_to_write, data);
			}
		}
	else
		{
		/* fallback, tiled tiff */
		if (!TIFFReadRGBAImageOriented (tiff, width, height, reinterpret_cast<guint32 *>(pixels), ORIENTATION_TOPLEFT, 1))
			{
			TIFFClose(tiff);
			return FALSE;
			}

#if G_BYTE_ORDER == G_BIG_ENDIAN
		/* Turns out that the packing used by TIFFRGBAImage depends on
		 * the host byte order...
		 */
		{
		guchar *ptr = pixels;
		while (ptr < pixels + bytes)
			{
			guint32 pixel = *(guint32 *)ptr;
			int r = TIFFGetR(pixel);
			int g = TIFFGetG(pixel);
			int b = TIFFGetB(pixel);
			int a = TIFFGetA(pixel);
			*ptr++ = r;
			*ptr++ = g;
			*ptr++ = b;
			*ptr++ = a;
			}
		}
#endif

		area_updated_cb(nullptr, 0, 0, width, height, data);
		}
	TIFFClose(tiff);

	chunk_size = count;
	return TRUE;
}


void ImageLoaderTiff::init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->size_prepared_cb = size_prepared_cb;
	this->area_prepared_cb = area_prepared_cb;
	this->data = data;
}

void ImageLoaderTiff::set_size(int width, int height)
{
	requested_width = width;
	requested_height = height;
}

GdkPixbuf *ImageLoaderTiff::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderTiff::get_format_name()
{
	return g_strdup("tiff");
}

gchar **ImageLoaderTiff::get_format_mime_types()
{
	static const gchar *mime[] = {"image/tiff", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderTiff::abort()
{
	aborted = TRUE;
}

ImageLoaderTiff::~ImageLoaderTiff()
{
	if (pixbuf) g_object_unref(pixbuf);
}

void ImageLoaderTiff::set_page_num(gint page_num)
{
	this->page_num = page_num;
}

gint ImageLoaderTiff::get_page_total()
{
	return page_total;
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_tiff()
{
	return std::make_unique<ImageLoaderTiff>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
