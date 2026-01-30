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

#include "collect-table.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include <glib-object.h>

#include "cellrenderericon.h"
#include "collect-dlg.h"
#include "collect-io.h"
#include "collect.h"
#include "compat-deprecated.h"
#include "compat.h"
#include "dnd.h"
#include "dupe.h"
#include "filedata.h"
#include "geometry.h"
#include "img-view.h"
#include "intl.h"
#include "layout-image.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "print.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "uri-utils.h"
#include "utilops.h"
#include "view-file.h"

namespace
{

enum {
	CTABLE_COLUMN_POINTER = 0,
	CTABLE_COLUMN_COUNT
};

struct ColumnData
{
	CollectTable *ct;
	gint number;
};

/* between these, the icon width is increased by thumb_max_width / 2 */
constexpr gint THUMB_MIN_ICON_WIDTH = 128;
constexpr gint THUMB_MAX_ICON_WIDTH = 150;

constexpr gint COLLECT_TABLE_MAX_COLUMNS = 32;

constexpr gint THUMB_BORDER_PADDING = 2;

constexpr gint COLLECT_TABLE_TIP_DELAY = 500;
constexpr gint COLLECT_TABLE_TIP_DELAY_PATH = 850;

constexpr std::array<GtkTargetEntry, 3> collection_drag_types{{
	{ const_cast<gchar *>(TARGET_APP_COLLECTION_MEMBER_STRING), 0, TARGET_APP_COLLECTION_MEMBER },
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
}};

constexpr std::array<GtkTargetEntry, 2> collection_drop_types{{
	{ const_cast<gchar *>(TARGET_APP_COLLECTION_MEMBER_STRING), 0, TARGET_APP_COLLECTION_MEMBER },
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST }
}};

inline gboolean info_selected(const CollectInfo *info)
{
	return info->flag_mask & SELECTION_SELECTED;
}

} // namespace

static void collection_table_populate_at_new_size(CollectTable *ct, gint w, gint h, gboolean force);

/**
 * This array must be kept in sync with the contents of:\n
 * @link collection_table_press_key_cb @endlink \n
 * @link collection_window_keypress @endlink \n
 * @link collection_table_popup_menu @endlink
 *
 * See also @link HardcodedWindowKey @endlink
 **/
static HardcodedWindowKeyList collection_window_keys{
	{GDK_CONTROL_MASK, 'C', N_("Copy")},
	{GDK_CONTROL_MASK, 'M', N_("Move")},
	{GDK_CONTROL_MASK, 'R', N_("Rename")},
	{GDK_CONTROL_MASK, 'D', N_("Move selection to Trash")},
	{GDK_CONTROL_MASK, 'W', N_("Close window")},
	{static_cast<GdkModifierType>(0), GDK_KEY_Delete, N_("Remove")},
	{static_cast<GdkModifierType>(0), GDK_KEY_Return, N_("View")},
	{static_cast<GdkModifierType>(0), 'V', N_("View in new window")},
	{GDK_CONTROL_MASK, 'A', N_("Select all")},
	{static_cast<GdkModifierType>(GDK_CONTROL_MASK + GDK_SHIFT_MASK), 'A', N_("Select none")},
	{GDK_MOD1_MASK, 'R', N_("Rectangular selection")},
	{static_cast<GdkModifierType>(0), GDK_KEY_space, N_("Select single file")},
	{GDK_CONTROL_MASK, GDK_KEY_space, N_("Toggle select image")},
	{GDK_CONTROL_MASK, 'L', N_("Append from file selection")},
	{static_cast<GdkModifierType>(0), 'A', N_("Append from collection")},
	{static_cast<GdkModifierType>(0), 'S', N_("Save collection")},
	{GDK_CONTROL_MASK, 'S', N_("Save collection as")},
	{GDK_CONTROL_MASK, 'T', N_("Show filename text")},
	{GDK_CONTROL_MASK, 'I', N_("Show infotext")},
	{static_cast<GdkModifierType>(0), 'N', N_("Sort by name")},
	{static_cast<GdkModifierType>(0), 'D', N_("Sort by date")},
	{static_cast<GdkModifierType>(0), 'B', N_("Sort by size")},
	{static_cast<GdkModifierType>(0), 'P', N_("Sort by path")},
	{GDK_SHIFT_MASK, 'P', N_("Print")},
	{GDK_MOD1_MASK, 'A', N_("Append (Append collection dialog)")},
	{GDK_MOD1_MASK, 'D', N_("Discard (Close modified collection dialog)")},
};

/*
 *-------------------------------------------------------------------
 * more misc
 *-------------------------------------------------------------------
 */

static gboolean collection_table_find_position(CollectTable *ct, CollectInfo *info, gint *row, gint *col)
{
	gint n;

	n = g_list_index(ct->cd->list, info);

	if (n < 0) return FALSE;

	*row = n / ct->columns;
	*col = n - (*row * ct->columns);

	return TRUE;
}

static gboolean collection_table_find_iter(CollectTable *ct, CollectInfo *info, GtkTreeIter *iter, gint *column)
{
	GtkTreeModel *store;
	gint row;
	gint col;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));
	if (!collection_table_find_position(ct, info, &row, &col)) return FALSE;
	if (!gtk_tree_model_iter_nth_child(store, iter, nullptr, row)) return FALSE;
	if (column) *column = col;

	return TRUE;
}

static CollectInfo *collection_table_find_data(CollectTable *ct, gint row, gint col, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	GtkTreeIter p;

	if (row < 0 || col < 0) return nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));
	if (gtk_tree_model_iter_nth_child(store, &p, nullptr, row))
		{
		GList *list;

		gtk_tree_model_get(store, &p, CTABLE_COLUMN_POINTER, &list, -1);
		if (!list) return nullptr;

		if (iter) *iter = p;

		return static_cast<CollectInfo *>(g_list_nth_data(list, col));
		}

	return nullptr;
}

static CollectInfo *collection_table_find_data_by_coord(CollectTable *ct, gint x, gint y, GtkTreeIter *iter)
{
	GtkTreeViewColumn *column;
	GtkTreeModel *store;
	GtkTreeIter row;
	GList *list;
	gint n;

	g_autoptr(GtkTreePath) tpath = nullptr;
	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(ct->listview), x, y,
					   &tpath, &column, nullptr, nullptr))
		return nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));
	gtk_tree_model_get_iter(store, &row, tpath);

	gtk_tree_model_get(store, &row, CTABLE_COLUMN_POINTER, &list, -1);
	if (!list) return nullptr;

	n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_number"));
	if (iter) *iter = row;
	return static_cast<CollectInfo *>(g_list_nth_data(list, n));
}

static guint collection_list_count(GList *list, gint64 &bytes)
{
	struct ListSize
	{
		gint64 bytes;
		guint count;
	} ls{0, 0};

	static const auto inc_list_size = [](gpointer data, gpointer user_data)
	{
		auto *ci = static_cast<CollectInfo *>(data);
		auto *ls = static_cast<ListSize *>(user_data);

		ls->bytes += ci->fd->size;
		ls->count++;
	};

	g_list_foreach(list, inc_list_size, &ls);

	bytes = ls.bytes;
	return ls.count;
}

static void collection_table_update_status(CollectTable *ct)
{
	if (!ct->status_label) return;

	gint64 n_bytes = 0;
	const guint n = collection_list_count(ct->cd->list, n_bytes);

	g_autoptr(GString) buf = g_string_new(nullptr);
	if (n > 0)
		{
		g_autofree gchar *b = text_from_size_abrev(n_bytes);
		g_string_append_printf(buf, _("%s, %d images"), b, n);

		gint64 s_bytes = 0;
		const guint s = collection_list_count(ct->selection, s_bytes);
		if (s > 0)
			{
			g_autofree gchar *sb = text_from_size_abrev(s_bytes);
			g_string_append_printf(buf, " (%s, %d)", sb, s);
			}
		}
	else
		{
		buf = g_string_append(buf, _("Empty"));
		}

	gtk_label_set_text(GTK_LABEL(ct->status_label), buf->str);
}

static void collection_table_update_extras(CollectTable *ct, gboolean loading, gdouble value)
{
	const gchar *text;

	if (!ct->extra_label) return;

	if (loading)
		text = _("Loading thumbs...");
	else
		text = " ";

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ct->extra_label), value);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ct->extra_label), text);
}

static void collection_table_toggle_filenames(CollectTable *ct)
{
	GtkAllocation allocation;
	ct->show_text = !ct->show_text;
	options->show_icon_names = ct->show_text;

	gtk_widget_get_allocation(ct->listview, &allocation);
	collection_table_populate_at_new_size(ct, allocation.width, allocation.height, TRUE);
}

