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

#include "view-file-icon.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <glib-object.h>

#include "cellrenderericon.h"
#include "collect.h"
#include "compat-deprecated.h"
#include "compat.h"
#include "dnd.h"
#include "filedata.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "utilops.h"
#include "view-file.h"

namespace
{

/* between these, the icon width is increased by thumb_max_width / 2 */
constexpr gint THUMB_MIN_ICON_WIDTH = 128;
constexpr gint THUMB_MAX_ICON_WIDTH = 160;

constexpr gint THUMB_MIN_ICON_WIDTH_WITH_MARKS = TOGGLE_SPACING * FILEDATA_MARKS_SIZE;

constexpr gint VFICON_MAX_COLUMNS = 32;

constexpr gint THUMB_BORDER_PADDING = 2;

constexpr gint VFICON_TIP_DELAY = 500;

enum {
	FILE_COLUMN_POINTER = 0,
	FILE_COLUMN_COUNT
};

struct ColumnData
{
	ViewFile *vf;
	gint number;
};

} // namespace

static void vficon_toggle_filenames(ViewFile *vf);
static void vficon_selection_remove(ViewFile *vf, FileData *fd, SelectionType mask, GtkTreeIter *iter);
static void vficon_move_focus(ViewFile *vf, gint row, gint col, gboolean relative);
static void vficon_set_focus(ViewFile *vf, FileData *fd);
static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint h, gboolean force);


/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

GList *vficon_selection_get_one(ViewFile *, FileData *fd)
{
	return g_list_prepend(filelist_copy(fd->sidecar_files), file_data_ref(fd));
}

void vficon_pop_menu_rename_cb(ViewFile *vf)
{
	file_util_rename(nullptr, vf_pop_menu_file_list(vf), vf->listview);
}

static void vficon_pop_menu_show_names_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vficon_toggle_filenames(vf);
}

void vficon_pop_menu_add_items(ViewFile *vf, GtkWidget *menu)
{
	menu_item_add_check(menu, _("Show filename _text"), VFICON(vf)->show_text,
	                    G_CALLBACK(vficon_pop_menu_show_names_cb), vf);
}

void vficon_pop_menu_show_star_rating_cb(ViewFile *vf)
{
	GtkAllocation allocation;

	gtk_widget_get_allocation(vf->listview, &allocation);
	vficon_populate_at_new_size(vf, allocation.width, allocation.height, TRUE);
}

void vficon_pop_menu_refresh_cb(ViewFile *vf)
{
	vficon_refresh(vf);
}

void vficon_popup_destroy_cb(ViewFile *vf)
{
	vficon_selection_remove(vf, vf->click_fd, SELECTION_PRELIGHT, nullptr);
}

/*
 *-------------------------------------------------------------------
 * signals
 *-------------------------------------------------------------------
 */

static void vficon_send_layout_select(ViewFile *vf, FileData *fd)
{
	FileData *read_ahead_fd = nullptr;
	FileData *sel_fd;
	FileData *cur_fd;

	if (!vf->layout || !fd) return;

	sel_fd = fd;

	cur_fd = layout_image_get_fd(vf->layout);
	if (sel_fd == cur_fd) return; /* no change */

	if (options->image.enable_read_ahead)
		{
		gint row;

		row = g_list_index(vf->list, fd);
		if (row > vficon_index_by_fd(vf, cur_fd) &&
		    static_cast<guint>(row + 1) < vf_count(vf))
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

	if (!VFICON(vf)->show_text && !vf->marks_enabled ) return options->thumbnails.max_width;

	width = options->thumbnails.max_width + options->thumbnails.max_width / 2;
	width = std::max(width, THUMB_MIN_ICON_WIDTH);
	if (width > THUMB_MAX_ICON_WIDTH) width = options->thumbnails.max_width;
	if (vf->marks_enabled && width < THUMB_MIN_ICON_WIDTH_WITH_MARKS) width = THUMB_MIN_ICON_WIDTH_WITH_MARKS;

	return width;
}

/*
 *-------------------------------------------------------------------
 * misc utils
 *-------------------------------------------------------------------
 */

static gboolean vficon_find_position(ViewFile *vf, FileData *fd, gint *row, gint *col)
{
	gint n;

	n = g_list_index(vf->list, fd);

	if (n < 0) return FALSE;

	*row = n / VFICON(vf)->columns;
	*col = n - (*row * VFICON(vf)->columns);

	return TRUE;
}

static gboolean vficon_find_iter(ViewFile *vf, FileData *fd, GtkTreeIter *iter, gint *column)
{
	GtkTreeModel *store;
	gint row;
	gint col;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	if (!vficon_find_position(vf, fd, &row, &col)) return FALSE;
	if (!gtk_tree_model_iter_nth_child(store, iter, nullptr, row)) return FALSE;
	if (column) *column = col;

	return TRUE;
}

static FileData *vficon_find_data(ViewFile *vf, gint row, gint col, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	GtkTreeIter p;

	if (row < 0 || col < 0) return nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	if (gtk_tree_model_iter_nth_child(store, &p, nullptr, row))
		{
		GList *list;

		gtk_tree_model_get(store, &p, FILE_COLUMN_POINTER, &list, -1);
		if (!list) return nullptr;

		if (iter) *iter = p;

		return static_cast<FileData *>(g_list_nth_data(list, col));
		}

	return nullptr;
}

FileData *vficon_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter)
{
	g_autoptr(GtkTreePath) tpath = nullptr;
	GtkTreeViewColumn *column;

	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), x, y,
					  &tpath, &column, nullptr, nullptr))
		{
		return nullptr;
		}

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	GtkTreeIter row;
	gtk_tree_model_get_iter(store, &row, tpath);

	GList *list;
	gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);
	if (!list) return nullptr;

	gint n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_number"));

	if (iter) *iter = row;

	return static_cast<FileData *>(g_list_nth_data(list, n));
}

