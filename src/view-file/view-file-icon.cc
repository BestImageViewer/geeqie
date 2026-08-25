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

#include "collect.h"
#include "dnd.h"
#include "filedata.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "utilops.h"
#include "view-file.h"

namespace
{

/* between these, the icon width is increased by thumb_max_width / 2 */
constexpr gint THUMB_MIN_ICON_WIDTH = 128;
constexpr gint THUMB_MAX_ICON_WIDTH = 160;

constexpr gint THUMB_MIN_ICON_WIDTH_WITH_MARKS = 16 * FILEDATA_MARKS_SIZE;

constexpr gint VFICON_MAX_COLUMNS = 32;

constexpr gint THUMB_BORDER_PADDING = 2;

constexpr gint VFICON_TIP_DELAY = 500;

struct ViewFileIconItem
{
	GObject parent;
	FileData *fd;
};

struct ViewFileIconItemClass
{
	GObjectClass parent_class;
};

G_DEFINE_TYPE(ViewFileIconItem, view_file_icon_item, G_TYPE_OBJECT)

enum { VIEW_FILE_ICON_ITEM_CHANGED, VIEW_FILE_ICON_ITEM_SIGNAL_COUNT };
guint view_file_icon_item_signals[VIEW_FILE_ICON_ITEM_SIGNAL_COUNT];

void view_file_icon_item_class_init(ViewFileIconItemClass *klass)
{
	view_file_icon_item_signals[VIEW_FILE_ICON_ITEM_CHANGED] =
		g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		             0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
}

void view_file_icon_item_init(ViewFileIconItem *)
{
}

ViewFileIconItem *view_file_icon_item_new(FileData *fd)
{
	auto *item = static_cast<ViewFileIconItem *>(g_object_new(view_file_icon_item_get_type(), nullptr));
	item->fd = fd;
	return item;
}

constexpr auto VIEW_FILE_ICON_DATA_KEY = "view-file-icon-data";

} // namespace

static void vficon_toggle_filenames(ViewFile *vf);
static void vficon_selection_remove(ViewFile *vf, FileData *fd, SelectionType mask, GtkTreeIter *iter);
static void vficon_move_focus(ViewFile *vf, gint row, gint col, gboolean relative);
static void vficon_set_focus(ViewFile *vf, FileData *fd);
static gint vficon_viewport_width(ViewFile *vf);
static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint h, gboolean force, gboolean keep_position = TRUE);


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
	popover_item_add_check(menu, _("Show filename _text"), VFICON(vf)->show_text,
	                    G_CALLBACK(vficon_pop_menu_show_names_cb), vf);
}

void vficon_pop_menu_show_star_rating_cb(ViewFile *vf)
{
	vficon_populate_at_new_size(vf, gtk_widget_get_width(vf->listview), gtk_widget_get_height(vf->listview), TRUE);
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
	VFICON(vf)->show_text = !VFICON(vf)->show_text;
	options->show_icon_names = VFICON(vf)->show_text;

	vficon_populate_at_new_size(vf, gtk_widget_get_width(vf->listview), gtk_widget_get_height(vf->listview), TRUE);
}

static gint vficon_get_icon_width(ViewFile *vf)
{
	if (!VFICON(vf)->show_text && !vf->marks_enabled) return options->thumbnails.size.width;

	gint width = options->thumbnails.size.width + (options->thumbnails.size.width / 2);
	width = std::max(width, THUMB_MIN_ICON_WIDTH);
	if (width > THUMB_MAX_ICON_WIDTH) width = options->thumbnails.size.width;
	if (vf->marks_enabled && width < THUMB_MIN_ICON_WIDTH_WITH_MARKS) width = THUMB_MIN_ICON_WIDTH_WITH_MARKS;

	return width;
}

/*
 *-------------------------------------------------------------------
 * misc utils
 *-------------------------------------------------------------------
 */

static gboolean vficon_find_position(ViewFile *vf, const FileData *fd, gint *row, gint *col)
{
	gint n;

	n = g_list_index(vf->list, fd);

	if (n < 0) return FALSE;

	*row = n / VFICON(vf)->columns;
	*col = n - (*row * VFICON(vf)->columns);

	return TRUE;
}

