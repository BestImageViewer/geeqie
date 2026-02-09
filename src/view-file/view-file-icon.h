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

#ifndef VIEW_FILE_VIEW_FILE_ICON_H
#define VIEW_FILE_VIEW_FILE_ICON_H

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "view-file.h"

class FileData;

struct ViewFileInfoIcon
{
	/* table stuff */
	gint columns;
	gint rows;

	GList *selection;
	FileData *prev_selection;

	GtkWidget *tip_window;
	guint tip_delay_id; /**< event source id */
	FileData *tip_fd;

	FileData *focus_fd;
	gint focus_row;
	gint focus_column;

	gboolean show_text;
};

#define VFICON(_vf_) ((ViewFileInfoIcon *)((_vf_)->info))

gboolean vficon_press_key_cb(ViewFile *vf, GtkWidget *widget, GdkEventKey *event);
gboolean vficon_press_cb(ViewFile *vf, GtkWidget *widget, GdkEventButton *bevent);
gboolean vficon_release_cb(ViewFile *vf, GtkWidget *widget, GdkEventButton *bevent);

FileData *vficon_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter);

#if !HAVE_GTK4
void vficon_dnd_begin(ViewFile *vf, GtkWidget *widget, GdkDragContext *context);
void vficon_dnd_end(ViewFile *vf, GdkDragContext *context);
#endif

void vficon_destroy_cb(ViewFile *vf);
ViewFile *vficon_new(ViewFile *vf);

gboolean vficon_set_fd(ViewFile *vf, FileData *dir_fd);
gboolean vficon_refresh(ViewFile *vf);


void vficon_marks_set(ViewFile *vf, gboolean enable);
void vficon_sort_set(ViewFile *vf, FileData::FileList::SortSettings settings);

GList *vficon_selection_get_one(ViewFile *vf, FileData *fd);
void vficon_pop_menu_rename_cb(ViewFile *vf);
void vficon_pop_menu_add_items(ViewFile *vf, GtkWidget *menu);
void vficon_pop_menu_show_star_rating_cb(ViewFile *vf);
void vficon_pop_menu_refresh_cb(ViewFile *vf);
void vficon_popup_destroy_cb(ViewFile *vf);

gint vficon_index_by_fd(const ViewFile *vf, const FileData *fd);

gboolean vficon_is_selected(ViewFile *vf, FileData *fd);
guint vficon_selection_count(ViewFile *vf, gint64 *bytes);
GList *vficon_selection_get_list(ViewFile *vf);
std::vector<int> vficon_selection_get_list_by_index(const ViewFile *vf);
void vficon_selection_foreach(ViewFile *vf, const ViewFile::SelectionCallback &func);

void vficon_select_all(ViewFile *vf);
void vficon_select_none(ViewFile *vf);
void vficon_select_invert(ViewFile *vf);
void vficon_select_by_fd(ViewFile *vf, FileData *fd);
void vficon_select_list(ViewFile *vf, GList *list);

void vficon_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode);
void vficon_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode);


void vficon_thumb_progress_count(const GList *list, gint &count, gint &done);
void vficon_read_metadata_progress_count(const GList *list, gint &count, gint &done);
void vficon_set_thumb_fd(ViewFile *vf, FileData *fd);
FileData *vficon_thumb_next_fd(ViewFile *vf);

FileData *vficon_star_next_fd(ViewFile *vf);
void vficon_set_star_fd(ViewFile *vf, FileData *fd);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
