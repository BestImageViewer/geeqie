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

#include "main.h"
#include "view_file_icon.h"

#include "bar.h"
#include "cellrenderericon.h"
#include "collect.h"
#include "collect-io.h"
#include "collect-table.h"
#include "dnd.h"
#include "editors.h"
#include "img-view.h"
#include "filedata.h"
#include "layout.h"
#include "layout_image.h"
#include "menu.h"
#include "metadata.h"
#include "thumb.h"
#include "utilops.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "ui_tree_edit.h"
#include "uri_utils.h"
#include "view_file.h"

#include <gdk/gdkkeysyms.h> /* for keyboard values */


/* between these, the icon width is increased by thumb_max_width / 2 */
#define THUMB_MIN_ICON_WIDTH 128
#define THUMB_MAX_ICON_WIDTH 150

#define VFICON_MAX_COLUMNS 32
#define THUMB_BORDER_PADDING 2

#define VFICON_TIP_DELAY 500

enum {
	FILE_COLUMN_POINTER = 0,
	FILE_COLUMN_COUNT
};

static gint vficon_index_by_id(ViewFile *vf, IconData *in_id);

static IconData *vficon_icon_data(ViewFile *vf, FileData *fd)
{
	IconData *id = NULL;
	GList *work;

	if (!fd) return NULL;
	work = vf->list;
	while (work && !id)
		{
		IconData *chk = work->data;
		work = work->next;
		if (chk->fd == fd) id = chk;
		}
	return id;
}

static void iconlist_free(GList *list)
{
	GList *work = list;
	while (work)
		{
		IconData *id = work->data;
		file_data_unref(id->fd);
		g_free(id);
		work = work->next;
		}

	g_list_free(list);

}

gint iconlist_sort_file_cb(gpointer a, gpointer b)
{
	IconData *ida = a;
	IconData *idb = b;
	return filelist_sort_compare_filedata(ida->fd, idb->fd);
}

GList *iconlist_sort(GList *list, SortType method, gboolean ascend)
{
	return filelist_sort_full(list, method, ascend, (GCompareFunc) iconlist_sort_file_cb);
}

GList *iconlist_insert_sort(GList *list, IconData *id, SortType method, gboolean ascend)
{
	return filelist_insert_sort_full(list, id, method, ascend, (GCompareFunc) iconlist_sort_file_cb);
}


static void vficon_toggle_filenames(ViewFile *vf);
static void vficon_selection_remove(ViewFile *vf, IconData *id, SelectionType mask, GtkTreeIter *iter);
static void vficon_move_focus(ViewFile *vf, gint row, gint col, gboolean relative);
static void vficon_set_focus(ViewFile *vf, IconData *id);
static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint h, gboolean force);


/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

GList *vficon_selection_get_one(ViewFile *vf, FileData *fd)
{
	return g_list_prepend(filelist_copy(fd->sidecar_files), file_data_ref(fd));
}

GList *vficon_pop_menu_file_list(ViewFile *vf)
{
	if (!VFICON(vf)->click_id) return NULL;

	if (VFICON(vf)->click_id->selected & SELECTION_SELECTED)
		{
		return vf_selection_get_list(vf);
		}

	return vficon_selection_get_one(vf, VFICON(vf)->click_id->fd);
}

void vficon_pop_menu_view_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	if (!VFICON(vf)->click_id) return;

	if (VFICON(vf)->click_id->selected & SELECTION_SELECTED)
		{
		GList *list;

		list = vf_selection_get_list(vf);
		view_window_new_from_list(list);
		filelist_free(list);
		}
	else
		{
		view_window_new(VFICON(vf)->click_id->fd);
		}
}

void vficon_pop_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	file_util_rename(NULL, vf_pop_menu_file_list(vf), vf->listview);
}

void vficon_pop_menu_show_names_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	vficon_toggle_filenames(vf);
}

void vficon_pop_menu_refresh_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	vf_refresh(vf);
}

void vficon_popup_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vficon_selection_remove(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, NULL);
	VFICON(vf)->click_id = NULL;
	vf->popup = NULL;
}

/*
 *-------------------------------------------------------------------
 * signals
 *-------------------------------------------------------------------
 */

static void vficon_send_layout_select(ViewFile *vf, IconData *id)
{
	FileData *read_ahead_fd = NULL;
	FileData *sel_fd;
	FileData *cur_fd;

	if (!vf->layout || !id || !id->fd) return;

	sel_fd = id->fd;

	cur_fd = layout_image_get_fd(vf->layout);
	if (sel_fd == cur_fd) return; /* no change */

	if (options->image.enable_read_ahead)
		{
		gint row;

		row = g_list_index(vf->list, id);
		if (row > vficon_index_by_fd(vf, cur_fd) &&
		    (guint) (row + 1) < vf_count(vf, NULL))
			{
			read_ahead_fd = vf_index_get_data(vf, row + 1);
			}
		else if (row > 0)
			{
			read_ahead_fd = vf_index_get_data(vf, row - 1);
			}
		}

	layout_image_set_with_ahead(vf->layout, sel_fd, read_ahead_fd);
}

static void vficon_toggle_filenames(ViewFile *vf)
{
	GtkAllocation allocation;
	VFICON(vf)->show_text = !VFICON(vf)->show_text;
	options->show_icon_names = VFICON(vf)->show_text;

	gtk_widget_get_allocation(vf->listview, &allocation);
	vficon_populate_at_new_size(vf, allocation.width, allocation.height, TRUE);
}

static gint vficon_get_icon_width(ViewFile *vf)
{
	gint width;

	if (!VFICON(vf)->show_text) return options->thumbnails.max_width;

	width = options->thumbnails.max_width + options->thumbnails.max_width / 2;
	if (width < THUMB_MIN_ICON_WIDTH) width = THUMB_MIN_ICON_WIDTH;
	if (width > THUMB_MAX_ICON_WIDTH) width = options->thumbnails.max_width;

	return width;
}

/*
 *-------------------------------------------------------------------
 * misc utils
 *-------------------------------------------------------------------
 */

static gboolean vficon_find_position(ViewFile *vf, IconData *id, gint *row, gint *col)
{
	gint n;

	n = g_list_index(vf->list, id);

	if (n < 0) return FALSE;

	*row = n / VFICON(vf)->columns;
	*col = n - (*row * VFICON(vf)->columns);

	return TRUE;
}

static gboolean vficon_find_iter(ViewFile *vf, IconData *id, GtkTreeIter *iter, gint *column)
{
	GtkTreeModel *store;
	gint row, col;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	if (!vficon_find_position(vf, id, &row, &col)) return FALSE;
	if (!gtk_tree_model_iter_nth_child(store, iter, NULL, row)) return FALSE;
	if (column) *column = col;

	return TRUE;
}

static IconData *vficon_find_data(ViewFile *vf, gint row, gint col, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	GtkTreeIter p;

	if (row < 0 || col < 0) return NULL;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	if (gtk_tree_model_iter_nth_child(store, &p, NULL, row))
		{
		GList *list;

		gtk_tree_model_get(store, &p, FILE_COLUMN_POINTER, &list, -1);
		if (!list) return NULL;

		if (iter) *iter = p;

		return g_list_nth_data(list, col);
		}

	return NULL;
}