static void vficon_mark_toggled_cb(GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	g_autoptr(GtkTreePath) path = gtk_tree_path_new_from_string(path_str);
	if (!path) return;

	auto *vf = static_cast<ViewFile *>(data);
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	GtkTreeIter row;
	if (!gtk_tree_model_get_iter(model, &row, path)) return;

	GList *list;
	gtk_tree_model_get(model, &row, FILE_COLUMN_POINTER, &list, -1);

	auto column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column_number"));

	auto *fd = static_cast<FileData *>(g_list_nth_data(list, column));
	if (!fd) return;

	guint toggled_mark;
	g_object_get(cell, "toggled_mark", &toggled_mark, NULL);

	file_data_set_mark(fd, toggled_mark, !file_data_get_mark(fd, toggled_mark));
}


/*
 *-------------------------------------------------------------------
 * tooltip type window
 *-------------------------------------------------------------------
 */

static void tip_show(ViewFile *vf)
{
	GtkWidget *label;
	gint x;
	gint y;
	GdkDisplay *display;
	GdkSeat *seat;
	GdkDevice *device;

	if (VFICON(vf)->tip_window) return;

#if HAVE_GTK4
	seat = gdk_display_get_default_seat(gtk_widget_get_display(GTK_WIDGET(vf->listview)));
#else
	seat = gdk_display_get_default_seat(gdk_window_get_display(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(vf->listview))));
#endif

	device = gdk_seat_get_pointer(seat);
	get_pointer_position(vf->listview, device, &x, &y, nullptr);

	VFICON(vf)->tip_fd = vficon_find_data_by_coord(vf, x, y, nullptr);
	if (!VFICON(vf)->tip_fd) return;

	VFICON(vf)->tip_window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_transient_for(GTK_WINDOW(VFICON(vf)->tip_window), GTK_WINDOW(widget_get_toplevel(vf->listview)));
	gtk_window_set_resizable(GTK_WINDOW(VFICON(vf)->tip_window), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(VFICON(vf)->tip_window), 2);

	label = gtk_label_new(VFICON(vf)->tip_fd->name);

	g_object_set_data(G_OBJECT(VFICON(vf)->tip_window), "tip_label", label);
	gq_gtk_container_add(VFICON(vf)->tip_window, label);
	gtk_widget_show(label);

	display = gdk_display_get_default();
	seat = gdk_display_get_default_seat(display);
	device = gdk_seat_get_pointer(seat);
	get_device_position(device, x, y);

	if (!gtk_widget_get_realized(VFICON(vf)->tip_window)) gtk_widget_realize(VFICON(vf)->tip_window);
	gq_gtk_window_move(GTK_WINDOW(VFICON(vf)->tip_window), x + 16, y + 16);
	gtk_widget_show(VFICON(vf)->tip_window);
}

static void tip_hide(ViewFile *vf)
{
	if (VFICON(vf)->tip_window) gq_gtk_widget_destroy(VFICON(vf)->tip_window);
	VFICON(vf)->tip_window = nullptr;
}

static gboolean tip_schedule_cb(gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	if (VFICON(vf)->tip_delay_id)
		{
		auto *window = widget_get_toplevel(vf->listview);

#if HAVE_GTK4
		if (GTK_IS_WINDOW(window))
			{
			if (gtk_widget_get_sensitive(GTK_WIDGET(window)) && gtk_window_is_active(window))
				{
				tip_show(vf);
				}
			}
#else
		if (gtk_widget_get_sensitive(window) && gtk_window_has_toplevel_focus(GTK_WINDOW(window)))
			{
			tip_show(vf);
			}
#endif

		VFICON(vf)->tip_delay_id = 0;
		}

	return G_SOURCE_REMOVE;
}

static void tip_unschedule(ViewFile *vf)
{
	tip_hide(vf);

	g_clear_handle_id(&(VFICON(vf)->tip_delay_id), g_source_remove);
}

static void tip_schedule(ViewFile *vf)
{
	tip_unschedule(vf);

	if (!VFICON(vf)->show_text)
		{
		VFICON(vf)->tip_delay_id = g_timeout_add(VFICON_TIP_DELAY, tip_schedule_cb, vf);
		}
}

static void tip_update(ViewFile *vf, FileData *fd)
{
	GdkDisplay *display = gdk_display_get_default();
	GdkSeat *seat = gdk_display_get_default_seat(display);
	GdkDevice *device = gdk_seat_get_pointer(seat);

	if (VFICON(vf)->tip_window)
		{
		gint x;
		gint y;

		get_device_position(device, x, y);

		gq_gtk_window_move(GTK_WINDOW(VFICON(vf)->tip_window), x + 16, y + 16);

		if (fd != VFICON(vf)->tip_fd)
			{
			GtkWidget *label;

			VFICON(vf)->tip_fd = fd;

			if (!VFICON(vf)->tip_fd)
				{
				tip_hide(vf);
				tip_schedule(vf);
				return;
				}

			label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(VFICON(vf)->tip_window), "tip_label"));
			gtk_label_set_text(GTK_LABEL(label), VFICON(vf)->tip_fd->name);
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

#if !HAVE_GTK4
void vficon_dnd_begin(ViewFile *vf, GtkWidget *widget, GdkDragContext *context)
{
	tip_unschedule(vf);

	if (vf->click_fd && vf->click_fd->thumb_pixbuf)
		{
		gint items;

		if (vf->click_fd->selected & SELECTION_SELECTED)
			items = g_list_length(VFICON(vf)->selection);
		else
			items = 1;

		dnd_set_drag_icon(widget, context, vf->click_fd->thumb_pixbuf, items);
		}
}

void vficon_dnd_end(ViewFile *vf, GdkDragContext *context)
{
	vficon_selection_remove(vf, vf->click_fd, SELECTION_PRELIGHT, nullptr);

	if (gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE)
		{
		vficon_refresh(vf);
		}

	tip_unschedule(vf);
}
#endif

/*
 *-------------------------------------------------------------------
 * cell updates
 *-------------------------------------------------------------------
 */

static void vficon_selection_set(ViewFile *vf, FileData *fd, SelectionType value, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	GList *list;

	if (!fd) return;

	if (fd->selected == value) return;
	fd->selected = value;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	if (iter)
		{
		gtk_tree_model_get(store, iter, FILE_COLUMN_POINTER, &list, -1);
		if (list) gtk_list_store_set(GTK_LIST_STORE(store), iter, FILE_COLUMN_POINTER, list, -1);
		}
	else
		{
		GtkTreeIter row;

		if (vficon_find_iter(vf, fd, &row, nullptr))
			{
			gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &list, -1);
			if (list) gtk_list_store_set(GTK_LIST_STORE(store), &row, FILE_COLUMN_POINTER, list, -1);
			}
		}
}

