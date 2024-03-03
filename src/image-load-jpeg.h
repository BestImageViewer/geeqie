/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#ifndef IMAGE_LOAD_JPEG_H
#define IMAGE_LOAD_JPEG_H

#include <memory>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "image-load.h"

struct ImageLoaderJpeg : public ImageLoaderBackend
{
public:
	~ImageLoaderJpeg() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, AreaPreparedCb area_prepared_cb, gpointer data) override;
	void set_size(int width, int height) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	void abort() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;

private:
	AreaUpdatedCb area_updated_cb;
	SizePreparedCb size_prepared_cb;
	AreaPreparedCb area_prepared_cb;

	gpointer data;

	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;

	gboolean aborted;
	gboolean stereo;
};

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_jpeg();

#endif /* IMAGE_LOAD_JPEG_H */

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
