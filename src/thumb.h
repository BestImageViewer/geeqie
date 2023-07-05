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
#include <glib.h>

#include "typedefs.h"

ThumbLoader *thumb_loader_new(gint width, gint height);
void thumb_loader_set_callbacks(ThumbLoader *tl,
				ThumbLoaderFunc func_done,
				ThumbLoaderFunc func_error,
				ThumbLoaderFunc func_progress,
				gpointer data);
void thumb_loader_set_cache(ThumbLoader *tl, gboolean enable_cache, gboolean local, gboolean retry_failed);

gboolean thumb_loader_start(ThumbLoader *tl, FileData *fd);
void thumb_loader_free(ThumbLoader *tl);

GdkPixbuf *thumb_loader_get_pixbuf(ThumbLoader *tl);

void thumb_notify_cb(FileData *fd, NotifyType type, gpointer data);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
