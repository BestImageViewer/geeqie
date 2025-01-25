/*
 * Copyright (C) 1999 Michael Zucchi
 * Copyright (C) 1999 The Free Software Foundation
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Michael Zucchi <zucchi@zedzone.mmc.com.au>
 *          Federico Mena-Quintero <federico@gimp.org>
 *          Michael Fulbright <drmike@redhat.com>
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

/** This is a Will Not Fix */
#pragma GCC diagnostic ignored "-Wclobbered"

#include "image-load-jpeg.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio> // for FILE and size_t in jpeglib.h

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <jerror.h>
#include <jpeglib.h>

#include "image-load.h"
#include "intl.h"
#include "jpeg-parser.h"
#include "typedefs.h"

/* error handler data */
struct error_handler_data {
	struct jpeg_error_mgr pub;
	sigjmp_buf setjmp_buffer;
	GError **error;
};

/* explode gray image data from jpeg library into rgb components in pixbuf */
static void
explode_gray_into_buf (struct jpeg_decompress_struct *cinfo,
		       guchar **lines)
{
	gint i;
	gint j;
	guint w;

	g_return_if_fail (cinfo != nullptr);
	g_return_if_fail (cinfo->output_components == 1);
	g_return_if_fail (cinfo->out_color_space == JCS_GRAYSCALE);

	/* Expand grey->colour.  Expand from the end of the
	 * memory down, so we can use the same buffer.
	 */
	w = cinfo->output_width;
	for (i = cinfo->rec_outbuf_height - 1; i >= 0; i--) {
		guchar *from;
		guchar *to;

		from = lines[i] + w - 1;
		to = lines[i] + (w - 1) * 3;
		for (j = w - 1; j >= 0; j--) {
			to[0] = from[0];
			to[1] = from[0];
			to[2] = from[0];
			to -= 3;
			from--;
		}
	}
}


static void
convert_cmyk_to_rgb (struct jpeg_decompress_struct *cinfo,
		     guchar **lines)
{
	gint i;
	guint j;

	g_return_if_fail (cinfo != nullptr);
	g_return_if_fail (cinfo->output_components == 4);
	g_return_if_fail (cinfo->out_color_space == JCS_CMYK);

	for (i = cinfo->rec_outbuf_height - 1; i >= 0; i--) {
		guchar *p;

		p = lines[i];
		for (j = 0; j < cinfo->output_width; j++) {
			int c;
			int m;
			int y;
			int k;
			c = p[0];
			m = p[1];
			y = p[2];
			k = p[3];
			if (cinfo->saw_Adobe_marker) {
				p[0] = k*c / 255;
				p[1] = k*m / 255;
				p[2] = k*y / 255;
			}
			else {
				p[0] = (255 - k)*(255 - c) / 255;
				p[1] = (255 - k)*(255 - m) / 255;
				p[2] = (255 - k)*(255 - y) / 255;
			}
			p[3] = 255;
			p += 4;
		}
	}
}


void ImageLoaderJpeg::init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->size_prepared_cb = size_prepared_cb;
	this->area_prepared_cb = area_prepared_cb;
	this->data = data;
}

static void
fatal_error_handler (j_common_ptr cinfo)
{
	struct error_handler_data *errmgr;
        char buffer[JMSG_LENGTH_MAX];

	errmgr = reinterpret_cast<struct error_handler_data *>(cinfo->err);

        /* Create the message */
        (* cinfo->err->format_message) (cinfo, buffer);

        /* broken check for *error == NULL for robustness against
         * crappy JPEG library
         */
        if (errmgr->error && *errmgr->error == nullptr) {
                g_set_error (errmgr->error,
                             GDK_PIXBUF_ERROR,
                             cinfo->err->msg_code == JERR_OUT_OF_MEMORY
			     ? GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY
			     : GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                             _("Error interpreting JPEG image file (%s)"),
                             buffer);
        }

	siglongjmp (errmgr->setjmp_buffer, 1);

        g_assert_not_reached ();
}

static void
output_message_handler (j_common_ptr)
{
  /* This method keeps libjpeg from dumping crap to stderr */

  /* do nothing */
}


