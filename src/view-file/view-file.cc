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

#include "view-file.h"

#include <array>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "accelerators.h"
#include "actions.h"
#include "archives.h"
#include "collect.h"
#include "compat.h"
#include "dnd.h"
#include "dupe.h"
#include "filedata.h"
#include "filefilter.h"
#include "history-list.h"
#include "img-view.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "main.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "sort-type.h"
#include "thumb.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-utildlg.h"
#include "uri-utils.h"
#include "utilops.h"
#include "view-file/view-file-icon.h"
#include "view-file/view-file-list.h"
#include "window.h"

/*
 *-----------------------------------------------------------------------------
 * signals
 *-----------------------------------------------------------------------------
 */

void vf_send_update(ViewFile *vf)
{
	if (vf->func_status) vf->func_status(vf, vf->data_status);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

void vf_sort_set(ViewFile *vf, FileData::FileList::SortSettings settings)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_sort_set(vf, settings); break;
	case FILEVIEW_ICON: vficon_sort_set(vf, settings); break;
	}
}

/*
 *-----------------------------------------------------------------------------
 * row stuff
 *-----------------------------------------------------------------------------
 */

FileData *vf_index_get_data(ViewFile *vf, gint row)
{
	return static_cast<FileData *>(g_list_nth_data(vf->list, row));
}

gint vf_index_by_fd(ViewFile *vf, FileData *fd)
{
	gint ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_index_by_fd(vf, fd); break;
	case FILEVIEW_ICON: ret = vficon_index_by_fd(vf, fd); break;
	default: ret = 0;
	}

	return ret;
}

guint vf_count(ViewFile *vf, gint64 *bytes)
{
	if (bytes)
		{
		gint64 b = 0;
		GList *work;

		work = vf->list;
		while (work)
			{
			auto fd = static_cast<FileData *>(work->data);
			work = work->next;

			b += fd->size;
			}

		*bytes = b;
		}

	return g_list_length(vf->list);
}

GList *vf_get_list(ViewFile *vf)
{
	return filelist_copy(vf->list);
}

/*
 *-------------------------------------------------------------------
 * keyboard
 *-------------------------------------------------------------------
 */

static gboolean vf_press_key_common(ViewFile *vf, GtkWidget *widget, guint keyval, GdkModifierType state)
{
	switch (vf->type)
		{
		case FILEVIEW_LIST:
			return vflist_press_key_cb(vf, widget, keyval, state);

		case FILEVIEW_ICON:
			return vficon_press_key_cb(vf, widget, keyval, state);

		default:
			return FALSE;
		}
}

static gboolean vf_press_key_cb(GtkEventControllerKey *, guint keyval, guint, GdkModifierType state, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkWidget *widget = vf->listview;

	return vf_press_key_common(vf, widget, keyval, state);
}

/*
 *-------------------------------------------------------------------
 * mouse
 *-------------------------------------------------------------------
 */

static gboolean vf_press_cb(GtkWidget *widget, const GqMouseButtonEvent *event, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_press_cb(vf, widget, event); break;
	case FILEVIEW_ICON: ret = vficon_press_cb(vf, widget, event); break;
	default: ret = FALSE;
	}

	return ret;
}

static gboolean vf_release_cb(GtkWidget *widget, const GqMouseButtonEvent *event, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_release_cb(vf, widget, event); break;
	case FILEVIEW_ICON: ret = vficon_release_cb(vf, widget, event); break;

	default: ret = FALSE;
	}

	return ret;
}


/*
 *-----------------------------------------------------------------------------
 * selections
 *-----------------------------------------------------------------------------
 */

static bool vf_is_selected(const ViewFile *vf, const FileData *fd)
{
	switch (vf->type)
		{
		case FILEVIEW_LIST: return vflist_is_selected(vf, fd);
		case FILEVIEW_ICON: return vficon_is_selected(vf, fd);
		}

	return false;
}

guint vf_selection_count(ViewFile *vf, gint64 *bytes)
{
	guint ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_selection_count(vf, bytes); break;
	case FILEVIEW_ICON: ret = vficon_selection_count(vf, bytes); break;
	default: ret = 0;
	}

	return ret;
}

GList *vf_selection_get_list(ViewFile *vf)
{
	GList *ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_selection_get_list(vf); break;
	case FILEVIEW_ICON: ret = vficon_selection_get_list(vf); break;
	default: ret = nullptr;
	}

	return ret;
}

std::vector<int> vf_selection_get_list_by_index(const ViewFile *vf)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: return vflist_selection_get_list_by_index(vf);
	case FILEVIEW_ICON: return vficon_selection_get_list_by_index(vf);
	default: return {};
	}
}

void vf_selection_foreach(ViewFile *vf, const ViewFile::SelectionCallback &func)
{
	if (!vf) return;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_selection_foreach(vf, func); break;
	case FILEVIEW_ICON: vficon_selection_foreach(vf, func); break;
	}
}

void vf_select_all(ViewFile *vf)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_select_all(vf); break;
	case FILEVIEW_ICON: vficon_select_all(vf); break;
	}
}

void vf_select_none(ViewFile *vf)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_select_none(vf); break;
	case FILEVIEW_ICON: vficon_select_none(vf); break;
	}
}

void vf_select_invert(ViewFile *vf)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_select_invert(vf); break;
	case FILEVIEW_ICON: vficon_select_invert(vf); break;
	}
}

void vf_select_by_fd(ViewFile *vf, FileData *fd)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_select_by_fd(vf, fd); break;
	case FILEVIEW_ICON: vficon_select_by_fd(vf, fd); break;
	}
}

void vf_select_list(ViewFile *vf, const FileDataList *list)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_select_list(vf, list); break;
	case FILEVIEW_ICON: vficon_select_list(vf, list); break;
	}
}

void vf_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_mark_to_selection(vf, mark, mode); break;
	case FILEVIEW_ICON: vficon_mark_to_selection(vf, mark, mode); break;
	}
}

void vf_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_selection_to_mark(vf, mark, mode); break;
	case FILEVIEW_ICON: vficon_selection_to_mark(vf, mark, mode); break;
	}
}

/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

GList *vf_pop_menu_file_list(ViewFile *vf)
{
	if (!vf->click_fd) return nullptr;

	if (vf_is_selected(vf, vf->click_fd))
		{
		return vf_selection_get_list(vf);
		}

	return vf_selection_get_one(vf, vf->click_fd);
}

GList *vf_selection_get_one(ViewFile *vf, FileData *fd)
{
	GList *ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_selection_get_one(vf, fd); break;
	case FILEVIEW_ICON: ret = vficon_selection_get_one(vf, fd); break;
	default: ret = nullptr;
	}

	return ret;
}