static void collection_table_toggle_stars(CollectTable *ct)
{
	GtkAllocation allocation;
	ct->show_stars = !ct->show_stars;
	options->show_star_rating = ct->show_stars;

	gtk_widget_get_allocation(ct->listview, &allocation);
	collection_table_populate_at_new_size(ct, allocation.width, allocation.height, TRUE);
}

static void collection_table_toggle_info(CollectTable *ct)
{
	GtkAllocation allocation;
	ct->show_infotext = !ct->show_infotext;
	options->show_collection_infotext = ct->show_infotext;

	gtk_widget_get_allocation(ct->listview, &allocation);
	collection_table_populate_at_new_size(ct, allocation.width, allocation.height, TRUE);
}

static gint collection_table_get_icon_width(CollectTable *ct)
{
	gint width;

	if (!ct->show_text && !ct->show_infotext) return options->thumbnails.max_width;

	width = options->thumbnails.max_width + options->thumbnails.max_width / 2;
	width = std::max(width, THUMB_MIN_ICON_WIDTH);
	if (width > THUMB_MAX_ICON_WIDTH) width = options->thumbnails.max_width;

	return width;
}

/*
 *-------------------------------------------------------------------
 * cell updates
 *-------------------------------------------------------------------
 */

static void collection_table_selection_set(CollectTable *ct, CollectInfo *info, SelectionType value, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	GList *list;

	if (!info) return;

	if (info->flag_mask == value) return;
	info->flag_mask = value;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));
	if (iter)
		{
		gtk_tree_model_get(store, iter, CTABLE_COLUMN_POINTER, &list, -1);
		if (list) gtk_list_store_set(GTK_LIST_STORE(store), iter, CTABLE_COLUMN_POINTER, list, -1);
		}
	else
		{
		GtkTreeIter row;

		if (collection_table_find_iter(ct, info, &row, nullptr))
			{
			gtk_tree_model_get(store, &row, CTABLE_COLUMN_POINTER, &list, -1);
			if (list) gtk_list_store_set(GTK_LIST_STORE(store), &row, CTABLE_COLUMN_POINTER, list, -1);
			}
		}
}

static void collection_table_selection_add(CollectTable *ct, CollectInfo *info, SelectionType mask, GtkTreeIter *iter)
{
	if (!info) return;

	collection_table_selection_set(ct, info, static_cast<SelectionType>(info->flag_mask | mask), iter);
}

static void collection_table_selection_remove(CollectTable *ct, CollectInfo *info, SelectionType mask, GtkTreeIter *iter)
{
	if (!info) return;

	collection_table_selection_set(ct, info, static_cast<SelectionType>(info->flag_mask & ~mask), iter);
}
/*
 *-------------------------------------------------------------------
 * selections
 *-------------------------------------------------------------------
 */

static void collection_table_verify_selections(CollectTable *ct)
{
	GList *work;

	work = ct->selection;
	while (work)
		{
		auto info = static_cast<CollectInfo *>(work->data);
		work = work->next;
		if (!g_list_find(ct->cd->list, info))
			{
			ct->selection = g_list_remove(ct->selection, info);
			}
		}
}

void collection_table_select_all(CollectTable *ct)
{
	GList *work;

	g_list_free(ct->selection);
	ct->selection = nullptr;

	work = ct->cd->list;
	while (work)
		{
		ct->selection = g_list_append(ct->selection, work->data);
		collection_table_selection_add(ct, static_cast<CollectInfo *>(work->data), SELECTION_SELECTED, nullptr);
		work = work->next;
		}

	collection_table_update_status(ct);
}

void collection_table_unselect_all(CollectTable *ct)
{
	GList *work;

	work = ct->selection;
	while (work)
		{
		collection_table_selection_remove(ct, static_cast<CollectInfo *>(work->data), SELECTION_SELECTED, nullptr);
		work = work->next;
		}

	g_list_free(ct->selection);
	ct->selection = nullptr;

	collection_table_update_status(ct);
}

/* Invert the current collection's selection */
static void collection_table_select_invert_all(CollectTable *ct)
{
	GList *work;
	GList *new_selection = nullptr;

	work = ct->cd->list;
	while (work)
		{
		auto info = static_cast<CollectInfo *>(work->data);

		if (info_selected(info))
			{
			collection_table_selection_remove(ct, info, SELECTION_SELECTED, nullptr);
			}
		else
			{
			new_selection = g_list_append(new_selection, info);
			collection_table_selection_add(ct, info, SELECTION_SELECTED, nullptr);

			}

		work = work->next;
		}

	g_list_free(ct->selection);
	ct->selection = new_selection;

	collection_table_update_status(ct);
}

void collection_table_select(CollectTable *ct, CollectInfo *info)
{
	ct->prev_selection = info;

	if (!info || info_selected(info)) return;

	ct->selection = g_list_append(ct->selection, info);
	collection_table_selection_add(ct, info, SELECTION_SELECTED, nullptr);

	collection_table_update_status(ct);
}

static void collection_table_unselect(CollectTable *ct, CollectInfo *info)
{
	ct->prev_selection = info;

	if (!info || !info_selected(info) ) return;

	ct->selection = g_list_remove(ct->selection, info);
	collection_table_selection_remove(ct, info, SELECTION_SELECTED, nullptr);

	collection_table_update_status(ct);
}

static void collection_table_select_util(CollectTable *ct, CollectInfo *info, gboolean select)
{
	if (select)
		{
		collection_table_select(ct, info);
		}
	else
		{
		collection_table_unselect(ct, info);
		}
}

static void collection_table_select_region_util(CollectTable *ct, CollectInfo *start, CollectInfo *end, gboolean select)
{
	gint row1;
	gint col1;
	gint row2;
	gint col2;
	gint i;
	gint j;

	if (!collection_table_find_position(ct, start, &row1, &col1) ||
	    !collection_table_find_position(ct, end, &row2, &col2) ) return;

	ct->prev_selection = end;

	if (!options->collections.rectangular_selection)
		{
		GList *work;
		CollectInfo *info;

		if (g_list_index(ct->cd->list, start) > g_list_index(ct->cd->list, end))
			{
			info = start;
			start = end;
			end = info;
			}

		work = g_list_find(ct->cd->list, start);
		while (work)
			{
			info = static_cast<CollectInfo *>(work->data);
			collection_table_select_util(ct, info, select);

			if (work->data != end)
				work = work->next;
			else
				work = nullptr;
			}
		return;
		}

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
			CollectInfo *info = collection_table_find_data(ct, i, j, nullptr);
			if (info) collection_table_select_util(ct, info, select);
			}
		}
}

GList *collection_table_selection_get_list(CollectTable *ct)
{
	return collection_list_to_filelist(ct->selection);
}

/*
 *-------------------------------------------------------------------
 * tooltip type window
 *-------------------------------------------------------------------
 */

static void tip_show(CollectTable *ct)
{
	GtkWidget *label;
	gint x;
	gint y;
	GdkDisplay *display;
	GdkSeat *seat;
	GdkDevice *device;

	if (ct->tip_window) return;

	seat = gdk_display_get_default_seat(gdk_window_get_display(gtk_widget_get_window(ct->listview)));
	device = gdk_seat_get_pointer(seat);
	get_pointer_position(ct->listview, device, &x, &y, nullptr);

	ct->tip_info = collection_table_find_data_by_coord(ct, x, y, nullptr);
	if (!ct->tip_info) return;

	ct->tip_window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_resizable(GTK_WINDOW(ct->tip_window), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(ct->tip_window), 2);

	label = gtk_label_new(ct->show_text ? ct->tip_info->fd->path : ct->tip_info->fd->name);

	g_object_set_data(G_OBJECT(ct->tip_window), "tip_label", label);
	gq_gtk_container_add(ct->tip_window, label);
	gtk_widget_show(label);

	display = gdk_display_get_default();
	seat = gdk_display_get_default_seat(display);
	device = gdk_seat_get_pointer(seat);
	get_device_position(device, x, y);

	if (!gtk_widget_get_realized(ct->tip_window)) gtk_widget_realize(ct->tip_window);
	gq_gtk_window_move(GTK_WINDOW(ct->tip_window), x + 16, y + 16);
	gtk_widget_show(ct->tip_window);
}

static void tip_hide(CollectTable *ct)
{
	if (ct->tip_window) gq_gtk_widget_destroy(ct->tip_window);
	ct->tip_window = nullptr;
}

