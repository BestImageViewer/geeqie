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

#include <glib-object.h>

#include "accelerators.h"
#include "actions.h"
#include "collect-dlg.h"
#include "collect-io.h"
#include "collect.h"
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
#include "pixbuf-util.h"
#include "print.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "uri-utils.h"
#include "utilops.h"
#include "view-file.h"
#include "window.h"

namespace
{

struct CollectTableItem
{
	GObject parent;
	CollectInfo *info;
};

struct CollectTableItemClass
{
	GObjectClass parent_class;
};

G_DEFINE_TYPE(CollectTableItem, collect_table_item, G_TYPE_OBJECT)

enum { COLLECT_TABLE_ITEM_CHANGED, COLLECT_TABLE_ITEM_SIGNAL_COUNT };
guint collect_table_item_signals[COLLECT_TABLE_ITEM_SIGNAL_COUNT];

void collect_table_item_class_init(CollectTableItemClass *klass)
{
	collect_table_item_signals[COLLECT_TABLE_ITEM_CHANGED] =
		g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		             0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
}

void collect_table_item_init(CollectTableItem *)
{
}

CollectTableItem *collect_table_item_new(CollectInfo *info)
{
	auto *item = static_cast<CollectTableItem *>(g_object_new(collect_table_item_get_type(), nullptr));
	item->info = info;
	return item;
}

/* between these, the icon width is increased by thumb_max_width / 2 */
constexpr gint THUMB_MIN_ICON_WIDTH = 128;
constexpr gint THUMB_MAX_ICON_WIDTH = 150;

constexpr gint THUMB_BORDER_PADDING = 2;

constexpr auto COLLECT_TABLE_DATA_KEY = "collect-table";

inline gboolean info_selected(const CollectInfo *info)
{
	return info->flag_mask & SELECTION_SELECTED;
}

} // namespace

static void collection_table_populate_at_new_size(CollectTable *ct, gint w, gint h, gboolean force);

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

static CollectInfo *collection_table_find_data(CollectTable *ct, gint row, gint col)
{
	if (row < 0 || col < 0) return nullptr;
	return static_cast<CollectInfo *>(g_list_nth_data(ct->cd->list, (row * ct->columns) + col));
}

static CollectInfo *collection_table_find_data_by_coord(CollectTable *ct, gint x, gint y)
{
	GtkWidget *picked = gtk_widget_pick(ct->listview, x, y, GTK_PICK_DEFAULT);
	while (picked && picked != ct->listview)
		{
		if (auto *info = static_cast<CollectInfo *>(g_object_get_data(G_OBJECT(picked), "collect-info"))) return info;
		picked = gtk_widget_get_parent(picked);
		}
	return nullptr;
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
		text = _("Loading thumbs…");
	else
		text = " ";

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ct->extra_label), value);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ct->extra_label), text);
}

static void collection_table_toggle_filenames(CollectTable *ct)
{
	ct->show_text = !ct->show_text;
	options->show_icon_names = ct->show_text;

	collection_table_populate_at_new_size(ct, gtk_widget_get_width(ct->listview), gtk_widget_get_height(ct->listview), TRUE);
}

static void collection_table_toggle_stars(CollectTable *ct)
{
	ct->show_stars = !ct->show_stars;
	options->show_star_rating = ct->show_stars;

	collection_table_populate_at_new_size(ct, gtk_widget_get_width(ct->listview), gtk_widget_get_height(ct->listview), TRUE);
}

static void collection_table_toggle_info(CollectTable *ct)
{
	ct->show_infotext = !ct->show_infotext;
	options->show_collection_infotext = ct->show_infotext;

	collection_table_populate_at_new_size(ct, gtk_widget_get_width(ct->listview), gtk_widget_get_height(ct->listview), TRUE);
}

static void collection_table_toggle_marks(CollectTable *ct)
{
	ct->show_marks = !ct->show_marks;
	options->show_collection_marks = ct->show_marks;

	collection_table_populate_at_new_size(ct, gtk_widget_get_width(ct->listview), gtk_widget_get_height(ct->listview), TRUE);
}

static gint collection_table_get_icon_width(CollectTable *ct)
{
	if (!ct->show_text && !ct->show_infotext) return options->thumbnails.size.width;

	gint width = options->thumbnails.size.width + (options->thumbnails.size.width / 2);
	width = std::max(width, THUMB_MIN_ICON_WIDTH);
	if (width > THUMB_MAX_ICON_WIDTH) width = options->thumbnails.size.width;

	return width;
}

/*
 *-------------------------------------------------------------------
 * cell updates
 *-------------------------------------------------------------------
 */

static void collection_table_selection_set(CollectTable *ct, CollectInfo *info, SelectionType value)
{
	if (!info) return;

	if (info->flag_mask == value) return;
	info->flag_mask = value;

	const gint position = g_list_index(ct->cd->list, info);
	if (position >= 0 && static_cast<guint>(position) < g_list_model_get_n_items(G_LIST_MODEL(ct->store)))
		{
		auto *item = static_cast<CollectTableItem *>(g_list_model_get_item(G_LIST_MODEL(ct->store), position));
		g_signal_emit(item, collect_table_item_signals[COLLECT_TABLE_ITEM_CHANGED], 0);
		g_object_unref(item);
		}
}

static void collection_table_selection_add(CollectTable *ct, CollectInfo *info, SelectionType mask)
{
	if (!info) return;

	collection_table_selection_set(ct, info, static_cast<SelectionType>(info->flag_mask | mask));
}

static void collection_table_selection_remove(CollectTable *ct, CollectInfo *info, SelectionType mask)
{
	if (!info) return;

	collection_table_selection_set(ct, info, static_cast<SelectionType>(info->flag_mask & ~mask));
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
		collection_table_selection_add(ct, static_cast<CollectInfo *>(work->data), SELECTION_SELECTED);
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
		collection_table_selection_remove(ct, static_cast<CollectInfo *>(work->data), SELECTION_SELECTED);
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
			collection_table_selection_remove(ct, info, SELECTION_SELECTED);
			}
		else
			{
			new_selection = g_list_append(new_selection, info);
			collection_table_selection_add(ct, info, SELECTION_SELECTED);

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
	collection_table_selection_add(ct, info, SELECTION_SELECTED);

	collection_table_update_status(ct);
}

