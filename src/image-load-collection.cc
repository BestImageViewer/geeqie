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

#include "main.h"
#include "image-load.h"
#include "image-load-collection.h"

#include "cache.h"
#include "misc.h"
#include "ui-fileops.h"

typedef struct _ImageLoaderCOLLECTION ImageLoaderCOLLECTION;
struct _ImageLoaderCOLLECTION {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
};

static gboolean image_loader_collection_load(gpointer loader, const guchar *UNUSED(buf), gsize UNUSED(count), GError **UNUSED(error))
{
	auto ld = static_cast<ImageLoaderCOLLECTION *>(loader);
	auto il = static_cast<ImageLoader *>(ld->data);

	#define LINE_LENGTH 1000

	gboolean ret = FALSE;
	gchar *randname;
	gchar *cmd_line;
	FILE *fp = NULL;
	gint line_count = 0;
	GString *file_names = g_string_new(NULL);
	gchar line[LINE_LENGTH];
	gchar **split_line = NULL;
	gchar *cache_found;
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
					split_line = g_strsplit(line, "\"", 4);
					cache_found = cache_find_location(CACHE_TYPE_THUMB, split_line[1]);
					if (cache_found)
						{
						file_names = g_string_append(file_names, g_strconcat("\"", cache_found,"\" ", NULL));
						line_count++;
						}
					g_free(cache_found);
					}
					if (split_line)
						{
						g_strfreev(split_line);
						}
					split_line = NULL;
				}
			fclose(fp);

			if (file_names->len > 0)
				{
				randname = g_strdup("/tmp/geeqie_collection_XXXXXX.png");
				g_mkstemp(randname);

				cmd_line = g_strdup_printf("montage %s -geometry %dx%d+1+1 %s >/dev/null 2>&1", file_names->str, options->thumbnails.max_width, options->thumbnails.max_height, randname);

				runcmd(cmd_line);
				ld->pixbuf = gdk_pixbuf_new_from_file(randname, NULL);
				if (ld->pixbuf)
					{
					ld->area_updated_cb(loader, 0, 0, gdk_pixbuf_get_width(ld->pixbuf), gdk_pixbuf_get_height(ld->pixbuf), ld->data);
					}

				unlink(randname);
				g_free(randname);
				g_string_free(file_names, TRUE);
				g_free(cmd_line);

				ret = TRUE;
				}
			else
				{
				g_string_free(file_names, TRUE);
				}
			}
		}
	return ret;
}

static gpointer image_loader_collection_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderCOLLECTION, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return loader;
}

static void image_loader_collection_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderCOLLECTION *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_collection_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderCOLLECTION *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_collection_get_format_name(gpointer UNUSED(loader))
{
	return g_strdup("collection");
}
static gchar** image_loader_collection_get_format_mime_types(gpointer UNUSED(loader))
{
	static const gchar *mime[] = {"image/png", NULL};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gboolean image_loader_collection_close(gpointer UNUSED(loader), GError **UNUSED(error))
{
	return TRUE;
}

static void image_loader_collection_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderCOLLECTION *>(loader);
	ld->abort = TRUE;
}

static void image_loader_collection_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderCOLLECTION *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_collection(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_collection_new;
	funcs->set_size = image_loader_collection_set_size;
	funcs->load = image_loader_collection_load;
	funcs->write = NULL;
	funcs->get_pixbuf = image_loader_collection_get_pixbuf;
	funcs->close = image_loader_collection_close;
	funcs->abort = image_loader_collection_abort;
	funcs->free = image_loader_collection_free;
	funcs->get_format_name = image_loader_collection_get_format_name;
	funcs->get_format_mime_types = image_loader_collection_get_format_mime_types;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