static gboolean tip_schedule_cb(gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (ct->tip_delay_id)
		{
		tip_show(ct);

		ct->tip_delay_id = 0;
		}

	return G_SOURCE_REMOVE;
}

static void tip_unschedule(CollectTable *ct)
{
	tip_hide(ct);

	g_clear_handle_id(&ct->tip_delay_id, g_source_remove);
}

static void tip_schedule(CollectTable *ct)
{
	tip_unschedule(ct);

	ct->tip_delay_id = g_timeout_add(ct->show_text ? COLLECT_TABLE_TIP_DELAY_PATH : COLLECT_TABLE_TIP_DELAY, tip_schedule_cb, ct);
}

static void tip_update(CollectTable *ct, CollectInfo *info)
{
	GdkDisplay *display = gdk_display_get_default();
	GdkSeat *seat = gdk_display_get_default_seat(display);
	GdkDevice *device = gdk_seat_get_pointer(seat);

	tip_schedule(ct);

	if (ct->tip_window)
		{
		gint x;
		gint y;
		get_device_position(device, x, y);

		gq_gtk_window_move(GTK_WINDOW(ct->tip_window), x + 16, y + 16);

		if (info != ct->tip_info)
			{
			GtkWidget *label;

			ct->tip_info = info;

			if (!ct->tip_info)
				{
				return;
				}

			label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(ct->tip_window), "tip_label"));
			gtk_label_set_text(GTK_LABEL(label), ct->show_text ? ct->tip_info->fd->path : ct->tip_info->fd->name);
			}
		}
}

/*
 *-------------------------------------------------------------------
 * popup menus
 *-------------------------------------------------------------------
 */

static void collection_table_popup_save_as_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_dialog_save(ct->cd);
}

static void collection_table_popup_save_cb(GtkWidget *widget, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (!ct->cd->path)
		{
		collection_table_popup_save_as_cb(widget, data);
		return;
		}

	if (!collection_save(ct->cd, ct->cd->path))
		{
		log_printf("failed saving to collection path: %s\n", ct->cd->path);
		}
}

static GList *collection_table_popup_file_list(CollectTable *ct)
{
	if (!ct->click_info) return nullptr;

	if (info_selected(ct->click_info))
		{
		return collection_table_selection_get_list(ct);
		}

	return g_list_append(nullptr, file_data_ref(ct->click_info->fd));
}

static void collection_table_popup_edit_cb(GtkWidget *widget, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(submenu_item_get_data(widget));
	if (!ct) return;

	auto *key = static_cast<const gchar *>(data);

	file_util_start_editor_from_filelist(key, collection_table_popup_file_list(ct), nullptr, ct->listview);
}

static void collection_table_popup_copy_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_copy(nullptr, collection_table_popup_file_list(ct), nullptr, ct->listview);
}

static void collection_table_popup_move_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_move(nullptr, collection_table_popup_file_list(ct), nullptr, ct->listview);
}

static void collection_table_popup_rename_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_rename(nullptr, collection_table_popup_file_list(ct), ct->listview);
}

template<gboolean safe_delete>
static void collection_table_popup_delete_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_delete(nullptr, collection_table_popup_file_list(ct), ct->listview, safe_delete);

	collection_table_refresh(ct);
}

template<gboolean quoted>
static void collection_table_popup_copy_path_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_path_list_to_clipboard(collection_table_popup_file_list(ct), quoted, ClipboardAction::COPY);
}

static void collection_table_popup_sort_cb(GtkWidget *widget, gpointer data)
{
	CollectTable *ct;
	SortType type;

	ct = static_cast<CollectTable *>(submenu_item_get_data(widget));

	if (!ct) return;

	type = static_cast<SortType>GPOINTER_TO_INT(data);

	collection_set_sort_method(ct->cd, type);
}

static void collection_table_popup_randomize_cb(GtkWidget *widget, gpointer)
{
	CollectTable *ct;

	ct = static_cast<CollectTable *>(submenu_item_get_data(widget));

	if (!ct) return;

	collection_randomize(ct->cd);
}

static void collection_table_popup_view_new_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (ct->click_info && g_list_find(ct->cd->list, ct->click_info))
		{
		view_window_new_from_collection(ct->cd, ct->click_info);
		}
}

static void collection_table_popup_view_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (ct->click_info && g_list_find(ct->cd->list, ct->click_info))
		{
		layout_image_set_collection(nullptr, ct->cd, ct->click_info);
		}
}

static void collection_table_popup_selectall_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_select_all(ct);
	ct->prev_selection= ct->click_info;
}

static void collection_table_popup_unselectall_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_unselect_all(ct);
	ct->prev_selection= ct->click_info;
}

static void collection_table_popup_select_invert_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_select_invert_all(ct);
	ct->prev_selection= ct->click_info;
}

static void collection_table_popup_rectangular_selection_cb(GtkWidget *, gpointer)
{
	options->collections.rectangular_selection = !(options->collections.rectangular_selection);
}

static void collection_table_popup_remove_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	GList *list;

	if (!ct->click_info) return;

	if (info_selected(ct->click_info))
		{
		list = g_list_copy(ct->selection);
		}
	else
		{
		list = g_list_append(nullptr, ct->click_info);
		}

	collection_remove_by_info_list(ct->cd, list);
	collection_table_refresh(ct);
	g_list_free(list);
}

static void collection_table_popup_add_file_selection_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	LayoutWindow *lw = get_current_layout();
	if (!lw) return;

	g_autoptr(FileDataList) list = vf_selection_get_list(lw->vf);
	if (!list) return;

	collection_table_add_filelist(ct, list);
}

static void collection_table_popup_add_collection_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_dialog_append(ct->cd);
}

static void collection_table_popup_goto_original_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	GList *list;
	FileData *fd;

	LayoutWindow *lw = get_current_layout();
	if (!lw) return;

	list = collection_table_selection_get_list(ct);
	if (list)
		{
		fd = static_cast<FileData *>(list->data);
		if (fd)
			{
			layout_set_fd(lw, fd);
			}
		}
	g_list_free(list);
}

static void collection_table_popup_find_dupes_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	DupeWindow *dw;

	dw = dupe_window_new();
	dupe_window_add_collection(dw, ct->cd);
}

static void collection_table_popup_print_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	print_window_new(collection_table_selection_get_list(ct), widget_get_toplevel(ct->listview));
}

static void collection_table_popup_show_names_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_toggle_filenames(ct);
}

static void collection_table_popup_show_stars_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_toggle_stars(ct);
}

static void collection_table_popup_show_infotext_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_toggle_info(ct);
}

static void collection_table_popup_destroy_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_selection_remove(ct, ct->click_info, SELECTION_PRELIGHT, nullptr);
	ct->click_info = nullptr;
	ct->popup = nullptr;

	file_data_list_free(ct->drop_list);
	ct->drop_list = nullptr;
	ct->drop_info = nullptr;

	file_data_list_free(ct->editmenu_fd_list);
	ct->editmenu_fd_list = nullptr;
}

