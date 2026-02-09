/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Laurent Monin
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

#ifndef VIEW_FILE_H
#define VIEW_FILE_H

#include <ctime>
#include <functional>

#include <glib.h>
#include <gtk/gtk.h>

#include "filedata.h"
#include "main-defines.h"

struct LayoutWindow;
struct ThumbLoader;

enum FileViewType : guint {
	FILEVIEW_LIST,
	FILEVIEW_ICON,

	FILEVIEW_LAST = FILEVIEW_ICON /**< Keep this up to date! */
};

struct ViewFile
{
	FileViewType type; 	/**< @todo (xsdg): Turn this into a union (see VFLIST and VFICON). */

	gpointer info;

	GtkWidget *widget;
	GtkWidget *listview;
	GtkWidget *scrolled;
	GtkWidget *filter;
	GtkWidget *filter_check[FILEDATA_MARKS_SIZE];

	struct {
		GtkWidget *combo;
		GtkWidget *frame;
		gint count;
		gint last_selected;
		gboolean case_sensitive;
	} file_filter;

	FileData *dir_fd;
	GList *list;

	FileData *click_fd;

	FileData::FileList::SortSettings sort;

	/* func list */
	void (*func_thumb_status)(ViewFile *vf, gdouble val, const gchar *text, gpointer data);
	gpointer data_thumb_status;

	void (*func_status)(ViewFile *vf, gpointer data);
	gpointer data_status;

	LayoutWindow *layout;

	GtkWidget *popup;

	/* thumbs updates*/
	gboolean thumbs_running;
	ThumbLoader *thumbs_loader;
	FileData *thumbs_filedata;

	/* marks */
	gboolean marks_enabled;
	gint active_mark;
	gint clicked_mark;

	/* stars */
	FileData *stars_filedata;
	guint stars_id;

	/* refresh */
	guint refresh_idle_id; /**< event source id */
	time_t time_refresh_set; /**< time when refresh_idle_id was set */

	GList *editmenu_fd_list; /**< file list for edit menu */

	guint read_metadata_in_idle_id;

	using SelectionCallback = std::function<void(FileData *)>;
};

void vf_send_update(ViewFile *vf);

ViewFile *vf_new(FileViewType type, FileData *dir_fd);

void vf_set_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gpointer data), gpointer data);
void vf_set_thumb_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gdouble val, const gchar *text, gpointer data), gpointer data);

void vf_set_layout(ViewFile *vf, LayoutWindow *layout);

gboolean vf_set_fd(ViewFile *vf, FileData *fd);
gboolean vf_refresh(ViewFile *vf);
void vf_refresh_idle(ViewFile *vf);

void vf_thumb_set(ViewFile *vf, gboolean enable);
void vf_marks_set(ViewFile *vf, gboolean enable);
void vf_sort_set(ViewFile *vf, FileData::FileList::SortSettings settings);

guint vf_marks_get_filter(ViewFile *vf);
void vf_mark_filter_toggle(ViewFile *vf, gint mark);

guint vf_class_get_filter(ViewFile *vf);

GList *vf_selection_get_one(ViewFile *vf, FileData *fd);
GList *vf_pop_menu_file_list(ViewFile *vf);
GtkWidget *vf_pop_menu(ViewFile *vf);

FileData *vf_index_get_data(ViewFile *vf, gint row);
gint vf_index_by_fd(ViewFile *vf, FileData *in_fd);
guint vf_count(ViewFile *vf, gint64 *bytes = nullptr);
GList *vf_get_list(ViewFile *vf);

guint vf_selection_count(ViewFile *vf, gint64 *bytes);
GList *vf_selection_get_list(ViewFile *vf);
std::vector<int> vf_selection_get_list_by_index(const ViewFile *vf);
void vf_selection_foreach(ViewFile *vf, const ViewFile::SelectionCallback &func);

void vf_select_all(ViewFile *vf);
void vf_select_none(ViewFile *vf);
void vf_select_invert(ViewFile *vf);
void vf_select_by_fd(ViewFile *vf, FileData *fd);
void vf_select_list(ViewFile *vf, GList *list);

void vf_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode);
void vf_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode);

void vf_refresh_idle_cancel(ViewFile *vf);
void vf_notify_cb(FileData *fd, NotifyType type, gpointer data);

void vf_thumb_update(ViewFile *vf);
void vf_thumb_cleanup(ViewFile *vf);
void vf_thumb_stop(ViewFile *vf);
void vf_read_metadata_in_idle(ViewFile *vf);
void vf_file_filter_set(ViewFile *vf, gboolean enable);
GRegex *vf_file_filter_get_filter(ViewFile *vf);

void vf_star_update(ViewFile *vf);
void vf_star_stop(ViewFile *vf);
void vf_star_cleanup(ViewFile *vf);

#endif /* VIEW_FILE_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