static IconData *vficon_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter)
{
	GtkTreePath *tpath;
	GtkTreeViewColumn *column;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), x, y,
					  &tpath, &column, NULL, NULL))
		{
		GtkTreeModel *store;
		GtkTreeIter row;
		GList *list;
		gint n;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &row, tpath);
		gtk_tree_path_free(tpath);

		gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);

		n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_number"));
		if (list)
			{
			if (iter) *iter = row;
			return g_list_nth_data(list, n);
			}
		}

	return NULL;
}

static void vficon_mark_toggled_cb(GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	ViewFile *vf = data;
	GtkTreeModel *store;
	GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
	GtkTreeIter row;
	gint column;
	GList *list;
	guint toggled_mark;
	IconData *id;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	if (!path || !gtk_tree_model_get_iter(store, &row, path))
		return;

	gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);

	column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column_number"));
	g_object_get(G_OBJECT(cell), "toggled_mark", &toggled_mark, NULL);

	id = g_list_nth_data(list, column);
	if (id)
		{
		FileData *fd = id->fd;
		file_data_set_mark(fd, toggled_mark, !file_data_get_mark(fd, toggled_mark));
		}
}


/*
 *-------------------------------------------------------------------
 * tooltip type window
 *-------------------------------------------------------------------
 */

static void tip_show(ViewFile *vf)
{
	GtkWidget *label;
	gint x, y;
#if GTK_CHECK_VERSION(3,0,0)
	GdkDisplay *display;
	GdkDeviceManager *device_manager;
	GdkDevice *device;
#endif

	if (VFICON(vf)->tip_window) return;

#if GTK_CHECK_VERSION(3,0,0)
	device_manager = gdk_display_get_device_manager(gdk_window_get_display(
						gtk_tree_view_get_bin_window(GTK_TREE_VIEW(vf->listview))));
	device = gdk_device_manager_get_client_pointer(device_manager);
	gdk_window_get_device_position(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(vf->listview)),
						device, &x, &y, NULL);
#else
	gdk_window_get_pointer(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(vf->listview)), &x, &y, NULL);
#endif

	VFICON(vf)->tip_id = vficon_find_data_by_coord(vf, x, y, NULL);
	if (!VFICON(vf)->tip_id) return;

	VFICON(vf)->tip_window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_resizable(GTK_WINDOW(VFICON(vf)->tip_window), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(VFICON(vf)->tip_window), 2);

	label = gtk_label_new(VFICON(vf)->tip_id->fd->name);

	g_object_set_data(G_OBJECT(VFICON(vf)->tip_window), "tip_label", label);
	gtk_container_add(GTK_CONTAINER(VFICON(vf)->tip_window), label);
	gtk_widget_show(label);

#if GTK_CHECK_VERSION(3,0,0)
	display = gdk_display_get_default();
	device_manager = gdk_display_get_device_manager(display);
	device = gdk_device_manager_get_client_pointer(device_manager);
	gdk_device_get_position(device, NULL, &x, &y);
#else
	gdk_window_get_pointer(NULL, &x, &y, NULL);
#endif

	if (!gtk_widget_get_realized(VFICON(vf)->tip_window)) gtk_widget_realize(VFICON(vf)->tip_window);
	gtk_window_move(GTK_WINDOW(VFICON(vf)->tip_window), x + 16, y + 16);
	gtk_widget_show(VFICON(vf)->tip_window);
}

static void tip_hide(ViewFile *vf)
{
	if (VFICON(vf)->tip_window) gtk_widget_destroy(VFICON(vf)->tip_window);
	VFICON(vf)->tip_window = NULL;
}

static gboolean tip_schedule_cb(gpointer data)
{
	ViewFile *vf = data;
	GtkWidget *window;

	if (!VFICON(vf)->tip_delay_id) return FALSE;

	window = gtk_widget_get_toplevel(vf->listview);

	if (gtk_widget_get_sensitive(window) &&
	    gtk_window_has_toplevel_focus(GTK_WINDOW(window)))
		{
		tip_show(vf);
		}

	VFICON(vf)->tip_delay_id = 0;
	return FALSE;
}

static void tip_schedule(ViewFile *vf)
{
	tip_hide(vf);

	if (VFICON(vf)->tip_delay_id)
		{
		g_source_remove(VFICON(vf)->tip_delay_id);
		VFICON(vf)->tip_delay_id = 0;
		}

	if (!VFICON(vf)->show_text)
		{
		VFICON(vf)->tip_delay_id = g_timeout_add(VFICON_TIP_DELAY, tip_schedule_cb, vf);
		}
}

static void tip_unschedule(ViewFile *vf)
{
	tip_hide(vf);

	if (VFICON(vf)->tip_delay_id)
		{
		g_source_remove(VFICON(vf)->tip_delay_id);
		VFICON(vf)->tip_delay_id = 0;
		}
}

static void tip_update(ViewFile *vf, IconData *id)
{
#if GTK_CHECK_VERSION(3,0,0)
	GdkDisplay *display = gdk_display_get_default();
	GdkDeviceManager *device_manager = gdk_display_get_device_manager(display);
	GdkDevice *device = gdk_device_manager_get_client_pointer(device_manager);
#endif

	if (VFICON(vf)->tip_window)
		{
		gint x, y;

#if GTK_CHECK_VERSION(3,0,0)
		gdk_device_get_position(device, NULL, &x, &y);
#else
		gdk_window_get_pointer(NULL, &x, &y, NULL);
#endif
		gtk_window_move(GTK_WINDOW(VFICON(vf)->tip_window), x + 16, y + 16);

		if (id != VFICON(vf)->tip_id)
			{
			GtkWidget *label;

			VFICON(vf)->tip_id = id;

			if (!VFICON(vf)->tip_id)
				{
				tip_hide(vf);
				tip_schedule(vf);
				return;
				}

			label = g_object_get_data(G_OBJECT(VFICON(vf)->tip_window), "tip_label");
			gtk_label_set_text(GTK_LABEL(label), VFICON(vf)->tip_id->fd->name);
			}
		}
	else
		{
		tip_schedule(vf);
		}
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

static void vficon_dnd_get(GtkWidget *widget, GdkDragContext *context,
			   GtkSelectionData *selection_data, guint info,
			   guint time, gpointer data)
{
	ViewFile *vf = data;
	GList *list = NULL;

	if (!VFICON(vf)->click_id) return;

	if (VFICON(vf)->click_id->selected & SELECTION_SELECTED)
		{
		list = vf_selection_get_list(vf);
		}
	else
		{
		list = g_list_append(NULL, file_data_ref(VFICON(vf)->click_id->fd));
		}

	if (!list) return;
	uri_selection_data_set_uris_from_filelist(selection_data, list);
	filelist_free(list);
}

static void vficon_drag_data_received(GtkWidget *entry_widget, GdkDragContext *context,
				      int x, int y, GtkSelectionData *selection,
				      guint info, guint time, gpointer data)
{
	ViewFile *vf = data;

	if (info == TARGET_TEXT_PLAIN) {
		IconData *id = vficon_find_data_by_coord(vf, x, y, NULL);

		if (id && id->fd) {
			/* Add keywords to file */
			FileData *fd = id->fd;
			gchar *str = (gchar *) gtk_selection_data_get_text(selection);
			GList *kw_list = string_to_keywords_list(str);

			metadata_append_list(fd, KEYWORD_KEY, kw_list);
			string_list_free(kw_list);
			g_free(str);
		}
	}
}

static void vficon_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewFile *vf = data;

	tip_unschedule(vf);

	if (VFICON(vf)->click_id && VFICON(vf)->click_id->fd->thumb_pixbuf)
		{
		gint items;

		if (VFICON(vf)->click_id->selected & SELECTION_SELECTED)
			items = g_list_length(VFICON(vf)->selection);
		else
			items = 1;

		dnd_set_drag_icon(widget, context, VFICON(vf)->click_id->fd->thumb_pixbuf, items);
		}
}