static FileData *vficon_find_data(ViewFile *vf, gint row, gint col, GtkTreeIter *iter)
{
	if (row < 0 || col < 0) return nullptr;
	if (iter) *iter = {};
	return static_cast<FileData *>(g_list_nth_data(vf->list, row * VFICON(vf)->columns + col));
}

FileData *vficon_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter)
{
	if (iter) *iter = {};
	GtkWidget *picked = gtk_widget_pick(vf->listview, x, y, GTK_PICK_DEFAULT);
	while (picked && picked != vf->listview)
		{
		if (auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(picked), "view-file-fd"))) return fd;
		picked = gtk_widget_get_parent(picked);
		}
	return nullptr;
}

static gint vficon_mark_at_coord(ViewFile *vf, gint x, gint y)
{
	GtkWidget *picked = gtk_widget_pick(vf->listview, x, y, GTK_PICK_DEFAULT);
	while (picked && picked != vf->listview)
		{
		if (GTK_IS_CHECK_BUTTON(picked))
			{
			return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(picked), "view-file-mark"));
			}
		picked = gtk_widget_get_parent(picked);
		}
	return -1;
}


/*
 *-------------------------------------------------------------------
 * tooltip type window
 *-------------------------------------------------------------------
 */

static void tip_show(ViewFile *vf)
{
	if (VFICON(vf)->tip_window) return;

	gint x;
	gint y;
	get_pointer_position(vf->listview, x, y);

	VFICON(vf)->tip_fd = vficon_find_data_by_coord(vf, x, y, nullptr);
	if (!VFICON(vf)->tip_fd) return;

	VFICON(vf)->tip_window = gtk_window_new();
	gtk_window_set_transient_for(GTK_WINDOW(VFICON(vf)->tip_window), GTK_WINDOW(widget_get_toplevel(vf->listview)));
	gtk_window_set_resizable(GTK_WINDOW(VFICON(vf)->tip_window), FALSE);
	gtk_widget_set_margin_top(VFICON(vf)->tip_window, 2);
	gtk_widget_set_margin_bottom(VFICON(vf)->tip_window, 2);
	gtk_widget_set_margin_start(VFICON(vf)->tip_window, 2);
	gtk_widget_set_margin_end(VFICON(vf)->tip_window, 2);

	VFICON(vf)->tip_label = gtk_label_new(VFICON(vf)->tip_fd->name);
	gtk_window_set_child(GTK_WINDOW(VFICON(vf)->tip_window), VFICON(vf)->tip_label);

	if (!gtk_widget_get_realized(VFICON(vf)->tip_window)) gtk_widget_realize(VFICON(vf)->tip_window);
	gtk_window_present(GTK_WINDOW(VFICON(vf)->tip_window));
}

static void tip_hide(ViewFile *vf)
{
	if (VFICON(vf)->tip_window) gtk_window_destroy(GTK_WINDOW(VFICON(vf)->tip_window));
	VFICON(vf)->tip_window = nullptr;
}