static void collection_table_unselect(CollectTable *ct, CollectInfo *info)
{
	ct->prev_selection = info;

	if (!info || !info_selected(info) ) return;

	ct->selection = g_list_remove(ct->selection, info);
	collection_table_selection_remove(ct, info, SELECTION_SELECTED);

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
			CollectInfo *info = collection_table_find_data(ct, i, j);
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
 * popup menus
 *-------------------------------------------------------------------
 */

static void collection_table_popup_save_as_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_dialog_save(ct->cd);
}

static void collection_table_popup_save_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (!ct->cd->path)
		{
		collection_table_popup_save_as_cb(nullptr, nullptr, data);
		return;
		}

	if (!collection_save(ct->cd, ct->cd->path))
		{
		log_printf("failed saving to collection path: %s\n", ct->cd->path);
		}
}

static GList *collection_table_popup_file_list(CollectTable *ct)
{
	if (!ct->click_info) return collection_table_selection_get_list(ct);

	if (info_selected(ct->click_info))
		{
		return collection_table_selection_get_list(ct);
		}

	return g_list_append(nullptr, file_data_ref(ct->click_info->fd));
}

static void collection_table_popup_edit_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	if (!ct) return;

	const char *key = g_variant_get_string(parameter, nullptr);

	file_util_start_editor_from_filelist(key, collection_table_popup_file_list(ct), nullptr, ct->listview);
}

static void collection_table_popup_menu(CollectTable *ct, bool over_icon, GtkWidget *parent = nullptr, gdouble x = 0, gdouble y = 0);

static void collection_table_help_cb(GSimpleAction *, GVariant *, gpointer)
{
	help_window_show("GuideCollections.html");
}

static void collection_table_popup_copy_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_copy(nullptr, collection_table_popup_file_list(ct), nullptr, ct->listview);
}

static void collection_table_popup_move_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_move(nullptr, collection_table_popup_file_list(ct), nullptr, ct->listview);
}

static void collection_table_popup_rename_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_rename(nullptr, collection_table_popup_file_list(ct), ct->listview);
}

template<gboolean safe_delete>
static void collection_table_popup_delete_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_delete(nullptr, collection_table_popup_file_list(ct), ct->listview, safe_delete);

	collection_table_refresh(ct);
}

template<gboolean quoted>
static void collection_table_popup_copy_path_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	file_util_path_list_to_clipboard(collection_table_popup_file_list(ct), quoted, ClipboardAction::COPY);
}

static SortType sort_type_from_string(const char *value)
{
	if (g_strcmp0(value, "class") == 0) return SORT_CLASS;
	if (g_strcmp0(value, "date") == 0) return SORT_TIME;
	if (g_strcmp0(value, "date-creation") == 0) return SORT_CTIME;
	if (g_strcmp0(value, "exif-digitized") == 0) return SORT_EXIFTIMEDIGITIZED;
	if (g_strcmp0(value, "exif-original") == 0) return SORT_EXIFTIME;
	if (g_strcmp0(value, "media-date") == 0) return SORT_MEDIA_TIME;
	if (g_strcmp0(value, "name") == 0) return SORT_NAME;
	if (g_strcmp0(value, "number") == 0) return SORT_NUMBER;
	if (g_strcmp0(value, "path") == 0) return SORT_PATH;
	if (g_strcmp0(value, "rating") == 0) return SORT_RATING;
	if (g_strcmp0(value, "size") == 0) return SORT_SIZE;

	return SORT_NONE;
}

static void collection_table_popup_sort_cb(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (!ct || !parameter)
		{
		return;
		}

	const char *value = g_variant_get_string(parameter, nullptr);
	SortType type = sort_type_from_string(value);

	if (type == SORT_NONE)
		return;

	g_simple_action_set_state(action, parameter);

	collection_set_sort_method(ct->cd, type);
}

static void collection_table_popup_randomize_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (!ct) return;

	collection_randomize(ct->cd);
}

static void collection_table_append_main_window_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	g_autoptr(FileDataList) list = layout_list(nullptr);

	if (list) collection_table_add_filelist(ct, list);
}

static void collection_table_close_window_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	gtk_window_close(GTK_WINDOW(widget_get_toplevel(ct->listview)));
}

static void collection_table_popup_view_new_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	CollectInfo *info = ct->click_info ? ct->click_info : collection_table_get_focus_info(ct);

	if (info && g_list_find(ct->cd->list, info))
		{
		view_window_new_from_collection(ct->cd, info);
		}
}

static void collection_table_popup_view_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	CollectInfo *info = ct->click_info ? ct->click_info : collection_table_get_focus_info(ct);

	if (info && g_list_find(ct->cd->list, info))
		{
		layout_image_set_collection(nullptr, ct->cd, info);
		}
}

static void collection_table_popup_selectall_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_select_all(ct);
	ct->prev_selection= ct->click_info;
}

static void collection_table_popup_unselectall_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_unselect_all(ct);
	ct->prev_selection= ct->click_info;
}

static void collection_table_popup_select_invert_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_select_invert_all(ct);
	ct->prev_selection= ct->click_info;
}

static void collection_table_popup_rectangular_selection_cb(GSimpleAction *, GVariant *, gpointer)
{
	options->collections.rectangular_selection = !(options->collections.rectangular_selection);
}

static void collection_table_popup_remove_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	GList *list;

	if (!ct->click_info)
		{
		list = g_list_copy(ct->selection);
		if (!list)
			{
			CollectInfo *info = collection_table_get_focus_info(ct);
			if (!info) return;
			list = g_list_append(nullptr, info);
			}
		}
	else if (info_selected(ct->click_info))
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

static void collection_table_popup_add_file_selection_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	LayoutWindow *lw = get_current_layout();
	if (!lw) return;

	g_autoptr(FileDataList) list = vf_selection_get_list(lw->vf);
	if (!list) return;

	collection_table_add_filelist(ct, list);
}

