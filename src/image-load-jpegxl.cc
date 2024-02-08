/*
 * Copyright (C) 2021- The Geeqie Team
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
 *
 *
 * Including parts:
 *
 * Copyright (c) the JPEG XL Project Authors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <config.h>

#ifdef HAVE_JPEGXL

#include "image-load-jpegxl.h"

#include <memory>

#include <jxl/decode.h>

#include "debug.h"
#include "image-load.h"

struct ImageLoaderJPEGXL {
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
	g_free(pixels);
}

static uint8_t *JxlMemoryToPixels(const uint8_t *next_in, size_t size, size_t &xsize, size_t &ysize, size_t &stride)
{
	std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> dec{JxlDecoderCreate(nullptr), JxlDecoderDestroy};
	if (!dec)
		{
		log_printf("JxlDecoderCreate failed\n");
		return nullptr;
		}
	if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE))
		{
		log_printf("JxlDecoderSubscribeEvents failed\n");
		return nullptr;
		}

	/* Avoid compiler warning - used uninitialized */
	/* This file will be replaced by libjxl at some time */
	ysize = 0;
	stride = 0;
	
	std::unique_ptr<uint8_t, decltype(&free)> pixels{nullptr, free};
	JxlBasicInfo info;
	JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
	JxlDecoderSetInput(dec.get(), next_in, size);

	for (;;)
		{
		JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

		switch (status)
			{
			case JXL_DEC_ERROR:
				log_printf("Decoder error\n");
				return nullptr;
			case JXL_DEC_NEED_MORE_INPUT:
				log_printf("Error, already provided all input\n");
				return nullptr;
			case JXL_DEC_BASIC_INFO:
				if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info))
					{
					log_printf("JxlDecoderGetBasicInfo failed\n");
					return nullptr;
					}
				xsize = info.xsize;
				ysize = info.ysize;
				stride = info.xsize * 4;
				break;
			case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
				{
				size_t buffer_size;
				if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec.get(), &format, &buffer_size))
					{
					log_printf("JxlDecoderImageOutBufferSize failed\n");
					return nullptr;
					}
				if (buffer_size != stride * ysize)
					{
					log_printf("Invalid out buffer size %zu %zu\n", buffer_size, stride * ysize);
					return nullptr;
					}
				size_t pixels_buffer_size = buffer_size * sizeof(uint8_t);
				pixels.reset(static_cast<uint8_t *>(malloc(pixels_buffer_size)));
				if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec.get(), &format, pixels.get(), pixels_buffer_size))
					{
					log_printf("JxlDecoderSetImageOutBuffer failed\n");
					return nullptr;
					}
				}
				break;
			case JXL_DEC_FULL_IMAGE:
				// This means the decoder has decoded all pixels into the buffer.
				return pixels.release();
			case JXL_DEC_SUCCESS:
				log_printf("Decoding finished before receiving pixel data\n");
				return nullptr;
			default:
				log_printf("Unexpected decoder status: %d\n", status);
				return nullptr;
			}
		}

	return nullptr;
}

static gboolean image_loader_jpegxl_load(gpointer loader, const guchar *buf, gsize count, GError **)
{
	auto ld = static_cast<ImageLoaderJPEGXL *>(loader);
	gboolean ret = FALSE;
	size_t xsize;
	size_t ysize;
	size_t stride;
	uint8_t *decoded = nullptr;

	decoded = JxlMemoryToPixels(buf, count, xsize, ysize, stride);

	if (decoded)
		{
		ld->pixbuf = gdk_pixbuf_new_from_data(decoded, GDK_COLORSPACE_RGB, TRUE, 8, xsize, ysize, stride, free_buffer, nullptr);

		ld->area_updated_cb(loader, 0, 0, xsize, ysize, ld->data);

		ret = TRUE;
		}

	return ret;
}

static gpointer image_loader_jpegxl_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderJPEGXL, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return loader;
}

static void image_loader_jpegxl_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderJPEGXL *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_jpegxl_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderJPEGXL *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_jpegxl_get_format_name(gpointer)
{
	return g_strdup("jxl");
}

static gchar** image_loader_jpegxl_get_format_mime_types(gpointer)
{
	static const gchar *mime[] = {"image/jxl", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gboolean image_loader_jpegxl_close(gpointer, GError **)
{
	return TRUE;
}

static void image_loader_jpegxl_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderJPEGXL *>(loader);
	ld->abort = TRUE;
}

static void image_loader_jpegxl_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderJPEGXL *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_jpegxl(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_jpegxl_new;
	funcs->set_size = image_loader_jpegxl_set_size;
	funcs->load = image_loader_jpegxl_load;
	funcs->write = nullptr;
	funcs->get_pixbuf = image_loader_jpegxl_get_pixbuf;
	funcs->close = image_loader_jpegxl_close;
	funcs->abort = image_loader_jpegxl_abort;
	funcs->free = image_loader_jpegxl_free;
	funcs->get_format_name = image_loader_jpegxl_get_format_name;
	funcs->get_format_mime_types = image_loader_jpegxl_get_format_mime_types;
}

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
