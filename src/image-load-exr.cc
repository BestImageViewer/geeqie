/*
 * Copyright (C) 2024 - The Geeqie Team
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

#include "image-load-exr.h"

#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <vector>

#include "image-load.h"

namespace
{

struct ImageLoaderEXR : public ImageLoaderBackend
{
public:
	~ImageLoaderEXR() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, gpointer data) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;
	void set_page_num(gint page_num) override;
	gint get_page_total() override;

private:
	AreaUpdatedCb area_updated_cb;
	gpointer data;

	GdkPixbuf *pixbuf = nullptr;
	gint page_num;
	gint page_total;
};

class MemBufferIStream : public Imf::IStream
{
public:
	MemBufferIStream(const char* filename, const guchar* buffer, size_t size, size_t pos)
		: Imf::IStream(filename), _buffer(buffer), _size(size), _pos(pos) {}

	bool read(char c[], int n) override
	{
		if (_pos + n > _size)
			{
			throw std::runtime_error("Read beyond end of buffer");
			}
		memcpy(c, _buffer + _pos, n);
		_pos += n;
		return true;
	}

	uint64_t tellg() override
	{
		return _pos;
	}

	void seekg(uint64_t pos) override
	{
		_pos = pos;
	}

	[[nodiscard]] bool isMemoryMapped() const override
	{
		return true;
	}

	char* readMemoryMapped(int n) override
	{
		if (_pos + n > _size)
			{
			throw std::runtime_error("Read beyond end of buffer");
			}
		char* ptr = const_cast<char*>(reinterpret_cast<const char*>(_buffer + _pos));
		_pos += n;
		return ptr;
	}

private:
	const guchar* _buffer;
	size_t _size;
	size_t _pos;
};

gboolean ImageLoaderEXR::write(const guchar *buffer, gsize &chunk_size, gsize count, GError **)
{
	try
		{
		MemBufferIStream stream("buffer.exr", buffer, count, 0);
		Imf::RgbaInputFile file(stream);
		Imath::Box2i dw = file.dataWindow();

		gint width = dw.max.x - dw.min.x + 1;
		gint height = dw.max.y - dw.min.y + 1;

		// Allocate memory for the pixel data
		Imf::Array2D<Imf::Rgba> pixels(height, width);
		file.setFrameBuffer(&pixels[0][0] - dw.min.x - (dw.min.y * width), 1, width);
		file.readPixels(dw.min.y, dw.max.y);

		// Convert EXR pixel data to GdkPixbuf format (8-bit RGBA)
		auto *image_data = g_new0(guchar, width * height * 4);
		for (gint y = 0; y < height; ++y)
			{
			for (gint x = 0; x < width; ++x)
				{
				const Imf::Rgba& pixel = pixels[y][x];

				// Convert floating-point HDR values to 8-bit per channel
				gint r = static_cast<gint>(std::min(1.0F, static_cast<gfloat>(pixel.r)) * 255.0F);
				gint g = static_cast<gint>(std::min(1.0F, static_cast<gfloat>(pixel.g)) * 255.0F);
				gint b = static_cast<gint>(std::min(1.0F, static_cast<gfloat>(pixel.b)) * 255.0F);
				gint a = static_cast<gint>(std::min(1.0F, static_cast<gfloat>(pixel.a)) * 255.0F);

				// Store in RGBA format
				image_data[4 * (y * width + x)] = r;
				image_data[(4 * (y * width + x)) + 1] = g;
				image_data[(4 * (y * width + x)) + 2] = b;
				image_data[(4 * (y * width + x)) + 3] = a;
				}
			}

		/* Create GdkPixbuf from raw RGBA data
		 * EXR always has alpha */
		pixbuf = gdk_pixbuf_new_from_data(image_data, GDK_COLORSPACE_RGB, TRUE, 8, width, height, width * 4, free_pixels, nullptr);

		area_updated_cb(nullptr, 0, 0, width, height, data);

		chunk_size = count;

		return true;
		}

	catch (const std::exception& e)
		{
		log_printf("Error loading EXR: %s", e.what());
		return false;
		}
}

void ImageLoaderEXR::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
	page_num = 0;
}

GdkPixbuf *ImageLoaderEXR::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderEXR::get_format_name()
{
	return g_strdup("exr");
}

gchar **ImageLoaderEXR::get_format_mime_types()
{
	static const gchar *mime[] = {"image/x-exr", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderEXR::set_page_num(gint page_num)
{
	this->page_num = page_num;
}

gint ImageLoaderEXR::get_page_total()
{
	return page_total;
}

ImageLoaderEXR::~ImageLoaderEXR()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_exr()
{
	return std::make_unique<ImageLoaderEXR>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