static void collection_table_popup_add_collection_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_dialog_append(ct->cd);
}

static void collection_table_popup_goto_original_cb(GSimpleAction *, GVariant *, gpointer data)
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

static void collection_table_popup_find_dupes_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	DupeWindow *dw;

	dw = dupe_window_new();
	dupe_window_add_collection(dw, ct->cd);
}

static void collection_table_popup_print_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	print_window_new(collection_table_selection_get_list(ct), widget_get_toplevel(ct->listview));
}

static void collection_table_popup_show_names_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	bool enabled = g_variant_get_boolean(state);
	g_simple_action_set_state(action, g_variant_new_boolean(enabled));

	collection_table_toggle_filenames(ct);
}

static void collection_table_popup_show_stars_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	bool enabled = g_variant_get_boolean(state);
	g_simple_action_set_state(action, g_variant_new_boolean(enabled));

	collection_table_toggle_stars(ct);
}

static void collection_table_popup_show_infotext_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	bool enabled = g_variant_get_boolean(state);
	g_simple_action_set_state(action, g_variant_new_boolean(enabled));

	collection_table_toggle_info(ct);
}

static void collection_table_popup_show_marks_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	bool enabled = g_variant_get_boolean(state);
	g_simple_action_set_state(action, g_variant_new_boolean(enabled));

	collection_table_toggle_marks(ct);
}

static void collection_table_popup_destroy_cb(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_selection_remove(ct, ct->click_info, SELECTION_PRELIGHT);
	ct->click_info = nullptr;
	ct->popup = nullptr;

	file_data_list_free(ct->drop_list);
	ct->drop_list = nullptr;
	ct->drop_info = nullptr;

	file_data_list_free(ct->editmenu_fd_list);
	ct->editmenu_fd_list = nullptr;
}

static void collection_table_popup_menu(CollectTable *ct, bool over_icon, GtkWidget *parent, gdouble x, gdouble y)
{
	GAction *action;
	g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-collection.ui");
	GMenu *menu_model = G_MENU(gtk_builder_get_object(builder, "menu-collection"));

	CollectWindow *cw = collection_window_find(ct->cd);

	ct->editmenu_fd_list = collection_table_selection_get_list(ct);

	GMenu *plugins_menu = G_MENU(gtk_builder_get_object(builder, "plugins-submenu"));
	plugins_menu_populate(plugins_menu, "win.collection-win-plugin-run", ct->editmenu_fd_list);

	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-view");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), over_icon);

	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-view-new-window");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), over_icon);

	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-go-to-original");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), over_icon);

	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-remove");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), over_icon);

	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-append-from-file-selection");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), TRUE);

	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-append-from-collection");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), TRUE);

	if (parent)
		{
		popup_menu_at(menu_model, parent, x, y);
		}
	else
		{
		popup_menu(menu_model, cw->window);
		}
}
/*
 *-------------------------------------------------------------------
 * keyboard callbacks
 *-------------------------------------------------------------------
 */

void collection_table_set_focus(CollectTable *ct, CollectInfo *info)
{
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
		collection_table_selection_remove(ct, ct->focus_info, SELECTION_FOCUS);
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
	collection_table_selection_add(ct, ct->focus_info, SELECTION_FOCUS);

	const gint position = g_list_index(ct->cd->list, ct->focus_info);
	if (position >= 0)
		{
		gtk_grid_view_scroll_to(GTK_GRID_VIEW(ct->listview), position,
		                        GTK_LIST_SCROLL_FOCUS, nullptr);
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

	collection_table_set_focus(ct, collection_table_find_data(ct, new_row, new_col));
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
	gint ret;

	adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(ct->listview));
	page_size = static_cast<gint>(gtk_adjustment_get_page_increment(adj));

	gint row_height = options->thumbnails.size.height + (THUMB_BORDER_PADDING * 2);
	if (ct->show_text) row_height += options->thumbnails.size.height / 3;
	if (ct->show_infotext) row_height += options->thumbnails.size.height / 3;

	ret = page_size / row_height;
	ret = std::max(ret, 1);

	return ret;
}