static void vficon_selection_add(ViewFile *vf, FileData *fd, SelectionType mask, GtkTreeIter *iter)
{
	if (!fd) return;

	vficon_selection_set(vf, fd, static_cast<SelectionType>(fd->selected | mask), iter);
}

static void vficon_selection_remove(ViewFile *vf, FileData *fd, SelectionType mask, GtkTreeIter *iter)
{
	if (!fd) return;

	vficon_selection_set(vf, fd, static_cast<SelectionType>(fd->selected & ~mask), iter);
}

void vficon_marks_set(ViewFile *vf, gint)
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
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (vficon_index_by_fd(vf, fd) >= 0) continue;

		VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, fd);
		}
}

void vficon_select_all(ViewFile *vf)
{
	GList *work;

	g_list_free(VFICON(vf)->selection);
	VFICON(vf)->selection = nullptr;

	work = vf->list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, fd);
		vficon_selection_add(vf, fd, SELECTION_SELECTED, nullptr);
		}

	vf_send_update(vf);
}

void vficon_select_none(ViewFile *vf)
{
	GList *work;

	work = VFICON(vf)->selection;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		vficon_selection_remove(vf, fd, SELECTION_SELECTED, nullptr);
		}

	g_list_free(VFICON(vf)->selection);
	VFICON(vf)->selection = nullptr;

	vf_send_update(vf);
}

void vficon_select_invert(ViewFile *vf)
{
	GList *work;

	work = vf->list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (fd->selected & SELECTION_SELECTED)
			{
			VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, fd);
			vficon_selection_remove(vf, fd, SELECTION_SELECTED, nullptr);
			}
		else
			{
			VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, fd);
			vficon_selection_add(vf, fd, SELECTION_SELECTED, nullptr);
			}
		}

	vf_send_update(vf);
}

static void vficon_select(ViewFile *vf, FileData *fd)
{
	VFICON(vf)->prev_selection = fd;

	if (!fd || fd->selected & SELECTION_SELECTED) return;

	VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, fd);
	vficon_selection_add(vf, fd, SELECTION_SELECTED, nullptr);

	vf_send_update(vf);
}

static void vficon_unselect(ViewFile *vf, FileData *fd)
{
	VFICON(vf)->prev_selection = fd;

	if (!fd || !(fd->selected & SELECTION_SELECTED) ) return;

	VFICON(vf)->selection = g_list_remove(VFICON(vf)->selection, fd);
	vficon_selection_remove(vf, fd, SELECTION_SELECTED, nullptr);

	vf_send_update(vf);
}

static void vficon_select_util(ViewFile *vf, FileData *fd, gboolean select)
{
	if (select)
		{
		vficon_select(vf, fd);
		}
	else
		{
		vficon_unselect(vf, fd);
		}
}

static void vficon_select_region_util(ViewFile *vf, FileData *start, FileData *end, gboolean select)
{
	gint row1;
	gint col1;
	gint row2;
	gint col2;
	gint i;
	gint j;

	if (!vficon_find_position(vf, start, &row1, &col1) ||
	    !vficon_find_position(vf, end, &row2, &col2) ) return;

	VFICON(vf)->prev_selection = end;

	if (!options->collections.rectangular_selection)
		{
		GList *work;

		if (g_list_index(vf->list, start) > g_list_index(vf->list, end))
			{
			std::swap(start, end);
			}

		work = g_list_find(vf->list, start);
		while (work)
			{
			auto fd = static_cast<FileData *>(work->data);
			vficon_select_util(vf, fd, select);

			if (work->data != end)
				work = work->next;
			else
				work = nullptr;
			}
		return;
		}

	// rectangular_selection==true.
	if (row2 < row1)
		{
		std::swap(row1, row2);
		}
	if (col2 < col1)
		{
		std::swap(col1, col2);
		}

	DEBUG_1("table: %d x %d to %d x %d", row1, col1, row2, col2);

	for (i = row1; i <= row2; i++)
		{
		for (j = col1; j <= col2; j++)
			{
			FileData *fd = vficon_find_data(vf, i, j, nullptr);
			if (fd) vficon_select_util(vf, fd, select);
			}
		}
}

gboolean vficon_is_selected(ViewFile *, FileData *fd)
{
	return (fd->selected & SELECTION_SELECTED);
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
			auto fd = static_cast<FileData *>(work->data);
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
	GList *list = nullptr;

	for (GList *work = g_list_last(VFICON(vf)->selection); work; work = work->prev)
		{
		auto fd = static_cast<FileData *>(work->data);
		g_assert(fd->magick == FD_MAGICK);

		list = g_list_concat(filelist_copy(fd->sidecar_files), list);
		list = g_list_prepend(list, file_data_ref(fd));
		}

	return list;
}