static void vficon_dnd_end(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	ViewFile *vf = data;

	vficon_selection_remove(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, NULL);

	if (gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE)
		{
		vf_refresh(vf);
		}

	tip_unschedule(vf);
}

void vficon_dnd_init(ViewFile *vf)
{
	gtk_drag_source_set(vf->listview, GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			    dnd_file_drag_types, dnd_file_drag_types_count,
			    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
	gtk_drag_dest_set(vf->listview, GTK_DEST_DEFAULT_ALL,
			    dnd_file_drag_types, dnd_file_drag_types_count,
			    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

	g_signal_connect(G_OBJECT(vf->listview), "drag_data_get",
			 G_CALLBACK(vficon_dnd_get), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_begin",
			 G_CALLBACK(vficon_dnd_begin), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_end",
			 G_CALLBACK(vficon_dnd_end), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_data_received",
			 G_CALLBACK(vficon_drag_data_received), vf);
}

/*
 *-------------------------------------------------------------------
 * cell updates
 *-------------------------------------------------------------------
 */

static void vficon_selection_set(ViewFile *vf, IconData *id, SelectionType value, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	GList *list;

	if (!id) return;


	if (id->selected == value) return;
	id->selected = value;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	if (iter)
		{
		gtk_tree_model_get(store, iter, FILE_COLUMN_POINTER, &list, -1);
		if (list) gtk_list_store_set(GTK_LIST_STORE(store), iter, FILE_COLUMN_POINTER, list, -1);
		}
	else
		{
		GtkTreeIter row;

		if (vficon_find_iter(vf, id, &row, NULL))
			{
			gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);
			if (list) gtk_list_store_set(GTK_LIST_STORE(store), &row, FILE_COLUMN_POINTER, list, -1);
			}
		}
}

static void vficon_selection_add(ViewFile *vf, IconData *id, SelectionType mask, GtkTreeIter *iter)
{
	if (!id) return;

	vficon_selection_set(vf, id, id->selected | mask, iter);
}

static void vficon_selection_remove(ViewFile *vf, IconData *id, SelectionType mask, GtkTreeIter *iter)
{
	if (!id) return;

	vficon_selection_set(vf, id, id->selected & ~mask, iter);
}

void vficon_marks_set(ViewFile *vf, gint enable)
{
	GtkAllocation allocation;
	gtk_widget_get_allocation(vf->listview, &allocation);
	vficon_populate_at_new_size(vf, allocation.width, allocation.height, TRUE);
}

/*
 *-------------------------------------------------------------------
 * selections
 *-------------------------------------------------------------------
 */

static void vficon_verify_selections(ViewFile *vf)
{
	GList *work;

	work = VFICON(vf)->selection;
	while (work)
		{
		IconData *id = work->data;
		work = work->next;

		if (vficon_index_by_id(vf, id) >= 0) continue;

		VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, id);
		}
}

void vficon_select_all(ViewFile *vf)
{
	GList *work;

	g_list_free(VFICON(vf)->selection);
	VFICON(vf)->selection = NULL;

	work = vf->list;
	while (work)
		{
		IconData *id = work->data;
		work = work->next;

		VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, id);
		vficon_selection_add(vf, id, SELECTION_SELECTED, NULL);
		}

	vf_send_update(vf);
}

void vficon_select_none(ViewFile *vf)
{
	GList *work;

	work = VFICON(vf)->selection;
	while (work)
		{
		IconData *id = work->data;
		work = work->next;

		vficon_selection_remove(vf, id, SELECTION_SELECTED, NULL);
		}

	g_list_free(VFICON(vf)->selection);
	VFICON(vf)->selection = NULL;

	vf_send_update(vf);
}

void vficon_select_invert(ViewFile *vf)
{
	GList *work;

	work = vf->list;
	while (work)
		{
		IconData *id = work->data;
		work = work->next;

		if (id->selected & SELECTION_SELECTED)
			{
			VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, id);
			vficon_selection_remove(vf, id, SELECTION_SELECTED, NULL);
			}
		else
			{
			VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, id);
			vficon_selection_add(vf, id, SELECTION_SELECTED, NULL);
			}
		}

	vf_send_update(vf);
}

static void vficon_select(ViewFile *vf, IconData *id)
{
	VFICON(vf)->prev_selection = id;

	if (!id || id->selected & SELECTION_SELECTED) return;

	VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, id);
	vficon_selection_add(vf, id, SELECTION_SELECTED, NULL);

	vf_send_update(vf);
}

static void vficon_unselect(ViewFile *vf, IconData *id)
{
	VFICON(vf)->prev_selection = id;

	if (!id || !(id->selected & SELECTION_SELECTED) ) return;

	VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, id);
	vficon_selection_remove(vf, id, SELECTION_SELECTED, NULL);

	vf_send_update(vf);
}

static void vficon_select_util(ViewFile *vf, IconData *id, gboolean select)
{
	if (select)
		{
		vficon_select(vf, id);
		}
	else
		{
		vficon_unselect(vf, id);
		}
}

static void vficon_select_region_util(ViewFile *vf, IconData *start, IconData *end, gboolean select)
{
	gint row1, col1;
	gint row2, col2;
	gint t;
	gint i, j;

	if (!vficon_find_position(vf, start, &row1, &col1) ||
	    !vficon_find_position(vf, end, &row2, &col2) ) return;

	VFICON(vf)->prev_selection = end;

	if (!options->collections.rectangular_selection)
		{
		GList *work;
		IconData *id;

		if (g_list_index(vf->list, start) > g_list_index(vf->list, end))
			{
			id = start;
			start = end;
			end = id;
			}

		work = g_list_find(vf->list, start);
		while (work)
			{
			id = work->data;
			vficon_select_util(vf, id, select);

			if (work->data != end)
				work = work->next;
			else
				work = NULL;
			}
		return;
		}

	if (row2 < row1)
		{
		t = row1;
		row1 = row2;
		row2 = t;
		}
	if (col2 < col1)
		{
		t = col1;
		col1 = col2;
		col2 = t;
		}

	DEBUG_1("table: %d x %d to %d x %d", row1, col1, row2, col2);

	for (i = row1; i <= row2; i++)
		{
		for (j = col1; j <= col2; j++)
			{
			IconData *id = vficon_find_data(vf, i, j, NULL);
			if (id) vficon_select_util(vf, id, select);
			}
		}
}