static GtkWidget *collection_table_popup_menu(CollectTable *ct, gboolean over_icon)
{
	GtkWidget *menu;
	GtkWidget *item;
	GtkWidget *submenu;
 	GtkAccelGroup *accel_group;

	menu = popup_menu_short_lived();

	accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "window_keys", &collection_window_keys);
	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(collection_table_popup_destroy_cb), ct);

	menu_item_add_sensitive(menu, _("_View"), over_icon,
			G_CALLBACK(collection_table_popup_view_cb), ct);
	menu_item_add_icon_sensitive(menu, _("View in _new window"), GQ_ICON_NEW, over_icon,
			G_CALLBACK(collection_table_popup_view_new_cb), ct);
	menu_item_add_icon(menu, _("Go to original"), GQ_ICON_FIND,
			G_CALLBACK(collection_table_popup_goto_original_cb), ct);
	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("Rem_ove"), GQ_ICON_REMOVE, over_icon,
			G_CALLBACK(collection_table_popup_remove_cb), ct);

	menu_item_add_icon(menu, _("Append from file selection"), GQ_ICON_ADD,
			G_CALLBACK(collection_table_popup_add_file_selection_cb), ct);
	menu_item_add_icon(menu, _("Append from collection..."), GQ_ICON_OPEN,
			G_CALLBACK(collection_table_popup_add_collection_cb), ct);
	menu_item_add_divider(menu);

	item = menu_item_add(menu, _("_Selection"), nullptr, nullptr);
	submenu = gtk_menu_new();
	menu_item_add(submenu, _("Select all"),
			G_CALLBACK(collection_table_popup_selectall_cb), ct);
	menu_item_add(submenu, _("Select none"),
			G_CALLBACK(collection_table_popup_unselectall_cb), ct);
	menu_item_add(submenu, _("Invert selection"),
			G_CALLBACK(collection_table_popup_select_invert_cb), ct);
	menu_item_add_check(submenu, _("Rectangular selection"), (options->collections.rectangular_selection != FALSE),
			G_CALLBACK(collection_table_popup_rectangular_selection_cb), ct);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	menu_item_add_divider(menu);


	ct->editmenu_fd_list = collection_table_selection_get_list(ct);
	submenu_add_edit(menu, over_icon, ct->editmenu_fd_list,
	                 G_CALLBACK(collection_table_popup_edit_cb), ct);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("_Copy..."), GQ_ICON_COPY, over_icon,
			G_CALLBACK(collection_table_popup_copy_cb), ct);
	menu_item_add_sensitive(menu, _("_Move..."), over_icon,
			G_CALLBACK(collection_table_popup_move_cb), ct);
	menu_item_add_sensitive(menu, _("_Rename..."), over_icon,
			G_CALLBACK(collection_table_popup_rename_cb), ct);
	menu_item_add_sensitive(menu, _("_Copy path"), over_icon,
	                        G_CALLBACK(collection_table_popup_copy_path_cb<TRUE>), ct);
	menu_item_add_sensitive(menu, _("_Copy path unquoted"), over_icon,
	                        G_CALLBACK(collection_table_popup_copy_path_cb<FALSE>), ct);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, options->file_ops.confirm_move_to_trash ?
	                                 _("Move selection to Trash...") : _("Move selection to Trash"),
	                             GQ_ICON_DELETE, over_icon,
	                             G_CALLBACK(collection_table_popup_delete_cb<TRUE>), ct);
	menu_item_add_icon_sensitive(menu, options->file_ops.confirm_delete ?
	                                 _("_Delete selection...") : _("_Delete selection"),
	                             GQ_ICON_DELETE_SHRED, over_icon,
	                             G_CALLBACK(collection_table_popup_delete_cb<FALSE>), ct);

	menu_item_add_divider(menu);
	submenu = submenu_add_sort(menu, G_CALLBACK(collection_table_popup_sort_cb), ct, FALSE, SORT_NONE);
	menu_item_add(submenu, sort_type_get_text(SORT_PATH),
	              G_CALLBACK(collection_table_popup_sort_cb), GINT_TO_POINTER(SORT_PATH));
	menu_item_add_divider(submenu);
	menu_item_add(submenu, _("Randomize"),
			G_CALLBACK(collection_table_popup_randomize_cb), ct);

	menu_item_add_check(menu, _("Show filename _text"), ct->show_text,
			G_CALLBACK(collection_table_popup_show_names_cb), ct);
	menu_item_add_check(menu, _("Show star rating"), ct->show_stars,
				G_CALLBACK(collection_table_popup_show_stars_cb), ct);
	menu_item_add_check(menu, _("Show infotext"), ct->show_infotext,
			G_CALLBACK(collection_table_popup_show_infotext_cb), ct);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("_Save collection"), GQ_ICON_SAVE,
			G_CALLBACK(collection_table_popup_save_cb), ct);
	menu_item_add_icon(menu, _("Save collection _as..."), GQ_ICON_SAVE_AS,
			G_CALLBACK(collection_table_popup_save_as_cb), ct);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("_Find duplicates..."), GQ_ICON_FIND,
			G_CALLBACK(collection_table_popup_find_dupes_cb), ct);
	menu_item_add_icon_sensitive(menu, _("Print..."), GQ_ICON_PRINT, over_icon,
			G_CALLBACK(collection_table_popup_print_cb), ct);

	return menu;
}
/*
 *-------------------------------------------------------------------
 * keyboard callbacks
 *-------------------------------------------------------------------
 */

void collection_table_set_focus(CollectTable *ct, CollectInfo *info)
{
	GtkTreeIter iter;
	gint row;
	gint col;

	if (g_list_find(ct->cd->list, ct->focus_info))
		{
		if (info == ct->focus_info)
			{
			/* ensure focus row col are correct */
			collection_table_find_position(ct, ct->focus_info,
						       &ct->focus_row, &ct->focus_column);
			return;
			}
		collection_table_selection_remove(ct, ct->focus_info, SELECTION_FOCUS, nullptr);
		}

	if (!collection_table_find_position(ct, info, &row, &col))
		{
		ct->focus_info = nullptr;
		ct->focus_row = -1;
		ct->focus_column = -1;
		return;
		}

	ct->focus_info = info;
	ct->focus_row = row;
	ct->focus_column = col;
	collection_table_selection_add(ct, ct->focus_info, SELECTION_FOCUS, nullptr);

	if (collection_table_find_iter(ct, ct->focus_info, &iter, nullptr))
		{
		GtkTreeViewColumn *column;
		GtkTreeModel *store;

		tree_view_row_make_visible(GTK_TREE_VIEW(ct->listview), &iter, FALSE);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));
		g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
		/* focus is set to an extra column with 0 width to hide focus, we draw it ourself */
		column = gtk_tree_view_get_column(GTK_TREE_VIEW(ct->listview), COLLECT_TABLE_MAX_COLUMNS);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(ct->listview), tpath, column, FALSE);
		}
}

static void collection_table_move_focus(CollectTable *ct, gint row, gint col, gboolean relative)
{
	gint new_row;
	gint new_col;

	if (relative)
		{
		new_row = std::clamp(ct->focus_row + row, 0, ct->rows - 1);
		new_col = ct->focus_column;

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
					new_col = ct->columns - 1;
					}
				else
					{
					new_col = 0;
					}
				}
			if (new_col >= ct->columns)
				{
				if (new_row < ct->rows - 1)
					{
					new_row++;
					new_col = 0;
					}
				else
					{
					new_col = ct->columns - 1;
					}
				}
			}
		}
	else
		{
		new_row = row;
		new_col = col;

		if (new_row >= ct->rows)
			{
			if (ct->rows > 0)
				new_row = ct->rows - 1;
			else
				new_row = 0;
			new_col = ct->columns - 1;
			}
		if (new_col >= ct->columns) new_col = ct->columns - 1;
		}

	if (new_row == ct->rows - 1)
		{
		gint l;

		/* if we moved beyond the last image, go to the last image */

		l = g_list_length(ct->cd->list);
		if (ct->rows > 1) l -= (ct->rows - 1) * ct->columns;
		if (new_col >= l) new_col = l - 1;
		}

	if (new_row == -1 || new_col == -1)
		{
		if (!ct->cd->list) return;
		new_row = new_col = 0;
		}

	collection_table_set_focus(ct, collection_table_find_data(ct, new_row, new_col, nullptr));
}

static void collection_table_update_focus(CollectTable *ct)
{
	gint new_row = 0;
	gint new_col = 0;

	if (ct->focus_info && collection_table_find_position(ct, ct->focus_info, &new_row, &new_col))
		{
		/* first find the old focus, if it exists and is valid */
		}
	else
		{
		/* (try to) stay where we were */
		new_row = ct->focus_row;
		new_col = ct->focus_column;
		}

	collection_table_move_focus(ct, new_row, new_col, FALSE);
}

/* used to figure the page up/down distances */
static gint page_height(CollectTable *ct)
{
	GtkAdjustment *adj;
	gint page_size;
	gint row_height;
	gint ret;

	adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(ct->listview));
	page_size = static_cast<gint>(gtk_adjustment_get_page_increment(adj));

	row_height = options->thumbnails.max_height + THUMB_BORDER_PADDING * 2;
	if (ct->show_text) row_height += options->thumbnails.max_height / 3;
	if (ct->show_infotext) row_height += options->thumbnails.max_height / 3;

	ret = page_size / row_height;
	ret = std::max(ret, 1);

	return ret;
}