static void vf_pop_menu_view_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (!vf->click_fd) return;

	if (vf_is_selected(vf, vf->click_fd))
		{
		g_autoptr(FileDataList) list = vf_selection_get_list(vf);
		view_window_new_from_list(list);
		}
	else
		{
		view_window_new(vf->click_fd);
		}
}

static void vf_pop_menu_open_archive_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	LayoutWindow *lw_new;

	g_autofree gchar *dest_dir = open_archive(vf->click_fd);
	if (dest_dir)
		{
		lw_new = layout_new_from_default();
		layout_set_path(lw_new, dest_dir);
		}
	else
		{
		warning_dialog(_("Cannot open archive file"), _("See the Log Window"), GQ_ICON_DIALOG_WARNING, nullptr);
		}
}

static void vf_pop_menu_copy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_copy(nullptr, vf_pop_menu_file_list(vf), nullptr, vf->listview);
}

static void vf_pop_menu_move_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_move(nullptr, vf_pop_menu_file_list(vf), nullptr, vf->listview);
}

static void vf_pop_menu_rename_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_pop_menu_rename_cb(vf); break;
	case FILEVIEW_ICON: vficon_pop_menu_rename_cb(vf); break;
	}
}

template<gboolean safe_delete>
static void vf_pop_menu_delete_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_delete(nullptr, vf_pop_menu_file_list(vf), vf->listview, safe_delete);
}

template<gboolean quoted>
static void vf_pop_menu_copy_path_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_path_list_to_clipboard(vf_pop_menu_file_list(vf), quoted, ClipboardAction::COPY);
}

static void vf_pop_menu_cut_path_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_path_list_to_clipboard(vf_pop_menu_file_list(vf), FALSE, ClipboardAction::CUT);
}

template<gboolean disable>
static void vf_pop_menu_disable_grouping_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_data_disable_grouping_list(vf_pop_menu_file_list(vf), disable);
}

static void vf_pop_menu_duplicates_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	DupeWindow *dw;

	dw = dupe_window_new();
	dupe_window_add_files(dw, vf_pop_menu_file_list(vf), FALSE);
}

static void vf_pop_menu_refresh_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_pop_menu_refresh_cb(vf); break;
	case FILEVIEW_ICON: vficon_pop_menu_refresh_cb(vf); break;
	}
}

static void vf_popup_destroy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_popup_destroy_cb(vf); break;
	case FILEVIEW_ICON: vficon_popup_destroy_cb(vf); break;
	}

	vf->click_fd = nullptr;
	vf->popup = nullptr;

	file_data_list_free(vf->editmenu_fd_list);
	vf->editmenu_fd_list = nullptr;
}

static ViewFile *vf_from_action_data(gpointer data)
{
	auto *layout = static_cast<LayoutWindow *>(data);

	return layout ? layout->vf : nullptr;
}

static void vf_pop_menu_edit_action_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !parameter) return;

	const gchar *key = g_variant_get_string(parameter, nullptr);
	file_util_start_editor_from_filelist(key, vf_pop_menu_file_list(vf), vf->dir_fd->path, vf->listview);
}

static void vf_pop_menu_view_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_view_cb(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_open_archive_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_open_archive_cb(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_copy_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_copy_cb(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_move_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_move_cb(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_rename_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_rename_cb(nullptr, vf_from_action_data(data));
}

template<gboolean safe_delete>
static void vf_pop_menu_delete_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_delete_cb<safe_delete>(nullptr, vf_from_action_data(data));
}

template<gboolean quoted>
static void vf_pop_menu_copy_path_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_copy_path_cb<quoted>(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_cut_path_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_cut_path_cb(nullptr, vf_from_action_data(data));
}

template<gboolean disable>
static void vf_pop_menu_disable_grouping_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_disable_grouping_cb<disable>(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_duplicates_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_duplicates_cb(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_set_sort(ViewFile *vf, FileData::FileList::SortSettings sort)
{
	if (!vf) return;

	if (sort_type_requires_metadata(sort.method))
		{
		vf_read_metadata_in_idle(vf);
		}

	if (vf->layout)
		{
		layout_sort_set_files(vf->layout, sort);
		}
	else
		{
		vf_sort_set(vf, sort);
		}
}

static void vf_pop_menu_sort_action_cb(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !parameter) return;

	auto sort = vf->sort;
	sort.method = static_cast<SortType>(g_variant_get_int32(parameter));
	vf_pop_menu_set_sort(vf, sort);

	g_simple_action_set_state(action, parameter);
}

static void vf_pop_menu_sort_ascending_action_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !state) return;

	auto sort = vf->sort;
	sort.ascending = g_variant_get_boolean(state);
	vf_pop_menu_set_sort(vf, sort);

	g_simple_action_set_state(action, state);
}

static void vf_pop_menu_sort_case_action_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !state) return;

	auto sort = vf->sort;
	sort.case_sensitive = g_variant_get_boolean(state);
	vf_pop_menu_set_sort(vf, sort);

	g_simple_action_set_state(action, state);
}

static void vf_pop_menu_mark_to_selection_action_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !parameter) return;

	switch (g_variant_get_int32(parameter))
		{
		case MTS_MODE_SET: vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_SET); break;
		case MTS_MODE_OR: vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_OR); break;
		case MTS_MODE_AND: vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_AND); break;
		case MTS_MODE_MINUS: vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_MINUS); break;
		default: break;
		}
}

static void vf_pop_menu_selection_to_mark_action_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !parameter) return;

	switch (g_variant_get_int32(parameter))
		{
		case STM_MODE_SET: vf_selection_to_mark(vf, vf->active_mark, STM_MODE_SET); break;
		case STM_MODE_RESET: vf_selection_to_mark(vf, vf->active_mark, STM_MODE_RESET); break;
		case STM_MODE_TOGGLE: vf_selection_to_mark(vf, vf->active_mark, STM_MODE_TOGGLE); break;
		default: break;
		}
}

static void vf_pop_menu_view_type_action_cb(GSimpleAction *action, GVariant *parameter, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !vf->layout || !parameter) return;

	auto file_view_type = static_cast<FileViewType>(g_variant_get_int32(parameter));
	layout_views_set(vf->layout, vf->layout->options.dir_view_type, file_view_type);

	g_simple_action_set_state(action, parameter);
}

static void vf_pop_menu_show_thumbnails_action_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || vf->type != FILEVIEW_LIST || !state) return;

	const gboolean enabled = g_variant_get_boolean(state);
	vflist_color_set(vf, vf->click_fd, FALSE);
	if (vf->layout)
		{
		layout_thumb_set(vf->layout, enabled);
		}
	else
		{
		vflist_thumb_set(vf, enabled);
		}

	g_simple_action_set_state(action, state);
}

