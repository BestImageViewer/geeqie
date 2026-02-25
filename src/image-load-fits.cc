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

#include "image-load-fits.h"

#include <cmath>
#include <fitsio.h>
#include <limits>

#include "image-load.h"

namespace
{

struct ImageLoaderFITS : public ImageLoaderBackend
{
public:
	~ImageLoaderFITS() override;

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

	GdkPixbuf *pixbuf;
	gint page_num;
	gint page_total;
};

gboolean ImageLoaderFITS::write(const guchar *buf, gsize &chunk_size, gsize count, GError **)
{
	fitsfile *fptr;           // FITS file pointer
	gint anynul;
	gint bitpix;
	gint naxis;
	gint status = 0;          // cfitsio status value must be initialized to zero
	glong fpixel = 1;
	glong naxes[2] = {1, 1};  // Image width and height

	/* Open FITS file from memory buffer */
	if (fits_open_memfile(&fptr, "mem://", READONLY, (void **)&buf, &count, 0, nullptr, &status))
		{
		fits_report_error(stderr, status);
		return FALSE;
		}

	/* Check that the file is an image and get its dimensions */
	if (fits_get_img_param(fptr, 2, &bitpix, &naxis, naxes, &status))
		{
		fits_report_error(stderr, status);
		fits_close_file(fptr, &status);
		return FALSE;
		}

	if (naxis != 2)    // Ensure it's a 2D image
		{
		log_printf("Error: FITS image is not 2D");
		fits_close_file(fptr, &status);
		return FALSE;
		}

	glong width = naxes[0];
	glong height = naxes[1];

	/* Allocate memory for the image data */
	auto *image_data = (gfloat *)malloc(width * height * sizeof(gfloat));
	if (!image_data)
		{
		log_printf("Memory allocation error when processing .fits file");
		fits_close_file(fptr, &status);
		return FALSE;
		}

	/* Read the image data */
	if (fits_read_img(fptr, TFLOAT, fpixel, width * height, nullptr, image_data, &anynul, &status))
		{
		fits_report_error(stderr, status);
		free(image_data);
		fits_close_file(fptr, &status);
		return FALSE;
		}

	/* Close the FITS file */
	fits_close_file(fptr, &status);

	/* Create a GdkPixbuf in RGB format (24-bit depth, 8 bits per channel) */
	g_autoptr(GdkPixbuf) pixbuf_tmp = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
	if (!pixbuf_tmp)
		{
		log_printf("Failed to create GdkPixbuf for .fits file");
		free(image_data);

		return FALSE;
		}

	guchar *pixels = gdk_pixbuf_get_pixels(pixbuf_tmp);
	gint rowstride = gdk_pixbuf_get_rowstride(pixbuf_tmp);

	gfloat max_value = 0;
	gfloat min_value = std::numeric_limits<float>::max();

	/* Get the max and min intensity values in the image */
	for (glong y = 0; y < height; y++)
		{
		for (glong x = 0; x < width; x++)
			{
			gfloat value =  (image_data[(y * width) + x]);

			max_value = std::max(max_value, value);
			min_value = std::min(min_value, value);
			}
		}

	/* Map the float data to RGB and load it into the GdkPixbuf */
	for (glong y = 0; y < height; y++)
		{
		for (glong x = 0; x < width; x++)
			{
			gint pixel_index = (y * rowstride) + (x * 3);
			gfloat value = (image_data[(y * width) + x]);
			/* fits images seem to have a large intensity range, but the useful data is mostly at the lower end.
			 * Using linear scaling results in a black image. */
			auto intensity = (guchar)(255.0 * (log(value - min_value) / log((max_value - min_value))));

			/* Set the RGB channels to the intensity value for grayscale */
			pixels[pixel_index] = intensity;     // Red
			pixels[pixel_index + 1] = intensity; // Green
			pixels[pixel_index + 2] = intensity; // Blue
			}
		}

	pixbuf = g_steal_pointer(&pixbuf_tmp);

	area_updated_cb(nullptr, 0, 0, width, height, data);

	chunk_size = count;

	return TRUE;
}

void ImageLoaderFITS::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
	page_num = 0;
}

GdkPixbuf *ImageLoaderFITS::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderFITS::get_format_name()
{
	return g_strdup("fits");
}

gchar **ImageLoaderFITS::get_format_mime_types()
{
	static const gchar *mime[] = {"image/fits", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

void ImageLoaderFITS::set_page_num(gint page_num)
{
	this->page_num = page_num;
}

gint ImageLoaderFITS::get_page_total()
{
	return page_total;
}

ImageLoaderFITS::~ImageLoaderFITS()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_fits()
{
	return std::make_unique<ImageLoaderFITS>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
