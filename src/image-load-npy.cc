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

#include "image-load-npy.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "image-load.h"

namespace
{

struct ImageLoaderNPY : public ImageLoaderBackend
{
public:
	~ImageLoaderNPY() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;
	void set_page_num(gint page_num) override;
	gint get_page_total() override;

private:
	AreaUpdatedCb area_updated_cb;
	gpointer data;

	GdkPixbuf *pixbuf;
	gint page_num;
	gint page_total;
};

bool parse_npy_header(const gchar* data, size_t& offset, int& height, int& width, int& channels)
{
	/* Check for the .npy magic number */
	if (std::strncmp(data, "\x93NUMPY", 6) != 0)
		{
		log_printf("Not a valid .npy file.");
		return false;
		}

	int header_len = *reinterpret_cast<const uint16_t*>(data + 8);
	const char* header = data + 10;

	std::string header_str(header, header_len);
	auto shape_pos = header_str.find("'shape': (");
	if (shape_pos == std::string::npos)
		{
		log_printf("Could not find shape in npy header");
		return false;
		}

	sscanf(header_str.c_str() + shape_pos, "'shape': (%d, %d, %d)", &height, &width, &channels);

	offset = 10 + header_len;

	return true;
}

GdkPixbuf *load_npy_to_pixbuf(gchar *buf)
{
	gint channels;
	gint height;
	gint width;
	size_t offset;

	if (!parse_npy_header(buf, offset, height, width, channels))
		{
		return nullptr;
		}

	if (channels != 3)
		{
		log_printf("Only npy RGB images with 3 channels are supported.");
		return nullptr;
		}

	const guint8 *pixel_data = reinterpret_cast<guint8*>(buf + offset);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(pixel_data, GDK_COLORSPACE_RGB, false, 8, width, height, width * channels, nullptr, nullptr);

	if (!pixbuf)
		{
		log_printf("Failed to create GdkPixbuf");
		}

	return pixbuf;
}
gboolean ImageLoaderNPY::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	GdkPixbuf *pixbuf_tmp;

	pixbuf_tmp = load_npy_to_pixbuf(reinterpret_cast<gchar *>(const_cast<guchar *>(buf)));
	if (!pixbuf_tmp)
		{
		log_printf("Failed to load image from buffer");

		return false;
		return 1;
		}

	pixbuf = gdk_pixbuf_copy(pixbuf_tmp);
	chunk_size = count;
	g_object_unref(pixbuf_tmp);

	area_updated_cb(nullptr, 0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), data);

	return TRUE;
}

void ImageLoaderNPY::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
	page_num = 0;
}

GdkPixbuf *ImageLoaderNPY::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderNPY::get_format_name()
{
	return g_strdup("npy");
}

gchar **ImageLoaderNPY::get_format_mime_types()
{
	static const gchar *mime[] = {"application/octet-stream", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderNPY::set_page_num(gint page_num)
{
	this->page_num = page_num;
}

gint ImageLoaderNPY::get_page_total()
{
	return page_total;
}

ImageLoaderNPY::~ImageLoaderNPY()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_npy()
{
	return std::make_unique<ImageLoaderNPY>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