static void vf_pop_menu_show_filename_text_action_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || vf->type != FILEVIEW_ICON || !state) return;

	VFICON(vf)->show_text = g_variant_get_boolean(state);
	options->show_icon_names = VFICON(vf)->show_text;
	vficon_refresh(vf);

	g_simple_action_set_state(action, state);
}

static void vf_pop_menu_refresh_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	vf_pop_menu_refresh_cb(nullptr, vf_from_action_data(data));
}

static void vf_pop_menu_collections_action_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !parameter) return;

	g_autoptr(FileDataList) selection_list = vf_pop_menu_file_list(vf);
	collection_by_index_add_filelist(g_variant_get_int32(parameter), selection_list);
}

static void vf_pop_menu_show_star_rating_action_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto *vf = vf_from_action_data(data);
	if (!vf || !state) return;

	options->show_star_rating = g_variant_get_boolean(state);
	switch (vf->type)
		{
		case FILEVIEW_LIST: vflist_pop_menu_show_star_rating_cb(vf); break;
		case FILEVIEW_ICON: vficon_pop_menu_show_star_rating_cb(vf); break;
		}

	g_simple_action_set_state(action, state);
}

#include "view-file-actions.inc"

static GSimpleAction *vf_pop_menu_action(ViewFile *vf, const gchar *name)
{
	if (!vf || !vf->layout || !vf->layout->window) return nullptr;

	return G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(vf->layout->window), name));
}

static void vf_pop_menu_set_action_enabled(ViewFile *vf, const gchar *name, gboolean enabled)
{
	GSimpleAction *action = vf_pop_menu_action(vf, name);
	if (action) g_simple_action_set_enabled(action, enabled);
}

static void vf_pop_menu_set_boolean_state(ViewFile *vf, const gchar *name, gboolean state)
{
	GSimpleAction *action = vf_pop_menu_action(vf, name);
	if (action) g_simple_action_set_state(action, g_variant_new_boolean(state));
}

static void vf_pop_menu_set_int32_state(ViewFile *vf, const gchar *name, gint32 state)
{
	GSimpleAction *action = vf_pop_menu_action(vf, name);
	if (action) g_simple_action_set_state(action, g_variant_new_int32(state));
}

static void gmenu_append_action_item(GMenu *menu, const gchar *label, const gchar *action)
{
	g_autoptr(GMenuItem) item = g_menu_item_new(label, action);
	g_menu_append_item(menu, item);
}

static void gmenu_append_int32_action_item(GMenu *menu, const gchar *label, const gchar *action, gint32 target)
{
	g_autoptr(GMenuItem) item = g_menu_item_new(label, nullptr);
	g_menu_item_set_action_and_target(item, action, "i", target);
	g_menu_append_item(menu, item);
}

GtkWidget *vf_pop_menu(ViewFile *vf)
{
	gboolean active = FALSE;
	gboolean class_archive = FALSE;

	if (vf->type == FILEVIEW_LIST)
		{
		vflist_color_set(vf, vf->click_fd, TRUE);
		}

	active = (vf->click_fd != nullptr);
	class_archive = (vf->click_fd != nullptr && vf->click_fd->format_class == FORMAT_CLASS_ARCHIVE);

	g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-view-file.ui");
	GMenu *menu_model = G_MENU(gtk_builder_get_object(builder, "menu-view-file"));

	if (vf->clicked_mark > 0)
		{
		gint mark = vf->clicked_mark;
		g_autofree gchar *str_set_mark = g_strdup_printf(_("Set mark %d"), mark);
		g_autofree gchar *str_res_mark = g_strdup_printf(_("Reset mark %d"), mark);
		g_autofree gchar *str_toggle_mark = g_strdup_printf(_("Toggle mark %d"), mark);
		g_autofree gchar *str_sel_mark = g_strdup_printf(_("Select mark %d"), mark);
		g_autofree gchar *str_sel_mark_or = g_strdup_printf(_("Add mark %d"), mark);
		g_autofree gchar *str_sel_mark_and = g_strdup_printf(_("Intersection with mark %d"), mark);
		g_autofree gchar *str_sel_mark_minus = g_strdup_printf(_("Unselect mark %d"), mark);

		g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

		vf->active_mark = mark;
		vf->clicked_mark = 0;

		GMenu *marks_menu = G_MENU(gtk_builder_get_object(builder, "marks-section"));
		gmenu_append_int32_action_item(marks_menu, str_set_mark, "win.view-file-selection-to-mark", STM_MODE_SET);
		gmenu_append_int32_action_item(marks_menu, str_res_mark, "win.view-file-selection-to-mark", STM_MODE_RESET);
		gmenu_append_int32_action_item(marks_menu, str_toggle_mark, "win.view-file-selection-to-mark", STM_MODE_TOGGLE);
		gmenu_append_int32_action_item(marks_menu, str_sel_mark, "win.view-file-mark-to-selection", MTS_MODE_SET);
		gmenu_append_int32_action_item(marks_menu, str_sel_mark_or, "win.view-file-mark-to-selection", MTS_MODE_OR);
		gmenu_append_int32_action_item(marks_menu, str_sel_mark_and, "win.view-file-mark-to-selection", MTS_MODE_AND);
		gmenu_append_int32_action_item(marks_menu, str_sel_mark_minus, "win.view-file-mark-to-selection", MTS_MODE_MINUS);
		}

	vf->editmenu_fd_list = vf_pop_menu_file_list(vf);
	GMenu *plugins_menu = G_MENU(gtk_builder_get_object(builder, "plugins-submenu"));
	plugins_menu_populate(plugins_menu, "win.view-file-plugin-run", vf->editmenu_fd_list);

	GMenu *collections_menu = G_MENU(gtk_builder_get_object(builder, "collections-submenu"));
	submenu_add_collections_new(collections_menu, active, "win.view-file-collections", vf);

	GMenu *sort_menu = G_MENU(gtk_builder_get_object(builder, "sort-submenu"));
	for (const SortType sort_type : { SORT_NAME, SORT_NUMBER, SORT_TIME, SORT_CTIME, SORT_EXIFTIME,
	                                  SORT_EXIFTIMEDIGITIZED, SORT_SIZE, SORT_RATING, SORT_CLASS })
		{
		gmenu_append_int32_action_item(sort_menu, sort_type_get_text(sort_type), "win.view-file-sort", sort_type);
		}

	GMenu *view_specific_menu = G_MENU(gtk_builder_get_object(builder, "view-specific-section"));
	switch (vf->type)
		{
		case FILEVIEW_LIST:
			gmenu_append_action_item(view_specific_menu, _("Show thumbnails"), "win.view-file-show-thumbnails");
			vf_pop_menu_set_boolean_state(vf, "view-file-show-thumbnails", VFLIST(vf)->thumbs_enabled);
			break;
		case FILEVIEW_ICON:
			gmenu_append_action_item(view_specific_menu, _("Show filename text"), "win.view-file-show-filename-text");
			vf_pop_menu_set_boolean_state(vf, "view-file-show-filename-text", VFICON(vf)->show_text);
			break;
		}

	vf_pop_menu_set_action_enabled(vf, "view-file-plugin-run", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-view-new", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-open-archive", active && class_archive);
	vf_pop_menu_set_action_enabled(vf, "view-file-copy", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-move", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-rename", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-copy-path", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-copy-path-unquoted", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-cut-path", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-delete", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-delete-permanent", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-enable-grouping", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-disable-grouping", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-duplicates", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-collections", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-mark-to-selection", active);
	vf_pop_menu_set_action_enabled(vf, "view-file-selection-to-mark", active);

	if (options->file_ops.confirm_move_to_trash)
		{
		menu_item_include_ellipsis(G_MENU_MODEL(menu_model), "win.view-file-delete");
		}
	if (options->file_ops.confirm_delete)
		{
		menu_item_include_ellipsis(G_MENU_MODEL(menu_model), "win.view-file-delete-permanent");
		}

	vf_pop_menu_set_int32_state(vf, "view-file-sort", vf->sort.method);
	vf_pop_menu_set_boolean_state(vf, "view-file-sort-ascending", vf->sort.ascending);
	vf_pop_menu_set_boolean_state(vf, "view-file-sort-case", vf->sort.case_sensitive);
	vf_pop_menu_set_int32_state(vf, "view-file-view-type", vf->type);
	vf_pop_menu_set_boolean_state(vf, "view-file-show-star-rating", options->show_star_rating);

	GtkWidget *menu = popup_menu(menu_model, vf->listview);
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vf_popup_destroy_cb), vf);

	return menu;
}

