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

#include <config.h>

#ifdef HAVE_J2K

#include "image-load-j2k.h"

#include <openjpeg.h>

#include "debug.h"
#include "image-load.h"
#include "intl.h"
#include "misc.h"

struct ImageLoaderJ2K {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
};

static void free_buffer(guchar *pixels, gpointer)
{
	g_free (pixels);
}

struct opj_buffer_info_t {
    OPJ_BYTE* buf;
    OPJ_BYTE* cur;
    OPJ_SIZE_T len;
};

static OPJ_SIZE_T opj_read_from_buffer (void* pdst, OPJ_SIZE_T len, opj_buffer_info_t* psrc)
{
    OPJ_SIZE_T n = psrc->buf + psrc->len - psrc->cur;

    if (n) {
        if (n > len)
            n = len;

        memcpy (pdst, psrc->cur, n);
        psrc->cur += n;
    }
    else
        n = static_cast<OPJ_SIZE_T>(-1);

    return n;
}

static OPJ_SIZE_T opj_write_to_buffer (void* p_buffer, OPJ_SIZE_T p_nb_bytes,
                     opj_buffer_info_t* p_source_buffer)
{
    void* pbuf = p_source_buffer->buf;
    void* pcur = p_source_buffer->cur;

    OPJ_SIZE_T len = p_source_buffer->len;

    if (0 == len)
        len = 1;

    OPJ_SIZE_T dist = static_cast<guchar *>(pcur) - static_cast<guchar *>(pbuf);
    OPJ_SIZE_T n = len - dist;
    g_assert (dist <= len);

    while (n < p_nb_bytes) {
        len *= 2;
        n = len - dist;
    }

    if (len != p_source_buffer->len) {
        pbuf = malloc (len);

        if (nullptr == pbuf)
            return static_cast<OPJ_SIZE_T>(-1);

        if (p_source_buffer->buf) {
            memcpy (pbuf, p_source_buffer->buf, dist);
            free (p_source_buffer->buf);
        }

        p_source_buffer->buf = static_cast<OPJ_BYTE *>(pbuf);
        p_source_buffer->cur = static_cast<guchar *>(pbuf) + dist;
        p_source_buffer->len = len;
    }

    memcpy (p_source_buffer->cur, p_buffer, p_nb_bytes);
    p_source_buffer->cur += p_nb_bytes;

    return p_nb_bytes;
}

static OPJ_SIZE_T opj_skip_from_buffer (OPJ_SIZE_T len, opj_buffer_info_t* psrc)
{
    OPJ_SIZE_T n = psrc->buf + psrc->len - psrc->cur;

    if (n) {
        if (n > len)
            n = len;

        psrc->cur += len;
    }
    else
        n = static_cast<OPJ_SIZE_T>(-1);

    return n;
}

static OPJ_BOOL opj_seek_from_buffer (OPJ_OFF_T len, opj_buffer_info_t* psrc)
{
    OPJ_SIZE_T n = psrc->len;

    if (n > static_cast<gulong>(len))
        n = len;

    psrc->cur = psrc->buf + n;

    return OPJ_TRUE;
}

opj_stream_t* OPJ_CALLCONV opj_stream_create_buffer_stream (opj_buffer_info_t* psrc, OPJ_BOOL input)
{
    if (!psrc)
        return nullptr;

    opj_stream_t* ps = opj_stream_default_create (input);

    if (nullptr == ps)
        return nullptr;

    opj_stream_set_user_data        (ps, psrc, nullptr);
    opj_stream_set_user_data_length (ps, psrc->len);

    if (input)
        opj_stream_set_read_function (
            ps, reinterpret_cast<opj_stream_read_fn>(opj_read_from_buffer));
    else
        opj_stream_set_write_function(
            ps,reinterpret_cast<opj_stream_write_fn>(opj_write_to_buffer));

    opj_stream_set_skip_function (
        ps, reinterpret_cast<opj_stream_skip_fn>(opj_skip_from_buffer));

    opj_stream_set_seek_function (
        ps, reinterpret_cast<opj_stream_seek_fn>(opj_seek_from_buffer));

    return ps;
}