static gboolean collection_table_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	gint focus_row = 0;
	gint focus_col = 0;
	CollectInfo *info;
	gboolean stop_signal = TRUE;

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
			focus_row = -page_height(ct);
			break;
		case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
			focus_row = page_height(ct);
			break;
		case GDK_KEY_Home: case GDK_KEY_KP_Home:
			focus_row = -ct->focus_row;
			focus_col = -ct->focus_column;
			break;
		case GDK_KEY_End: case GDK_KEY_KP_End:
			focus_row = ct->rows - 1 - ct->focus_row;
			focus_col = ct->columns - 1 - ct->focus_column;
			break;
		case GDK_KEY_space:
			info = collection_table_find_data(ct, ct->focus_row, ct->focus_column, nullptr);
			if (info)
				{
				ct->click_info = info;
				if (event->state & GDK_CONTROL_MASK)
					{
					collection_table_select_util(ct, info, !info_selected(info));
					}
				else
					{
					collection_table_unselect_all(ct);
					collection_table_select(ct, info);
					}
				}
			break;
		case 'T': case 't':
			if (event->state & GDK_CONTROL_MASK) collection_table_toggle_filenames(ct);
			break;
		case 'I': case 'i':
			if (event->state & GDK_CONTROL_MASK) collection_table_toggle_info(ct);
			break;
		case GDK_KEY_Menu:
		case GDK_KEY_F10:
			info = collection_table_find_data(ct, ct->focus_row, ct->focus_column, nullptr);
			ct->click_info = info;

			collection_table_selection_add(ct, ct->click_info, SELECTION_PRELIGHT, nullptr);
			tip_unschedule(ct);

			ct->popup = collection_table_popup_menu(ct, (info != nullptr));
			gtk_menu_popup_at_widget(GTK_MENU(ct->popup), widget, GDK_GRAVITY_SOUTH, GDK_GRAVITY_CENTER, nullptr);

			break;
		default:
			stop_signal = FALSE;
			break;
		}

	if (focus_row != 0 || focus_col != 0)
		{
		CollectInfo *new_info;
		CollectInfo *old_info;

		old_info = collection_table_find_data(ct, ct->focus_row, ct->focus_column, nullptr);
		collection_table_move_focus(ct, focus_row, focus_col, TRUE);
		new_info = collection_table_find_data(ct, ct->focus_row, ct->focus_column, nullptr);

		if (new_info != old_info)
			{
			if (event->state & GDK_SHIFT_MASK)
				{
				if (!options->collections.rectangular_selection)
					{
					collection_table_select_region_util(ct, old_info, new_info, FALSE);
					}
				else
					{
					collection_table_select_region_util(ct, ct->click_info, old_info, FALSE);
					}
				collection_table_select_region_util(ct, ct->click_info, new_info, TRUE);
				}
			else if (event->state & GDK_CONTROL_MASK)
				{
				ct->click_info = new_info;
				}
			else
				{
				ct->click_info = new_info;
				collection_table_unselect_all(ct);
				collection_table_select(ct, new_info);
				}
			}
		}

	if (stop_signal)
		{
		tip_unschedule(ct);
		}

	return stop_signal;
}

/*
 *-------------------------------------------------------------------
 * insert marker
 *-------------------------------------------------------------------
 */

static CollectInfo *collection_table_insert_find(CollectTable *ct, CollectInfo *source, gboolean *after, GdkRectangle *cell,
						 gboolean use_coord, gint x, gint y)
{
	CollectInfo *info = nullptr;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreeViewColumn *column;
	GdkSeat *seat;
	GdkDevice *device;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));

	if (!use_coord)
		{
		seat = gdk_display_get_default_seat(gdk_window_get_display(gtk_widget_get_window(ct->listview)));
		device = gdk_seat_get_pointer(seat);
		get_pointer_position(ct->listview, device, &x, &y, nullptr);
		}

	if (source)
		{
		gint col;
		if (collection_table_find_iter(ct, source, &iter, &col))
			{
			g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
			column = gtk_tree_view_get_column(GTK_TREE_VIEW(ct->listview), col);
			gtk_tree_view_get_background_area(GTK_TREE_VIEW(ct->listview), tpath, column, cell);

			info = source;
			*after = !!(x > cell->x + (cell->width / 2));
			}
		return info;
		}

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(ct->listview), x, y,
	                                  &tpath, &column, nullptr, nullptr))
		{
		GList *list;
		gint n;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, CTABLE_COLUMN_POINTER, &list, -1);

		n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_number"));
		info = static_cast<CollectInfo *>(g_list_nth_data(list, n));

		if (info)
			{
			gtk_tree_view_get_background_area(GTK_TREE_VIEW(ct->listview), tpath, column, cell);
			*after = !!(x > cell->x + (cell->width / 2));
			}
		}

	if (info == nullptr)
		{
		GList *work;

		work = g_list_last(ct->cd->list);
		if (work)
			{
			gint col;

			info = static_cast<CollectInfo *>(work->data);
			*after = TRUE;

			if (collection_table_find_iter(ct, info, &iter, &col))
				{
				g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
				column = gtk_tree_view_get_column(GTK_TREE_VIEW(ct->listview), col);
				gtk_tree_view_get_background_area(GTK_TREE_VIEW(ct->listview), tpath, column, cell);
				}
			}
		}

	return info;
}

static CollectInfo *collection_table_insert_point(CollectTable *ct, gint x, gint y)
{
	CollectInfo *info;
	GdkRectangle cell;
	gboolean after = FALSE;

	info = collection_table_insert_find(ct, nullptr, &after, &cell, TRUE, x, y);

	if (info && after)
		{
		GList *work;

		work = g_list_find(ct->cd->list, info);
		if (work && work->next)
			{
			info = static_cast<CollectInfo *>(work->next->data);
			}
		else
			{
			info = nullptr;
			}
		}

	return info;
}

/*
 *-------------------------------------------------------------------
 * mouse drag auto-scroll
 *-------------------------------------------------------------------
 */

static void collection_table_motion_update(CollectTable *ct, gint x, gint y, gboolean drop_event)
{
	CollectInfo *info;

	info = collection_table_find_data_by_coord(ct, x, y, nullptr);

	if (drop_event)
		{
		tip_unschedule(ct);
		}
	else
		{
		tip_update(ct, info);
		}
}

static gboolean collection_table_auto_scroll_idle_cb(gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (ct->drop_idle_id)
		{
		if (ct->pointer_valid)
			{
			collection_table_motion_update(ct, ct->last_x, ct->last_y, TRUE);
			}

		ct->drop_idle_id = 0;
		}

	return G_SOURCE_REMOVE;
}

static void collection_table_scroll(CollectTable *ct, gboolean scroll)
{
	if (!scroll)
		{
		g_clear_handle_id(&ct->drop_idle_id, g_source_remove);
		widget_auto_scroll_stop(ct->listview);
		}
	else
		{
		const auto collection_table_auto_scroll_notify_cb = [ct](GtkWidget *, GqPoint)
		{
			if (!ct->drop_idle_id)
				{
				ct->drop_idle_id = g_idle_add(collection_table_auto_scroll_idle_cb, ct);
				}

			return true;
		};
		widget_auto_scroll_start(ct->listview, -1, options->thumbnails.max_height / 2,
		                         collection_table_auto_scroll_notify_cb);
		}
}

/*
 *-------------------------------------------------------------------
 * mouse callbacks
 *-------------------------------------------------------------------
 */

static gboolean collection_table_motion_cb(GtkWidget *, GdkEventMotion *event, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_motion_update(ct, static_cast<gint>(event->x), static_cast<gint>(event->y), FALSE);

	return FALSE;
}

static gboolean collection_table_press_cb(GtkWidget *, GdkEventButton *bevent, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	GtkTreeIter iter;
	CollectInfo *info;

	tip_unschedule(ct);

	info = collection_table_find_data_by_coord(ct, static_cast<gint>(bevent->x), static_cast<gint>(bevent->y), &iter);

	ct->click_info = info;
	collection_table_selection_add(ct, ct->click_info, SELECTION_PRELIGHT, &iter);

	switch (bevent->button)
		{
		case GDK_BUTTON_PRIMARY:
			if (bevent->type == GDK_2BUTTON_PRESS)
				{
				if (info)
					{
					layout_image_set_collection(nullptr, ct->cd, info);
					}
				}
			else if (!gtk_widget_has_focus(ct->listview))
				{
				gtk_widget_grab_focus(ct->listview);
				}
			break;
		case GDK_BUTTON_SECONDARY:
			ct->popup = collection_table_popup_menu(ct, (info != nullptr));
			gtk_menu_popup_at_pointer(GTK_MENU(ct->popup), nullptr);
			break;
		default:
			break;
		}

	return TRUE;
}