GList *vficon_selection_get_list_by_index(ViewFile *vf)
{
	GList *list = nullptr;
	GList *work;

	work = VFICON(vf)->selection;
	while (work)
		{
		list = g_list_prepend(list, GINT_TO_POINTER(g_list_index(vf->list, work->data)));
		work = work->next;
		}

	return g_list_reverse(list);
}

void vficon_selection_foreach(ViewFile *vf, const ViewFile::SelectionCallback &func)
{
	for (GList *work = VFICON(vf)->selection; work; work = work->next)
		{
		auto *fd_n = static_cast<FileData *>(work->data);

		func(fd_n);
		}
}

void vficon_select_by_fd(ViewFile *vf, FileData *fd)
{
	if (!fd) return;
	if (!g_list_find(vf->list, fd)) return;

	if (!(fd->selected & SELECTION_SELECTED))
		{
		vficon_select_none(vf);
		vficon_select(vf, fd);
		}

	vficon_set_focus(vf, fd);
}

void vficon_select_list(ViewFile *vf, GList *list)
{
	GList *work;
	FileData *fd;

	if (!list) return;

	work = list;
	while (work)
		{
		fd = static_cast<FileData *>(work->data);
		if (g_list_find(vf->list, fd))
			{
			VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, fd);
			vficon_selection_add(vf, fd, SELECTION_SELECTED, nullptr);
			}
		work = work->next;
		}
}

void vficon_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode)
{
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	for (GList *work = vf->list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);
		gboolean selected;

		g_assert(fd->magick == FD_MAGICK);

		selected = file_data_mark_to_selection(fd, mark, mode, fd->selected & SELECTION_SELECTED);

		vficon_select_util(vf, fd, selected);
		}
}

void vficon_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode)
{
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	g_autoptr(FileDataList) slist = vficon_selection_get_list(vf);
	for (GList *work = slist; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		file_data_selection_to_mark(fd, mark, mode);
		}
}

static void vficon_select_closest(ViewFile *vf, FileData *sel_fd)
{
	GList *work;
	FileData *fd = nullptr;

	if (sel_fd->parent) sel_fd = sel_fd->parent;
	work = vf->list;

	while (work)
		{
		gint match;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		match = filelist_sort_compare_filedata_full(fd, sel_fd, vf->sort.method, vf->sort.ascending);

		if (match >= 0) break;
		}

	if (fd)
		{
		vficon_select(vf, fd);
		vficon_send_layout_select(vf, fd);
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
		new_row = std::clamp(VFICON(vf)->focus_row + row, 0, VFICON(vf)->rows - 1);
		new_col = VFICON(vf)->focus_column;

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

	vficon_set_focus(vf, vficon_find_data(vf, new_row, new_col, nullptr));
}

static void vficon_set_focus(ViewFile *vf, FileData *fd)
{
	GtkTreeIter iter;
	gint row;
	gint col;

	if (g_list_find(vf->list, VFICON(vf)->focus_fd))
		{
		if (fd == VFICON(vf)->focus_fd)
			{
			/* ensure focus row col are correct */
			vficon_find_position(vf, VFICON(vf)->focus_fd, &VFICON(vf)->focus_row, &VFICON(vf)->focus_column);

/** @FIXME Refer to issue #467 on Github. The thumbnail position is not
 * preserved when the icon view is refreshed. Caused by an unknown call from
 * the idle loop. This patch hides the problem.
 */
			if (vficon_find_iter(vf, VFICON(vf)->focus_fd, &iter, nullptr))
				{
				tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE);
				}

			return;
			}
		vficon_selection_remove(vf, VFICON(vf)->focus_fd, SELECTION_FOCUS, nullptr);
		}

	if (!vficon_find_position(vf, fd, &row, &col))
		{
		VFICON(vf)->focus_fd = nullptr;
		VFICON(vf)->focus_row = -1;
		VFICON(vf)->focus_column = -1;
		return;
		}

	VFICON(vf)->focus_fd = fd;
	VFICON(vf)->focus_row = row;
	VFICON(vf)->focus_column = col;
	vficon_selection_add(vf, VFICON(vf)->focus_fd, SELECTION_FOCUS, nullptr);

	if (vficon_find_iter(vf, VFICON(vf)->focus_fd, &iter, nullptr))
		{
		GtkTreeViewColumn *column;
		GtkTreeModel *store;

		tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
		/* focus is set to an extra column with 0 width to hide focus, we draw it ourself */
		column = gtk_tree_view_get_column(GTK_TREE_VIEW(vf->listview), VFICON_MAX_COLUMNS);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(vf->listview), tpath, column, FALSE);
		}
}

/* used to figure the page up/down distances */
static gint page_height(ViewFile *vf)
{
	GtkAdjustment *adj;
	gint page_size;
	gint row_height;
	gint ret;

	adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vf->listview));
	page_size = static_cast<gint>(gtk_adjustment_get_page_increment(adj));

	row_height = options->thumbnails.max_height + THUMB_BORDER_PADDING * 2;
	if (VFICON(vf)->show_text) row_height += options->thumbnails.max_height / 3;

	ret = page_size / row_height;
	ret = std::max(ret, 1);

	return ret;
}

/*
 *-------------------------------------------------------------------
 * keyboard
 *-------------------------------------------------------------------
 */