gboolean vficon_index_is_selected(ViewFile *vf, gint row)
{
	IconData *id = g_list_nth_data(vf->list, row);

	if (!id) return FALSE;

	return (id->selected & SELECTION_SELECTED);
}

guint vficon_selection_count(ViewFile *vf, gint64 *bytes)
{
	if (bytes)
		{
		gint64 b = 0;
		GList *work;

		work = VFICON(vf)->selection;
		while (work)
			{
			IconData *id = work->data;
			FileData *fd = id->fd;
			g_assert(fd->magick == FD_MAGICK);
			b += fd->size;

			work = work->next;
			}

		*bytes = b;
		}

	return g_list_length(VFICON(vf)->selection);
}

GList *vficon_selection_get_list(ViewFile *vf)
{
	GList *list = NULL;
	GList *work, *work2;

	work = VFICON(vf)->selection;
	while (work)
		{
		IconData *id = work->data;
		FileData *fd = id->fd;
		g_assert(fd->magick == FD_MAGICK);

		list = g_list_prepend(list, file_data_ref(fd));

		work2 = fd->sidecar_files;
		while (work2)
			{
			fd = work2->data;
			list = g_list_prepend(list, file_data_ref(fd));
			work2 = work2->next;
			}

		work = work->next;
		}

	list = g_list_reverse(list);

	return list;
}

GList *vficon_selection_get_list_by_index(ViewFile *vf)
{
	GList *list = NULL;
	GList *work;

	work = VFICON(vf)->selection;
	while (work)
		{
		list = g_list_prepend(list, GINT_TO_POINTER(g_list_index(vf->list, work->data)));
		work = work->next;
		}

	return g_list_reverse(list);
}

static void vficon_select_by_id(ViewFile *vf, IconData *id)
{
	if (!id) return;

	if (!(id->selected & SELECTION_SELECTED))
		{
		vf_select_none(vf);
		vficon_select(vf, id);
		}

	vficon_set_focus(vf, id);
}

void vficon_select_by_fd(ViewFile *vf, FileData *fd)
{
	IconData *id = NULL;
	GList *work;

	if (!fd) return;
	work = vf->list;
	while (work && !id)
		{
		IconData *chk = work->data;
		work = work->next;
		if (chk->fd == fd) id = chk;
		}
	vficon_select_by_id(vf, id);
}

void vficon_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode)
{
	GList *work;
	gint n = mark - 1;

	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	work = vf->list;
	while (work)
		{
		IconData *id = work->data;
		FileData *fd = id->fd;
		gboolean mark_val, selected;

		g_assert(fd->magick == FD_MAGICK);

		mark_val = file_data_get_mark(fd, n);
		selected = (id->selected & SELECTION_SELECTED);

		switch (mode)
			{
			case MTS_MODE_SET: selected = mark_val;
				break;
			case MTS_MODE_OR: selected = mark_val || selected;
				break;
			case MTS_MODE_AND: selected = mark_val && selected;
				break;
			case MTS_MODE_MINUS: selected = !mark_val && selected;
				break;
			}

		vficon_select_util(vf, id, selected);

		work = work->next;
		}
}

void vficon_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode)
{
	GList *slist;
	GList *work;
	gint n = mark -1;

	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	slist = vf_selection_get_list(vf);
	work = slist;
	while (work)
		{
		FileData *fd = work->data;

		switch (mode)
			{
			case STM_MODE_SET: file_data_set_mark(fd, n, 1);
				break;
			case STM_MODE_RESET: file_data_set_mark(fd, n, 0);
				break;
			case STM_MODE_TOGGLE: file_data_set_mark(fd, n, !file_data_get_mark(fd, n));
				break;
			}
		work = work->next;
		}
	filelist_free(slist);
}

static void vficon_select_closest(ViewFile *vf, FileData *sel_fd)
{
	GList *work;
	IconData *id = NULL;

	if (sel_fd->parent) sel_fd = sel_fd->parent;
	work = vf->list;

	while (work)
		{
		gint match;
		FileData *fd;

		id = work->data;
		fd = id->fd;
		work = work->next;

		match = filelist_sort_compare_filedata_full(fd, sel_fd, vf->sort_method, vf->sort_ascend);

		if (match >= 0) break;
		}

	if (id)
		{
		vficon_select(vf, id);
		vficon_send_layout_select(vf, id);
		}
}


/*
 *-------------------------------------------------------------------
 * focus
 *-------------------------------------------------------------------
 */

static void vficon_move_focus(ViewFile *vf, gint row, gint col, gboolean relative)
{
	gint new_row;
	gint new_col;

	if (relative)
		{
		new_row = VFICON(vf)->focus_row;
		new_col = VFICON(vf)->focus_column;

		new_row += row;
		if (new_row < 0) new_row = 0;
		if (new_row >= VFICON(vf)->rows) new_row = VFICON(vf)->rows - 1;

		while (col != 0)
			{
			if (col < 0)
				{
				new_col--;
				col++;
				}
			else
				{
				new_col++;
				col--;
				}

			if (new_col < 0)
				{
				if (new_row > 0)
					{
					new_row--;
					new_col = VFICON(vf)->columns - 1;
					}
				else
					{
					new_col = 0;
					}
				}
			if (new_col >= VFICON(vf)->columns)
				{
				if (new_row < VFICON(vf)->rows - 1)
					{
					new_row++;
					new_col = 0;
					}
				else
					{
					new_col = VFICON(vf)->columns - 1;
					}
				}
			}
		}
	else
		{
		new_row = row;
		new_col = col;

		if (new_row >= VFICON(vf)->rows)
			{
			if (VFICON(vf)->rows > 0)
				new_row = VFICON(vf)->rows - 1;
			else
				new_row = 0;
			new_col = VFICON(vf)->columns - 1;
			}
		if (new_col >= VFICON(vf)->columns) new_col = VFICON(vf)->columns - 1;
		}

	if (new_row == VFICON(vf)->rows - 1)
		{
		gint l;

		/* if we moved beyond the last image, go to the last image */

		l = g_list_length(vf->list);
		if (VFICON(vf)->rows > 1) l -= (VFICON(vf)->rows - 1) * VFICON(vf)->columns;
		if (new_col >= l) new_col = l - 1;
		}

	vficon_set_focus(vf, vficon_find_data(vf, new_row, new_col, NULL));
}