static gboolean collection_table_release_cb(GtkWidget *, GdkEventButton *bevent, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	GtkTreeIter iter;
	CollectInfo *info = nullptr;

	tip_schedule(ct);

	if (static_cast<gint>(bevent->x) != 0 || static_cast<gint>(bevent->y) != 0)
		{
		info = collection_table_find_data_by_coord(ct, static_cast<gint>(bevent->x), static_cast<gint>(bevent->y), &iter);
		}

	if (ct->click_info)
		{
		collection_table_selection_remove(ct, ct->click_info, SELECTION_PRELIGHT, nullptr);
		}

	if (bevent->button == GDK_BUTTON_PRIMARY &&
	    info && ct->click_info == info)
		{
		collection_table_set_focus(ct, info);

		if (bevent->state & GDK_CONTROL_MASK)
			{
			gboolean select = !info_selected(info);

			if ((bevent->state & GDK_SHIFT_MASK) && ct->prev_selection)
				{
				collection_table_select_region_util(ct, ct->prev_selection, info, select);
				}
			else
				{
				collection_table_select_util(ct, info, select);
				}
			}
		else
			{
			collection_table_unselect_all(ct);

			if ((bevent->state & GDK_SHIFT_MASK) &&
			    ct->prev_selection)
				{
				collection_table_select_region_util(ct, ct->prev_selection, info, TRUE);
				}
			else
				{
				collection_table_select_util(ct, info, TRUE);
				}
			}
		}
	else if (bevent->button == GDK_BUTTON_MIDDLE &&
		 info && ct->click_info == info)
		{
		collection_table_select_util(ct, info, !info_selected(info));
		}

	return TRUE;
}

static gboolean collection_table_leave_cb(GtkWidget *, GdkEventCrossing *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	tip_unschedule(ct);
	return FALSE;
}

/*
 *-------------------------------------------------------------------
 * populate, add, insert, etc.
 *-------------------------------------------------------------------
 */

static gboolean collection_table_destroy_node_cb(GtkTreeModel *store, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	GList *list;

	gtk_tree_model_get(store, iter, CTABLE_COLUMN_POINTER, &list, -1);
	g_list_free(list);

	return FALSE;
}

static void collection_table_clear_store(CollectTable *ct)
{
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));
	gtk_tree_model_foreach(store, collection_table_destroy_node_cb, nullptr);

	gtk_list_store_clear(GTK_LIST_STORE(store));
}

static GList *collection_table_add_row(CollectTable *ct, GtkTreeIter *iter)
{
	GtkListStore *store;
	GList *list = nullptr;
	gint i;

	for (i = 0; i < ct->columns; i++) list = g_list_prepend(list, nullptr);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview)));
	gtk_list_store_append(store, iter);
	gtk_list_store_set(store, iter, CTABLE_COLUMN_POINTER, list, -1);

	return list;
}

static void collection_table_populate(CollectTable *ct, gboolean resize)
{
	gint row;
	GList *work;

	collection_table_verify_selections(ct);

	collection_table_clear_store(ct);

	if (resize)
		{
		gint i;
		gint thumb_width;

		thumb_width = collection_table_get_icon_width(ct);

		for (i = 0; i < COLLECT_TABLE_MAX_COLUMNS; i++)
			{
			GtkTreeViewColumn *column;
			GtkCellRenderer *cell;
			GList *list;

			column = gtk_tree_view_get_column(GTK_TREE_VIEW(ct->listview), i);
			gtk_tree_view_column_set_visible(column, (i < ct->columns));
			gtk_tree_view_column_set_fixed_width(column, thumb_width + (THUMB_BORDER_PADDING * 6));

			list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
			cell = static_cast<GtkCellRenderer *>((list) ? list->data : nullptr);
			g_list_free(list);

			if (cell && GQV_IS_CELL_RENDERER_ICON(cell))
				{
				g_object_set(cell,
				             "fixed_width", thumb_width,
				             "fixed_height", options->thumbnails.max_height,
				             "show_text", ct->show_text || ct->show_stars || ct->show_infotext,
				             NULL);
				}
			}
		if (gtk_widget_get_realized(ct->listview)) gtk_tree_view_columns_autosize(GTK_TREE_VIEW(ct->listview));
		}

	row = -1;
	work = ct->cd->list;
	while (work)
		{
		GList *list;
		GtkTreeIter iter;

		row++;

		list = collection_table_add_row(ct, &iter);
		while (work && list)
			{
			list->data = work->data;
			list = list->next;
			work = work->next;
			}
		}

	ct->rows = row + 1;

	collection_table_update_focus(ct);
	collection_table_update_status(ct);
}

static void collection_table_populate_at_new_size(CollectTable *ct, gint w, gint, gboolean force)
{
	gint new_cols;
	gint thumb_width;

	thumb_width = collection_table_get_icon_width(ct);

	new_cols = w / (thumb_width + (THUMB_BORDER_PADDING * 6));
	new_cols = std::max(new_cols, 1);

	if (!force && new_cols == ct->columns) return;

	ct->columns = new_cols;

	collection_table_populate(ct, TRUE);

	DEBUG_1("col tab pop cols=%d rows=%d", ct->columns, ct->rows);
}

static void collection_table_sync(CollectTable *ct)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GList *work;
	gint r;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));

	r = -1;

	work = ct->cd->list;
	while (work)
		{
		GList *list;
		r++;
		if (gtk_tree_model_iter_nth_child(store, &iter, nullptr, r))
			{
			gtk_tree_model_get(store, &iter, CTABLE_COLUMN_POINTER, &list, -1);
			gtk_list_store_set(GTK_LIST_STORE(store), &iter, CTABLE_COLUMN_POINTER, list, -1);
			}
		else
			{
			list = collection_table_add_row(ct, &iter);
			}

		for (; list; list = list->next)
			{
			CollectInfo *info;
			if (work)
				{
				info = static_cast<CollectInfo *>(work->data);
				work = work->next;
				}
			else
				{
				info = nullptr;
				}

			list->data = info;
			}
		}

	r++;
	while (gtk_tree_model_iter_nth_child(store, &iter, nullptr, r))
		{
		GList *list;

		gtk_tree_model_get(store, &iter, CTABLE_COLUMN_POINTER, &list, -1);
		gtk_list_store_remove(GTK_LIST_STORE(store), &iter);
		g_list_free(list);
		}

	ct->rows = r;

	collection_table_update_focus(ct);
	collection_table_update_status(ct);
}

static gboolean collection_table_sync_idle_cb(gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (ct->sync_idle_id)
		{
		g_clear_handle_id(&ct->sync_idle_id, g_source_remove);

		collection_table_sync(ct);
		}

	return G_SOURCE_REMOVE;
}

static void collection_table_sync_idle(CollectTable *ct)
{
	if (!ct->sync_idle_id)
		{
		/* high priority, the view needs to be resynced before a redraw
		 * may contain invalid pointers at this time
		 */
		ct->sync_idle_id = g_idle_add_full(G_PRIORITY_HIGH, collection_table_sync_idle_cb, ct, nullptr);
		}
}

void collection_table_add_filelist(CollectTable *ct, GList *list)
{
	GList *work;

	if (!list) return;

	work = list;
	while (work)
		{
		collection_add(ct->cd, static_cast<FileData *>(work->data), FALSE);
		work = work->next;
		}
}

static void collection_table_insert_filelist(CollectTable *ct, GList *list, CollectInfo *insert_info)
{
	GList *work;

	if (!list) return;

	work = list;
	while (work)
		{
		collection_insert(ct->cd, static_cast<FileData *>(work->data), insert_info, FALSE);
		work = work->next;
		}

	collection_table_sync_idle(ct);
}

static void collection_table_move_by_info_list(CollectTable *ct, GList *info_list, gint row, gint col)
{
	GList *work;
	GList *insert_pos = nullptr;
	GList *temp;
	CollectInfo *info;

	if (!info_list) return;

	info = collection_table_find_data(ct, row, col, nullptr);

	if (!info_list->next && info_list->data == info) return;

	if (info) insert_pos = g_list_find(ct->cd->list, info);

	/** @FIXME this may get slow for large lists */
	work = info_list;
	while (insert_pos && work)
		{
		if (insert_pos->data == work->data)
			{
			insert_pos = insert_pos->next;
			work = info_list;
			}
		else
			{
			work = work->next;
			}
		}

	work = info_list;
	while (work)
		{
		ct->cd->list = g_list_remove(ct->cd->list, work->data);
		work = work->next;
		}

	/* place them back in */
	temp = g_list_copy(info_list);

	if (insert_pos)
		{
		ct->cd->list = uig_list_insert_list(ct->cd->list, insert_pos, temp);
		}
	else if (info)
		{
		ct->cd->list = g_list_concat(temp, ct->cd->list);
		}
	else
		{
		ct->cd->list = g_list_concat(ct->cd->list, temp);
		}

	ct->cd->changed = TRUE;

	collection_table_sync_idle(ct);
}


/*
 *-------------------------------------------------------------------
 * updating
 *-------------------------------------------------------------------
 */