gboolean vficon_press_key_cb(ViewFile *vf, GtkWidget *widget, GdkEventKey *event)
{
	gint focus_row = 0;
	gint focus_col = 0;
	FileData *fd;
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
			fd = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, nullptr);
			if (fd)
				{
				vf->click_fd = fd;
				if (event->state & GDK_CONTROL_MASK)
					{
					gint selected;

					selected = fd->selected & SELECTION_SELECTED;
					if (selected)
						{
						vficon_unselect(vf, fd);
						}
					else
						{
						vficon_select(vf, fd);
						vficon_send_layout_select(vf, fd);
						}
					}
				else
					{
					vficon_select_none(vf);
					vficon_select(vf, fd);
					vficon_send_layout_select(vf, fd);
					}
				}
			break;
		case GDK_KEY_Menu:
			fd = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, nullptr);
			vf->click_fd = fd;

			vficon_selection_add(vf, vf->click_fd, SELECTION_PRELIGHT, nullptr);
			tip_unschedule(vf);

			vf->popup = vf_pop_menu(vf);
			gtk_menu_popup_at_widget(GTK_MENU(vf->popup), widget, GDK_GRAVITY_EAST, GDK_GRAVITY_CENTER, nullptr);
			break;
		default:
			stop_signal = FALSE;
			break;
		}

	if (focus_row != 0 || focus_col != 0)
		{
		FileData *new_fd;
		FileData *old_fd;

		old_fd = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, nullptr);
		vficon_move_focus(vf, focus_row, focus_col, TRUE);
		new_fd = vficon_find_data(vf, VFICON(vf)->focus_row, VFICON(vf)->focus_column, nullptr);

		if (new_fd != old_fd)
			{
			if (event->state & GDK_SHIFT_MASK)
				{
				if (!options->collections.rectangular_selection)
					{
					vficon_select_region_util(vf, old_fd, new_fd, FALSE);
					}
				else
					{
					vficon_select_region_util(vf, vf->click_fd, old_fd, FALSE);
					}
				vficon_select_region_util(vf, vf->click_fd, new_fd, TRUE);
				vficon_send_layout_select(vf, new_fd);
				}
			else if (event->state & GDK_CONTROL_MASK)
				{
				vf->click_fd = new_fd;
				}
			else
				{
				vf->click_fd = new_fd;
				vficon_select_none(vf);
				vficon_select(vf, new_fd);
				vficon_send_layout_select(vf, new_fd);
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

static gboolean vficon_motion_cb(GtkWidget *, GdkEventMotion *event, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	FileData *fd;

	fd = vficon_find_data_by_coord(vf, static_cast<gint>(event->x), static_cast<gint>(event->y), nullptr);
	tip_update(vf, fd);

	return FALSE;
}

gboolean vficon_press_cb(ViewFile *vf, GtkWidget *, GdkEventButton *bevent)
{
	GtkTreeIter iter;
	FileData *fd;

	tip_unschedule(vf);

	fd = vficon_find_data_by_coord(vf, static_cast<gint>(bevent->x), static_cast<gint>(bevent->y), &iter);

	if (fd)
		{
		vf->click_fd = fd;
		vficon_selection_add(vf, vf->click_fd, SELECTION_PRELIGHT, &iter);

		switch (bevent->button)
			{
			case GDK_BUTTON_PRIMARY:
				if (!gtk_widget_has_focus(vf->listview))
					{
					gtk_widget_grab_focus(vf->listview);
					}

				if (bevent->type == GDK_2BUTTON_PRESS && vf->layout)
					{
					if (vf->click_fd->format_class == FORMAT_CLASS_COLLECTION)
						{
						collection_window_new(vf->click_fd->path);
						}
					else
						{
						vficon_selection_remove(vf, vf->click_fd, SELECTION_PRELIGHT, &iter);
						layout_image_full_screen_start(vf->layout);
						}
					}
				break;
			case GDK_BUTTON_SECONDARY:
				vf->popup = vf_pop_menu(vf);
				gtk_menu_popup_at_pointer(GTK_MENU(vf->popup), nullptr);
				break;
			default:
				break;
			}
		}

	return FALSE;
}

gboolean vficon_release_cb(ViewFile *vf, GtkWidget *, GdkEventButton *bevent)
{
	GtkTreeIter iter;
	FileData *fd = nullptr;
	gboolean was_selected;

	tip_schedule(vf);

	if (layout_handle_user_defined_mouse_buttons(vf->layout, bevent))
		{
		return TRUE;
		}

	if (static_cast<gint>(bevent->x) != 0 || static_cast<gint>(bevent->y) != 0)
		{
		fd = vficon_find_data_by_coord(vf, static_cast<gint>(bevent->x), static_cast<gint>(bevent->y), &iter);
		}

	if (vf->click_fd)
		{
		vficon_selection_remove(vf, vf->click_fd, SELECTION_PRELIGHT, nullptr);
		}

	if (!fd || vf->click_fd != fd) return TRUE;

	was_selected = !!(fd->selected & SELECTION_SELECTED);

	switch (bevent->button)
		{
		case GDK_BUTTON_PRIMARY:
			{
			vficon_set_focus(vf, fd);

			if (bevent->state & GDK_CONTROL_MASK)
				{
				gboolean select;

				select = !(fd->selected & SELECTION_SELECTED);
				if ((bevent->state & GDK_SHIFT_MASK) && VFICON(vf)->prev_selection)
					{
					vficon_select_region_util(vf, VFICON(vf)->prev_selection, fd, select);
					}
				else
					{
					vficon_select_util(vf, fd, select);
					}
				}
			else
				{
				vficon_select_none(vf);

				if ((bevent->state & GDK_SHIFT_MASK) && VFICON(vf)->prev_selection)
					{
					vficon_select_region_util(vf, VFICON(vf)->prev_selection, fd, TRUE);
					}
				else
					{
					vficon_select_util(vf, fd, TRUE);
					was_selected = FALSE;
					}
				}
			}
			break;
		case GDK_BUTTON_MIDDLE:
			{
			vficon_select_util(vf, fd, !(fd->selected & SELECTION_SELECTED));
			}
			break;
		default:
			break;
		}

	if (!was_selected && (fd->selected & SELECTION_SELECTED))
		{
		vficon_send_layout_select(vf, fd);
		}

	return TRUE;
}

static gboolean vficon_leave_cb(GtkWidget *, GdkEventCrossing *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	tip_unschedule(vf);
	return FALSE;
}

/*
 *-------------------------------------------------------------------
 * population
 *-------------------------------------------------------------------
 */

static gboolean vficon_destroy_node_cb(GtkTreeModel *store, GtkTreePath *, GtkTreeIter *iter, gpointer)
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
	gtk_tree_model_foreach(store, vficon_destroy_node_cb, nullptr);

	gtk_list_store_clear(GTK_LIST_STORE(store));
}

static GList *vficon_add_row(ViewFile *vf, GtkTreeIter *iter)
{
	GtkListStore *store;
	GList *list = nullptr;
	gint i;

	for (i = 0; i < VFICON(vf)->columns; i++) list = g_list_prepend(list, nullptr);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	gtk_list_store_append(store, iter);
	gtk_list_store_set(store, iter, FILE_COLUMN_POINTER, list, -1);

	return list;
}

static void vficon_populate(ViewFile *vf, gboolean resize, gboolean keep_position)
{
	GtkTreeModel *store;
	GList *work;
	FileData *visible_fd = nullptr;
	gint r;
	gboolean valid;
	GtkTreeIter iter;

	vficon_verify_selections(vf);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    keep_position && gtk_widget_get_realized(vf->listview) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeIter iter;
		GList *list;

		gtk_tree_model_get_iter(store, &iter, tpath);

		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
		if (list) visible_fd = static_cast<FileData *>(list->data);
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
			cell = static_cast<GtkCellRenderer *>((list) ? list->data : nullptr);
			g_list_free(list);

			if (cell && GQV_IS_CELL_RENDERER_ICON(cell))
				{
				g_object_set(cell,
				             "fixed_width", thumb_width,
				             "fixed_height", options->thumbnails.max_height,
				             "show_text", VFICON(vf)->show_text || options->show_star_rating,
				             "show_marks", vf->marks_enabled,
				             "num_marks", FILEDATA_MARKS_SIZE,
				             NULL);
				}
			}
		if (gtk_widget_get_realized(vf->listview)) gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
		}

	r = -1;

	valid = gtk_tree_model_iter_children(store, &iter, nullptr);

	work = vf->list;
	while (work)
		{
		GList *list;
		r++;
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
			FileData *fd;

			if (work)
				{
				fd = static_cast<FileData *>(work->data);
				work = work->next;
				}
			else
				{
				fd = nullptr;
				}

			list->data = fd;
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

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    visible_fd &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeIter iter;
		GList *list;

		gtk_tree_model_get_iter(store, &iter, tpath);

		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
		if (g_list_find(list, visible_fd) == nullptr &&
		    vficon_find_iter(vf, visible_fd, &iter, nullptr))
			{
			tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE);
			}
		}


	vf_send_update(vf);
	vf_thumb_update(vf);
	vf_star_update(vf);
}