static gboolean collection_table_press_key_cb(GtkEventControllerKey *, guint keyval, guint, GdkModifierType state, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	gint focus_row = 0;
	gint focus_col = 0;
	CollectInfo *info;
	gboolean stop_signal = TRUE;

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
			info = collection_table_find_data(ct, ct->focus_row, ct->focus_column);
			if (info)
				{
				ct->click_info = info;
				if (state & GDK_CONTROL_MASK)
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
		default:
			stop_signal = FALSE;
			break;
		}

	if (focus_row != 0 || focus_col != 0)
		{
		CollectInfo *old_info = collection_table_find_data(ct, ct->focus_row, ct->focus_column);

		collection_table_move_focus(ct, focus_row, focus_col, TRUE);

		CollectInfo *new_info = collection_table_find_data(ct, ct->focus_row, ct->focus_column);

		if (new_info != old_info)
			{
			if (state & GDK_SHIFT_MASK)
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
			else if (state & GDK_CONTROL_MASK)
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

	return stop_signal;
}

/*
 *-------------------------------------------------------------------
 * insert marker
 *-------------------------------------------------------------------
 */

static CollectInfo *collection_table_insert_find(CollectTable *ct, gboolean *after, gint x, gint y)
{
	CollectInfo *info = collection_table_find_data_by_coord(ct, x, y);
	GtkWidget *picked = gtk_widget_pick(ct->listview, x, y, GTK_PICK_DEFAULT);
	GtkWidget *item_widget = picked;
	while (item_widget && item_widget != ct->listview &&
	       g_object_get_data(G_OBJECT(item_widget), "collect-info") != info)
		{
		item_widget = gtk_widget_get_parent(item_widget);
		}

	if (info && item_widget && item_widget != ct->listview)
		{
		graphene_rect_t bounds{};
		if (gtk_widget_compute_bounds(item_widget, ct->listview, &bounds))
			{
			*after = x > bounds.origin.x + (bounds.size.width / 2);
			}
		return info;
		}

	if (info == nullptr)
		{
		GList *work;

		work = g_list_last(ct->cd->list);
		if (work)
			{
			info = static_cast<CollectInfo *>(work->data);
			*after = TRUE;
			}
		}

	return info;
}

static CollectInfo *collection_table_insert_point(CollectTable *ct, gint x, gint y)
{
	CollectInfo *info;
	gboolean after = FALSE;

	info = collection_table_insert_find(ct, &after, x, y);

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

static gint collection_table_drop_index_from_info(CollectTable *ct, CollectInfo *info)
{
	if (!info) return -1;

	GList *work = g_list_find(ct->cd->list, info);
	return work ? g_list_position(ct->cd->list, work) : -1;
}

static CollectInfo *collection_table_drop_info_from_index(CollectTable *ct, gint index)
{
	if (index < 0) return nullptr;

	return static_cast<CollectInfo *>(g_list_nth_data(ct->cd->list, index));
}

/*
 *-------------------------------------------------------------------
 * mouse drag auto-scroll
 *-------------------------------------------------------------------
 */

static void collection_table_scroll(CollectTable *ct, gboolean scroll)
{
	if (!scroll)
		{
		g_clear_handle_id(&ct->drop_idle_id, g_source_remove);
		widget_auto_scroll_stop(ct->listview);
		}
}

/*
 *-------------------------------------------------------------------
 * mouse callbacks
 *-------------------------------------------------------------------
 */
static void collection_table_press_cb(GtkGestureClick *gesture,  gint n_press, gdouble x, gdouble y, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	CollectInfo *info;
	const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	info = collection_table_find_data_by_coord(ct, static_cast<gint>(x), static_cast<gint>(y));

	ct->click_info = info;
	collection_table_selection_add(ct, ct->click_info, SELECTION_PRELIGHT);

	switch (button)
		{
		case GDK_BUTTON_PRIMARY:
			if (n_press == 1 && !gtk_widget_has_focus(ct->listview))
				{
				gtk_widget_grab_focus(ct->listview);
				}
			break;

		case GDK_BUTTON_SECONDARY:
			collection_table_popup_menu(ct, info != nullptr, ct->listview, x, y);
			break;

		default:
			break;
		}
}

static void collection_table_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);
	CollectInfo *info = nullptr;

	if (static_cast<gint>(x) != 0 || static_cast<gint>(y) != 0)
		{
		info = collection_table_find_data_by_coord(ct, static_cast<gint>(x), static_cast<gint>(y));
		}

	if (ct->click_info)
		{
		collection_table_selection_remove(ct, ct->click_info, SELECTION_PRELIGHT);
		}

	const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	if (button == GDK_BUTTON_PRIMARY && n_press == 2 && info && ct->click_info == info)
		{
		layout_image_set_collection(nullptr, ct->cd, info);
		}

	GdkModifierType state = GDK_NO_MODIFIER_MASK;
	if (GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture)))
		{
		state = gdk_event_get_modifier_state(event);
		}

	if (button == GDK_BUTTON_PRIMARY &&
	    info && ct->click_info == info)
		{
		collection_table_set_focus(ct, info);

		if (state & GDK_CONTROL_MASK)
			{
			gboolean select = !info_selected(info);

			if ((state & GDK_SHIFT_MASK) && ct->prev_selection)
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

			if ((state & GDK_SHIFT_MASK) && ct->prev_selection)
				{
				collection_table_select_region_util(ct, ct->prev_selection, info, TRUE);
				}
			else
				{
				collection_table_select_util(ct, info, TRUE);
				}
			}
		}
	else if (button == GDK_BUTTON_MIDDLE &&
	         info && ct->click_info == info)
		{
		collection_table_select_util(ct, info, !info_selected(info));
		}
}

static void collection_menu_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);

	collection_table_popup_menu(ct, ct->selection);
}

/*
 *-------------------------------------------------------------------
 * populate, add, insert, etc.
 *-------------------------------------------------------------------
 */

static void collection_table_clear_store(CollectTable *ct)
{
	g_list_store_remove_all(ct->store);
}

static void collection_table_populate(CollectTable *ct, gboolean resize)
{
	collection_table_verify_selections(ct);

	collection_table_clear_store(ct);

	if (resize)
		{
		gtk_grid_view_set_max_columns(GTK_GRID_VIEW(ct->listview), ct->columns);
		}

	for (GList *work = ct->cd->list; work; work = work->next)
		{
		auto *item = collect_table_item_new(static_cast<CollectInfo *>(work->data));
		g_list_store_append(ct->store, item);
		g_object_unref(item);
		}

	ct->rows = ct->columns > 0 ? (g_list_length(ct->cd->list) + ct->columns - 1) / ct->columns : 0;

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
	ct->columns = std::max(ct->columns, 1);
	collection_table_populate(ct, FALSE);
}