static gboolean image_loader_j2k_load(gpointer loader, const guchar *buf, gsize count, GError **)
{
	auto ld = static_cast<ImageLoaderJ2K *>(loader);
	opj_stream_t *stream;
	opj_codec_t *codec;
	opj_dparameters_t parameters;
	opj_image_t *image;
	gint width;
	gint height;
	gint num_components;
	gint i;
	gint j;
	gint k;
	guchar *pixels;
	gint  bytes_per_pixel;
	opj_buffer_info_t *decode_buffer;
    guchar *buf_copy;

	stream = nullptr;
	codec = nullptr;
	image = nullptr;

	buf_copy = static_cast<guchar *>(g_malloc(count));
	memcpy(buf_copy, buf, count);

	decode_buffer = g_new0(opj_buffer_info_t, 1);
	decode_buffer->buf = buf_copy;
	decode_buffer->len = count;
	decode_buffer->cur = buf_copy;

	stream = opj_stream_create_buffer_stream(decode_buffer, OPJ_TRUE);

	if (!stream)
		{
		log_printf(_("Could not open file for reading"));
		return FALSE;
		}

	if (memcmp(buf_copy + 20, "jp2", 3) == 0)
		{
		codec = opj_create_decompress(OPJ_CODEC_JP2);
		}
	else
		{
		log_printf(_("Unknown jpeg2000 decoder type"));
		return FALSE;
		}

	opj_set_default_decoder_parameters(&parameters);
	if (opj_setup_decoder (codec, &parameters) != OPJ_TRUE)
		{
		log_printf(_("Couldn't set parameters on decoder for file."));
		return FALSE;
		}

	opj_codec_set_threads(codec, get_cpu_cores());

	if (opj_read_header(stream, codec, &image) != OPJ_TRUE)
		{
		log_printf(_("Couldn't read JP2 header from file"));
		return FALSE;
		}

	if (opj_decode(codec, stream, image) != OPJ_TRUE)
		{
		log_printf(_("Couldn't decode JP2 image in file"));
		return FALSE;
		}

	if (opj_end_decompress(codec, stream) != OPJ_TRUE)
		{
		log_printf(_("Couldn't decompress JP2 image in file"));
		return FALSE;
		}

	num_components = image->numcomps;
	if (num_components != 3)
		{
		log_printf(_("JP2 image not rgb"));
		return FALSE;
		}

	width = image->comps[0].w;
	height = image->comps[0].h;

	bytes_per_pixel = 3;

	pixels = g_new0(guchar, width * bytes_per_pixel * height);
	for (i = 0; i < height; i++)
		{
		for (j = 0; j < num_components; j++)
			{
			for (k = 0; k < width; k++)
				{
				pixels[(k * bytes_per_pixel + j) + (i * width * bytes_per_pixel)] =   image->comps[j].data[i * width + k];
				}
			}
		}

	ld->pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, FALSE , 8, width, height, width * bytes_per_pixel, free_buffer, nullptr);

	ld->area_updated_cb(loader, 0, 0, width, height, ld->data);

	g_free(decode_buffer);
	g_free(buf_copy);
	if (image)
		opj_image_destroy (image);
	if (codec)
		opj_destroy_codec (codec);
	if (stream)
		opj_stream_destroy (stream);

	return TRUE;
}

static gpointer image_loader_j2k_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderJ2K, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return loader;
}

static void image_loader_j2k_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderJ2K *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_j2k_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderJ2K *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_j2k_get_format_name(gpointer)
{
	return g_strdup("j2k");
}

static gchar** image_loader_j2k_get_format_mime_types(gpointer)
{
	static const gchar *mime[] = {"image/jp2", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gboolean image_loader_j2k_close(gpointer, GError **)
{
	return TRUE;
}

static void image_loader_j2k_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderJ2K *>(loader);
	ld->abort = TRUE;
}

static void image_loader_j2k_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderJ2K *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_j2k(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_j2k_new;
	funcs->set_size = image_loader_j2k_set_size;
	funcs->load = image_loader_j2k_load;
	funcs->write = nullptr;
	funcs->get_pixbuf = image_loader_j2k_get_pixbuf;
	funcs->close = image_loader_j2k_close;
	funcs->abort = image_loader_j2k_abort;
	funcs->free = image_loader_j2k_free;
	funcs->get_format_name = image_loader_j2k_get_format_name;
	funcs->get_format_mime_types = image_loader_j2k_get_format_mime_types;
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
