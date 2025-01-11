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

#include "image-load-jpegxl.h"

#include <cstdint>
#include <cstdlib>
#include <memory>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <jxl/codestream_header.h>
#include <jxl/decode_cxx.h>
#include <jxl/types.h>

#include "image-load.h"

namespace
{

struct ImageLoaderJPEGXL : public ImageLoaderBackend
{
public:
	~ImageLoaderJPEGXL() override;

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
	g_free(pixels);
}

uint8_t *JxlMemoryToPixels(const uint8_t *next_in, size_t size, size_t &xsize, size_t &ysize, size_t &stride)
{
	JxlDecoderPtr dec = JxlDecoderMake(nullptr);
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

gboolean ImageLoaderJPEGXL::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	gboolean ret = FALSE;
	size_t xsize;
	size_t ysize;
	size_t stride;
	uint8_t *pixels = nullptr;

	pixels = JxlMemoryToPixels(buf, count, xsize, ysize, stride);

	if (pixels)
		{
		pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, TRUE, 8, xsize, ysize, stride, free_buffer, nullptr);

		area_updated_cb(nullptr, 0, 0, xsize, ysize, data);

		chunk_size = count;
		ret = TRUE;
		}

	return ret;
}

void ImageLoaderJPEGXL::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderJPEGXL::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderJPEGXL::get_format_name()
{
	return g_strdup("jxl");
}

gchar **ImageLoaderJPEGXL::get_format_mime_types()
{
	static const gchar *mime[] = {"image/jxl", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

ImageLoaderJPEGXL::~ImageLoaderJPEGXL()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_jpegxl()
{
	return std::make_unique<ImageLoaderJPEGXL>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