static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint, gboolean force)
{
	gint new_cols;
	gint thumb_width;

	thumb_width = vficon_get_icon_width(vf);

	new_cols = w / (thumb_width + (THUMB_BORDER_PADDING * 6));
	new_cols = std::max(new_cols, 1);

	if (!force && new_cols == VFICON(vf)->columns) return;

	VFICON(vf)->columns = new_cols;

	vficon_populate(vf, TRUE, TRUE);

	DEBUG_1("col tab pop cols=%d rows=%d", VFICON(vf)->columns, VFICON(vf)->rows);
}

static void vficon_sized_cb(GtkWidget *, GtkAllocation *allocation, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vficon_populate_at_new_size(vf, allocation->width, allocation->height, FALSE);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

void vficon_sort_set(ViewFile *vf, FileData::FileList::SortSettings settings)
{
	if (vf->sort == settings) return;

	vf->sort = settings;

	if (!vf->list) return;

	vficon_refresh(vf);
}

/*
 *-----------------------------------------------------------------------------
 * thumb updates
 *-----------------------------------------------------------------------------
 */

void vficon_thumb_progress_count(const GList *list, gint &count, gint &done)
{
	for (const GList *work = list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (fd->thumb_pixbuf) done++;
		count++;
		}
}

void vficon_read_metadata_progress_count(const GList *list, gint &count, gint &done)
{
	for (const GList *work = list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (fd->metadata_in_idle_loaded) done++;
		count++;
		}
}

void vficon_set_thumb_fd(ViewFile *vf, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GList *list;

	if (!g_list_find(vf->list, fd)) return;
	if (!vficon_find_iter(vf, fd, &iter, nullptr)) return;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
	gtk_list_store_set(GTK_LIST_STORE(store), &iter, FILE_COLUMN_POINTER, list, -1);
}

/* Returns the next fd without a loaded pixbuf, so the thumb-loader can load the pixbuf for it. */
FileData *vficon_thumb_next_fd(ViewFile *vf)
{
	/* First see if there are visible files that don't have a loaded thumb... */
	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid = TRUE;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);

		while (valid && tree_view_row_is_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE))
			{
			GList *list;
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);

			/** @todo (xsdg): for loop here. */
			for (; list; list = list->next)
				{
				auto fd = static_cast<FileData *>(list->data);
				if (fd && !fd->thumb_pixbuf) return fd;
				}

			valid = gtk_tree_model_iter_next(store, &iter);
			}
		}

	/* Then iterate through the entire list to load all of them. */
	GList *work;
	for (work = vf->list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		// Note: This implementation differs from view-file-list.cc because sidecar files are not
		// distinct list elements here, as they are in the list view.
		if (!fd->thumb_pixbuf) return fd;
		}

	return nullptr;
}