gboolean vf_refresh(ViewFile *vf)
{
	gboolean ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_refresh(vf); break;
	case FILEVIEW_ICON: ret = vficon_refresh(vf); break;
	default: ret = FALSE;
	}

	return ret;
}

gboolean vf_set_fd(ViewFile *vf, FileData *dir_fd)
{
	gboolean ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_set_fd(vf, dir_fd); break;
	case FILEVIEW_ICON: ret = vficon_set_fd(vf, dir_fd); break;
	default: ret = FALSE;
	}

	return ret;
}

static void vf_destroy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_destroy_cb(vf); break;
	case FILEVIEW_ICON: vficon_destroy_cb(vf); break;
	}

	if (vf->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(vf->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, nullptr, nullptr, vf);
		gq_gtk_widget_destroy(vf->popup);
		}

	if (vf->read_metadata_in_idle_id)
		{
		g_idle_remove_by_data(vf);
		}
	file_data_unref(vf->dir_fd);
	g_free(vf->info);
	g_free(vf);
}

static void vf_marks_filter_toggle_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vf_refresh_idle(vf);
}

struct MarksTextEntry {
	gint mark_no;
	GtkWidget *edit_widget;
	GtkWidget *parent;
};

static void vf_marks_tooltip_cancel_cb(GenericDialog *gd, gpointer)
{
	generic_dialog_close(gd);
}

static void vf_marks_tooltip_ok_cb(GenericDialog *gd, gpointer data)
{
	auto mte = static_cast<MarksTextEntry *>(data);

	g_free(options->marks_tooltips[mte->mark_no]);
	options->marks_tooltips[mte->mark_no] = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(mte->edit_widget)));

	gtk_widget_set_tooltip_text(mte->parent, options->marks_tooltips[mte->mark_no]);

	generic_dialog_close(gd);
}

static void vf_marks_filter_on_icon_press(GtkEntry *edit_widget, GtkEntryIconPosition, GdkEvent *, gpointer)
{
	gq_gtk_entry_set_text(edit_widget, "");
}

static void vf_marks_tooltip_help_cb(GenericDialog *, gpointer)
{
	help_window_show("GuideImageMarks.html");
}

static void vf_marks_tooltip_open_dialog(GtkWidget *widget, gint mark_no)
{
	auto mte = g_new0(MarksTextEntry, 1);
	mte->mark_no = mark_no;
	mte->parent = widget;

	GenericDialog *gd = generic_dialog_new(_("Mark text"), "mark_text", widget, FALSE,
	                                       vf_marks_tooltip_cancel_cb, mte);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Set mark text"),
	                           _("This will set or clear the mark text."), FALSE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK",
	                          vf_marks_tooltip_ok_cb, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_HELP, _("Help"),
	                          vf_marks_tooltip_help_cb, FALSE);

	GtkWidget *table = pref_table_new(gd->vbox, 3, 1, FALSE, TRUE);

	g_autofree gchar *text = g_strdup_printf("%s%d", _("Mark "), mte->mark_no + 1);
	pref_table_label(table, 0, 0, text, GTK_ALIGN_END);

	mte->edit_widget = gtk_entry_new();
	gtk_widget_set_size_request(mte->edit_widget, 300, -1);
	if (options->marks_tooltips[mte->mark_no])
		{
		gq_gtk_entry_set_text(GTK_ENTRY(mte->edit_widget), options->marks_tooltips[mte->mark_no]);
		}
	gtk_grid_attach(GTK_GRID(table), mte->edit_widget, 1, 0, 1, 1);
	generic_dialog_attach_default(gd, mte->edit_widget);

	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(mte->edit_widget),
				      GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text(GTK_ENTRY(mte->edit_widget),
					GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	g_signal_connect(GTK_ENTRY(mte->edit_widget), "icon-press",
	                 G_CALLBACK(vf_marks_filter_on_icon_press), nullptr);

	gtk_widget_show(mte->edit_widget);
	gtk_widget_grab_focus(mte->edit_widget);
	gtk_widget_show(gd->dialog);
}

static void vf_marks_tooltip_cb(GtkGestureClick *gesture, gint, gdouble, gdouble, gpointer user_data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

	vf_marks_tooltip_open_dialog(widget, GPOINTER_TO_INT(user_data));
}