static gboolean tip_schedule_cb(gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	if (VFICON(vf)->tip_delay_id)
		{
		GtkRoot *root = gtk_widget_get_root(vf->listview);

		if (GTK_IS_WINDOW(root))
			{
			auto *window = GTK_WINDOW(root);

			if (gtk_widget_get_sensitive(GTK_WIDGET(window)) && gtk_window_is_active(window))
				{
				tip_show(vf);
				}
			}

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
	if (!VFICON(vf)->tip_window)
		{
		tip_schedule(vf);
		return;
		}

	if (VFICON(vf)->tip_fd == fd) return;

	VFICON(vf)->tip_fd = fd;

	if (!VFICON(vf)->tip_fd)
		{
		tip_hide(vf);
		tip_schedule(vf);
		return;
		}

	gtk_label_set_text(GTK_LABEL(VFICON(vf)->tip_label), VFICON(vf)->tip_fd->name);
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

/*
 *-------------------------------------------------------------------
 * cell updates
 *-------------------------------------------------------------------
 */

static void vficon_selection_set(ViewFile *vf, FileData *fd, SelectionType value, GtkTreeIter *iter)
{
	if (!fd) return;

	if (fd->selected == value) return;
	fd->selected = value;
	if (iter) *iter = {};

	const gint position = g_list_index(vf->list, fd);
	if (position >= 0 && static_cast<guint>(position) < g_list_model_get_n_items(G_LIST_MODEL(VFICON(vf)->store)))
		{
		auto *item = static_cast<ViewFileIconItem *>(g_list_model_get_item(G_LIST_MODEL(VFICON(vf)->store), position));
		g_signal_emit(item, view_file_icon_item_signals[VIEW_FILE_ICON_ITEM_CHANGED], 0);
		g_object_unref(item);
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
	vficon_populate_at_new_size(vf, gtk_widget_get_width(vf->listview), gtk_widget_get_height(vf->listview), TRUE);
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

bool vficon_is_selected(const ViewFile *, const FileData *fd)
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

std::vector<int> vficon_selection_get_list_by_index(const ViewFile *vf)
{
	std::vector<int> list;

	for (GList *work = VFICON(vf)->selection; work; work = work->next)
		{
		list.push_back(g_list_index(vf->list, work->data));
		}

	return list;
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

void vficon_select_list(ViewFile *vf, const FileDataList *list)
{
	if (!list) return;

	for (const GList *work = list; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		if (g_list_find(vf->list, fd))
			{
			VFICON(vf)->selection = g_list_append(VFICON(vf)->selection, fd);
			vficon_selection_add(vf, fd, SELECTION_SELECTED, nullptr);
			}
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
	gint row;
	gint col;

	if (g_list_find(vf->list, VFICON(vf)->focus_fd))
		{
		if (fd == VFICON(vf)->focus_fd)
			{
			/* ensure focus row col are correct */
			vficon_find_position(vf, VFICON(vf)->focus_fd, &VFICON(vf)->focus_row, &VFICON(vf)->focus_column);
			const gint position = vficon_index_by_fd(vf, VFICON(vf)->focus_fd);
			if (position >= 0) gtk_grid_view_scroll_to(GTK_GRID_VIEW(vf->listview), position, GTK_LIST_SCROLL_NONE, nullptr);

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

	const gint position = vficon_index_by_fd(vf, VFICON(vf)->focus_fd);
	if (position >= 0) gtk_grid_view_scroll_to(GTK_GRID_VIEW(vf->listview), position, GTK_LIST_SCROLL_NONE, nullptr);
}

/* used to figure the page up/down distances */
static gint page_height(ViewFile *vf)
{
	GtkAdjustment *adj;
	gint page_size;
	gint ret;

	adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vf->listview));
	page_size = static_cast<gint>(gtk_adjustment_get_page_increment(adj));

	gint row_height = options->thumbnails.size.height + (THUMB_BORDER_PADDING * 2);
	if (VFICON(vf)->show_text) row_height += options->thumbnails.size.height / 3;

	ret = page_size / row_height;
	ret = std::max(ret, 1);

	return ret;
}

/*
 *-------------------------------------------------------------------
 * keyboard
 *-------------------------------------------------------------------
 */

gboolean vficon_press_key_cb(ViewFile *vf, GtkWidget *, guint keyval, GdkModifierType state)
{
	gint focus_row = 0;
	gint focus_col = 0;
	FileData *fd;
	gboolean stop_signal;

	stop_signal = TRUE;
	switch (keyval)
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
				if (state & GDK_CONTROL_MASK)
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
			if (state & GDK_SHIFT_MASK)
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
			else if (state & GDK_CONTROL_MASK)
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

static gboolean vficon_motion_cb(GtkEventControllerMotion *, double x, double y, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	FileData *fd;

	fd = vficon_find_data_by_coord(vf, static_cast<gint>(x), static_cast<gint>(y), nullptr);
	tip_update(vf, fd);

	return FALSE;
}

void vficon_press_cb(ViewFile *vf, const ViewFileMouseButtonEvent &event)
{
	tip_unschedule(vf);

	FileData *fd = vficon_find_data_by_coord(vf, static_cast<gint>(event.x), static_cast<gint>(event.y), nullptr);
	if (!fd) return;

	vf->click_fd = fd;
	vficon_selection_add(vf, vf->click_fd, SELECTION_PRELIGHT, nullptr);

	switch (event.button)
		{
		case GDK_BUTTON_PRIMARY:
			if (!gtk_widget_has_focus(vf->listview))
				{
				gtk_widget_grab_focus(vf->listview);
				}

			if (event.n_press == 2 && vf->layout)
				{
				if (vf->click_fd->format_class == FORMAT_CLASS_COLLECTION)
					{
					collection_window_new(vf->click_fd->path);
					}
				else
					{
					vficon_selection_remove(vf, vf->click_fd, SELECTION_PRELIGHT, nullptr);
					layout_image_full_screen_start(vf->layout);
					}
				}
			break;
		case GDK_BUTTON_SECONDARY:
			{
			if (!vficon_is_selected(vf, fd))
				{
				vficon_select_none(vf);
				vficon_select_util(vf, fd, TRUE);
				vficon_set_focus(vf, fd);
				vficon_send_layout_select(vf, fd);
				}

			gint mark = vficon_mark_at_coord(vf, static_cast<gint>(event.x), static_cast<gint>(event.y));
			vf->clicked_mark = mark >= 0 ? mark + 1 : 0;
			vf->popup = vf_pop_menu(vf, event.widget, event.x, event.y);
			}
			break;
		default:
			break;
		}
}

void vficon_release_cb(ViewFile *vf, const ViewFileMouseButtonEvent &event)
{
	FileData *fd = nullptr;
	gboolean was_selected;

	tip_schedule(vf);

	if (layout_handle_user_defined_mouse_buttons(vf->layout, event.button))
		{
		return;
		}

	if (static_cast<gint>(event.x) != 0 || static_cast<gint>(event.y) != 0)
		{
		fd = vficon_find_data_by_coord(vf, static_cast<gint>(event.x), static_cast<gint>(event.y), nullptr);
		}

	if (vf->click_fd)
		{
		vficon_selection_remove(vf, vf->click_fd, SELECTION_PRELIGHT, nullptr);
		}

	if (event.button == GDK_BUTTON_PRIMARY && vf->drag_started) return;

	if (!fd || vf->click_fd != fd) return;

	was_selected = !!(fd->selected & SELECTION_SELECTED);

	switch (event.button)
		{
		case GDK_BUTTON_PRIMARY:
			{
			vficon_set_focus(vf, fd);

			if (event.state & GDK_CONTROL_MASK)
				{
				gboolean select;

				select = !(fd->selected & SELECTION_SELECTED);
				if ((event.state & GDK_SHIFT_MASK) && VFICON(vf)->prev_selection)
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

				if ((event.state & GDK_SHIFT_MASK) && VFICON(vf)->prev_selection)
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
}

static void vficon_leave_cb(GtkEventControllerMotion *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	tip_unschedule(vf);
}

/*
 *-------------------------------------------------------------------
 * population
 *-------------------------------------------------------------------
 */

static void vficon_clear_store(ViewFile *vf)
{
	g_list_store_remove_all(VFICON(vf)->store);
}

static void vficon_populate(ViewFile *vf, gboolean resize, gboolean keep_position)
{
	vficon_verify_selections(vf);
	GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(vf->scrolled));
	const gdouble scroll_value = keep_position ? gtk_adjustment_get_value(vadjustment) : 0.0;

	vficon_clear_store(vf);
	if (resize) gtk_grid_view_set_max_columns(GTK_GRID_VIEW(vf->listview), VFICON(vf)->columns);

	for (GList *work = vf->list; work; work = work->next)
		{
		auto *item = view_file_icon_item_new(static_cast<FileData *>(work->data));
		g_list_store_append(VFICON(vf)->store, item);
		g_object_unref(item);
		}

	VFICON(vf)->rows = VFICON(vf)->columns > 0 ?
		(g_list_length(vf->list) + VFICON(vf)->columns - 1) / VFICON(vf)->columns : 0;
	if (keep_position)
		{
		const gint focus_position = vficon_index_by_fd(vf, VFICON(vf)->focus_fd);
		if (focus_position >= 0)
			{
			gtk_grid_view_scroll_to(GTK_GRID_VIEW(vf->listview), focus_position, GTK_LIST_SCROLL_NONE, nullptr);
			}
		else
			{
			gtk_adjustment_set_value(vadjustment, scroll_value);
			}
		}


	vf_send_update(vf);
	vf_thumb_update(vf);
	vf_star_update(vf);
}

static void vficon_populate_at_new_size(ViewFile *vf, gint w, gint, gboolean force, gboolean keep_position)
{
	gint new_cols;
	gint thumb_width;

	thumb_width = vficon_get_icon_width(vf);

	new_cols = w / (thumb_width + (THUMB_BORDER_PADDING * 6));
	new_cols = std::clamp(new_cols, 1, VFICON_MAX_COLUMNS);

	if (!force && new_cols == VFICON(vf)->columns) return;

	VFICON(vf)->columns = new_cols;

	vficon_populate(vf, TRUE, keep_position);

	DEBUG_1("col tab pop cols=%d rows=%d", VFICON(vf)->columns, VFICON(vf)->rows);
}

static gint vficon_viewport_width(ViewFile *vf)
{
	GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(vf->scrolled));
	const gint page_width = static_cast<gint>(gtk_adjustment_get_page_size(hadjustment));

	if (page_width > 0) return page_width;

	const gint scrolled_width = gtk_widget_get_width(vf->scrolled);
	if (scrolled_width > 0) return scrolled_width;

	return gtk_widget_get_width(vf->listview);
}

static void vficon_sized_cb(GObject *, GParamSpec *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vficon_populate_at_new_size(vf, vficon_viewport_width(vf), gtk_widget_get_height(vf->scrolled), FALSE);
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
	const gint position = g_list_index(vf->list, fd);
	if (position < 0 || static_cast<guint>(position) >= g_list_model_get_n_items(G_LIST_MODEL(VFICON(vf)->store))) return;
	auto *item = static_cast<ViewFileIconItem *>(g_list_model_get_item(G_LIST_MODEL(VFICON(vf)->store), position));
	g_signal_emit(item, view_file_icon_item_signals[VIEW_FILE_ICON_ITEM_CHANGED], 0);
	g_object_unref(item);
}

/* Returns the next fd without a loaded pixbuf, so the thumb-loader can load the pixbuf for it. */
FileData *vficon_thumb_next_fd(ViewFile *vf)
{
	for (GList *work = vf->list; work; work = work->next)
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
	vficon_set_thumb_fd(vf, fd);
}

FileData *vficon_star_next_fd(ViewFile *vf)
{
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

	if (vf->dir_fd)
		{
		ret = filelist_read(vf->dir_fd, &new_filelist, nullptr);
		new_filelist = file_data_filter_marks_list(new_filelist, vf_marks_get_filter(vf));

		g_autoptr(GRegex) filter = vf_file_filter_get_filter(vf);
		new_filelist = g_list_first(new_filelist);
		new_filelist = file_data_filter_file_filter_list(new_filelist, filter);

		new_filelist = g_list_first(new_filelist);
		new_filelist = file_data_filter_class_list(new_filelist, vf_class_get_filter(vf));

		new_filelist = g_list_first(new_filelist);
		new_filelist = file_data_filter_rating_list(new_filelist, options->rating_filter);
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

	vficon_populate_at_new_size(vf, vficon_viewport_width(vf), gtk_widget_get_height(vf->scrolled), TRUE, keep_position);

	if (first_selected && !VFICON(vf)->selection)
		{
		/* all selected files disappeared */
		vficon_select_closest(vf, first_selected);
		}
	file_data_unref(first_selected);

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

static void vficon_mark_toggled_cb(GtkCheckButton *button, gpointer)
{
	auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(button), "view-file-fd"));
	if (!fd) return;
	const guint mark = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "view-file-mark"));
	const gboolean active = gtk_check_button_get_active(button);
	if (active != file_data_get_mark(fd, mark)) file_data_set_mark(fd, mark, active);
}

static void vficon_item_update(ViewFileIconItem *item, GtkWidget *child)
{
	auto *vf = static_cast<ViewFile *>(g_object_get_data(G_OBJECT(child), VIEW_FILE_ICON_DATA_KEY));
	FileData *fd = item->fd;
	g_object_set_data(G_OBJECT(child), "view-file-fd", fd);
	gtk_widget_set_size_request(child, vficon_get_icon_width(vf), -1);

	auto *picture = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(child), "view-file-picture"));
	g_autoptr(GdkTexture) texture = fd->thumb_pixbuf ? pixbuf_to_texture(fd->thumb_pixbuf) : nullptr;
	gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(texture));
	gtk_widget_set_size_request(picture, vficon_get_icon_width(vf), options->thumbnails.size.height);

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
		if (star_rating)
			{
			name_sidecars = g_string_append(name_sidecars, star_rating);
			}
		}

	auto *label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(child), "view-file-label"));
	gtk_widget_set_size_request(label, vficon_get_icon_width(vf), -1);
	gtk_label_set_max_width_chars(GTK_LABEL(label), std::max(vficon_get_icon_width(vf) / 8, 1));
	gtk_label_set_text(GTK_LABEL(label), name_sidecars->str);
	gtk_widget_set_visible(label, name_sidecars->len > 0);

	auto *marks = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(child), "view-file-marks"));
	gtk_widget_set_visible(marks, vf->marks_enabled);
	for (GtkWidget *button = gtk_widget_get_first_child(marks); button; button = gtk_widget_get_next_sibling(button))
		{
		const guint mark = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "view-file-mark"));
		g_object_set_data(G_OBJECT(button), "view-file-fd", fd);
		g_signal_handlers_block_by_func(button, (gpointer)vficon_mark_toggled_cb, nullptr);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(button), file_data_get_mark(fd, mark));
		g_signal_handlers_unblock_by_func(button, (gpointer)vficon_mark_toggled_cb, nullptr);
		}

	gtk_widget_remove_css_class(child, "view-file-grid-selected");
	gtk_widget_remove_css_class(child, "view-file-grid-prelight");
	gtk_widget_remove_css_class(child, "view-file-grid-focus");
	if (fd->selected & SELECTION_SELECTED) gtk_widget_add_css_class(child, "view-file-grid-selected");
	if (fd->selected & SELECTION_PRELIGHT) gtk_widget_add_css_class(child, "view-file-grid-prelight");
	if (VFICON(vf)->focus_fd == fd && gtk_widget_has_focus(vf->listview))
		gtk_widget_add_css_class(child, "view-file-grid-focus");
}

