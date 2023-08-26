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

#ifndef VIEW_DIR_H
#define VIEW_DIR_H

struct FileData;
struct LayoutWindow;

enum {
	DIR_COLUMN_POINTER = 0,
	DIR_COLUMN_ICON,
	DIR_COLUMN_NAME,
	DIR_COLUMN_COLOR,
	DIR_COLUMN_DATE,
	DIR_COLUMN_LINK,
	DIR_COLUMN_COUNT
};

struct PixmapFolders
{
	GdkPixbuf *close;
	GdkPixbuf *open;
	GdkPixbuf *deny;
	GdkPixbuf *parent;
	GdkPixbuf *link;
	GdkPixbuf *read_only;
};

struct ViewDir
{
	DirViewType type;
	gpointer info;

	GtkWidget *widget;
	GtkWidget *view;

	FileData *dir_fd;

	FileData *click_fd;

	FileData *drop_fd;
	GList *drop_list;
	guint drop_scroll_id; /**< event source id */

	/* func list */
	void (*select_func)(ViewDir *vd, FileData *fd, gpointer data);
	gpointer select_data;

	void (*dnd_drop_update_func)(ViewDir *vd);
	void (*dnd_drop_leave_func)(ViewDir *vd);

	LayoutWindow *layout;

	GtkWidget *popup;

	PixmapFolders *pf;
};

ViewDir *vd_new(LayoutWindow *lw);

void vd_set_select_func(ViewDir *vdl, void (*func)(ViewDir *vdl, FileData *fd, gpointer data), gpointer data);

gboolean vd_set_fd(ViewDir *vdl, FileData *dir_fd);
void vd_refresh(ViewDir *vdl);
gboolean vd_find_row(ViewDir *vd, FileData *fd, GtkTreeIter *iter);

void vd_color_set(ViewDir *vd, FileData *fd, gint color_set);
void vd_popup_destroy_cb(GtkWidget *widget, gpointer data);

GtkWidget *vd_drop_menu(ViewDir *vd, gint active);
GtkWidget *vd_pop_menu(ViewDir *vd, FileData *fd);

void vd_new_folder(ViewDir *vd, FileData *dir_fd);

void vd_dnd_drop_scroll_cancel(ViewDir *vd);
void vd_dnd_init(ViewDir *vd);

void vd_activate_cb(GtkTreeView *tview, GtkTreePath *tpath, GtkTreeViewColumn *column, gpointer data);
void vd_color_cb(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data);

gboolean vd_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data);
gboolean vd_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);
gboolean vd_press_cb(GtkWidget *widget,  GdkEventButton *bevent, gpointer data);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