static gboolean collection_table_sync_idle_cb(gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	if (ct->sync_idle_id)
		{
		ct->sync_idle_id = 0;

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

/*
 *-------------------------------------------------------------------
 * updating
 *-------------------------------------------------------------------
 */

void collection_table_file_update(CollectTable *ct, CollectInfo *info)
{
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

	const gint position = g_list_index(ct->cd->list, info);
	if (position >= 0 && static_cast<guint>(position) < g_list_model_get_n_items(G_LIST_MODEL(ct->store)))
		{
		auto *item = static_cast<CollectTableItem *>(g_list_model_get_item(G_LIST_MODEL(ct->store), position));
		g_signal_emit(item, collect_table_item_signals[COLLECT_TABLE_ITEM_CHANGED], 0);
		g_object_unref(item);
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

static void collection_table_add_dir_recursive(CollectTable *ct, FileData *dir_fd, gboolean recursive, CollectInfo *insert_info)
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

	collection_table_insert_filelist(ct, f, insert_info);

	work = g_list_last(d);
	while (work)
		{
		collection_table_add_dir_recursive(ct, static_cast<FileData *>(work->data), TRUE, insert_info);
		work = work->prev;
		}

	file_data_list_free(f);
	file_data_list_free(d);
}

template<gboolean recursive>
static void confirm_dir_list_add(GSimpleAction *, GVariant *, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	CollectInfo *drop_info = collection_table_drop_info_from_index(ct, ct->drop_index);

	for (GList *work = ct->drop_list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (isdir(fd->path)) collection_table_add_dir_recursive(ct, fd, recursive, drop_info);
		}

	collection_table_insert_filelist(ct, ct->drop_list, drop_info);
}

static void confirm_dir_list_skip(GSimpleAction *, GVariant *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_insert_filelist(ct, ct->drop_list, collection_table_drop_info_from_index(ct, ct->drop_index));
}

static void collection_table_drop_menu_append_item(GMenu *menu, const gchar *label, const gchar *icon_name, const gchar *action_name)
{
	g_autoptr(GMenuItem) item = g_menu_item_new(label, action_name);

	if (icon_name)
		{
		g_autoptr(GIcon) icon = g_themed_icon_new(icon_name);
		g_menu_item_set_icon(item, icon);
		}

	g_menu_append_item(menu, item);
}

static GtkWidget *collection_table_drop_menu(CollectTable *ct)
{
	g_autoptr(GSimpleActionGroup) action_group = g_simple_action_group_new();
	g_autoptr(GMenu) menu = g_menu_new();
	g_autoptr(GMenu) info_section = g_menu_new();
	g_autoptr(GMenu) choice_section = g_menu_new();
	g_autoptr(GMenu) cancel_section = g_menu_new();

	g_autoptr(GSimpleAction) info_action = g_simple_action_new("info", nullptr);
	g_autoptr(GSimpleAction) add_action = g_simple_action_new("add", nullptr);
	g_autoptr(GSimpleAction) add_recursive_action = g_simple_action_new("add-recursive", nullptr);
	g_autoptr(GSimpleAction) skip_action = g_simple_action_new("skip", nullptr);
	g_autoptr(GSimpleAction) cancel_action = g_simple_action_new("cancel", nullptr);

	g_simple_action_set_enabled(info_action, FALSE);

	g_signal_connect(add_action, "activate", G_CALLBACK(confirm_dir_list_add<FALSE>), ct);
	g_signal_connect(add_recursive_action, "activate", G_CALLBACK(confirm_dir_list_add<TRUE>), ct);
	g_signal_connect(skip_action, "activate", G_CALLBACK(confirm_dir_list_skip), ct);

	g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(info_action));
	g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(add_action));
	g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(add_recursive_action));
	g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(skip_action));
	g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(cancel_action));

	collection_table_drop_menu_append_item(info_section, _("Dropped list includes folders."), GQ_ICON_DIRECTORY, "collection-drop.info");
	g_menu_append_section(menu, nullptr, G_MENU_MODEL(info_section));

	collection_table_drop_menu_append_item(choice_section, _("_Add contents"), GQ_ICON_OK, "collection-drop.add");
	collection_table_drop_menu_append_item(choice_section, _("Add contents _recursive"), GQ_ICON_ADD, "collection-drop.add-recursive");
	collection_table_drop_menu_append_item(choice_section, _("_Skip folders"), GQ_ICON_REMOVE, "collection-drop.skip");
	g_menu_append_section(menu, nullptr, G_MENU_MODEL(choice_section));

	collection_table_drop_menu_append_item(cancel_section, _("Cancel"), GQ_ICON_CANCEL, "collection-drop.cancel");
	g_menu_append_section(menu, nullptr, G_MENU_MODEL(cancel_section));

	GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	gtk_widget_insert_action_group(popover, "collection-drop", G_ACTION_GROUP(action_group));
	popover_set_parent(popover, ct->listview);
	g_signal_connect(G_OBJECT(popover), "destroy", G_CALLBACK(collection_table_popup_destroy_cb), ct);
	popover_popup(popover);

	return popover;
}

struct CollectTableDropData
{
	GtkWidget *listview;
	GList *source_info_list;
	gint drop_index;
};

struct CollectTableDropInsertData
{
	GtkWidget *listview;
	GList *list;
	GList *source_info_list;
	gint drop_index;
};

static void collection_table_drop_insert_data_free(CollectTableDropInsertData *insert_data)
{
	if (!insert_data) return;

	file_data_list_free(insert_data->list);
	g_list_free(insert_data->source_info_list);
	g_object_unref(insert_data->listview);
	g_free(insert_data);
}

static gboolean collection_table_dnd_get_listview_coords(GtkDropTargetAsync *target, CollectTable *ct, gdouble x, gdouble y, gint &listview_x, gint &listview_y)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));

	if (widget == ct->listview)
		{
		listview_x = static_cast<gint>(x);
		listview_y = static_cast<gint>(y);
		return TRUE;
		}

	graphene_point_t widget_point{static_cast<float>(x), static_cast<float>(y)};
	graphene_point_t listview_point{};
	if (!gtk_widget_compute_point(widget, ct->listview, &widget_point, &listview_point))
		{
		return FALSE;
		}

	listview_x = static_cast<gint>(listview_point.x);
	listview_y = static_cast<gint>(listview_point.y);
	return TRUE;
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

static GdkContentProvider *collection_table_dnd_prepare(GtkDragSource *source, gdouble, gdouble, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
	ct->click_info = static_cast<CollectInfo *>(g_object_get_data(G_OBJECT(widget), "collect-info"));

	if (!ct->click_info) return nullptr;
	g_list_free(ct->drag_info_list);
	ct->drag_info_list = info_selected(ct->click_info) ? g_list_copy(ct->selection)
	                                                 : g_list_append(nullptr, ct->click_info);

	g_autoptr(FileDataList) list = nullptr;
	if (info_selected(ct->click_info))
		{
		list = collection_table_selection_get_list(ct);
		}
	else
		{
		list = g_list_append(nullptr, file_data_ref(ct->click_info->fd));
		}

	if (!list) return nullptr;

	dnd_set_drag_icon(source, ct->click_info->pixbuf, g_list_length(list), ct->click_info->fd);
	return dnd_file_list_content_provider(list);
}

