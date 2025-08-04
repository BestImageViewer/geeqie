/*
 * Copyright (C) 2006 John Ellis
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

#ifndef PAN_VIEW_PAN_VIEW_H
#define PAN_VIEW_PAN_VIEW_H

#include <glib.h>

#include "filedata.h"

struct PanItem;
struct PanWindow;

void pan_layout_update(PanWindow *pw);
GList *pan_layout_intersect(PanWindow *pw, gint x, gint y, gint width, gint height);
void pan_layout_resize(PanWindow *pw);

GList *pan_cache_sort(GList *list, FileData::FileList::SortSettings settings);
void pan_cache_sync_date(PanWindow *pw, GList *list);
void pan_cache_get_image_size(PanWindow *pw, const FileData *fd, gint &w, gint &h);

void pan_info_update(PanWindow *pw, PanItem *pi);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