static void image_loader_jpeg_read_scanline(struct jpeg_decompress_struct *cinfo, guchar **dptr, guint rowstride)
{
	guchar *lines[4];
	guchar **lptr;
	gint i;

	lptr = lines;
	for (i = 0; i < cinfo->rec_outbuf_height; i++)
		{
		*lptr++ = *dptr;
		*dptr += rowstride;
		}

	jpeg_read_scanlines (cinfo, lines, cinfo->rec_outbuf_height);

	switch (cinfo->out_color_space)
		{
		    case JCS_GRAYSCALE:
		      explode_gray_into_buf (cinfo, lines);
		      break;
		    case JCS_RGB:
		      /* do nothing */
		      break;
		    case JCS_CMYK:
		      convert_cmyk_to_rgb (cinfo, lines);
		      break;
		    default:
		      break;
		}
}


static void init_source (j_decompress_ptr) {}
static boolean fill_input_buffer (j_decompress_ptr cinfo)
{
	ERREXIT(cinfo, JERR_INPUT_EMPTY);
	return TRUE;
}
static void skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
	auto src = static_cast<struct jpeg_source_mgr*>(cinfo->src);

	if (static_cast<gulong>(num_bytes) > src->bytes_in_buffer)
		{
		ERREXIT(cinfo, JERR_INPUT_EOF);
		}
	else if (num_bytes > 0)
		{
		src->next_input_byte += static_cast<size_t>(num_bytes);
		src->bytes_in_buffer -= static_cast<size_t>(num_bytes);
		}
}
static void term_source (j_decompress_ptr) {}
static void set_mem_src (j_decompress_ptr cinfo, const guchar *buffer, guint nbytes)
{
	if (cinfo->src == nullptr)
		{   /* first time for this JPEG object? */
		cinfo->src = static_cast<struct jpeg_source_mgr *>((*cinfo->mem->alloc_small) (
					reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT,
					sizeof(struct jpeg_source_mgr)));
		}

	struct jpeg_source_mgr *src = cinfo->src;
	src->init_source = init_source;
	src->fill_input_buffer = fill_input_buffer;
	src->skip_input_data = skip_input_data;
	src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
	src->term_source = term_source;
	src->bytes_in_buffer = nbytes;
	src->next_input_byte = static_cast<const JOCTET *>(buffer);
}