static GdkDragAction collection_table_dnd_motion(GtkDropTargetAsync *target, GdkDrop *drop, gdouble x, gdouble y, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);

	gint listview_x = -1;
	gint listview_y = -1;
	ct->marker_info = collection_table_dnd_get_listview_coords(target, ct, x, y, listview_x, listview_y)
	                  ? collection_table_insert_point(ct, listview_x, listview_y)
	                  : nullptr;
	collection_table_scroll(ct, TRUE);

	return (gdk_drop_get_actions(drop) & GDK_ACTION_COPY) ? GDK_ACTION_COPY : GDK_ACTION_NONE;
}

static void collection_table_dnd_leave(GtkDropTargetAsync *, GdkDrop *, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);

	collection_table_scroll(ct, FALSE);
}

static gboolean collection_table_dnd_insert_idle_cb(gpointer data)
{
	auto *insert_data = static_cast<CollectTableDropInsertData *>(data);
	auto *ct = static_cast<CollectTable *>(g_object_get_data(G_OBJECT(insert_data->listview), COLLECT_TABLE_DATA_KEY));

	if (!ct)
		{
		collection_table_drop_insert_data_free(insert_data);
		return G_SOURCE_REMOVE;
		}

	collection_table_scroll(ct, FALSE);
	CollectInfo *drop_info = collection_table_drop_info_from_index(ct, insert_data->drop_index);

	if (insert_data->source_info_list)
		{
		if (!g_list_find(insert_data->source_info_list, drop_info))
			{
			for (GList *work = insert_data->source_info_list; work; work = work->next)
				{
				ct->cd->list = g_list_remove(ct->cd->list, work->data);
				}
			GList *insert_before = drop_info ? g_list_find(ct->cd->list, drop_info) : nullptr;
			for (GList *work = insert_data->source_info_list; work; work = work->next)
				{
				ct->cd->list = g_list_insert_before(ct->cd->list, insert_before, work->data);
				}
			ct->cd->changed = TRUE;
			collection_table_sync(ct);
			}
		collection_table_drop_insert_data_free(insert_data);
		return G_SOURCE_REMOVE;
		}

	if (file_data_list_has_dir(insert_data->list))
		{
		file_data_list_free(ct->drop_list);
		ct->drop_list = filelist_copy(insert_data->list);
		ct->drop_info = drop_info;
		ct->marker_info = drop_info;
		ct->drop_index = insert_data->drop_index;

		collection_table_drop_menu(ct);
		}
	else
		{
		collection_table_insert_filelist(ct, insert_data->list, drop_info);
		}

	collection_table_drop_insert_data_free(insert_data);
	return G_SOURCE_REMOVE;
}

static void collection_table_dnd_file_received(GdkDrop *drop, GList *list, gpointer data)
{
	auto *drop_data = static_cast<CollectTableDropData *>(data);
	auto *ct = static_cast<CollectTable *>(g_object_get_data(G_OBJECT(drop_data->listview), COLLECT_TABLE_DATA_KEY));
	if (!ct)
		{
		gdk_drop_finish(drop, GDK_ACTION_NONE);
		g_object_unref(drop_data->listview);
		g_list_free(drop_data->source_info_list);
		g_free(drop_data);
		return;
		}

	auto action = GDK_ACTION_NONE;

	collection_table_scroll(ct, FALSE);

	if (list)
		{
		action = GDK_ACTION_COPY;
		}

	gdk_drop_finish(drop, action);

	if (list)
		{
		auto *insert_data = g_new0(CollectTableDropInsertData, 1);
		insert_data->listview = GTK_WIDGET(g_object_ref(drop_data->listview));
		insert_data->list = filelist_copy(list);
		insert_data->source_info_list = drop_data->source_info_list;
		drop_data->source_info_list = nullptr;
		insert_data->drop_index = drop_data->drop_index;
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, collection_table_dnd_insert_idle_cb, insert_data, nullptr);
		}

	g_object_unref(drop_data->listview);
	g_list_free(drop_data->source_info_list);
	g_free(drop_data);
}

static gboolean collection_table_dnd_drop(GtkDropTargetAsync *target, GdkDrop *drop, gdouble x, gdouble y, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	auto *drop_data = g_new0(CollectTableDropData, 1);
	gint listview_x = -1;
	gint listview_y = -1;

	collection_table_scroll(ct, FALSE);
	ct->marker_info = collection_table_dnd_get_listview_coords(target, ct, x, y, listview_x, listview_y)
	                  ? collection_table_insert_point(ct, listview_x, listview_y)
	                  : nullptr;
	ct->drop_index = collection_table_drop_index_from_info(ct, ct->marker_info);
	drop_data->listview = GTK_WIDGET(g_object_ref(ct->listview));
	drop_data->source_info_list = g_list_copy(ct->drag_info_list);
	drop_data->drop_index = ct->drop_index;
	dnd_read_file_list_async(drop, collection_table_dnd_file_received, drop_data);

	return TRUE;
}

static void collection_table_dnd_init_drop_target(CollectTable *ct, GtkWidget *widget)
{
	GdkContentFormats *formats = dnd_file_drop_formats();
	GtkDropTargetAsync *drop_target = gtk_drop_target_async_new(formats, static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
	g_signal_connect(drop_target, "drag-motion", G_CALLBACK(collection_table_dnd_motion), ct);
	g_signal_connect(drop_target, "drag-leave", G_CALLBACK(collection_table_dnd_leave), ct);
	g_signal_connect(drop_target, "drop", G_CALLBACK(collection_table_dnd_drop), ct);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drop_target));
}

static void collection_table_dnd_init(CollectTable *ct)
{
	collection_table_dnd_init_drop_target(ct, ct->listview);
	collection_table_dnd_init_drop_target(ct, ct->scrolled);
}

static void collection_table_dnd_end(GtkDragSource *, GdkDrag *, gboolean, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	collection_table_selection_remove(ct, ct->click_info, SELECTION_PRELIGHT);
	g_clear_pointer(&ct->drag_info_list, g_list_free);
}

/*
 *-----------------------------------------------------------------------------
 * draw, etc.
 *-----------------------------------------------------------------------------
 */