static void vficon_set_focus(ViewFile *vf, IconData *id)
{
	GtkTreeIter iter;
	gint row, col;

	if (g_list_find(vf->list, VFICON(vf)->focus_id))
		{
		if (id == VFICON(vf)->focus_id)
			{
			/* ensure focus row col are correct */
			vficon_find_position(vf, VFICON(vf)->focus_id, &VFICON(vf)->focus_row, &VFICON(vf)->focus_column);
			return;
			}
		vficon_selection_remove(vf, VFICON(vf)->focus_id, SELECTION_FOCUS, NULL);
		}

	if (!vficon_find_position(vf, id, &row, &col))
		{
		VFICON(vf)->focus_id = NULL;
		VFICON(vf)->focus_row = -1;
		VFICON(vf)->focus_column = -1;
		return;
		}

	VFICON(vf)->focus_id = id;
	VFICON(vf)->focus_row = row;
	VFICON(vf)->focus_column = col;
	vficon_selection_add(vf, VFICON(vf)->focus_id, SELECTION_FOCUS, NULL);

	if (vficon_find_iter(vf, VFICON(vf)->focus_id, &iter, NULL))
		{
		GtkTreePath *tpath;
		GtkTreeViewColumn *column;
		GtkTreeModel *store;

		tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		tpath = gtk_tree_model_get_path(store, &iter);
		/* focus is set to an extra column with 0 width to hide focus, we draw it ourself */
		column = gtk_tree_view_get_column(GTK_TREE_VIEW(vf->listview), VFICON_MAX_COLUMNS);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(vf->listview), tpath, column, FALSE);
		gtk_tree_path_free(tpath);
		}
}

/* used to figure the page up/down distances */
static gint page_height(ViewFile *vf)
{
	GtkAdjustment *adj;
	gint page_size;
	gint row_height;
	gint ret;

	adj = gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(vf->listview));
	page_size = (gint)gtk_adjustment_get_page_increment(adj);

	row_height = options->thumbnails.max_height + THUMB_BORDER_PADDING * 2;
	if (VFICON(vf)->show_text) row_height += options->thumbnails.max_height / 3;

	ret = page_size / row_height;
	if (ret < 1) ret = 1;

	return ret;
}

/*
 *-------------------------------------------------------------------
 * keyboard
 *-------------------------------------------------------------------
 */

static void vfi_menu_position_cb(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
{
	ViewFile *vf = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	gint column;
	GtkTreePath *tpath;
	gint cw, ch;

	if (!vficon_find_iter(vf, VFICON(vf)->click_id, &iter, &column)) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	tpath = gtk_tree_model_get_path(store, &iter);
	tree_view_get_cell_clamped(GTK_TREE_VIEW(vf->listview), tpath, column, FALSE, x, y, &cw, &ch);
	gtk_tree_path_free(tpath);
	*y += ch;
	popup_menu_position_clamp(menu, x, y, 0);
}

gboolean vficon_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	ViewFile *vf = data;
	gint focus_row = 0;
	gint focus_col = 0;
	IconData *id;
	gboolean stop_signal;

	stop_signal = TRUE;
	switch (event->keyval)
		{
		case GDK_KEY_Left: case GDK_KEY_KP_Left:
			focus_col = -1;
			break;
		case GDK_KEY_Right: case GDK_KEY_KP_Right:
			focus_col = 1;
			break;
		case GDK_KEY_Up: case GDK_KEY_KP_Up:
			focus_row = -1;
			break;
		case GDK_KEY_Down: case GDK_KEY_KP_Down:
			focus_row = 1;
			break;
		case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:
			focus_row = -page_height(vf);
			break;
		case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
			focus_row = page_height(vf);
			break;
		case GDK_KEY_Home: case GDK_KEY_KP_Home:
			focus_row = -VFICON(vf)->focus_row;
			focus_col = -VFICON(vf)->focus_column;
			break;
		case GDK_KEY_End: case GDK_KEY_KP_End:
			focus_row = VFICON(vf)->rows - 1 - VFICON(vf)->focus_row;
			focus_col = VFICON(vf)->columns - 1 - VFICON(vf)->focus_column;
			break;
		case GDK_KEY_space:
			id = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, NULL);
			if (id)
				{
				VFICON(vf)->click_id = id;
				if (event->state & GDK_CONTROL_MASK)
					{
					gint selected;

					selected = id->selected & SELECTION_SELECTED;
					if (selected)
						{
						vficon_unselect(vf, id);
						}
					else
						{
						vficon_select(vf, id);
						vficon_send_layout_select(vf, id);
						}
					}
				else
					{
					vf_select_none(vf);
					vficon_select(vf, id);
					vficon_send_layout_select(vf, id);
					}
				}
			break;
		case GDK_KEY_Menu:
			id = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, NULL);
			VFICON(vf)->click_id = id;

			vficon_selection_add(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, NULL);
			tip_unschedule(vf);

			vf->popup = vf_pop_menu(vf);
			gtk_menu_popup(GTK_MENU(vf->popup), NULL, NULL, vfi_menu_position_cb, vf, 0, GDK_CURRENT_TIME);
			break;
		default:
			stop_signal = FALSE;
			break;
		}

	if (focus_row != 0 || focus_col != 0)
		{
		IconData *new_id;
		IconData *old_id;

		old_id = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, NULL);
		vficon_move_focus(vf, focus_row, focus_col, TRUE);
		new_id = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, NULL);

		if (new_id != old_id)
			{
			if (event->state & GDK_SHIFT_MASK)
				{
				if (!options->collections.rectangular_selection)
					{
					vficon_select_region_util(vf, old_id, new_id, FALSE);
					}
				else
					{
					vficon_select_region_util(vf, VFICON(vf)->click_id, old_id, FALSE);
					}
				vficon_select_region_util(vf, VFICON(vf)->click_id, new_id, TRUE);
				vficon_send_layout_select(vf, new_id);
				}
			else if (event->state & GDK_CONTROL_MASK)
				{
				VFICON(vf)->click_id = new_id;
				}
			else
				{
				VFICON(vf)->click_id = new_id;
				vf_select_none(vf);
				vficon_select(vf, new_id);
				vficon_send_layout_select(vf, new_id);
				}
			}
		}

	if (stop_signal)
		{
		tip_unschedule(vf);
		}

	return stop_signal;
}

/*
 *-------------------------------------------------------------------
 * mouse
 *-------------------------------------------------------------------
 */

static gboolean vficon_motion_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
	ViewFile *vf = data;
	IconData *id;

	id = vficon_find_data_by_coord(vf, (gint)event->x, (gint)event->y, NULL);
	tip_update(vf, id);

	return FALSE;
}

gboolean vficon_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewFile *vf = data;
	GtkTreeIter iter;
	IconData *id;

	tip_unschedule(vf);

	id = vficon_find_data_by_coord(vf, (gint)bevent->x, (gint)bevent->y, &iter);

	VFICON(vf)->click_id = id;
	vficon_selection_add(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, &iter);

	switch (bevent->button)
		{
		case MOUSE_BUTTON_LEFT:
			if (!gtk_widget_has_focus(vf->listview))
				{
				gtk_widget_grab_focus(vf->listview);
				}

			if (bevent->type == GDK_2BUTTON_PRESS &&
			    vf->layout)
				{
				vficon_selection_remove(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, &iter);
				layout_image_full_screen_start(vf->layout);
				}
			break;
		case MOUSE_BUTTON_RIGHT:
			vf->popup = vf_pop_menu(vf);
			gtk_menu_popup(GTK_MENU(vf->popup), NULL, NULL, NULL, NULL, bevent->button, bevent->time);
			break;
		default:
			break;
		}

	return FALSE;
}

