/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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

#ifndef THUMB_H
#define THUMB_H

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "typedefs.h"

struct FileData;
struct ImageLoader;

struct ThumbLoader
{
	gboolean standard_loader;

	ImageLoader *il;
	FileData *fd;           /**< fd->pixbuf contains final (scaled) image when done */

	gboolean cache_enable;
	gboolean cache_hit;
	gdouble percent_done;

	gint max_w;
	gint max_h;

	using Func = void (*)(ThumbLoader *, gpointer);
	Func func_done;
	Func func_error;
	Func func_progress;

	gpointer data;

	guint idle_done_id; /**< event source id */
};


ThumbLoader *thumb_loader_new(gint width, gint height);
void thumb_loader_set_callbacks(ThumbLoader *tl,
				ThumbLoader::Func func_done,
				ThumbLoader::Func func_error,
				ThumbLoader::Func func_progress,
				gpointer data);
void thumb_loader_set_cache(ThumbLoader *tl, gboolean enable_cache, gboolean local, gboolean retry_failed);

gboolean thumb_loader_start(ThumbLoader *tl, FileData *fd);
void thumb_loader_free(ThumbLoader *tl);

GdkPixbuf *thumb_loader_get_pixbuf(ThumbLoader *tl);

void thumb_notify_cb(FileData *fd, NotifyType type, gpointer data);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