static void vf_file_filter_save_cb(GtkEntry *combo_entry, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	const gchar *entry_text = gq_gtk_entry_get_text(combo_entry);

	if (entry_text[0] != '\0')
		{
		bool text_found = false;
		for (gint i = 0; !text_found && i < vf->file_filter.count; i++)
			{
			gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), i);

			g_autofree gchar *index_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo));
			text_found = (g_strcmp0(index_text, entry_text) == 0);
			}

		if (!text_found)
			{
			history_list_add_to_key("file_filter", entry_text, 10);
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo), entry_text);
			vf->file_filter.count++;
			gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), vf->file_filter.count - 1);
			}
		}
	else if (vf->file_filter.last_selected >= 0)
		{
		gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), vf->file_filter.last_selected);
		g_autofree gchar *remove_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo));
		history_list_item_remove("file_filter", remove_text);
		gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(vf->file_filter.combo), vf->file_filter.last_selected);

		gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), -1);
		vf->file_filter.last_selected = - 1;
		gq_gtk_entry_set_text(combo_entry, "");
		vf->file_filter.count--;
		}

	vf_refresh(vf);
}

static void vf_file_filter_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf_refresh(vf);
}

static gboolean vf_file_filter_press_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vf->file_filter.last_selected = gtk_combo_box_get_active(GTK_COMBO_BOX(vf->file_filter.combo));

	gtk_widget_grab_focus(widget);

	return TRUE;
}

static void vf_gesture_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GqMouseButtonEvent event = {
		gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)),
		x,
		y,
		gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture)),
		static_cast<guint>(n_press)
	};
	vf_press_cb(widget, &event, data);
}

static void vf_gesture_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GqMouseButtonEvent event = {
		gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)),
		x,
		y,
		gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture)),
		static_cast<guint>(n_press)
	};
	vf_release_cb(widget, &event, data);
}

static void vf_file_filter_gesture_press_cb(GtkGestureClick *gesture, gint, gdouble, gdouble, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	vf_file_filter_press_cb(widget, data);
}

static GtkWidget *vf_marks_filter_init(ViewFile *vf)
{
	GtkWidget *frame = gtk_frame_new(nullptr);
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	gint i;

	for (i = 0; i < FILEDATA_MARKS_SIZE ; i++)
		{
		GtkWidget *check = gtk_check_button_new();
		gq_gtk_box_pack_start(GTK_BOX(hbox), check, FALSE, FALSE, 0);
		g_signal_connect(G_OBJECT(check), "toggled",
			 G_CALLBACK(vf_marks_filter_toggle_cb), vf);

		GtkGesture *gesture = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);

		g_signal_connect(gesture, "released", G_CALLBACK(vf_marks_tooltip_cb), GINT_TO_POINTER(i));

		gtk_widget_add_controller(check, GTK_EVENT_CONTROLLER(gesture));

		gtk_widget_set_tooltip_text(check, options->marks_tooltips[i]);

		vf->filter_check[i] = check;
		}

	gq_gtk_container_add(frame, hbox);

	return frame;
}

void vf_file_filter_set(ViewFile *vf, gboolean enable)
{
	gtk_widget_set_visible(vf->file_filter.combo, enable);
	gtk_widget_set_visible(vf->file_filter.frame, enable);

	vf_refresh(vf);
}

static gboolean vf_file_filter_class_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gint i;

	gboolean state = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
	i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "filter-index"));
	if (i >= 0 && i < FILE_FORMAT_CLASSES) options->class_filter[i] = state;
	vf_refresh(vf);

	return TRUE;
}

static gboolean vf_file_filter_rating_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gint i;

	gboolean state = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
	i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "filter-index"));

	options->rating_filter = state ? (options->rating_filter | (1U << i)) : (options->rating_filter & ~(1U << i));

	vf_refresh(vf);

	return TRUE;
}

static gboolean vf_file_filter_class_set_all(GtkWidget *widget, gpointer data, gboolean state)
{
	GtkWidget *parent = gtk_widget_get_parent(widget);
	g_autoptr(GList) children = gq_gtk_widget_get_children(GTK_WIDGET(parent));

	GList *work = children;
	for (gboolean &class_filter : options->class_filter)
		{
		class_filter = state;

		if (work)
			{
			gtk_check_button_set_active(GTK_CHECK_BUTTON(work->data), state);
			work = work->next;
			}
		}

	vf_refresh(static_cast<ViewFile *>(data));

	return TRUE;
}

static gboolean vf_file_filter_rating_set_all(GtkWidget *widget, gpointer data, gboolean state)
{
	GtkWidget *parent = gtk_widget_get_parent(widget);
	g_autoptr(GList) children = gq_gtk_widget_get_children(GTK_WIDGET(parent));

	if (state)
		{
		options->rating_filter = 0x00FFFF;
		}
	else
		{
		options->rating_filter = 0;
		}

	GList *work = children;

	while (work)
		{
		/* Select All and Ignore Rating are not check box menu items */
		if (GTK_IS_CHECK_BUTTON(work->data))
			{
			gtk_check_button_set_active(GTK_CHECK_BUTTON(work->data), state);
			}
		work = work->next;
		}

	vf_refresh(static_cast<ViewFile *>(data));

	return TRUE;
}

static gboolean vf_file_filter_class_select_all_cb(GtkWidget *widget, gpointer data)
{
	return vf_file_filter_class_set_all(widget, data, TRUE);
}

static gboolean vf_file_filter_class_select_none_cb(GtkWidget *widget, gpointer data)
{
	return vf_file_filter_class_set_all(widget, data, FALSE);
}

static gboolean vf_file_filter_rating_select_all_cb(GtkWidget *widget, gpointer data)
{
	return vf_file_filter_rating_set_all(widget, data, TRUE);
}

static gboolean vf_file_filter_star_select_none_cb(GtkWidget *widget, gpointer data)
{
	return vf_file_filter_rating_set_all(widget, data, FALSE);
}

static GtkWidget *class_filter_menu (ViewFile *vf)
{
	GtkWidget *menu = popup_menu_short_lived();

	for (int i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		GtkWidget *menu_item = gtk_check_button_new_with_label(format_class_list[i]);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(menu_item), options->class_filter[i]);
		g_object_set_data(G_OBJECT(menu_item), "filter-index", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(menu_item), "toggled", G_CALLBACK(vf_file_filter_class_cb), vf);
		gtk_box_append(GTK_BOX(menu), menu_item);
		gtk_widget_show(menu_item);
		}

	menu_item_add_simple(menu, _("Select all"), G_CALLBACK(vf_file_filter_class_select_all_cb), vf);
	menu_item_add_simple(menu, _("Select none"), G_CALLBACK(vf_file_filter_class_select_none_cb), vf);

	return menu;
}

