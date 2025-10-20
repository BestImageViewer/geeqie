/*
 * Copyright (C) 2021 The Geeqie Team
 *
 * Authors: Colin Clark
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

/**
 * @file
 * This uses libraw to extract a thumbnail from a raw image. The exiv2 library
 * does not (yet) extract thumbnails from .cr3 images.
 * LibRaw seems to be slower than exiv2, so let exiv2 have priority.
 */

#include "image-load-libraw.h"

#include <config.h>

#if HAVE_RAW

#include <sys/mman.h>

#include <cstddef>

#include <libraw/libraw.h>

#include "filefilter.h"
#include "ui-fileops.h"

struct UnmapData
{
	guchar *ptr;
	guchar *map_data;
	size_t map_len;
	LibRaw *lr;
};

static GList *libraw_unmap_list = nullptr;

void libraw_free_preview(const guchar *buf)
{
	GList *work = libraw_unmap_list;

	while (work)
		{
		auto ud = static_cast<UnmapData *>(work->data);
		if (ud->ptr == buf)
			{
			munmap(ud->map_data, ud->map_len);
			delete ud->lr;
			g_free(ud);
			libraw_unmap_list = g_list_delete_link(libraw_unmap_list, work);
			return;
			}
		work = work->next;
		}
	g_assert_not_reached();
}

guchar *libraw_get_preview(const gchar *path, gsize &data_len)
{
	if (!filter_file_class(path, FORMAT_CLASS_RAWIMAGE)) return nullptr;

	size_t map_len;
	guchar *map_data = map_file(path, map_len);
	if (!map_data)
		{
		return nullptr;
		}

	auto lr = std::make_unique<LibRaw>();

	int ret = lr->open_buffer(map_data, map_len);
	if (ret == LIBRAW_SUCCESS)
		{
		ret = lr->unpack_thumb();
		if (ret == LIBRAW_SUCCESS && lr->is_jpeg_thumb())
			{
			auto *ud = g_new(UnmapData, 1);
			ud->ptr = reinterpret_cast<guchar *>(lr->imgdata.thumbnail.thumb);
			ud->map_data = map_data;
			ud->map_len = map_len;
			ud->lr = lr.release();

			libraw_unmap_list = g_list_prepend(libraw_unmap_list, ud);

			data_len = ud->lr->imgdata.thumbnail.tlength;
			return ud->ptr;
			}
		}

	munmap(map_data, map_len);

	return nullptr;
}

#else /* !define HAVE_RAW */

void libraw_free_preview(const guchar *)
{
}

guchar *libraw_get_preview(const gchar *, gsize &)
{
	return nullptr;
}

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
