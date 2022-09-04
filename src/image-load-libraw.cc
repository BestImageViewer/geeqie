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

#include "main.h"

#include "filefilter.h"
#include "image-load.h"
#include "image-load-libraw.h"

#ifdef HAVE_RAW

#include <libraw/libraw.h>
#include <sys/mman.h>

typedef struct _UnmapData UnmapData;
struct _UnmapData
{
	guchar *ptr;
	guchar *map_data;
	size_t map_len;
	libraw_data_t *lrdt;
};

static GList *libraw_unmap_list = 0;

void libraw_free_preview(guchar *buf)
{
	GList *work = libraw_unmap_list;

	while (work)
		{
		UnmapData *ud = (UnmapData *)work->data;
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

	if (!filter_file_class(il->fd->path, FORMAT_CLASS_RAWIMAGE)) return NULL;

	fd = open(il->fd->path, O_RDONLY);
	if (fd == -1)
		{
		return NULL;
		}

	if (fstat(fd, &st) == -1)
		{
		close(fd);
		return NULL;
		}

	map_len = st.st_size;
	map_data = (guchar *) mmap(0, map_len, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (map_data == MAP_FAILED)
		{
		return NULL;
		}

	lrdt = libraw_init(0);
	if (!lrdt)
		{
		log_printf("Warning: Cannot create libraw handle\n");
		return NULL;
		}

	ret = libraw_open_buffer(lrdt, (void *)map_data, map_len);
	if (ret == LIBRAW_SUCCESS)
		{
		ret = libraw_unpack_thumb(lrdt);
		if (ret == LIBRAW_SUCCESS)
			{
			il->mapped_file = (guchar *)lrdt->thumbnail.thumb;
			*data_len = lrdt->thumbnail.tlength;

			ud = g_new(UnmapData, 1);
			ud->ptr =(guchar *)lrdt->thumbnail.thumb;
			ud->map_data = map_data;
			ud->map_len = lrdt->thumbnail.tlength;
			ud->lrdt = lrdt;

			libraw_unmap_list = g_list_prepend(libraw_unmap_list, ud);

			return (guchar *)lrdt->thumbnail.thumb;
			}
		}

	libraw_close(lrdt);

	return NULL;
}

#else /* !define HAVE_RAW */

void libraw_free_preview(guchar *UNUSED(buf))
{
}

guchar *libraw_get_preview(ImageLoader *UNUSED(il), guint *UNUSED(data_len))
{
	return NULL;
}

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