static GtkWidget *rating_filter_menu(ViewFile *vf)
{
	GtkWidget *menu = popup_menu_short_lived();

	for (int i = 0; i < FORMAT_RATING_COUNT; i++)
		{
		GtkWidget *menu_item = gtk_check_button_new_with_label(format_rating_list[i]);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(menu_item), (options->rating_filter & (1U << i)));
		g_object_set_data(G_OBJECT(menu_item), "filter-index", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(menu_item), "toggled", G_CALLBACK(vf_file_filter_rating_cb), vf);
		gtk_box_append(GTK_BOX(menu), menu_item);
		gtk_widget_show(menu_item);
		}

	menu_item_add_simple(menu, _("Select all"), G_CALLBACK(vf_file_filter_rating_select_all_cb), vf);
	menu_item_add_simple(menu, _("Ignore Rating"), G_CALLBACK(vf_file_filter_star_select_none_cb), vf);

	return menu;
}

static void case_sensitive_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf->file_filter.case_sensitive = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
	vf_refresh(vf);
}

static void file_filter_clear_cb(GtkEntry *entry, GtkEntryIconPosition pos, GdkEvent *, gpointer)
{
	if (pos != GTK_ENTRY_ICON_SECONDARY) return;

	gq_gtk_entry_set_text(entry, "");
	gtk_widget_grab_focus(GTK_WIDGET(entry));
}

static GtkWidget *vf_file_filter_init(ViewFile *vf)
{
	GtkWidget *frame = gtk_frame_new(nullptr);
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *combo_entry;
	GtkWidget *icon;
	GtkWidget *label;

	vf->file_filter.combo = gtk_combo_box_text_new_with_entry();
	combo_entry = gtk_combo_box_get_child(GTK_COMBO_BOX(vf->file_filter.combo));
	if (!GTK_IS_ENTRY(combo_entry)) return frame;

	gtk_widget_show(combo_entry);
	gtk_widget_show(vf->file_filter.combo);
	gtk_widget_set_tooltip_text(vf->file_filter.combo, _("Use regular expressions"));

	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(combo_entry), GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(combo_entry), GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	g_signal_connect(GTK_ENTRY(combo_entry), "icon-press",
	                 G_CALLBACK(file_filter_clear_cb), nullptr);

	const HistoryList *history_list = history_list_find_by_key("file_filter");
	if (history_list)
		{
		vf->file_filter.count = history_list->size();
		for (const std::string &item : *history_list)
			{
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo), item.c_str());
			}
		}
	gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), 0);

	g_signal_connect(G_OBJECT(combo_entry), "activate",
		G_CALLBACK(vf_file_filter_save_cb), vf);

	g_signal_connect(G_OBJECT(vf->file_filter.combo), "changed",
		G_CALLBACK(vf_file_filter_cb), vf);

	GtkGesture *filter_gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(filter_gesture), 0);
	g_signal_connect(filter_gesture, "pressed", G_CALLBACK(vf_file_filter_gesture_press_cb), vf);
	gtk_widget_add_controller(combo_entry, GTK_EVENT_CONTROLLER(filter_gesture));

	gq_gtk_box_pack_start(GTK_BOX(hbox), vf->file_filter.combo, FALSE, FALSE, 0);
	gtk_widget_show(vf->file_filter.combo);
	gq_gtk_container_add(frame, hbox);
	gtk_widget_show(hbox);

	GtkWidget *case_sensitive = gtk_check_button_new_with_label(_("Case"));
	gq_gtk_box_pack_start(GTK_BOX(hbox), case_sensitive, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(case_sensitive, _("Case sensitive"));
	g_signal_connect(G_OBJECT(case_sensitive), "toggled", G_CALLBACK(case_sensitive_cb), vf);
	gtk_widget_show(case_sensitive);

	/* Temporary GTK4 stub: the old class/rating GtkMenuBar filter UI is disabled until ported. */
	GtkWidget *box_class = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	icon = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN);
	label = gtk_label_new(_("Class"));

	gq_gtk_box_pack_start(GTK_BOX(box_class), label, FALSE, FALSE, 0);
	gq_gtk_box_pack_start(GTK_BOX(box_class), icon, FALSE, FALSE, 0);

	GtkWidget *box_rating = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	icon = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN);
	label = gtk_label_new(_("Rating"));

	gq_gtk_box_pack_start(GTK_BOX(box_rating), label, FALSE, FALSE, 0);
	gq_gtk_box_pack_end(GTK_BOX(box_rating), icon, FALSE, FALSE, 0);

	(void)box_class;
	(void)box_rating;
	(void)icon;
	(void)label;

	return frame;
}

void vf_mark_filter_toggle(ViewFile *vf, gint mark)
{
	gint n = mark - 1;
	auto *filter_check = GTK_CHECK_BUTTON(vf->filter_check[n]);
	gtk_check_button_set_active(filter_check, !gtk_check_button_get_active(filter_check));
}

ViewFile *vf_new(FileViewType type, FileData *dir_fd)
{
	ViewFile *vf;

	vf = g_new0(ViewFile, 1);

	vf->type = type;
	vf->sort = { SORT_NAME, TRUE, FALSE };
	vf->read_metadata_in_idle_id = 0;

	vf->scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(vf->scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vf->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	vf->filter = vf_marks_filter_init(vf);
	vf->file_filter.frame = vf_file_filter_init(vf);

	vf->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(vf->widget), vf->filter, FALSE, FALSE, 0);
	gq_gtk_box_pack_start(GTK_BOX(vf->widget), vf->file_filter.frame, FALSE, FALSE, 0);
	gq_gtk_box_pack_start(GTK_BOX(vf->widget), vf->scrolled, TRUE, TRUE, 0);
	gtk_widget_show(vf->scrolled);

	g_signal_connect(G_OBJECT(vf->widget), "destroy",
			 G_CALLBACK(vf_destroy_cb), vf);

	switch (type)
	{
	case FILEVIEW_LIST: vf = vflist_new(vf); break;
	case FILEVIEW_ICON: vf = vficon_new(vf); break;
	}

	GtkEventController *key_controller = gtk_event_controller_key_new();

	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(vf_press_key_cb), vf);

	gtk_widget_add_controller(vf->listview, key_controller);

	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	g_signal_connect(gesture, "pressed", G_CALLBACK(vf_gesture_press_cb), vf);
	g_signal_connect(gesture, "released", G_CALLBACK(vf_gesture_release_cb), vf);
	gtk_widget_add_controller(vf->listview, GTK_EVENT_CONTROLLER(gesture));

	gq_gtk_container_add(vf->scrolled, vf->listview);
	gtk_widget_show(vf->listview);

	if (dir_fd) vf_set_fd(vf, dir_fd);

	return vf;
}

void vf_set_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gpointer data), gpointer data)
{
	vf->func_status = func;
	vf->data_status = data;
}

void vf_set_thumb_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gdouble val, const gchar *text, gpointer data), gpointer data)
{
	vf->func_thumb_status = func;
	vf->data_thumb_status = data;
}

