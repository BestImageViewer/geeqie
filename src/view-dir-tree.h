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

#ifndef VIEW_DIR_TREE_H
#define VIEW_DIR_TREE_H

#include <ctime>

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

struct FileData;
struct ViewDir;

struct NodeData
{
	FileData *fd;
	gboolean expanded;
	time_t last_update;
	gint version;
};

ViewDir *vdtree_new(ViewDir *vd, FileData *dir_fd);

gboolean vdtree_set_fd(ViewDir *vd, FileData *dir_fd);
void vdtree_refresh(ViewDir *vd);

const gchar *vdtree_row_get_path(ViewDir *vd, gint row);
gboolean vdtree_find_row(ViewDir *vd, FileData *fd, GtkTreeIter *iter, GtkTreeIter *parent);
gboolean vdtree_populate_path_by_iter(ViewDir *vd, GtkTreeIter *iter, gboolean force, FileData *target_fd);

FileData *vdtree_populate_path(ViewDir *vd, FileData *target_fd, gboolean expand, gboolean force);
void vdtree_rename_by_data(ViewDir *vd, FileData *fd);

gboolean vdtree_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);
gboolean vdtree_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data);

void vdtree_destroy_cb(GtkWidget *widget, gpointer data);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
