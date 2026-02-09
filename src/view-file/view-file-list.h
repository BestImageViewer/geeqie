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

#ifndef VIEW_FILE_VIEW_FILE_LIST_H
#define VIEW_FILE_VIEW_FILE_LIST_H

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "view-file.h"

class FileData;

struct ViewFileInfoList
{
	FileData *select_fd;

	gboolean thumbs_enabled;

	guint select_idle_id; /**< event source id */
};

#define VFLIST(_vf_) ((ViewFileInfoList *)((_vf_)->info))

gboolean vflist_press_key_cb(ViewFile *vf, GtkWidget *widget, GdkEventKey *event);
gboolean vflist_press_cb(ViewFile *vf, GtkWidget *widget, GdkEventButton *bevent);
gboolean vflist_release_cb(ViewFile *vf, GtkWidget *widget, GdkEventButton *bevent);

FileData *vflist_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter);

#if !HAVE_GTK4
void vflist_dnd_begin(ViewFile *vf, GtkWidget *widget, GdkDragContext *context);
void vflist_dnd_end(ViewFile *vf, GdkDragContext *context);
#endif

void vflist_destroy_cb(ViewFile *vf);
ViewFile *vflist_new(ViewFile *vf);

gboolean vflist_set_fd(ViewFile *vf, FileData *dir_fd);
gboolean vflist_refresh(ViewFile *vf);

void vflist_thumb_set(ViewFile *vf, gboolean enable);
void vflist_marks_set(ViewFile *vf, gboolean enable);
void vflist_sort_set(ViewFile *vf, FileData::FileList::SortSettings settings);

GList *vflist_selection_get_one(ViewFile *vf, FileData *fd);
void vflist_pop_menu_rename_cb(ViewFile *vf);
void vflist_pop_menu_add_items(ViewFile *vf, GtkWidget *menu);
void vflist_pop_menu_show_star_rating_cb(ViewFile *vf);
void vflist_pop_menu_refresh_cb(ViewFile *vf);
void vflist_popup_destroy_cb(ViewFile *vf);

gint vflist_index_by_fd(const ViewFile *vf, const FileData *fd);

gboolean vflist_is_selected(ViewFile *vf, FileData *fd);
guint vflist_selection_count(ViewFile *vf, gint64 *bytes = nullptr);
GList *vflist_selection_get_list(ViewFile *vf);
std::vector<int> vflist_selection_get_list_by_index(const ViewFile *vf);
void vflist_selection_foreach(ViewFile *vf, const ViewFile::SelectionCallback &func);

void vflist_select_all(ViewFile *vf);
void vflist_select_none(ViewFile *vf);
void vflist_select_invert(ViewFile *vf);
void vflist_select_by_fd(ViewFile *vf, FileData *fd);
void vflist_select_list(ViewFile *vf, GList *list);

void vflist_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode);
void vflist_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode);

void vflist_color_set(ViewFile *vf, FileData *fd, gboolean color_set);

void vflist_thumb_progress_count(const GList *list, gint &count, gint &done);
void vflist_read_metadata_progress_count(const GList *list, gint &count, gint &done);
void vflist_set_thumb_fd(ViewFile *vf, FileData *fd);
FileData *vflist_thumb_next_fd(ViewFile *vf);

FileData *vflist_star_next_fd(ViewFile *vf);
void vflist_set_star_fd(ViewFile *vf, FileData *fd);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