void vficon_set_star_fd(ViewFile *vf, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GList *list;

	if (!g_list_find(vf->list, fd)) return;
	if (!vficon_find_iter(vf, fd, &iter, nullptr)) return;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);
	gtk_list_store_set(GTK_LIST_STORE(store), &iter, FILE_COLUMN_POINTER, list, -1);
}

FileData *vficon_star_next_fd(ViewFile *vf)
{
	/* first check the visible files */

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid = TRUE;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);

		while (valid && tree_view_row_is_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE))
			{
			GList *list;
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &list, -1);

			for (GList *work = list; work; work = work->next)
				{
				auto *fd = static_cast<FileData *>(work->data);
				if (fd && fd->rating == STAR_RATING_NOT_READ)
					{
					return fd;
					}
				}

			valid = gtk_tree_model_iter_next(store, &iter);
			}
		}

	/* Then iterate through the entire list to load all of them. */

	for (GList *work = vf->list; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);

		if (fd && fd->rating == STAR_RATING_NOT_READ)
			{
			return fd;
			}
		}

	return nullptr;
}

/*
 *-----------------------------------------------------------------------------
 * row stuff
 *-----------------------------------------------------------------------------
 */

gint vficon_index_by_fd(const ViewFile *vf, const FileData *fd)
{
	if (!fd) return -1;

	return g_list_index(vf->list, fd);
}

/*
 *-----------------------------------------------------------------------------
 *
 *-----------------------------------------------------------------------------
 */