void vf_thumb_set(ViewFile *vf, gboolean enable)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_thumb_set(vf, enable); break;
	case FILEVIEW_ICON: /*vficon_thumb_set(vf, enable);*/ break;
	}
}


static gboolean vf_thumb_next(ViewFile *vf);

static gdouble vf_thumb_progress(ViewFile *vf)
{
	gint count = 0;
	gint done = 0;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_thumb_progress_count(vf->list, count, done); break;
	case FILEVIEW_ICON: vficon_thumb_progress_count(vf->list, count, done); break;
	}

	DEBUG_1("thumb progress: %d of %d", done, count);
	return static_cast<gdouble>(done) / count;
}

static gdouble vf_read_metadata_in_idle_progress(ViewFile *vf)
{
	gint count = 0;
	gint done = 0;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_read_metadata_progress_count(vf->list, count, done); break;
	case FILEVIEW_ICON: vficon_read_metadata_progress_count(vf->list, count, done); break;
	}

	return static_cast<gdouble>(done) / count;
}

static void vf_set_thumb_fd(ViewFile *vf, FileData *fd)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_set_thumb_fd(vf, fd); break;
	case FILEVIEW_ICON: vficon_set_thumb_fd(vf, fd); break;
	}
}

static void vf_thumb_status(ViewFile *vf, gdouble val, const gchar *text)
{
	if (vf->func_thumb_status)
		{
		vf->func_thumb_status(vf, val, text, vf->data_thumb_status);
		}
}

static void vf_thumb_do(ViewFile *vf, FileData *fd)
{
	if (!fd) return;

	vf_set_thumb_fd(vf, fd);
	vf_thumb_status(vf, vf_thumb_progress(vf), _("Loading thumbs…"));
}

void vf_thumb_cleanup(ViewFile *vf)
{
	vf_thumb_status(vf, 0.0, nullptr);

	vf->thumbs_running = FALSE;

	thumb_loader_free(vf->thumbs_loader);
	vf->thumbs_loader = nullptr;

	vf->thumbs_filedata = nullptr;
}

void vf_thumb_stop(ViewFile *vf)
{
	if (vf->thumbs_running) vf_thumb_cleanup(vf);
}

static void vf_thumb_common_cb(ThumbLoader *tl, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (vf->thumbs_filedata && vf->thumbs_loader == tl)
		{
		vf_thumb_do(vf, vf->thumbs_filedata);
		}

	while (vf_thumb_next(vf));
}

static void vf_thumb_error_cb(ThumbLoader *tl, gpointer data)
{
	vf_thumb_common_cb(tl, data);
}

static void vf_thumb_done_cb(ThumbLoader *tl, gpointer data)
{
	vf_thumb_common_cb(tl, data);
}

static gboolean vf_thumb_next(ViewFile *vf)
{
	FileData *fd = nullptr;

	if (!gtk_widget_get_realized(vf->listview))
		{
		vf_thumb_status(vf, 0.0, nullptr);
		return FALSE;
		}

	switch (vf->type)
	{
	case FILEVIEW_LIST: fd = vflist_thumb_next_fd(vf); break;
	case FILEVIEW_ICON: fd = vficon_thumb_next_fd(vf); break;
	}

	if (!fd)
		{
		/* done */
		vf_thumb_cleanup(vf);
		return FALSE;
		}

	vf->thumbs_filedata = fd;

	thumb_loader_free(vf->thumbs_loader);

	vf->thumbs_loader = thumb_loader_new(options->thumbnails.max_width, options->thumbnails.max_height);
	thumb_loader_set_callbacks(vf->thumbs_loader,
				   vf_thumb_done_cb,
				   vf_thumb_error_cb,
				   nullptr,
				   vf);

	if (!thumb_loader_start(vf->thumbs_loader, fd))
		{
		/* set icon to unknown, continue */
		DEBUG_1("thumb loader start failed %s", fd->path);
		vf_thumb_do(vf, fd);

		return TRUE;
		}

	return FALSE;
}

static void vf_thumb_reset_all(ViewFile *vf)
{
	GList *work;

	for (work = vf->list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);
		if (fd->thumb_pixbuf)
			{
			g_object_unref(fd->thumb_pixbuf);
			fd->thumb_pixbuf = nullptr;
			}
		}
}

void vf_thumb_update(ViewFile *vf)
{
	vf_thumb_stop(vf);

	if (vf->type == FILEVIEW_LIST && !VFLIST(vf)->thumbs_enabled) return;

	vf_thumb_status(vf, 0.0, _("Loading thumbs…"));
	vf->thumbs_running = TRUE;

	if (thumb_format_changed)
		{
		vf_thumb_reset_all(vf);
		thumb_format_changed = FALSE;
		}

	while (vf_thumb_next(vf));
}

void vf_star_cleanup(ViewFile *vf)
{
	g_clear_handle_id(&vf->stars_id, g_source_remove);
	vf->stars_filedata = nullptr;
}

void vf_star_stop(ViewFile *vf)
{
	vf_star_cleanup(vf);
}

static void vf_set_star_fd(ViewFile *vf, FileData *fd)
{
	if (!fd) return;

	switch (vf->type)
		{
		case FILEVIEW_LIST: vflist_set_star_fd(vf, fd); break;
		case FILEVIEW_ICON: vficon_set_star_fd(vf, fd); break;
		default: break;
		}
}

static gboolean vf_stars_cb(gpointer data);

static gboolean vf_star_next(ViewFile *vf)
{
	FileData *fd = nullptr;

	switch (vf->type)
		{
		case FILEVIEW_LIST: fd = vflist_star_next_fd(vf); break;
		case FILEVIEW_ICON: fd = vficon_star_next_fd(vf); break;
		default: break;
		}

	if (!fd)
		{
		/* done */
		vf_star_cleanup(vf);
		return FALSE;
		}

	vf->stars_filedata = fd;

	if (vf->stars_id == 0)
		{
		vf->stars_id = g_idle_add_full(G_PRIORITY_LOW, vf_stars_cb, vf, nullptr);
		}

	return TRUE;
}

static gboolean vf_stars_cb(gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	FileData *fd = vf->stars_filedata;
	if (!fd) return G_SOURCE_REMOVE;

	read_rating_data(fd);

	vf_set_star_fd(vf, fd);

	if (!vf_star_next(vf))
		{
		vf->stars_filedata = nullptr;
		vf->stars_id = 0;
		return G_SOURCE_REMOVE;
		}

	return G_SOURCE_CONTINUE;
}

void vf_star_update(ViewFile *vf)
{
	vf_star_stop(vf);

	if (!options->show_star_rating)
		{
		return;
		}

	vf_star_next(vf);
}

