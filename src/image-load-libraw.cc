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

#include <cstddef>

#include <libraw/libraw.h>

#include "ui-fileops.h"

struct LibRawPreviewData
{
	guchar *ptr;
	guchar *source_data;
	LibRaw *lr;
};

static GList *libraw_previews = nullptr;

void libraw_free_preview(const guchar *buf)
{
	GList *work = libraw_previews;

	while (work)
		{
		auto ud = static_cast<LibRawPreviewData *>(work->data);
		if (ud->ptr == buf)
			{
			delete ud->lr;
			g_free(ud->source_data);
			g_free(ud);
			libraw_previews = g_list_delete_link(libraw_previews, work);
			return;
			}
		work = work->next;
		}
	g_assert_not_reached();
}

guchar *libraw_get_preview(const gchar *path, gsize &data_len)
{
	size_t source_size;
	g_autofree gchar *pathl = path_from_utf8(path);
	g_autofree guchar *source_data = read_file_snapshot(pathl, source_size);
	if (!source_data)
		{
		return nullptr;
		}

	auto lr = std::make_unique<LibRaw>();

	int ret = lr->open_buffer(source_data, source_size);
	if (ret == LIBRAW_SUCCESS)
		{
		ret = lr->unpack_thumb();
		if (ret == LIBRAW_SUCCESS && lr->is_jpeg_thumb())
			{
			auto *ud = g_new(LibRawPreviewData, 1);
			ud->ptr = reinterpret_cast<guchar *>(lr->imgdata.thumbnail.thumb);
			ud->source_data = g_steal_pointer(&source_data);
			ud->lr = lr.release();

			libraw_previews = g_list_prepend(libraw_previews, ud);

			data_len = ud->lr->imgdata.thumbnail.tlength;
			return ud->ptr;
			}
		}
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