gboolean vficon_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewFile *vf = data;
	GtkTreeIter iter;
	IconData *id = NULL;
	gboolean was_selected;

	tip_schedule(vf);

	if ((gint)bevent->x != 0 || (gint)bevent->y != 0)
		{
		id = vficon_find_data_by_coord(vf, (gint)bevent->x, (gint)bevent->y, &iter);
		}

	if (VFICON(vf)->click_id)
		{
		vficon_selection_remove(vf, VFICON(vf)->click_id, SELECTION_PRELIGHT, NULL);
		}

	if (!id || VFICON(vf)->click_id != id) return TRUE;

	was_selected = !!(id->selected & SELECTION_SELECTED);

	switch (bevent->button)
		{
		case MOUSE_BUTTON_LEFT:
			{
			vficon_set_focus(vf, id);

			if (bevent->state & GDK_CONTROL_MASK)
				{
				gboolean select;

				select = !(id->selected & SELECTION_SELECTED);
				if ((bevent->state & GDK_SHIFT_MASK) && VFICON(vf)->prev_selection)
					{
					vficon_select_region_util(vf, VFICON(vf)->prev_selection, id, select);
					}
				else
					{
					vficon_select_util(vf, id, select);
					}
				}
			else
				{
				vf_select_none(vf);

				if ((bevent->state & GDK_SHIFT_MASK) && VFICON(vf)->prev_selection)
					{
					vficon_select_region_util(vf, VFICON(vf)->prev_selection, id, TRUE);
					}
				else
					{
					vficon_select_util(vf, id, TRUE);
					was_selected = FALSE;
					}
				}
			}
			break;
		case MOUSE_BUTTON_MIDDLE:
			{
			vficon_select_util(vf, id, !(id->selected & SELECTION_SELECTED));
			}
			break;
		default:
			break;
		}

	if (!was_selected && (id->selected & SELECTION_SELECTED))
		{
		vficon_send_layout_select(vf, id);
		}

	return TRUE;
}

static gboolean vficon_leave_cb(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
	ViewFile *vf = data;

	tip_unschedule(vf);
	return FALSE;
}

/*
 *-------------------------------------------------------------------
 * population
 *-------------------------------------------------------------------
 */

static gboolean vficon_destroy_node_cb(GtkTreeModel *store, GtkTreePath *tpath, GtkTreeIter *iter, gpointer data)
{
	GList *list;

	gtk_tree_model_get(store, iter, FILE_COLUMN_POINTER, &list, -1);

	/* it seems that gtk_list_store_clear may call some callbacks
	   that use the column. Set the pointer to NULL to be safe. */
	gtk_list_store_set(GTK_LIST_STORE(store), iter, FILE_COLUMN_POINTER, NULL, -1);
	g_list_free(list);

	return FALSE;
}

static void vficon_clear_store(ViewFile *vf)
{
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	gtk_tree_model_foreach(store, vficon_destroy_node_cb, NULL);

	gtk_list_store_clear(GTK_LIST_STORE(store));
}

static GList *vficon_add_row(ViewFile *vf, GtkTreeIter *iter)
{
	GtkListStore *store;
	GList *list = NULL;
	gint i;

	for (i = 0; i < VFICON(vf)->columns; i++) list = g_list_prepend(list, NULL);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	gtk_list_store_append(store, iter);
	gtk_list_store_set(store, iter, FILE_COLUMN_POINTER, list, -1);

	return list;
}

static void vficon_populate(ViewFile *vf, gboolean resize, gboolean keep_position)
{
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GList *work;
	IconData *visible_id = NULL;
	gint r, c;
	gboolean valid;
	GtkTreeIter iter;

	vficon_verify_selections(vf);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	if (keep_position && gtk_widget_get_realized(vf->listview) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, NULL, NULL, NULL))
		{
		GtkTreeIter iter;
		GList *list;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_path_free(tpath);

		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
		if (list) visible_id = list->data;
		}


	if (resize)
		{
		gint i;
		gint thumb_width;

		vficon_clear_store(vf);

		thumb_width = vficon_get_icon_width(vf);

		for (i = 0; i < VFICON_MAX_COLUMNS; i++)
			{
			GtkTreeViewColumn *column;
			GtkCellRenderer *cell;
			GList *list;

			column = gtk_tree_view_get_column(GTK_TREE_VIEW(vf->listview), i);
			gtk_tree_view_column_set_visible(column, (i < VFICON(vf)->columns));
			gtk_tree_view_column_set_fixed_width(column, thumb_width + (THUMB_BORDER_PADDING * 6));

			list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
			cell = (list) ? list->data : NULL;
			g_list_free(list);

			if (cell && GQV_IS_CELL_RENDERER_ICON(cell))
				{
				g_object_set(G_OBJECT(cell), "fixed_width", thumb_width,
							     "fixed_height", options->thumbnails.max_height,
							     "show_text", VFICON(vf)->show_text,
							     "show_marks", vf->marks_enabled,
							     "num_marks", FILEDATA_MARKS_SIZE,
							     NULL);
				}
			}
		if (gtk_widget_get_realized(vf->listview)) gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
		}

	r = -1;
	c = 0;

	valid = gtk_tree_model_iter_children(store, &iter, NULL);

	work = vf->list;
	while (work)
		{
		GList *list;
		r++;
		c = 0;
		if (valid)
			{
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
			gtk_list_store_set(GTK_LIST_STORE(store), &iter, FILE_COLUMN_POINTER, list, -1);
			}
		else
			{
			list = vficon_add_row(vf, &iter);
			}

		while (list)
			{
			IconData *id;

			if (work)
				{
				id = work->data;
				work = work->next;
				c++;
				}
			else
				{
				id = NULL;
				}

			list->data = id;
			list = list->next;
			}
		if (valid) valid = gtk_tree_model_iter_next(store, &iter);
		}

	r++;
	while (valid)
		{
		GList *list;

		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
		valid = gtk_list_store_remove(GTK_LIST_STORE(store), &iter);
		g_list_free(list);
		}

	VFICON(vf)->rows = r;

	if (visible_id &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, NULL, NULL, NULL))
		{
		GtkTreeIter iter;
		GList *list;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_path_free(tpath);

		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
		if (g_list_find(list, visible_id) == NULL &&
		    vficon_find_iter(vf, visible_id, &iter, NULL))
			{
			tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE);
			}
		}


	vf_send_update(vf);
	vf_thumb_update(vf);
}

static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint h, gboolean force)
{
	gint new_cols;
	gint thumb_width;

	thumb_width = vficon_get_icon_width(vf);

	new_cols = w / (thumb_width + (THUMB_BORDER_PADDING * 6));
	if (new_cols < 1) new_cols = 1;

	if (!force && new_cols == VFICON(vf)->columns) return;

	VFICON(vf)->columns = new_cols;

	vficon_populate(vf, TRUE, TRUE);

	DEBUG_1("col tab pop cols=%d rows=%d", VFICON(vf)->columns, VFICON(vf)->rows);
}