void collection_table_file_update(CollectTable *ct, CollectInfo *info)
{
	GtkTreeIter iter;
	gint row;
	gint col;
	gdouble value;

	if (!info)
		{
		collection_table_update_extras(ct, FALSE, 0.0);
		return;
		}

	if (!collection_table_find_position(ct, info, &row, &col)) return;

	if (ct->columns != 0 && ct->rows != 0)
		{
		value = static_cast<gdouble>((row * ct->columns) + col) / (ct->columns * ct->rows);
		}
	else
		{
		value = 0.0;
		}

	collection_table_update_extras(ct, TRUE, value);

	if (collection_table_find_iter(ct, info, &iter, nullptr))
		{
		GtkTreeModel *store;
		GList *list;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(ct->listview));
		gtk_tree_model_get(store, &iter, CTABLE_COLUMN_POINTER, &list, -1);
		gtk_list_store_set(GTK_LIST_STORE(store), &iter, CTABLE_COLUMN_POINTER, list, -1);
		}
}

void collection_table_file_add(CollectTable *ct, CollectInfo *)
{
	collection_table_sync_idle(ct);
}

void collection_table_file_insert(CollectTable *ct, CollectInfo *)
{
	collection_table_sync_idle(ct);
}

void collection_table_file_remove(CollectTable *ct, CollectInfo *ci)
{
	if (ci && info_selected(ci))
		{
		ct->selection = g_list_remove(ct->selection, ci);
		}

	collection_table_sync_idle(ct);
}

void collection_table_refresh(CollectTable *ct)
{
	collection_table_populate(ct, FALSE);
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

static void collection_table_add_dir_recursive(CollectTable *ct, FileData *dir_fd, gboolean recursive)
{
	GList *d;
	GList *f;
	GList *work;

	if (!filelist_read(dir_fd, &f, recursive ? &d : nullptr))
		return;

	f = filelist_filter(f, FALSE);
	d = filelist_filter(d, TRUE);

	f = filelist_sort_path(f);
	d = filelist_sort_path(d);

	collection_table_insert_filelist(ct, f, ct->marker_info);

	work = g_list_last(d);
	while (work)
		{
		collection_table_add_dir_recursive(ct, static_cast<FileData *>(work->data), TRUE);
		work = work->prev;
		}

	file_data_list_free(f);
	file_data_list_free(d);
}

template<gboolean recursive>
static void confirm_dir_list_add(GtkWidget *, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);

	for (GList *work = ct->drop_list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (isdir(fd->path)) collection_table_add_dir_recursive(ct, fd, recursive);
		}

	collection_table_insert_filelist(ct, ct->drop_list, ct->marker_info);
}

static void confirm_dir_list_skip(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_insert_filelist(ct, ct->drop_list, ct->marker_info);
}

static GtkWidget *collection_table_drop_menu(CollectTable *ct)
{
	GtkWidget *menu;

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(collection_table_popup_destroy_cb), ct);

	menu_item_add_icon(menu, _("Dropped list includes folders."), GQ_ICON_DIRECTORY, nullptr, nullptr);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("_Add contents"), GQ_ICON_OK,
	                   G_CALLBACK(confirm_dir_list_add<FALSE>), ct);
	menu_item_add_icon(menu, _("Add contents _recursive"), GQ_ICON_ADD,
	                   G_CALLBACK(confirm_dir_list_add<TRUE>), ct);
	menu_item_add_icon(menu, _("_Skip folders"), GQ_ICON_REMOVE,
	                   G_CALLBACK(confirm_dir_list_skip), ct);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("Cancel"), GQ_ICON_CANCEL, nullptr, ct);

	return menu;
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */
#if !HAVE_GTK4
static void collection_table_dnd_get(GtkWidget *, GdkDragContext *,
				     GtkSelectionData *selection_data, guint info,
				     guint, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	gboolean selected;
	GList *list = nullptr;
	g_autofree gchar *uri_text = nullptr;
	gint total;

	if (!ct->click_info) return;

	selected = info_selected(ct->click_info);

	switch (info)
		{
		case TARGET_APP_COLLECTION_MEMBER:
			if (selected)
				{
				uri_text = collection_info_list_to_dnd_data(ct->cd, ct->selection, total);
				}
			else
				{
				list = g_list_append(nullptr, ct->click_info);
				uri_text = collection_info_list_to_dnd_data(ct->cd, list, total);
				g_list_free(list);
				}
			gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data),
						8, reinterpret_cast<guchar *>(uri_text), total);
			break;
		case TARGET_URI_LIST:
		case TARGET_TEXT_PLAIN:
		default:
			if (selected)
				{
				list = collection_table_selection_get_list(ct);
				}
			else
				{
				list = g_list_append(nullptr, file_data_ref(ct->click_info->fd));
				}
			if (!list) return;

			uri_selection_data_set_uris_from_filelist(selection_data, list);
			file_data_list_free(list);
			break;
		}
}


static void collection_table_dnd_receive(GtkWidget *, GdkDragContext *context,
					  gint x, gint y,
					  GtkSelectionData *selection_data, guint info,
					  guint, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	GList *info_list = nullptr;
	CollectionData *source;
	CollectInfo *drop_info;

	DEBUG_1("%s", gtk_selection_data_get_data(selection_data));

	collection_table_scroll(ct, FALSE);

	drop_info = collection_table_insert_point(ct, x, y);

	g_autoptr(FileDataList) list = nullptr;
	switch (info)
		{
		case TARGET_APP_COLLECTION_MEMBER:
			source = collection_from_dnd_data(reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data)), &list, &info_list);
			if (source)
				{
				if (source == ct->cd)
					{
					gint row = -1;
					gint col = -1;

					/* it is a move within a collection */
					g_clear_pointer(&list, file_data_list_free);

					if (!drop_info)
						{
						collection_table_move_by_info_list(ct, info_list, -1, -1);
						}
					else if (collection_table_find_position(ct, drop_info, &row, &col))
						{
						collection_table_move_by_info_list(ct, info_list, row, col);
						}
					}
				else
					{
					/* it is a move/copy across collections */
					if (gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE)
						{
						collection_remove_by_info_list(source, info_list);
						}
					}
				g_list_free(info_list);
				}
			break;
		case TARGET_URI_LIST:
			list = uri_filelist_from_gtk_selection_data(selection_data);
			if (file_data_list_has_dir(list))
				{
				ct->drop_list = g_steal_pointer(&list);
				ct->drop_info = drop_info;

				GtkWidget *menu = collection_table_drop_menu(ct);
				gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
				return;
				}
			break;
		default:
			break;
		}

	if (list)
		{
		collection_table_insert_filelist(ct, list, drop_info);
		}
}

static void collection_table_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (ct->click_info && ct->click_info->pixbuf)
		{
		gint items;

		if (info_selected(ct->click_info))
			items = g_list_length(ct->selection);
		else
			items = 1;
		dnd_set_drag_icon(widget, context, ct->click_info->pixbuf, items);
		}
}

static void collection_table_dnd_end(GtkWidget *, GdkDragContext *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	/* apparently a leave event is not generated on a drop */
	tip_unschedule(ct);

	collection_table_scroll(ct, FALSE);
}

static gint collection_table_dnd_motion(GtkWidget *, GdkDragContext *, gint x, gint y, guint, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_motion_update(ct, x, y, TRUE);
	collection_table_scroll(ct, TRUE);

	return FALSE;
}

static void collection_table_dnd_leave(GtkWidget *, GdkDragContext *, guint, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_scroll(ct, FALSE);
}

static void collection_table_dnd_init(CollectTable *ct)
{
	gq_gtk_drag_source_set(ct->listview, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
	                    collection_drag_types.data(), collection_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gq_drag_g_signal_connect(G_OBJECT(ct->listview), "drag_data_get",
			 G_CALLBACK(collection_table_dnd_get), ct);
	gq_drag_g_signal_connect(G_OBJECT(ct->listview), "drag_begin",
			 G_CALLBACK(collection_table_dnd_begin), ct);
	gq_drag_g_signal_connect(G_OBJECT(ct->listview), "drag_end",
			 G_CALLBACK(collection_table_dnd_end), ct);

	gq_gtk_drag_dest_set(ct->listview,
	                  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP),
	                  collection_drop_types.data(), collection_drop_types.size(),
	                  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK));
	gq_drag_g_signal_connect(G_OBJECT(ct->listview), "drag_motion",
			 G_CALLBACK(collection_table_dnd_motion), ct);
	gq_drag_g_signal_connect(G_OBJECT(ct->listview), "drag_leave",
			 G_CALLBACK(collection_table_dnd_leave), ct);
	gq_drag_g_signal_connect(G_OBJECT(ct->listview), "drag_data_received",
			 G_CALLBACK(collection_table_dnd_receive), ct);
}
#endif

