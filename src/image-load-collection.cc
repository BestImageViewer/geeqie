/*
 * Copyright (C) 20018 - The Geeqie Team
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

#include "image-load-collection.h"

#include <unistd.h>

#include <cstdio>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>

#include "cache.h"
#include "filedata.h"
#include "image-load.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"

namespace
{

struct ImageLoaderCOLLECTION : public ImageLoaderBackend
{
public:
	~ImageLoaderCOLLECTION() override;

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

gboolean ImageLoaderCOLLECTION::write(const guchar *, gsize &chunk_size, gsize count, GError **)
{
	auto il = static_cast<ImageLoader *>(data);

	#define LINE_LENGTH 1000

	gboolean ret = FALSE;
	gchar *randname;
	gchar *cmd_line;
	FILE *fp = nullptr;
	gint line_count = 0;
	GString *file_names = g_string_new(nullptr);
	gchar line[LINE_LENGTH];
	gchar *pathl;

	if (runcmd("which montage >/dev/null 2>&1") == 0)
		{
		pathl = path_from_utf8(il->fd->path);
		fp = fopen(pathl, "r");
		g_free(pathl);
		if (fp)
			{
			while(fgets(line, LINE_LENGTH, fp) && line_count < options->thumbnails.collection_preview)
				{
				if (line[0] && line[0] != '#')
					{
					g_auto(GStrv) split_line = g_strsplit(line, "\"", 4);
					g_autofree gchar *cache_found = cache_find_location(CACHE_TYPE_THUMB, split_line[1]);
					if (cache_found)
						{
						g_string_append_printf(file_names, "\"%s\" ", cache_found);
						line_count++;
						}
					}
				}
			fclose(fp);

			if (file_names->len > 0)
				{
				randname = g_strdup("/tmp/geeqie_collection_XXXXXX.png");
				g_mkstemp(randname);

				cmd_line = g_strdup_printf("montage %s -geometry %dx%d+1+1 %s >/dev/null 2>&1", file_names->str, options->thumbnails.max_width, options->thumbnails.max_height, randname);

				runcmd(cmd_line);
				pixbuf = gdk_pixbuf_new_from_file(randname, nullptr);
				if (pixbuf)
					{
					area_updated_cb(nullptr, 0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), data);
					}

				unlink(randname);
				g_free(randname);
				g_free(cmd_line);

				ret = TRUE;
				}

			g_string_free(file_names, TRUE);
			}
		}
	chunk_size = count;
	return ret;
}

void ImageLoaderCOLLECTION::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, AreaPreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderCOLLECTION::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderCOLLECTION::get_format_name()
{
	return g_strdup("collection");
}

gchar **ImageLoaderCOLLECTION::get_format_mime_types()
{
	static const gchar *mime[] = {"image/png", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

ImageLoaderCOLLECTION::~ImageLoaderCOLLECTION()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_collection()
{
	return std::make_unique<ImageLoaderCOLLECTION>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
