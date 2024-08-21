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

#include "image-load-j2k.h"

#include <cstdlib>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <openjpeg.h>

#include "debug.h"
#include "image-load.h"
#include "intl.h"
#include "misc.h"

namespace
{

G_DEFINE_AUTOPTR_CLEANUP_FUNC(opj_stream_t, opj_stream_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(opj_codec_t, opj_destroy_codec)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(opj_image_t, opj_image_destroy)

struct ImageLoaderJ2K : public ImageLoaderBackend
{
public:
	~ImageLoaderJ2K() override;

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

void free_buffer(guchar *pixels, gpointer)
{
	g_free (pixels);
}

struct OpjBufferInfo
{
	OpjBufferInfo(const OPJ_BYTE *buffer, OPJ_SIZE_T size)
	    : buf(buffer)
	    , cur(buffer)
	    , len(size)
	{}

	const OPJ_BYTE *buf;
	const OPJ_BYTE *cur;
	const OPJ_SIZE_T len;
};

OPJ_SIZE_T opj_read_from_buffer (void* pdst, OPJ_SIZE_T len, void* user_data)
{
	auto *psrc = static_cast<OpjBufferInfo *>(user_data);
	OPJ_SIZE_T n = psrc->buf + psrc->len - psrc->cur;

	if (n)
		{
		if (n > len)
			n = len;

		memcpy (pdst, psrc->cur, n);
		psrc->cur += n;
		}
	else
		n = static_cast<OPJ_SIZE_T>(-1);

	return n;
}

OPJ_OFF_T opj_skip_from_buffer (OPJ_OFF_T len, void* user_data)
{
	auto *psrc = static_cast<OpjBufferInfo *>(user_data);
	OPJ_SIZE_T n = psrc->buf + psrc->len - psrc->cur;

	if (n)
		{
		if (n > static_cast<gulong>(len))
			n = len;

		psrc->cur += len;
		}
	else
		n = static_cast<OPJ_SIZE_T>(-1);

	return n;
}

OPJ_BOOL opj_seek_from_buffer (OPJ_OFF_T len, void* user_data)
{
	auto *psrc = static_cast<OpjBufferInfo *>(user_data);
	OPJ_SIZE_T n = psrc->len;

	if (n > static_cast<gulong>(len))
		n = len;

	psrc->cur = psrc->buf + n;

	return OPJ_TRUE;
}

gboolean ImageLoaderJ2K::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	if (memcmp(buf + 20, "jp2", 3) != 0)
		{
		log_printf("%s", _("Unknown jpeg2000 decoder type"));
		return FALSE;
		}

	g_autoptr(opj_stream_t) stream = opj_stream_default_create(OPJ_TRUE);
	if (!stream)
		{
		log_printf("%s", _("Could not open file for reading"));
		return FALSE;
		}

	OpjBufferInfo decode_buffer{buf, count};
	opj_stream_set_user_data(stream, &decode_buffer, nullptr);
	opj_stream_set_user_data_length(stream, count);

	opj_stream_set_read_function(stream, opj_read_from_buffer);
	opj_stream_set_skip_function(stream, opj_skip_from_buffer);
	opj_stream_set_seek_function(stream, opj_seek_from_buffer);

	opj_dparameters_t parameters;
	opj_set_default_decoder_parameters(&parameters);

	g_autoptr(opj_codec_t) codec = opj_create_decompress(OPJ_CODEC_JP2);
	if (opj_setup_decoder (codec, &parameters) != OPJ_TRUE)
		{
		log_printf("%s", _("Couldn't set parameters on decoder for file."));
		return FALSE;
		}

	if (opj_codec_set_threads(codec, get_cpu_cores()) != OPJ_TRUE)
		{
		log_printf("%s", _("Couldn't allocate worker threads on decoder for file."));
		return FALSE;
		}

	g_autoptr(opj_image_t) image = nullptr;
	if (opj_read_header(stream, codec, &image) != OPJ_TRUE)
		{
		log_printf("%s", _("Couldn't read JP2 header from file"));
		return FALSE;
		}

	if (opj_decode(codec, stream, image) != OPJ_TRUE)
		{
		log_printf("%s", _("Couldn't decode JP2 image in file"));
		return FALSE;
		}

	if (opj_end_decompress(codec, stream) != OPJ_TRUE)
		{
		log_printf("%s", _("Couldn't decompress JP2 image in file"));
		return FALSE;
		}

	constexpr gint bytes_per_pixel = 3; // rgb
	if (image->numcomps != bytes_per_pixel)
		{
		log_printf("%s", _("JP2 image not rgb"));
		return FALSE;
		}

	const gint width = image->comps[0].w;
	const gint height = image->comps[0].h;

	auto *pixels = g_new0(guchar, width * bytes_per_pixel * height);
	for (gint y = 0; y < height; y++)
		{
		for (gint b = 0; b < bytes_per_pixel; b++)
			{
			for (gint x = 0; x < width; x++)
				{
				pixels[(y * width + x) * bytes_per_pixel + b] = image->comps[b].data[y * width + x];
				}
			}
		}

	pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, FALSE , 8, width, height, width * bytes_per_pixel, free_buffer, nullptr);

	area_updated_cb(nullptr, 0, 0, width, height, data);

	chunk_size = count;
	return TRUE;
}

void ImageLoaderJ2K::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderJ2K::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderJ2K::get_format_name()
{
	return g_strdup("j2k");
}

gchar **ImageLoaderJ2K::get_format_mime_types()
{
	static const gchar *mime[] = {"image/jp2", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

ImageLoaderJ2K::~ImageLoaderJ2K()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_j2k()
{
	return std::make_unique<ImageLoaderJ2K>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