/*
 *-----------------------------------------------------------------------------
 * draw, etc.
 *-----------------------------------------------------------------------------
 */

static void collection_table_cell_data_cb(GtkTreeViewColumn *, GtkCellRenderer *cell,
					  GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return;
#else
	if (!GQV_IS_CELL_RENDERER_ICON(cell)) return;

	auto *cd = static_cast<ColumnData *>(data);

	/** @FIXME this is a primitive hack to stop a crash.
	 * When compiled with GTK3, if a Collection window containing
	 * say, 50 or so, images has its width changed, there is a segfault
	 * https://github.com/BestImageViewer/geeqie/issues/531
	 */
	if (cd->number == COLLECT_TABLE_MAX_COLUMNS) return;

	GList *list;
	gtk_tree_model_get(tree_model, iter, CTABLE_COLUMN_POINTER, &list, -1);

	auto *info = static_cast<CollectInfo *>(g_list_nth_data(list, cd->number));
	if (!info)
		{
		g_object_set(cell,
		             "pixbuf", NULL,
		             "text", NULL,
		             "cell-background-set", FALSE,
		             "foreground-set", FALSE,
		             "has-focus", FALSE,
		             NULL);
		return;
		}

	const CollectTable *ct = cd->ct;

	GtkStyle *style = deprecated_gtk_widget_get_style(ct->listview);
	GdkRGBA color_bg;
	GdkRGBA color_fg;
	if (info->flag_mask & SELECTION_SELECTED)
		{
		color_fg = convert_gdkcolor_to_gdkrgba(&style->text[GTK_STATE_SELECTED]);
		color_bg = convert_gdkcolor_to_gdkrgba(&style->base[GTK_STATE_SELECTED]);
		}
	else
		{
		color_fg = convert_gdkcolor_to_gdkrgba(&style->text[GTK_STATE_NORMAL]);
		color_bg = convert_gdkcolor_to_gdkrgba(&style->base[GTK_STATE_NORMAL]);
		}

	if (info->flag_mask & SELECTION_PRELIGHT)
		{
		shift_color(color_bg);
		}

	g_autoptr(GString) display_text = g_string_new("");
	if (info->fd)
		{
		if (ct->show_text)
			{
			g_string_append(display_text, info->fd->name);
			}

		if (ct->show_stars)
			{
			if (display_text->len) g_string_append(display_text, "\n");
			g_autofree gchar *star_rating = metadata_read_rating_stars(info->fd);
			g_string_append(display_text, star_rating);
			}

		if (ct->show_infotext && info->infotext)
			{
			if (display_text->len) g_string_append(display_text, "\n");
			g_string_append(display_text, info->infotext);
			}
		}

	g_object_set(cell,
	             "pixbuf", info->pixbuf,
	             "text", display_text->str,
	             "cell-background-rgba", &color_bg,
	             "cell-background-set", TRUE,
	             "foreground-rgba", &color_fg,
	             "foreground-set", TRUE,
	             "has-focus", (ct->focus_info == info),
	             NULL);
#endif
}

static void collection_table_append_column(CollectTable *ct, gint n)
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
	g_object_set(renderer,
	             "xpad", THUMB_BORDER_PADDING * 2,
	             "ypad", THUMB_BORDER_PADDING,
	             "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
	             NULL);

	g_object_set_data(G_OBJECT(column), "column_number", GINT_TO_POINTER(n));

	cd = g_new0(ColumnData, 1);
	cd->ct = ct;
	cd->number = n;
	gtk_tree_view_column_set_cell_data_func(column, renderer, collection_table_cell_data_cb, cd, g_free);

	gtk_tree_view_append_column(GTK_TREE_VIEW(ct->listview), column);
}

/*
 *-------------------------------------------------------------------
 * init, destruction
 *-------------------------------------------------------------------
 */

static void collection_table_destroy(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	/* If there is no unsaved data, save the window geometry
	 */
	/** @FIXME  This code interferes with the code detecting files on unmounted drives. See collection_load_private() in collect-io,cc. If the user wants to save the geometry of an unchanged Collection, just slightly move one of the thumbnails. */
/*
	if (!ct->cd->changed)
		{
		if (!collection_save(ct->cd, ct->cd->path))
			{
			log_printf("failed saving to collection path: %s\n", ct->cd->path);
			}
		}
*/

	if (ct->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(ct->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, nullptr, nullptr, ct);
		gq_gtk_widget_destroy(ct->popup);
		}

	if (ct->sync_idle_id) g_source_remove(ct->sync_idle_id);

	tip_unschedule(ct);
	collection_table_scroll(ct, FALSE);

	g_free(ct);
}

static void collection_table_sized(GtkWidget *, GtkAllocation *allocation, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_populate_at_new_size(ct, allocation->width, allocation->height, FALSE);
}

#if HAVE_GTK4
static void listview_motion_cb(GtkEventControllerMotion *motion, gdouble x, gdouble y, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);

	ct->last_x = static_cast<gint>(x);
	ct->last_y = static_cast<gint>(y);
	ct->pointer_valid = TRUE;
}

#else
static gboolean listview_motion_cb(GtkWidget *, GdkEventMotion *event, gpointer data)
{
	auto *ct = static_cast<CollectTable*>(data);

	ct->last_x = event->x;
	ct->last_y = event->y;
	ct->pointer_valid = TRUE;

	return FALSE;
}
#endif

CollectTable *collection_table_new(CollectionData *cd)
{
	CollectTable *ct;
	GtkListStore *store;
	gint i;

	ct = g_new0(CollectTable, 1);

	ct->cd = cd;
	ct->show_text = options->show_icon_names;
	ct->show_stars = options->show_star_rating;
	ct->show_infotext = options->show_collection_infotext;

	ct->scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(ct->scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ct->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	store = gtk_list_store_new(1, G_TYPE_POINTER);
	ct->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ct->listview));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ct->listview), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(ct->listview), FALSE);

	for (i = 0; i < COLLECT_TABLE_MAX_COLUMNS; i++)
		{
		collection_table_append_column(ct, i);
		}

	/* zero width column to hide tree view focus, we draw it ourselves */
	collection_table_append_column(ct, i);
	/* end column to fill white space */
	collection_table_append_column(ct, i);

	g_signal_connect(G_OBJECT(ct->listview), "destroy",
			 G_CALLBACK(collection_table_destroy), ct);
	g_signal_connect(G_OBJECT(ct->listview), "size_allocate",
			 G_CALLBACK(collection_table_sized), ct);
	g_signal_connect(G_OBJECT(ct->listview), "key_press_event",
			 G_CALLBACK(collection_table_press_key_cb), ct);

	gq_gtk_container_add(ct->scrolled, ct->listview);
	gtk_widget_show(ct->listview);

	collection_table_dnd_init(ct);

	gtk_widget_set_events(ct->listview, GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK |
			      static_cast<GdkEventMask>(GDK_BUTTON_PRESS_MASK | GDK_LEAVE_NOTIFY_MASK));
	g_signal_connect(G_OBJECT(ct->listview),"button_press_event",
			 G_CALLBACK(collection_table_press_cb), ct);
	g_signal_connect(G_OBJECT(ct->listview),"button_release_event",
			 G_CALLBACK(collection_table_release_cb), ct);
	g_signal_connect(G_OBJECT(ct->listview),"motion_notify_event",
			 G_CALLBACK(collection_table_motion_cb), ct);
	g_signal_connect(G_OBJECT(ct->listview), "leave_notify_event",
			 G_CALLBACK(collection_table_leave_cb), ct);

#if HAVE_GTK4
	GtkEventController *motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "motion", G_CALLBACK(listview_motion_cb), ct);
	gtk_widget_add_controller(ct->listview, motion);
#else
	gtk_widget_add_events(ct->listview, GDK_POINTER_MOTION_MASK);
	g_signal_connect(ct->listview, "motion-notify-event", G_CALLBACK(listview_motion_cb), ct);
#endif
	return ct;
}

void collection_table_set_labels(CollectTable *ct, GtkWidget *status, GtkWidget *extra)
{
	ct->status_label = status;
	ct->extra_label = extra;
	collection_table_update_status(ct);
	collection_table_update_extras(ct, FALSE, 0.0);
}

CollectInfo *collection_table_get_focus_info(CollectTable *ct)
{
	return collection_table_find_data(ct, ct->focus_row, ct->focus_column, nullptr);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