static void collection_grid_mark_toggled_cb(GtkCheckButton *button, gpointer)
{
	auto *info = static_cast<CollectInfo *>(g_object_get_data(G_OBJECT(button), "collect-info"));
	if (!info || !info->fd) return;
	const guint mark = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "collect-mark"));
	const gboolean active = gtk_check_button_get_active(button);
	if (active != file_data_get_mark(info->fd, mark)) file_data_set_mark(info->fd, mark, active);
}

static void collection_grid_item_update(CollectTableItem *item, GtkWidget *child)
{
	auto *ct = static_cast<CollectTable *>(g_object_get_data(G_OBJECT(child), COLLECT_TABLE_DATA_KEY));
	CollectInfo *info = item->info;
	g_object_set_data(G_OBJECT(child), "collect-info", info);

	auto *picture = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(child), "collect-picture"));
	g_autoptr(GdkTexture) texture = info->pixbuf ? pixbuf_to_texture(info->pixbuf) : nullptr;
	gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(texture));
	gtk_widget_set_size_request(picture, collection_table_get_icon_width(ct), options->thumbnails.size.height);

	g_autoptr(GString) display_text = g_string_new(nullptr);
	if (info->fd)
		{
		if (ct->show_text) g_string_append(display_text, info->fd->name);
		if (ct->show_stars)
			{
			if (display_text->len) g_string_append_c(display_text, '\n');
			g_autofree gchar *stars = metadata_read_rating_stars(info->fd);
			g_string_append(display_text, stars);
			}
		if (ct->show_infotext && info->infotext)
			{
			if (display_text->len) g_string_append_c(display_text, '\n');
			g_string_append(display_text, info->infotext);
			}
		}
	auto *label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(child), "collect-label"));
	gtk_label_set_text(GTK_LABEL(label), display_text->str);
	gtk_widget_set_visible(label, display_text->len > 0);

	auto *marks = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(child), "collect-marks"));
	gtk_widget_set_visible(marks, ct->show_marks && info->fd);
	for (GtkWidget *button = gtk_widget_get_first_child(marks); button; button = gtk_widget_get_next_sibling(button))
		{
		const guint mark = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "collect-mark"));
		g_object_set_data(G_OBJECT(button), "collect-info", info);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(button), info->fd && file_data_get_mark(info->fd, mark));
		}

	gtk_widget_remove_css_class(child, "collection-grid-selected");
	gtk_widget_remove_css_class(child, "collection-grid-prelight");
	gtk_widget_remove_css_class(child, "collection-grid-focus");
	if (info->flag_mask & SELECTION_SELECTED) gtk_widget_add_css_class(child, "collection-grid-selected");
	if (info->flag_mask & SELECTION_PRELIGHT) gtk_widget_add_css_class(child, "collection-grid-prelight");
	if (ct->focus_info == info && gtk_widget_has_focus(ct->listview))
		{
		gtk_widget_add_css_class(child, "collection-grid-focus");
		}
}

static void collection_grid_focus_changed_cb(GtkWidget *, GParamSpec *, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	for (guint position = 0; position < g_list_model_get_n_items(G_LIST_MODEL(ct->store)); position++)
		{
		auto *item = static_cast<CollectTableItem *>(g_list_model_get_item(G_LIST_MODEL(ct->store), position));
		g_signal_emit(item, collect_table_item_signals[COLLECT_TABLE_ITEM_CHANGED], 0);
		g_object_unref(item);
		}
}

static void collection_grid_factory_setup(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, THUMB_BORDER_PADDING);
	gtk_widget_add_css_class(box, "collection-grid-item");
	gtk_widget_set_size_request(box, collection_table_get_icon_width(ct), -1);
	gtk_widget_set_margin_start(box, THUMB_BORDER_PADDING * 2);
	gtk_widget_set_margin_end(box, THUMB_BORDER_PADDING * 2);
	gtk_widget_set_margin_top(box, THUMB_BORDER_PADDING);
	gtk_widget_set_margin_bottom(box, THUMB_BORDER_PADDING);
	g_object_set_data(G_OBJECT(box), COLLECT_TABLE_DATA_KEY, ct);

	GtkWidget *picture = gtk_picture_new();
	gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
	gtk_box_append(GTK_BOX(box), picture);
	GtkWidget *label = gtk_label_new(nullptr);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_box_append(GTK_BOX(box), label);
	GtkWidget *marks = gtk_grid_new();
	gtk_widget_set_halign(marks, GTK_ALIGN_CENTER);
	for (guint mark = 0; mark < FILEDATA_MARKS_SIZE; mark++)
		{
		GtkWidget *button = gtk_check_button_new();
		gtk_widget_add_css_class(button, "marks-filter-button");
		g_object_set_data(G_OBJECT(button), "collect-mark", GUINT_TO_POINTER(mark));
		g_signal_connect(button, "toggled", G_CALLBACK(collection_grid_mark_toggled_cb), nullptr);
		gtk_grid_attach(GTK_GRID(marks), button, mark % 5, mark / 5, 1, 1);
		}
	gtk_box_append(GTK_BOX(box), marks);

	g_object_set_data(G_OBJECT(box), "collect-picture", picture);
	g_object_set_data(G_OBJECT(box), "collect-label", label);
	g_object_set_data(G_OBJECT(box), "collect-marks", marks);
	GtkDragSource *drag_source = gtk_drag_source_new();
	gtk_drag_source_set_actions(drag_source, static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_source), 0);
	g_signal_connect(drag_source, "prepare", G_CALLBACK(collection_table_dnd_prepare), ct);
	g_signal_connect(drag_source, "drag-end", G_CALLBACK(collection_table_dnd_end), ct);
	gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(drag_source));
	gtk_list_item_set_child(list_item, box);
}

static void collection_grid_factory_bind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	auto *item = static_cast<CollectTableItem *>(gtk_list_item_get_item(list_item));
	GtkWidget *child = gtk_list_item_get_child(list_item);
	const gulong handler_id = g_signal_connect(item, "changed", G_CALLBACK(collection_grid_item_update), child);
	g_object_set_data(G_OBJECT(list_item), "collect-changed-handler", GSIZE_TO_POINTER(handler_id));
	collection_grid_item_update(item, child);
}