static void vficon_sized_cb(GtkWidget *widget, GtkAllocation *allocation, gpointer data)
{
	ViewFile *vf = data;

	vficon_populate_at_new_size(vf, allocation->width, allocation->height, FALSE);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

void vficon_sort_set(ViewFile *vf, SortType type, gboolean ascend)
{
	if (vf->sort_method == type && vf->sort_ascend == ascend) return;

	vf->sort_method = type;
	vf->sort_ascend = ascend;

	if (!vf->list) return;

	vf_refresh(vf);
}

/*
 *-----------------------------------------------------------------------------
 * thumb updates
 *-----------------------------------------------------------------------------
 */

void vficon_thumb_progress_count(GList *list, gint *count, gint *done)
{
	GList *work = list;
	while (work)
		{
		IconData *id = work->data;
		FileData *fd = id->fd;
		work = work->next;

		if (fd->thumb_pixbuf) (*done)++;
		(*count)++;
		}
}

void vficon_set_thumb_fd(ViewFile *vf, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GList *list;

	if (!vficon_find_iter(vf, vficon_icon_data(vf, fd), &iter, NULL)) return;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
	gtk_list_store_set(GTK_LIST_STORE(store), &iter, FILE_COLUMN_POINTER, list, -1);
}


FileData *vficon_thumb_next_fd(ViewFile *vf)
{
	GtkTreePath *tpath;
	FileData *fd = NULL;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, NULL, NULL, NULL))
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid = TRUE;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_path_free(tpath);

		while (!fd && valid && tree_view_row_get_visibility(GTK_TREE_VIEW(vf->listview), &iter, FALSE) == 0)
			{
			GList *list;

			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);

			while (!fd && list)
				{
				IconData *id = list->data;
				if (id && !id->fd->thumb_pixbuf) fd = id->fd;
				list = list->next;
				}

			valid = gtk_tree_model_iter_next(store, &iter);
			}
		}

	/* then find first undone */

	if (!fd)
		{
		GList *work = vf->list;
		while (work && !fd)
			{
			IconData *id = work->data;
			FileData *fd_p = id->fd;
			work = work->next;

			if (!fd_p->thumb_pixbuf) fd = fd_p;
			}
		}

	return fd;
}

void vficon_thumb_reset_all(ViewFile *vf)
{
	GList *work = vf->list;

	while (work)
		{
		IconData *id = work->data;
		FileData *fd = id->fd;
		if (fd->thumb_pixbuf)
			{
			g_object_unref(fd->thumb_pixbuf);
			fd->thumb_pixbuf = NULL;
			}
		work = work->next;
		}
}


/*
 *-----------------------------------------------------------------------------
 * row stuff
 *-----------------------------------------------------------------------------
 */

FileData *vficon_index_get_data(ViewFile *vf, gint row)
{
	IconData *id;

	id = g_list_nth_data(vf->list, row);
	return id ? id->fd : NULL;
}


gint vficon_index_by_fd(ViewFile *vf, FileData *in_fd)
{
	gint p = 0;
	GList *work;

	if (!in_fd) return -1;

	work = vf->list;
	while (work)
		{
		IconData *id = work->data;
		FileData *fd = id->fd;
		if (fd == in_fd) return p;
		work = work->next;
		p++;
		}

	return -1;
}

static gint vficon_index_by_id(ViewFile *vf, IconData *in_id)
{
	gint p = 0;
	GList *work;

	if (!in_id) return -1;

	work = vf->list;
	while (work)
		{
		IconData *id = work->data;
		if (id == in_id) return p;
		work = work->next;
		p++;
		}

	return -1;
}

guint vficon_count(ViewFile *vf, gint64 *bytes)
{
	if (bytes)
		{
		gint64 b = 0;
		GList *work;

		work = vf->list;
		while (work)
			{
			IconData *id = work->data;
			FileData *fd = id->fd;
			work = work->next;

			b += fd->size;
			}

		*bytes = b;
		}

	return g_list_length(vf->list);
}

GList *vficon_get_list(ViewFile *vf)
{
	GList *list = NULL;
	GList *work;

	work = vf->list;
	while (work)
		{
		IconData *id = work->data;
		FileData *fd = id->fd;
		work = work->next;

		list = g_list_prepend(list, file_data_ref(fd));
		}

	return g_list_reverse(list);
}

/*
 *-----------------------------------------------------------------------------
 *
 *-----------------------------------------------------------------------------
 */

static gboolean vficon_refresh_real(ViewFile *vf, gboolean keep_position)
{
	gboolean ret = TRUE;
	GList *work, *work_fd;
	IconData *focus_id;
	GList *new_filelist = NULL;
	FileData *first_selected = NULL;
	GList *new_iconlist = NULL;

	focus_id = VFICON(vf)->focus_id;

	if (vf->dir_fd)
		{
		ret = filelist_read(vf->dir_fd, &new_filelist, NULL);
		new_filelist = file_data_filter_marks_list(new_filelist, vf_marks_get_filter(vf));
		}

	vf->list = iconlist_sort(vf->list, vf->sort_method, vf->sort_ascend); /* the list might not be sorted if there were renames */
	new_filelist = filelist_sort(new_filelist, vf->sort_method, vf->sort_ascend);

	if (VFICON(vf)->selection)
		{
		first_selected = ((IconData *)(VFICON(vf)->selection->data))->fd;
		file_data_ref(first_selected);
		g_list_free(VFICON(vf)->selection);
		VFICON(vf)->selection = NULL;


		}

	/* check for same files from old_list */
	work = vf->list;
	work_fd = new_filelist;
	while (work || work_fd)
		{
		IconData *id = NULL;
		FileData *fd = NULL;
		FileData *new_fd = NULL;
		gint match;

		if (work && work_fd)
			{
			id = work->data;
			fd = id->fd;

			new_fd = work_fd->data;

			if (fd == new_fd)
				{
				/* not changed, go to next */
				work = work->next;
				work_fd = work_fd->next;
				if (id->selected & SELECTION_SELECTED)
					{
					VFICON(vf)->selection = g_list_prepend(VFICON(vf)->selection, id);
					}
				continue;
				}

			match = filelist_sort_compare_filedata_full(fd, new_fd, vf->sort_method, vf->sort_ascend);
			if (match == 0) g_warning("multiple fd for the same path");
			}
		else if (work)
			{
			id = work->data;
			fd = id->fd;
			match = -1;
			}
		else /* work_fd */
			{
			new_fd = work_fd->data;
			match = 1;
			}

		if (match < 0)
			{
			/* file no longer exists, delete from vf->list */
			GList *to_delete = work;
			work = work->next;
			if (id == VFICON(vf)->prev_selection) VFICON(vf)->prev_selection = NULL;
			if (id == VFICON(vf)->click_id) VFICON(vf)->click_id = NULL;
			file_data_unref(fd);
			g_free(id);
			vf->list = g_list_delete_link(vf->list, to_delete);
			}
		else
			{
			/* new file, add to vf->list */
			id = g_new0(IconData, 1);

			id->selected = SELECTION_NONE;
			id->fd = file_data_ref(new_fd);
			if (work)
				vf->list = g_list_insert_before(vf->list, work, id);
			else
				new_iconlist = g_list_prepend(new_iconlist, id); /* it is faster to append all new entries together later */

			work_fd = work_fd->next;
			}

		}

	if (new_iconlist)
		{
		vf->list = g_list_concat(vf->list, g_list_reverse(new_iconlist));
		}

	VFICON(vf)->selection = g_list_reverse(VFICON(vf)->selection);

	filelist_free(new_filelist);

	vficon_populate(vf, TRUE, keep_position);

	if (first_selected && !VFICON(vf)->selection)
		{
		/* all selected files disappeared */
		vficon_select_closest(vf, first_selected);
		}
	file_data_unref(first_selected);

	/* attempt to keep focus on same icon when refreshing */
	if (focus_id && g_list_find(vf->list, focus_id))
		{
		vficon_set_focus(vf, focus_id);
		}

	return ret;
}