static gboolean vficon_refresh_real(ViewFile *vf, gboolean keep_position)
{
	gboolean ret = TRUE;
	GList *work;
	GList *new_work;
	FileData *first_selected = nullptr;
	GList *new_filelist = nullptr;
	GList *new_fd_list = nullptr;
	GList *old_selected = nullptr;
	GtkTreeIter iter;
	GtkTreeModel *store;

	g_autoptr(GtkTreePath) start_path = nullptr;
	g_autoptr(GtkTreePath) end_path = nullptr;
	gtk_tree_view_get_visible_range(GTK_TREE_VIEW(vf->listview), &start_path, &end_path);

	if (vf->dir_fd)
		{
		ret = filelist_read(vf->dir_fd, &new_filelist, nullptr);
		new_filelist = file_data_filter_marks_list(new_filelist, vf_marks_get_filter(vf));

		g_autoptr(GRegex) filter = vf_file_filter_get_filter(vf);
		new_filelist = g_list_first(new_filelist);
		new_filelist = file_data_filter_file_filter_list(new_filelist, filter);

		new_filelist = g_list_first(new_filelist);
		new_filelist = file_data_filter_class_list(new_filelist, vf_class_get_filter(vf));

		}

	vf->list = filelist_sort(vf->list, vf->sort); /* the list might not be sorted if there were renames */
	new_filelist = filelist_sort(new_filelist, vf->sort);

	if (VFICON(vf)->selection)
		{
		old_selected = g_list_copy(VFICON(vf)->selection);
		first_selected = static_cast<FileData *>(VFICON(vf)->selection->data);
		file_data_ref(first_selected);
		g_list_free(VFICON(vf)->selection);
		VFICON(vf)->selection = nullptr;
		}

	/* iterate old list and new list, looking for differences */
	work = vf->list;
	new_work = new_filelist;
	while (work || new_work)
		{
		FileData *fd = nullptr;
		FileData *new_fd = nullptr;
		gint match;

		if (work && new_work)
			{
			fd = static_cast<FileData *>(work->data);
			new_fd = static_cast<FileData *>(new_work->data);

			if (fd == new_fd)
				{
				/* not changed, go to next */
				work = work->next;
				new_work = new_work->next;
				if (fd->selected & SELECTION_SELECTED)
					{
					VFICON(vf)->selection = g_list_prepend(VFICON(vf)->selection, fd);
					}
				continue;
				}

			match = filelist_sort_compare_filedata_full(fd, new_fd, vf->sort.method, vf->sort.ascending);
			if (match == 0) g_warning("multiple fd for the same path");
			}
		else if (work)
			{
			/* old item was deleted */
			fd = static_cast<FileData *>(work->data);
			match = -1;
			}
		else
			{
			/* new item was added */
			new_fd = static_cast<FileData *>(new_work->data);
			match = 1;
			}

		if (match < 0)
			{
			/* file no longer exists, delete from vf->list */
			GList *to_delete = work;
			work = work->next;
			if (fd == VFICON(vf)->prev_selection) VFICON(vf)->prev_selection = nullptr;
			if (fd == vf->click_fd) vf->click_fd = nullptr;
			file_data_unref(fd);
			vf->list = g_list_delete_link(vf->list, to_delete);
			}
		else
			{
			/* new file, add to vf->list */
			file_data_ref(new_fd);
			new_fd->selected = SELECTION_NONE;
			if (work)
				{
				vf->list = g_list_insert_before(vf->list, work, new_fd);
				}
			else
				{
				/* it is faster to append all new entries together later */
				new_fd_list = g_list_prepend(new_fd_list, new_fd);
				}

			new_work = new_work->next;
			}
		}

	if (new_fd_list)
		{
		vf->list = g_list_concat(vf->list, g_list_reverse(new_fd_list));
		}

	VFICON(vf)->selection = g_list_reverse(VFICON(vf)->selection);

	/* Preserve the original selection order */
	if (old_selected)
		{
		GList *reversed_old_selected;

		reversed_old_selected = g_list_reverse(old_selected);
		for (old_selected = reversed_old_selected; old_selected; old_selected = old_selected->next)
			{
			GList *tmp = g_list_find(VFICON(vf)->selection, old_selected->data);
			if (tmp)
				{
				VFICON(vf)->selection = g_list_remove_link(VFICON(vf)->selection, tmp);
				VFICON(vf)->selection = g_list_concat(tmp, VFICON(vf)->selection);
				}
			}
		g_list_free(reversed_old_selected);
		}

	file_data_list_free(new_filelist);

	vficon_populate(vf, TRUE, keep_position);

	if (first_selected && !VFICON(vf)->selection)
		{
		/* all selected files disappeared */
		vficon_select_closest(vf, first_selected);
		}
	file_data_unref(first_selected);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	if (gtk_tree_model_get_iter_first(store, &iter) && start_path)
		{
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(vf->listview), start_path, nullptr, FALSE, 0.0, 0.0);
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

static void vficon_cell_data_cb(GtkTreeViewColumn *, GtkCellRenderer *cell,
				GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return;
#else
	if (!GQV_IS_CELL_RENDERER_ICON(cell)) return;

	auto cd = static_cast<ColumnData *>(data);

	GList *list;
	gtk_tree_model_get(tree_model, iter, FILE_COLUMN_POINTER, &list, -1);

	auto *fd = static_cast<FileData *>(g_list_nth_data(list, cd->number));
	if (!fd)
		{
		g_object_set(cell,
		             "pixbuf", NULL,
		             "text", NULL,
		             "show_marks", FALSE,
		             "cell-background-set", FALSE,
		             "foreground-set", FALSE,
		             "has-focus", FALSE,
		             NULL);
		return;
		}

	ViewFile *vf = cd->vf;

	g_assert(fd->magick == FD_MAGICK);

	g_autoptr(GString) name_sidecars = g_string_new(nullptr);

	if (VFICON(vf)->show_text)
		{
		if (islink(fd->path))
			{
			name_sidecars = g_string_append(name_sidecars, GQ_LINK_STR);
			}

		name_sidecars = g_string_append(name_sidecars, fd->name);

		if (fd->sidecar_files)
			{
			g_autofree gchar *sidecars = file_data_sc_list_to_string(fd);
			g_string_append_printf(name_sidecars, " %s", sidecars);
			}
		else if (fd->disable_grouping)
			{
			name_sidecars = g_string_append(name_sidecars, _(" [NO GROUPING]"));
			}
		}

	if (options->show_star_rating)
		{
		if (name_sidecars->len > 0)
			{
			name_sidecars = g_string_append_c(name_sidecars, '\n');
			}

		g_autofree gchar *star_rating = (fd->rating != STAR_RATING_NOT_READ) ? convert_rating_to_stars(fd->rating) : nullptr;
		name_sidecars = g_string_append(name_sidecars, star_rating);
		}

	GtkStyle *style = deprecated_gtk_widget_get_style(vf->listview);
	GtkStateType state = (fd->selected & SELECTION_SELECTED) ? GTK_STATE_SELECTED : GTK_STATE_NORMAL;

	GdkRGBA color_fg = convert_gdkcolor_to_gdkrgba(&style->text[state]);

	GdkRGBA color_bg = convert_gdkcolor_to_gdkrgba(&style->base[state]);
	if (fd->selected & SELECTION_PRELIGHT)
		{
		shift_color(color_bg);
		}

	g_object_set(cell,
	             "pixbuf", fd->thumb_pixbuf,
	             "text", name_sidecars->str,
	             "marks", file_data_get_marks(fd),
	             "show_marks", vf->marks_enabled,
	             "cell-background-rgba", &color_bg,
	             "cell-background-set", TRUE,
	             "foreground-rgba", &color_fg,
	             "foreground-set", TRUE,
	             "has-focus", VFICON(vf)->focus_fd == fd,
	             NULL);
#endif
}

static void vficon_append_column(ViewFile *vf, gint n)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_min_width(column, 0);

	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_alignment(column, 0.5);

	renderer = gqv_cell_renderer_icon_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	g_object_set(renderer,
	             "xpad", THUMB_BORDER_PADDING * 2,
	             "ypad", THUMB_BORDER_PADDING,
	             "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
	             NULL);

	g_object_set_data(G_OBJECT(column), "column_number", GINT_TO_POINTER(n));
	g_object_set_data(G_OBJECT(renderer), "column_number", GINT_TO_POINTER(n));

	auto cd = g_new0(ColumnData, 1);
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
	VFICON(vf)->selection = nullptr;

	g_list_free(vf->list);
	vf->list = nullptr;

	/* NOTE: populate will clear the store for us */
	ret = vficon_refresh_real(vf, FALSE);

	VFICON(vf)->focus_fd = nullptr;
	vficon_move_focus(vf, 0, 0, FALSE);

	return ret;
}

void vficon_destroy_cb(ViewFile *vf)
{
	vf_refresh_idle_cancel(vf);

	file_data_unregister_notify_func(vf_notify_cb, vf);

	tip_unschedule(vf);

	vf_thumb_cleanup(vf);
	vf_star_cleanup(vf);

	g_list_free(vf->list);
	g_list_free(VFICON(vf)->selection);
}

ViewFile *vficon_new(ViewFile *vf)
{
	GtkListStore *store;
	gint i;

	vf->info = g_new0(ViewFileInfoIcon, 1);

	VFICON(vf)->show_text = options->show_icon_names;

	store = gtk_list_store_new(1, G_TYPE_POINTER);
	vf->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

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
			      static_cast<GdkEventMask>(GDK_BUTTON_PRESS_MASK | GDK_LEAVE_NOTIFY_MASK));

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