static void vficon_focus_changed_cb(GtkWidget *, GParamSpec *, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);
	for (guint position = 0; position < g_list_model_get_n_items(G_LIST_MODEL(VFICON(vf)->store)); position++)
		{
		auto *item = static_cast<ViewFileIconItem *>(g_list_model_get_item(G_LIST_MODEL(VFICON(vf)->store), position));
		g_signal_emit(item, view_file_icon_item_signals[VIEW_FILE_ICON_ITEM_CHANGED], 0);
		g_object_unref(item);
		}
}

static void vficon_factory_setup(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, THUMB_BORDER_PADDING);
	gtk_widget_add_css_class(box, "view-file-grid-item");
	gtk_widget_set_size_request(box, vficon_get_icon_width(vf), -1);
	gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start(box, THUMB_BORDER_PADDING * 2);
	gtk_widget_set_margin_end(box, THUMB_BORDER_PADDING * 2);
	gtk_widget_set_margin_top(box, THUMB_BORDER_PADDING);
	gtk_widget_set_margin_bottom(box, THUMB_BORDER_PADDING);
	g_object_set_data(G_OBJECT(box), VIEW_FILE_ICON_DATA_KEY, vf);

	GtkWidget *picture = gtk_picture_new();
	gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
	gtk_box_append(GTK_BOX(box), picture);
	GtkWidget *label = gtk_label_new(nullptr);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_box_append(GTK_BOX(box), label);
	GtkWidget *marks = gtk_grid_new();
	gtk_widget_set_halign(marks, GTK_ALIGN_CENTER);
	for (guint mark = 0; mark < FILEDATA_MARKS_SIZE; mark++)
		{
		GtkWidget *button = gtk_check_button_new();
		gtk_widget_add_css_class(button, "marks-filter-button");
		g_object_set_data(G_OBJECT(button), "view-file-mark", GUINT_TO_POINTER(mark));
		g_signal_connect(button, "toggled", G_CALLBACK(vficon_mark_toggled_cb), nullptr);
		gtk_grid_attach(GTK_GRID(marks), button, mark % 5, mark / 5, 1, 1);
		}
	gtk_box_append(GTK_BOX(box), marks);

	g_object_set_data(G_OBJECT(box), "view-file-picture", picture);
	g_object_set_data(G_OBJECT(box), "view-file-label", label);
	g_object_set_data(G_OBJECT(box), "view-file-marks", marks);
	gtk_list_item_set_child(list_item, box);
}