gboolean ImageLoaderJpeg::write(const guchar *buf, gsize &chunk_size, gsize count, GError **error)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_decompress_struct cinfo2;
	guchar *dptr;
	guchar *dptr2;
	guint rowstride;
	const guchar *stereo_buf2 = nullptr;
	guint stereo_length = 0;

	struct error_handler_data jerr;

	stereo = FALSE;

	MPOData mpo = jpeg_get_mpo_data(buf, count);
	mpo.images.erase(std::remove_if(mpo.images.begin(), mpo.images.end(),
	                                [](const MPOEntry &mpe){ return mpe.type_code != 0x20002; }),
	                 mpo.images.end());
	if (mpo.images.size() > 1)
		{
		auto it1 = mpo.images.cend();
		auto it2 = mpo.images.cend();
		guint num2 = 1;

		for (auto it = mpo.images.cbegin(); it != mpo.images.cend(); ++it)
			{
			if (it->MPIndividualNum == 1)
				{
				it1 = it;
				}
			else if (it->MPIndividualNum > num2)
				{
				it2 = it;
				num2 = it->MPIndividualNum;
				}
			}

		if (it1 != mpo.images.cend() && it2 != mpo.images.cend())
			{
			stereo = TRUE;
			stereo_buf2 = buf + it2->offset;
			stereo_length = it2->length;

			buf = buf + it1->offset;
			count = it1->length;
			}
		}

	/* setup error handler */
	cinfo.err = jpeg_std_error (&jerr.pub);
	if (stereo) cinfo2.err = jpeg_std_error (&jerr.pub);
	jerr.pub.error_exit = fatal_error_handler;
	jerr.pub.output_message = output_message_handler;

	jerr.error = error;


	if (sigsetjmp(jerr.setjmp_buffer, 0))
		{
		/* If we get here, the JPEG code has signaled an error.
		 * We need to clean up the JPEG object, close the input file, and return.
		*/
		jpeg_destroy_decompress(&cinfo);
		if (stereo) jpeg_destroy_decompress(&cinfo2);
		return FALSE;
		}

	jpeg_create_decompress(&cinfo);

	set_mem_src(&cinfo, buf, count);


	jpeg_read_header(&cinfo, TRUE);

	if (stereo)
		{
		jpeg_create_decompress(&cinfo2);
		set_mem_src(&cinfo2, stereo_buf2, stereo_length);
		jpeg_read_header(&cinfo2, TRUE);

		if (cinfo.image_width != cinfo2.image_width ||
		    cinfo.image_height != cinfo2.image_height)
			{
			DEBUG_1("stereo data with different size");
			jpeg_destroy_decompress(&cinfo2);
			stereo = FALSE;
			}
		}


	requested_width = stereo ? cinfo.image_width * 2: cinfo.image_width;
	requested_height = cinfo.image_height;
	size_prepared_cb(nullptr, requested_width, requested_height, data);

	cinfo.scale_num = 1;
	for (cinfo.scale_denom = 2; cinfo.scale_denom <= 8; cinfo.scale_denom *= 2) {
		jpeg_calc_output_dimensions(&cinfo);
		if (cinfo.output_width < (stereo ? requested_width / 2 : requested_width) || cinfo.output_height < requested_height) {
			cinfo.scale_denom /= 2;
			break;
		}
	}
	jpeg_calc_output_dimensions(&cinfo);
	if (stereo)
		{
		cinfo2.scale_num = cinfo.scale_num;
		cinfo2.scale_denom = cinfo.scale_denom;
		jpeg_calc_output_dimensions(&cinfo2);
		jpeg_start_decompress(&cinfo2);
		}


	jpeg_start_decompress(&cinfo);


	if (stereo)
		{
		if (cinfo.output_width != cinfo2.output_width ||
		    cinfo.output_height != cinfo2.output_height ||
		    cinfo.out_color_components != cinfo2.out_color_components)
			{
			DEBUG_1("stereo data with different output size");
			jpeg_destroy_decompress(&cinfo2);
			stereo = FALSE;
			}
		}


	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
				     cinfo.out_color_components == 4 ? TRUE : FALSE,
	                 8, stereo ? cinfo.output_width * 2: cinfo.output_width, cinfo.output_height);

	if (!pixbuf)
		{
		jpeg_destroy_decompress (&cinfo);
		if (stereo) jpeg_destroy_decompress (&cinfo2);
		return FALSE;
		}
	if (stereo) g_object_set_data(G_OBJECT(pixbuf), "stereo_data", GINT_TO_POINTER(STEREO_PIXBUF_CROSS));
	area_prepared_cb(nullptr, data);

	rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	dptr = gdk_pixbuf_get_pixels(pixbuf);
	dptr2 = gdk_pixbuf_get_pixels(pixbuf) + ((cinfo.out_color_components == 4) ? 4 * cinfo.output_width : 3 * cinfo.output_width);


	while (cinfo.output_scanline < cinfo.output_height && !aborted)
		{
		guint scanline = cinfo.output_scanline;
		image_loader_jpeg_read_scanline(&cinfo, &dptr, rowstride);
		area_updated_cb(nullptr, 0, scanline, cinfo.output_width, cinfo.rec_outbuf_height, data);
		if (stereo)
			{
			guint scanline = cinfo2.output_scanline;
			image_loader_jpeg_read_scanline(&cinfo2, &dptr2, rowstride);
			area_updated_cb(nullptr, cinfo.output_width, scanline, cinfo2.output_width, cinfo2.rec_outbuf_height, data);
			}
		}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	if (stereo)
		{
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		}

	chunk_size = count;
	return TRUE;
}

void ImageLoaderJpeg::set_size(int width, int height)
{
	requested_width = width;
	requested_height = height;
}

GdkPixbuf *ImageLoaderJpeg::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderJpeg::get_format_name()
{
	return g_strdup("jpeg");
}

gchar **ImageLoaderJpeg::get_format_mime_types()
{
	static const gchar *mime[] = {"image/jpeg", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderJpeg::abort()
{
	aborted = TRUE;
}

ImageLoaderJpeg::~ImageLoaderJpeg()
{
	if (pixbuf) g_object_unref(pixbuf);
}

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_jpeg()
{
	return std::make_unique<ImageLoaderJpeg>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
