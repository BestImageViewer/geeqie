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

#ifndef VIEW_DIR_LIST_H
#define VIEW_DIR_LIST_H

#include <gtk/gtk.h>

struct FileData;
struct ViewDir;

ViewDir *vdlist_new(ViewDir *vd, FileData *dir_fd);

gboolean vdlist_set_fd(ViewDir *vd, FileData *dir_fd);
void vdlist_refresh(ViewDir *vd);

const gchar *vdlist_row_get_path(ViewDir *vd, gint row);
gboolean vdlist_find_row(ViewDir *vd, FileData *fd, GtkTreeIter *iter);

void vdlist_rename_by_row(ViewDir *vd, FileData *fd);
FileData *vdlist_row_by_path(ViewDir *vd, const gchar *path, gint *row);

gboolean vdlist_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);
gboolean vdlist_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data);

void vdlist_destroy_cb(GtkWidget *widget, gpointer data);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