static void vficon_factory_bind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	auto *item = static_cast<ViewFileIconItem *>(gtk_list_item_get_item(list_item));
	GtkWidget *child = gtk_list_item_get_child(list_item);
	const gulong handler_id = g_signal_connect(item, "changed", G_CALLBACK(vficon_item_update), child);
	g_object_set_data(G_OBJECT(list_item), "view-file-changed-handler", GSIZE_TO_POINTER(handler_id));
	vficon_item_update(item, child);
}

static void vficon_factory_unbind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	auto *item = static_cast<ViewFileIconItem *>(gtk_list_item_get_item(list_item));
	const gulong handler_id = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(list_item), "view-file-changed-handler"));
	if (item && handler_id) g_signal_handler_disconnect(item, handler_id);
	g_object_set_data(G_OBJECT(list_item), "view-file-changed-handler", nullptr);
	g_object_set_data(G_OBJECT(gtk_list_item_get_child(list_item)), "view-file-fd", nullptr);
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
	g_clear_object(&VFICON(vf)->store);
}

ViewFile *vficon_new(ViewFile *vf)
{
	vf->info = g_new0(ViewFileInfoIcon, 1);

	VFICON(vf)->show_text = options->show_icon_names;
	VFICON(vf)->columns = 1;
	VFICON(vf)->store = g_list_store_new(view_file_icon_item_get_type());
	GtkNoSelection *selection = gtk_no_selection_new(G_LIST_MODEL(g_object_ref(VFICON(vf)->store)));
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(vficon_factory_setup), vf);
	g_signal_connect(factory, "bind", G_CALLBACK(vficon_factory_bind), nullptr);
	g_signal_connect(factory, "unbind", G_CALLBACK(vficon_factory_unbind), nullptr);
	vf->listview = gtk_grid_view_new(GTK_SELECTION_MODEL(selection), factory);
	gtk_grid_view_set_single_click_activate(GTK_GRID_VIEW(vf->listview), FALSE);
	gtk_grid_view_set_min_columns(GTK_GRID_VIEW(vf->listview), 1);
	gtk_grid_view_set_max_columns(GTK_GRID_VIEW(vf->listview), 1);
	g_signal_connect(vf->listview, "notify::has-focus", G_CALLBACK(vficon_focus_changed_cb), vf);

	g_signal_connect(G_OBJECT(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(vf->scrolled))), "notify::page-size",
			 G_CALLBACK(vficon_sized_cb), vf);

	GtkEventController *controller = gtk_event_controller_motion_new();
	g_signal_connect(controller, "motion", G_CALLBACK(vficon_motion_cb), vf);
	gtk_widget_add_controller(vf->listview, controller);

	GtkEventController *motion_controller = gtk_event_controller_motion_new();
	g_signal_connect(motion_controller, "leave", G_CALLBACK(vficon_leave_cb), vf);
	gtk_widget_add_controller(vf->listview, motion_controller);

	/* force VFICON(vf)->columns to be at least 1 (sane) - this will be corrected in the size_cb */
	vficon_populate_at_new_size(vf, 1, 1, FALSE);

	file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);

	return vf;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
