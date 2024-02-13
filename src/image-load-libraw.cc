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

#include "image-load.h"

#ifdef HAVE_RAW

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <libraw/libraw.h>

#include "debug.h"
#include "filedata.h"
#include "filefilter.h"

struct UnmapData
{
	guchar *ptr;
	guchar *map_data;
	size_t map_len;
	libraw_data_t *lrdt;
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
			libraw_close(ud->lrdt);
			libraw_unmap_list = g_list_remove_link(libraw_unmap_list, work);
			g_free(ud);
			return;
			}
		work = work->next;
		}
	g_assert_not_reached();
}

guchar *libraw_get_preview(ImageLoader *il, guint *data_len)
{
	libraw_data_t *lrdt;
	int ret;
	UnmapData *ud;
	struct stat st;
	guchar *map_data;
	size_t map_len;
	int fd;

	if (!filter_file_class(il->fd->path, FORMAT_CLASS_RAWIMAGE)) return nullptr;

	fd = open(il->fd->path, O_RDONLY);
	if (fd == -1)
		{
		return nullptr;
		}

	if (fstat(fd, &st) == -1)
		{
		close(fd);
		return nullptr;
		}

	map_len = st.st_size;
	map_data = static_cast<guchar *>(mmap(nullptr, map_len, PROT_READ, MAP_PRIVATE, fd, 0));
	close(fd);

	if (map_data == MAP_FAILED)
		{
		return nullptr;
		}

	lrdt = libraw_init(0);
	if (!lrdt)
		{
		log_printf("Warning: Cannot create libraw handle\n");
		return nullptr;
		}

	ret = libraw_open_buffer(lrdt, map_data, map_len);
	if (ret == LIBRAW_SUCCESS)
		{
		ret = libraw_unpack_thumb(lrdt);
		if (ret == LIBRAW_SUCCESS)
			{
			il->mapped_file = reinterpret_cast<guchar *>(lrdt->thumbnail.thumb);
			*data_len = lrdt->thumbnail.tlength;

			ud = g_new(UnmapData, 1);
			ud->ptr =reinterpret_cast<guchar *>(lrdt->thumbnail.thumb);
			ud->map_data = map_data;
			ud->map_len = lrdt->thumbnail.tlength;
			ud->lrdt = lrdt;

			libraw_unmap_list = g_list_prepend(libraw_unmap_list, ud);

			return reinterpret_cast<guchar *>(lrdt->thumbnail.thumb);
			}
		}

	libraw_close(lrdt);

	return nullptr;
}

#else /* !define HAVE_RAW */

void libraw_free_preview(const guchar *)
{
}

guchar *libraw_get_preview(ImageLoader *, guint *)
{
	return nullptr;
}

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