void vf_marks_set(ViewFile *vf, gboolean enable)
{
	if (vf->marks_enabled == enable) return;

	vf->marks_enabled = enable;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_marks_set(vf, enable); break;
	case FILEVIEW_ICON: vficon_marks_set(vf, enable); break;
	}

	gtk_widget_set_visible(vf->filter, enable);

	vf_refresh_idle(vf);
}

guint vf_marks_get_filter(ViewFile *vf)
{
	guint ret = 0;
	gint i;
	if (!vf->marks_enabled) return 0;

	for (i = 0; i < FILEDATA_MARKS_SIZE ; i++)
		{
		if (gtk_check_button_get_active(GTK_CHECK_BUTTON(vf->filter_check[i])))
			{
			ret |= 1 << i;
			}
		}
	return ret;
}

GRegex *vf_file_filter_get_filter(ViewFile *vf)
{
	if (!gtk_widget_get_visible(vf->file_filter.combo))
		{
		return g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	g_autofree gchar *file_filter_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo));
	if (file_filter_text[0] == '\0')
		{
		return g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	g_autoptr(GError) error = nullptr;
	GRegex *ret = g_regex_new(file_filter_text, vf->file_filter.case_sensitive ? static_cast<GRegexCompileFlags>(0) : G_REGEX_CASELESS, static_cast<GRegexMatchFlags>(0), &error);
	if (error)
		{
		log_printf("Error: could not compile regular expression %s\n%s\n", file_filter_text, error->message);
		ret = g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	return ret;
}

guint vf_class_get_filter(ViewFile *vf)
{
	guint ret = 0;
	gint i;

	if (!gtk_widget_get_visible(vf->file_filter.combo))
		{
		return G_MAXUINT;
		}

	for ( i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		if (options->class_filter[i])
			{
			ret |= 1 << i;
			}
		}

	return ret;
}

void vf_set_layout(ViewFile *vf, LayoutWindow *layout)
{
	vf->layout = layout;

	if (layout && layout->window &&
	    !g_action_map_lookup_action(G_ACTION_MAP(layout->window), "view-file-view-new"))
		{
		auto *application = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(layout->window)));
		register_actions_from_table(application, layout->window, view_file_actions, get_keyfile_merged(), layout);
		}
}

const ActionDef *get_view_file_actions()
{
	return view_file_actions;
}


/*
 *-----------------------------------------------------------------------------
 * maintenance (for rename, move, remove)
 *-----------------------------------------------------------------------------
 */

static gboolean vf_refresh_idle_cb(gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf_refresh(vf);
	vf->refresh_idle_id = 0;
	return G_SOURCE_REMOVE;
}

void vf_refresh_idle_cancel(ViewFile *vf)
{
	g_clear_handle_id(&vf->refresh_idle_id, g_source_remove);
}


void vf_refresh_idle(ViewFile *vf)
{
	if (!vf->refresh_idle_id)
		{
		vf->time_refresh_set = time(nullptr);
		/* file operations run with G_PRIORITY_DEFAULT_IDLE */
		vf->refresh_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE + 50, vf_refresh_idle_cb, vf, nullptr);
		}
	else if (time(nullptr) - vf->time_refresh_set > 1)
		{
		/* more than 1 sec since last update - increase priority */
		vf_refresh_idle_cancel(vf);
		vf->time_refresh_set = time(nullptr);
		vf->refresh_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE - 50, vf_refresh_idle_cb, vf, nullptr);
		}
}

void vf_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean refresh;

	auto interested = static_cast<NotifyType>(NOTIFY_CHANGE | NOTIFY_REREAD | NOTIFY_GROUPING);
	if (options->show_star_rating)
		{
		interested = static_cast<NotifyType>(interested | NOTIFY_METADATA);
		}
	if (vf->marks_enabled) interested = static_cast<NotifyType>(interested | NOTIFY_MARKS | NOTIFY_METADATA);
	/** @FIXME NOTIFY_METADATA should be checked by the keyword-to-mark functions and converted to NOTIFY_MARKS only if there was a change */

	if (!(type & interested) || vf->refresh_idle_id || !vf->dir_fd) return;

	refresh = (fd == vf->dir_fd);

	if (!refresh)
		{
		g_autofree gchar *base = remove_level_from_path(fd->path);
		refresh = (g_strcmp0(base, vf->dir_fd->path) == 0);
		}

	if ((type & NOTIFY_CHANGE) && fd->change)
		{
		if (!refresh && fd->change->dest)
			{
			g_autofree gchar *dest_base = remove_level_from_path(fd->change->dest);
			refresh = (g_strcmp0(dest_base, vf->dir_fd->path) == 0);
			}

		if (!refresh && fd->change->source)
			{
			g_autofree gchar *source_base = remove_level_from_path(fd->change->source);
			refresh = (g_strcmp0(source_base, vf->dir_fd->path) == 0);
			}
		}

	if (refresh)
		{
		DEBUG_1("Notify vf: %s %04x", fd->path, type);
		vf_refresh_idle(vf);
		}
}

static gboolean vf_read_metadata_in_idle_cb(gpointer data)
{
	FileData *fd;
	auto vf = static_cast<ViewFile *>(data);
	GList *work;

	vf_thumb_status(vf, vf_read_metadata_in_idle_progress(vf), _("Loading meta…"));

	work = vf->list;

	while (work)
		{
		fd = static_cast<FileData *>(work->data);

		if (fd && !fd->metadata_in_idle_loaded)
			{
			if (!fd->exifdate)
				{
				read_exif_time_data(fd);
				}
			if (!fd->exifdate_digitized)
				{
				read_exif_time_digitized_data(fd);
				}
			if (fd->rating == STAR_RATING_NOT_READ)
				{
				read_rating_data(fd);
				}
			fd->metadata_in_idle_loaded = TRUE;
			return G_SOURCE_CONTINUE;
			}
		work = work->next;
		}

	vf_thumb_status(vf, 0.0, nullptr);
	vf->read_metadata_in_idle_id = 0;
	vf_refresh(vf);
	return G_SOURCE_REMOVE;
}

static void vf_read_metadata_in_idle_finished_cb(gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf_thumb_status(vf, 0.0, _("Loading meta…"));
	vf->read_metadata_in_idle_id = 0;
}

void vf_read_metadata_in_idle(ViewFile *vf)
{
	if (!vf) return;

	if (vf->read_metadata_in_idle_id)
		{
		g_idle_remove_by_data(vf);
		}
	vf->read_metadata_in_idle_id = 0;

	if (vf->list)
		{
		vf->read_metadata_in_idle_id = g_idle_add_full(G_PRIORITY_LOW, vf_read_metadata_in_idle_cb, vf, vf_read_metadata_in_idle_finished_cb);
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