gboolean vficon_refresh(ViewFile *vf)
{
	return vficon_refresh_real(vf, TRUE);
}

/*
 *-----------------------------------------------------------------------------
 * draw, etc.
 *-----------------------------------------------------------------------------
 */

typedef struct _ColumnData ColumnData;
struct _ColumnData
{
	ViewFile *vf;
	gint number;
};

static void vficon_cell_data_cb(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
				GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	GList *list;
	IconData *id;
	ColumnData *cd = data;
	ViewFile *vf = cd->vf;

	if (!GQV_IS_CELL_RENDERER_ICON(cell)) return;

	gtk_tree_model_get(tree_model, iter, FILE_COLUMN_POINTER, &list, -1);

	id = g_list_nth_data(list, cd->number);

	if (id)
		{
		GdkColor color_fg;
		GdkColor color_bg;
		GtkStyle *style;
		gchar *name_sidecars;
		gchar *link;
		GtkStateType state = GTK_STATE_NORMAL;

		g_assert(id->fd->magick == FD_MAGICK);

		link = islink(id->fd->path) ? GQ_LINK_STR : "";
		if (id->fd->sidecar_files)
			{
			gchar *sidecars = file_data_sc_list_to_string(id->fd);
			name_sidecars = g_strdup_printf("%s%s %s", link, id->fd->name, sidecars);
			g_free(sidecars);
			}
		else
			{
			gchar *disabled_grouping = id->fd->disable_grouping ? _(" [NO GROUPING]") : "";
			name_sidecars = g_strdup_printf("%s%s%s", link, id->fd->name, disabled_grouping);
			}

		style = gtk_widget_get_style(vf->listview);
		if (id->selected & SELECTION_SELECTED)
			{
			state = GTK_STATE_SELECTED;
			}

		memcpy(&color_fg, &style->text[state], sizeof(color_fg));
		memcpy(&color_bg, &style->base[state], sizeof(color_bg));

		if (id->selected & SELECTION_PRELIGHT)
			{
			shift_color(&color_bg, -1, 0);
			}

		g_object_set(cell,	"pixbuf", id->fd->thumb_pixbuf,
					"text", name_sidecars,
					"marks", file_data_get_marks(id->fd),
					"show_marks", vf->marks_enabled,
					"cell-background-gdk", &color_bg,
					"cell-background-set", TRUE,
					"foreground-gdk", &color_fg,
					"foreground-set", TRUE,
					"has-focus", (VFICON(vf)->focus_id == id), NULL);
		g_free(name_sidecars);
		}
	else
		{
		g_object_set(cell,	"pixbuf", NULL,
					"text", NULL,
					"show_marks", FALSE,
					"cell-background-set", FALSE,
					"foreground-set", FALSE,
					"has-focus", FALSE, NULL);
		}
}

static void vficon_append_column(ViewFile *vf, gint n)
{
	ColumnData *cd;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_min_width(column, 0);

	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_alignment(column, 0.5);

	renderer = gqv_cell_renderer_icon_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	g_object_set(G_OBJECT(renderer), "xpad", THUMB_BORDER_PADDING * 2,
					 "ypad", THUMB_BORDER_PADDING,
					 "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, NULL);

	g_object_set_data(G_OBJECT(column), "column_number", GINT_TO_POINTER(n));
	g_object_set_data(G_OBJECT(renderer), "column_number", GINT_TO_POINTER(n));

	cd = g_new0(ColumnData, 1);
	cd->vf = vf;
	cd->number = n;
	gtk_tree_view_column_set_cell_data_func(column, renderer, vficon_cell_data_cb, cd, g_free);

	gtk_tree_view_append_column(GTK_TREE_VIEW(vf->listview), column);

	g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(vficon_mark_toggled_cb), vf);
}

/*
 *-----------------------------------------------------------------------------
 * base
 *-----------------------------------------------------------------------------
 */

gboolean vficon_set_fd(ViewFile *vf, FileData *dir_fd)
{
	gboolean ret;

	if (!dir_fd) return FALSE;
	if (vf->dir_fd == dir_fd) return TRUE;

	file_data_unref(vf->dir_fd);
	vf->dir_fd = file_data_ref(dir_fd);

	g_list_free(VFICON(vf)->selection);
	VFICON(vf)->selection = NULL;

	iconlist_free(vf->list);
	vf->list = NULL;

	/* NOTE: populate will clear the store for us */
	ret = vficon_refresh_real(vf, FALSE);

	VFICON(vf)->focus_id = NULL;
	vficon_move_focus(vf, 0, 0, FALSE);

	return ret;
}

void vficon_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	vf_refresh_idle_cancel(vf);

	file_data_unregister_notify_func(vf_notify_cb, vf);

	tip_unschedule(vf);

	vf_thumb_cleanup(vf);

	iconlist_free(vf->list);
	g_list_free(VFICON(vf)->selection);
}

ViewFile *vficon_new(ViewFile *vf, FileData *dir_fd)
{
	GtkListStore *store;
	GtkTreeSelection *selection;
	gint i;

	vf->info = g_new0(ViewFileInfoIcon, 1);

	VFICON(vf)->show_text = options->show_icon_names;

	store = gtk_list_store_new(1, G_TYPE_POINTER);
	vf->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_NONE);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(vf->listview), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(vf->listview), FALSE);

	for (i = 0; i < VFICON_MAX_COLUMNS; i++)
		{
		vficon_append_column(vf, i);
		}

	/* zero width column to hide tree view focus, we draw it ourselves */
	vficon_append_column(vf, i);
	/* end column to fill white space */
	vficon_append_column(vf, i);

	g_signal_connect(G_OBJECT(vf->listview), "size_allocate",
			 G_CALLBACK(vficon_sized_cb), vf);

	gtk_widget_set_events(vf->listview, GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK |
			      GDK_BUTTON_PRESS_MASK | GDK_LEAVE_NOTIFY_MASK);

	g_signal_connect(G_OBJECT(vf->listview),"motion_notify_event",
			 G_CALLBACK(vficon_motion_cb), vf);
	g_signal_connect(G_OBJECT(vf->listview), "leave_notify_event",
			 G_CALLBACK(vficon_leave_cb), vf);

	/* force VFICON(vf)->columns to be at least 1 (sane) - this will be corrected in the size_cb */
	vficon_populate_at_new_size(vf, 1, 1, FALSE);

	file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);

	return vf;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
