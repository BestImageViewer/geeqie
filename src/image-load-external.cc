/*
 * Copyright (C) 2021 - The Geeqie Team
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

#include "image-load-external.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>

#include "filedata.h"
#include "image-load.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"

namespace
{

struct ImageLoaderExternal : public ImageLoaderBackend
{
public:
	~ImageLoaderExternal() override;

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

gboolean ImageLoaderExternal::write(const guchar *, gsize &chunk_size, gsize count, GError **)
{
	auto il = static_cast<ImageLoader *>(data);

	g_autofree gchar *tilde_filename = expand_tilde(options->external_preview.extract);

	g_autofree gchar *randname = g_strdup("/tmp/geeqie_external_preview_XXXXXX");
	g_mkstemp(randname);

	g_autofree gchar *cmd_line = g_strdup_printf("\"%s\" \"%s\" \"%s\"", tilde_filename, il->fd->path, randname);

	runcmd(cmd_line);

	pixbuf = gdk_pixbuf_new_from_file(randname, nullptr);

	area_updated_cb(nullptr, 0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), data);

	unlink_file(randname);

	chunk_size = count;
	return TRUE;
}

void ImageLoaderExternal::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderExternal::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderExternal::get_format_name()
{
	return g_strdup("external");
}

gchar **ImageLoaderExternal::get_format_mime_types()
{
	static const gchar *mime[] = {"application/octet-stream", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

ImageLoaderExternal::~ImageLoaderExternal()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_external()
{
	return std::make_unique<ImageLoaderExternal>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