static void collection_grid_factory_unbind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	auto *item = static_cast<CollectTableItem *>(gtk_list_item_get_item(list_item));
	const gulong handler_id = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(list_item), "collect-changed-handler"));
	if (item && handler_id) g_signal_handler_disconnect(item, handler_id);
	g_object_set_data(G_OBJECT(list_item), "collect-changed-handler", nullptr);
	g_object_set_data(G_OBJECT(gtk_list_item_get_child(list_item)), "collect-info", nullptr);
}

/*
 *-------------------------------------------------------------------
 * init, destruction
 *-------------------------------------------------------------------
 */

static void collection_table_destroy(GtkWidget *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	g_object_set_data(G_OBJECT(ct->listview), COLLECT_TABLE_DATA_KEY, nullptr);

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
		gtk_popover_popdown(GTK_POPOVER(ct->popup));
		gtk_widget_unparent(ct->popup);
		}

	if (ct->sync_idle_id) g_source_remove(ct->sync_idle_id);

	collection_table_scroll(ct, FALSE);
	g_clear_pointer(&ct->drag_info_list, g_list_free);
	g_clear_object(&ct->store);

	g_free(ct);
}

static gint collection_table_viewport_width(CollectTable *ct)
{
	GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(ct->scrolled));
	const gint page_width = static_cast<gint>(gtk_adjustment_get_page_size(hadjustment));

	if (page_width > 0) return page_width;

	const gint scrolled_width = gtk_widget_get_width(ct->scrolled);
	if (scrolled_width > 0) return scrolled_width;

	return gtk_widget_get_width(ct->listview);
}

static void collection_table_sized(GObject *, GParamSpec *, gpointer data)
{
	auto ct = static_cast<CollectTable *>(data);

	collection_table_populate_at_new_size(ct, collection_table_viewport_width(ct), gtk_widget_get_height(ct->scrolled), FALSE);
}

static void listview_motion_cb(GtkEventControllerMotion * /*motion*/, gdouble x, gdouble y, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);

	ct->last_x = static_cast<gint>(x);
	ct->last_y = static_cast<gint>(y);
	ct->pointer_valid = TRUE;
}

static gboolean collection_table_query_tooltip_cb(GtkWidget *, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer data)
{
	auto *ct = static_cast<CollectTable *>(data);

	if (keyboard_mode)
		{
		return FALSE;
		}

	CollectInfo *info = collection_table_find_data_by_coord(ct, x, y);

	if (!info || !info->fd)
		{
		return FALSE;
		}

	gtk_tooltip_set_text(tooltip, ct->show_text ? info->fd->path : info->fd->name);

	return TRUE;
}

#include "collection-actions.inc"

CollectTable *collection_table_new(CollectionData *cd)
{
	CollectTable *ct;

	ct = g_new0(CollectTable, 1);

	ct->cd = cd;
	ct->columns = 1;
	ct->drop_index = -1;
	ct->show_text = options->show_icon_names;
	ct->show_stars = options->show_star_rating;
	ct->show_infotext = options->show_collection_infotext;
	ct->show_marks = options->show_collection_marks;

	ct->scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(ct->scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ct->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	ct->store = g_list_store_new(collect_table_item_get_type());
	GtkNoSelection *selection = gtk_no_selection_new(G_LIST_MODEL(g_object_ref(ct->store)));
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(collection_grid_factory_setup), ct);
	g_signal_connect(factory, "bind", G_CALLBACK(collection_grid_factory_bind), nullptr);
	g_signal_connect(factory, "unbind", G_CALLBACK(collection_grid_factory_unbind), nullptr);
	ct->listview = gtk_grid_view_new(GTK_SELECTION_MODEL(selection), factory);
	gtk_grid_view_set_single_click_activate(GTK_GRID_VIEW(ct->listview), FALSE);
	gtk_grid_view_set_min_columns(GTK_GRID_VIEW(ct->listview), 1);
	gtk_grid_view_set_max_columns(GTK_GRID_VIEW(ct->listview), 1);
	g_object_set_data(G_OBJECT(ct->listview), COLLECT_TABLE_DATA_KEY, ct);
	g_signal_connect(ct->listview, "notify::has-focus", G_CALLBACK(collection_grid_focus_changed_cb), ct);

	gtk_widget_set_has_tooltip(ct->listview, TRUE);
	g_signal_connect(ct->listview, "query-tooltip", G_CALLBACK(collection_table_query_tooltip_cb), ct);

	g_signal_connect(G_OBJECT(ct->listview), "destroy",
			 G_CALLBACK(collection_table_destroy), ct);
	g_signal_connect(G_OBJECT(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(ct->scrolled))), "notify::page-size",
			 G_CALLBACK(collection_table_sized), ct);

	GtkEventController *controller = gtk_event_controller_key_new();
	g_signal_connect(controller, "key-pressed", G_CALLBACK(collection_table_press_key_cb), ct);
	gtk_widget_add_controller(ct->listview, controller);

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ct->scrolled), ct->listview);

	collection_table_dnd_init(ct);

	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
	g_signal_connect(click, "pressed", G_CALLBACK(collection_table_press_cb), ct);
	g_signal_connect(click, "released", G_CALLBACK(collection_table_release_cb), ct);
	gtk_widget_add_controller(ct->listview, GTK_EVENT_CONTROLLER(click));

	GtkEventController *motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "motion", G_CALLBACK(listview_motion_cb), ct);
	gtk_widget_add_controller(ct->listview, motion);

	CollectWindow *cw = collection_window_find(ct->cd);

	GApplication *app = g_application_get_default();
	register_actions_from_table(GTK_APPLICATION(app), cw->window, collection_actions, get_keyfile_merged(), ct);

	GAction *action;
	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-show-filename-text");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->show_icon_names));
	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-show-star-rating");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->show_star_rating));
	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-show-infotext");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->show_collection_infotext));
	action = g_action_map_lookup_action(G_ACTION_MAP(cw->window), "collection-win-show-marks");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->show_collection_marks));

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
	return collection_table_find_data(ct, ct->focus_row, ct->focus_column);
}

const ActionDef *get_collection_actions()
{
	return collection_actions;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
