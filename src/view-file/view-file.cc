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

static gboolean vf_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_press_key_cb(vf, widget, event); break;
	case FILEVIEW_ICON: ret = vficon_press_key_cb(vf, widget, event); break;
	default: ret = FALSE;
	}

	return ret;
}

/*
 *-------------------------------------------------------------------
 * mouse
 *-------------------------------------------------------------------
 */

static gboolean vf_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_press_cb(vf, widget, bevent); break;
	case FILEVIEW_ICON: ret = vficon_press_cb(vf, widget, bevent); break;
	default: ret = FALSE;
	}

	return ret;
}

static gboolean vf_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean ret;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_release_cb(vf, widget, bevent); break;
	case FILEVIEW_ICON: ret = vficon_release_cb(vf, widget, bevent); break;
	default: ret = FALSE;
	}

	return ret;
}


/*
 *-----------------------------------------------------------------------------
 * selections
 *-----------------------------------------------------------------------------
 */

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

void vf_select_list(ViewFile *vf, GList *list)
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

#if !HAVE_GTK4
static gboolean vf_is_selected(ViewFile *vf, FileData *fd)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: return vflist_is_selected(vf, fd);
	case FILEVIEW_ICON: return vficon_is_selected(vf, fd);
	}

	return FALSE;
}

static void vf_dnd_get(GtkWidget *, GdkDragContext *,
                       GtkSelectionData *selection_data, guint,
                       guint, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	if (!vf->click_fd) return;

	g_autoptr(FileDataList) list = nullptr;

	if (vf_is_selected(vf, vf->click_fd))
		{
		list = vf_selection_get_list(vf);
		}
	else
		{
		list = g_list_append(nullptr, file_data_ref(vf->click_fd));
		}

	if (!list) return;

	uri_selection_data_set_uris_from_filelist(selection_data, list);
}

static void vf_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_dnd_begin(vf, widget, context); break;
	case FILEVIEW_ICON: vficon_dnd_begin(vf, widget, context); break;
	}
}

static void vf_dnd_end(GtkWidget *, GdkDragContext *context, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_dnd_end(vf, context); break;
	case FILEVIEW_ICON: vficon_dnd_end(vf, context); break;
	}
}

static FileData *vf_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: return vflist_find_data_by_coord(vf, x, y, iter);
	case FILEVIEW_ICON: return vficon_find_data_by_coord(vf, x, y, iter);
	}

	return nullptr;
}

static void vf_drag_data_received(GtkWidget *, GdkDragContext *,
                                  int x, int y, GtkSelectionData *selection,
                                  guint info, guint, gpointer data)
{
	if (info != TARGET_TEXT_PLAIN) return;

	auto *vf = static_cast<ViewFile *>(data);

	FileData *fd = vf_find_data_by_coord(vf, x, y, nullptr);
	if (!fd) return;

	/* Add keywords to file */
	g_autofree auto str = reinterpret_cast<gchar *>(gtk_selection_data_get_text(selection));
	GList *kw_list = string_to_keywords_list(str);

	metadata_append_list(fd, KEYWORD_KEY, kw_list);

	g_list_free_full(kw_list, g_free);
}

static void vf_dnd_init(ViewFile *vf)
{
	gq_gtk_drag_source_set(vf->listview, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
	                    dnd_file_drag_types.data(), dnd_file_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gq_gtk_drag_dest_set(vf->listview, GTK_DEST_DEFAULT_ALL,
	                  dnd_file_drag_types.data(), dnd_file_drag_types.size(),
	                  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));

	gq_drag_g_signal_connect(G_OBJECT(vf->listview), "drag_data_get",
	                 G_CALLBACK(vf_dnd_get), vf);
	gq_drag_g_signal_connect(G_OBJECT(vf->listview), "drag_begin",
	                 G_CALLBACK(vf_dnd_begin), vf);
	gq_drag_g_signal_connect(G_OBJECT(vf->listview), "drag_end",
	                 G_CALLBACK(vf_dnd_end), vf);
	gq_drag_g_signal_connect(G_OBJECT(vf->listview), "drag_data_received",
	                 G_CALLBACK(vf_drag_data_received), vf);
}
#endif

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

static void vf_pop_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(submenu_item_get_data(widget));
	if (!vf) return;

	auto *key = static_cast<const gchar *>(data);

	file_util_start_editor_from_filelist(key, vf_pop_menu_file_list(vf), vf->dir_fd->path, vf->listview);
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

static void vf_pop_menu_sort_cb(GtkWidget *widget, gpointer data)
{
	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	auto *vf = static_cast<ViewFile *>(data);
	if (!vf) return;

	auto sort = vf->sort;
	sort.method = static_cast<SortType>(GPOINTER_TO_INT(menu_item_radio_get_data(widget)));

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

static void vf_pop_menu_sort_ascend_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	auto sort = vf->sort;
	sort.ascending = !sort.ascending;

	if (vf->layout)
		{
		layout_sort_set_files(vf->layout, sort);
		}
	else
		{
		vf_sort_set(vf, sort);
		}
}

static void vf_pop_menu_sort_case_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	auto sort = vf->sort;
	sort.case_sensitive = !sort.case_sensitive;

	if (vf->layout)
		{
		layout_sort_set_files(vf->layout, sort);
		}
	else
		{
		vf_sort_set(vf, sort);
		}
}

template<MarkToSelectionMode mode>
static void vf_pop_menu_mark_to_selection_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vf_mark_to_selection(vf, vf->active_mark, mode);
}

template<SelectionToMarkMode mode>
static void vf_pop_menu_selection_to_mark_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vf_selection_to_mark(vf, vf->active_mark, mode);
}

template<FileViewType file_view_type>
static void vf_pop_menu_toggle_view_type_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	if (!vf->layout) return;

	layout_views_set(vf->layout, vf->layout->options.dir_view_type, file_view_type);
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

/**
 * @brief Add file selection list to a collection
 * @param[in] widget
 * @param[in] data Index to the collection list menu item selected, or -1 for new collection
 *
 */
static void vf_pop_menu_collections_cb(GtkWidget *widget, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(submenu_item_get_data(widget));

	g_autoptr(FileDataList) selection_list = vf_selection_get_list(vf);
	collection_by_index_add_filelist(GPOINTER_TO_INT(data), selection_list);
}

static void vf_pop_menu_show_star_rating_cb(GtkWidget *, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	options->show_star_rating = !options->show_star_rating;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_pop_menu_show_star_rating_cb(vf); break;
	case FILEVIEW_ICON: vficon_pop_menu_show_star_rating_cb(vf); break;
	}
}

GtkWidget *vf_pop_menu(ViewFile *vf)
{
	GtkWidget *menu;
	GtkWidget *submenu;
	gboolean active = FALSE;
	gboolean class_archive = FALSE;
	GtkAccelGroup *accel_group;

	if (vf->type == FILEVIEW_LIST)
		{
		vflist_color_set(vf, vf->click_fd, TRUE);
		}

	active = (vf->click_fd != nullptr);
	class_archive = (vf->click_fd != nullptr && vf->click_fd->format_class == FORMAT_CLASS_ARCHIVE);

	menu = popup_menu_short_lived();

	accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vf_popup_destroy_cb), vf);

	if (vf->clicked_mark > 0)
		{
		gint mark = vf->clicked_mark;
		g_autofree gchar *str_set_mark = g_strdup_printf(_("_Set mark %d"), mark);
		g_autofree gchar *str_res_mark = g_strdup_printf(_("_Reset mark %d"), mark);
		g_autofree gchar *str_toggle_mark = g_strdup_printf(_("_Toggle mark %d"), mark);
		g_autofree gchar *str_sel_mark = g_strdup_printf(_("_Select mark %d"), mark);
		g_autofree gchar *str_sel_mark_or = g_strdup_printf(_("_Add mark %d"), mark);
		g_autofree gchar *str_sel_mark_and = g_strdup_printf(_("_Intersection with mark %d"), mark);
		g_autofree gchar *str_sel_mark_minus = g_strdup_printf(_("_Unselect mark %d"), mark);

		g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

		vf->active_mark = mark;
		vf->clicked_mark = 0;

		menu_item_add_sensitive(menu, str_set_mark, active,
		                        G_CALLBACK(vf_pop_menu_selection_to_mark_cb<STM_MODE_SET>), vf);

		menu_item_add_sensitive(menu, str_res_mark, active,
		                        G_CALLBACK(vf_pop_menu_selection_to_mark_cb<STM_MODE_RESET>), vf);

		menu_item_add_sensitive(menu, str_toggle_mark, active,
		                        G_CALLBACK(vf_pop_menu_selection_to_mark_cb<STM_MODE_TOGGLE>), vf);

		menu_item_add_divider(menu);

		menu_item_add_sensitive(menu, str_sel_mark, active,
		                        G_CALLBACK(vf_pop_menu_mark_to_selection_cb<MTS_MODE_SET>), vf);
		menu_item_add_sensitive(menu, str_sel_mark_or, active,
		                        G_CALLBACK(vf_pop_menu_mark_to_selection_cb<MTS_MODE_OR>), vf);
		menu_item_add_sensitive(menu, str_sel_mark_and, active,
		                        G_CALLBACK(vf_pop_menu_mark_to_selection_cb<MTS_MODE_AND>), vf);
		menu_item_add_sensitive(menu, str_sel_mark_minus, active,
		                        G_CALLBACK(vf_pop_menu_mark_to_selection_cb<MTS_MODE_MINUS>), vf);

		menu_item_add_divider(menu);
		}

	vf->editmenu_fd_list = vf_pop_menu_file_list(vf);
	submenu_add_edit(menu, active, vf->editmenu_fd_list, G_CALLBACK(vf_pop_menu_edit_cb), vf);

	menu_item_add_icon_sensitive(menu, _("View in _new window"), GQ_ICON_NEW, active,
				      G_CALLBACK(vf_pop_menu_view_cb), vf);

	menu_item_add_icon_sensitive(menu, _("Open archive"), GQ_ICON_OPEN, active & class_archive, G_CALLBACK(vf_pop_menu_open_archive_cb), vf);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("_Copy..."), GQ_ICON_COPY, active,
				      G_CALLBACK(vf_pop_menu_copy_cb), vf);
	menu_item_add_sensitive(menu, _("_Move..."), active,
				G_CALLBACK(vf_pop_menu_move_cb), vf);
	menu_item_add_sensitive(menu, _("_Rename..."), active,
				G_CALLBACK(vf_pop_menu_rename_cb), vf);
	menu_item_add_sensitive(menu, _("_Copy to clipboard"), active,
	                        G_CALLBACK(vf_pop_menu_copy_path_cb<TRUE>), vf);
	menu_item_add_sensitive(menu, _("_Copy to clipboard (unquoted)"), active,
	                        G_CALLBACK(vf_pop_menu_copy_path_cb<FALSE>), vf);
	menu_item_add_sensitive(menu, _("_Cut to clipboard"), active,
				G_CALLBACK(vf_pop_menu_cut_path_cb), vf);
	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, options->file_ops.confirm_move_to_trash ?
	                                 _("Move selection to Trash...") : _("Move selection to Trash"),
	                             GQ_ICON_DELETE, active,
	                             G_CALLBACK(vf_pop_menu_delete_cb<TRUE>), vf);
	menu_item_add_icon_sensitive(menu, options->file_ops.confirm_delete ?
	                                 _("_Delete selection...") : _("_Delete selection"),
	                             GQ_ICON_DELETE_SHRED, active,
	                             G_CALLBACK(vf_pop_menu_delete_cb<FALSE>), vf);
	menu_item_add_divider(menu);

	menu_item_add_sensitive(menu, _("Enable file _grouping"), active,
	                        G_CALLBACK(vf_pop_menu_disable_grouping_cb<FALSE>), vf);
	menu_item_add_sensitive(menu, _("Disable file groupi_ng"), active,
	                        G_CALLBACK(vf_pop_menu_disable_grouping_cb<TRUE>), vf);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("_Find duplicates..."), GQ_ICON_FIND, active,
				G_CALLBACK(vf_pop_menu_duplicates_cb), vf);
	menu_item_add_divider(menu);

	submenu = submenu_add_collections(menu, active,
	                                  G_CALLBACK(vf_pop_menu_collections_cb), vf);
	menu_item_add_divider(menu);

	submenu = submenu_add_sort(menu, G_CALLBACK(vf_pop_menu_sort_cb), vf,
	                           TRUE, vf->sort.method);
	menu_item_add_divider(submenu);
	menu_item_add_check(submenu, _("Ascending"), vf->sort.ascending,
	                    G_CALLBACK(vf_pop_menu_sort_ascend_cb), vf);
	menu_item_add_check(submenu, _("Case"), vf->sort.case_sensitive,
	                    G_CALLBACK(vf_pop_menu_sort_case_cb), vf);

	menu_item_add_radio(menu, _("Images as List"), nullptr, vf->type == FILEVIEW_LIST,
	                    G_CALLBACK(vf_pop_menu_toggle_view_type_cb<FILEVIEW_LIST>), vf);
	menu_item_add_radio(menu, _("Images as Icons"), nullptr, vf->type == FILEVIEW_ICON,
	                    G_CALLBACK(vf_pop_menu_toggle_view_type_cb<FILEVIEW_ICON>), vf);

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_pop_menu_add_items(vf, menu); break;
	case FILEVIEW_ICON: vficon_pop_menu_add_items(vf, menu); break;
	}

	menu_item_add_check(menu, _("Show star rating"), options->show_star_rating,
	                    G_CALLBACK(vf_pop_menu_show_star_rating_cb), vf);

	menu_item_add_icon(menu, _("Re_fresh"), GQ_ICON_REFRESH, G_CALLBACK(vf_pop_menu_refresh_cb), vf);

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

static gboolean vf_marks_tooltip_cb(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	if (event->button != GDK_BUTTON_SECONDARY) return FALSE;

	auto mte = g_new0(MarksTextEntry, 1);
	mte->mark_no = GPOINTER_TO_INT(user_data);
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
	gq_gtk_grid_attach_default(GTK_GRID(table), mte->edit_widget, 1, 2, 0, 1);
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

	return TRUE;
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

static gboolean vf_file_filter_press_cb(GtkWidget *widget, GdkEventButton *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vf->file_filter.last_selected = gtk_combo_box_get_active(GTK_COMBO_BOX(vf->file_filter.combo));

	gtk_widget_grab_focus(widget);

	return TRUE;
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
		g_signal_connect(G_OBJECT(check), "button_press_event",
			 G_CALLBACK(vf_marks_tooltip_cb), GINT_TO_POINTER(i));
		gtk_widget_set_tooltip_text(check, options->marks_tooltips[i]);

		gtk_widget_show(check);
		vf->filter_check[i] = check;
		}
	gq_gtk_container_add(frame, hbox);
	gtk_widget_show(hbox);
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

	gboolean state = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		if (g_strcmp0(format_class_list[i], gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) == 0)
			{
			options->class_filter[i] = state;
			}
		}
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
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(work->data), state);
			work = work->next;
			}
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

static GtkWidget *class_filter_menu (ViewFile *vf)
{
	GtkWidget *menu = gtk_menu_new();

	for (int i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		GtkWidget *menu_item = gtk_check_menu_item_new_with_label(format_class_list[i]);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item), options->class_filter[i]);
		g_signal_connect(G_OBJECT(menu_item), "toggled", G_CALLBACK(vf_file_filter_class_cb), vf);
		gtk_menu_shell_append(GTK_MENU_SHELL (menu), menu_item);
		gtk_widget_show(menu_item);
		}

	menu_item_add_simple(menu, _("Select all"), G_CALLBACK(vf_file_filter_class_select_all_cb), vf);
	menu_item_add_simple(menu, _("Select none"), G_CALLBACK(vf_file_filter_class_select_none_cb), vf);

	return menu;
}

static void case_sensitive_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf->file_filter.case_sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
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
	GList *work;
	gint n = 0;
	GtkWidget *combo_entry;
	GtkWidget *menubar;
	GtkWidget *box;
	GtkWidget *icon;
	GtkWidget *label;

	vf->file_filter.combo = gtk_combo_box_text_new_with_entry();
	combo_entry = gq_gtk_bin_get_child(GTK_WIDGET(vf->file_filter.combo));
	gtk_widget_show(gq_gtk_bin_get_child(GTK_WIDGET(vf->file_filter.combo)));
	gtk_widget_show(vf->file_filter.combo);
	gtk_widget_set_tooltip_text(vf->file_filter.combo, _("Use regular expressions"));

	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(combo_entry), GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(combo_entry), GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	g_signal_connect(GTK_ENTRY(combo_entry), "icon-press",
	                 G_CALLBACK(file_filter_clear_cb), nullptr);

	work = history_list_get_by_key("file_filter");
	while (work)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo), static_cast<gchar *>(work->data));
		work = work->next;
		n++;
		vf->file_filter.count = n;
		}
	gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), 0);

	g_signal_connect(G_OBJECT(combo_entry), "activate",
		G_CALLBACK(vf_file_filter_save_cb), vf);

	g_signal_connect(G_OBJECT(vf->file_filter.combo), "changed",
		G_CALLBACK(vf_file_filter_cb), vf);

	g_signal_connect(G_OBJECT(combo_entry), "button_press_event",
			 G_CALLBACK(vf_file_filter_press_cb), vf);

	gq_gtk_box_pack_start(GTK_BOX(hbox), vf->file_filter.combo, FALSE, FALSE, 0);
	gtk_widget_show(vf->file_filter.combo);
	gq_gtk_container_add(frame, hbox);
	gtk_widget_show(hbox);

	GtkWidget *case_sensitive = gtk_check_button_new_with_label(_("Case"));
	gq_gtk_box_pack_start(GTK_BOX(hbox), case_sensitive, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(case_sensitive, _("Case sensitive"));
	g_signal_connect(G_OBJECT(case_sensitive), "clicked", G_CALLBACK(case_sensitive_cb), vf);
	gtk_widget_show(case_sensitive);

	menubar = gtk_menu_bar_new();
	gq_gtk_box_pack_start(GTK_BOX(hbox), menubar, FALSE, TRUE, 0);
	gtk_widget_show(menubar);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	icon = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_MENU);
	label = gtk_label_new(_("Class"));

	gq_gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gq_gtk_box_pack_end(GTK_BOX(box), icon, FALSE, FALSE, 0);

	GtkWidget *menuitem = gtk_menu_item_new();

	gtk_widget_set_tooltip_text(menuitem, _("Select Class filter"));
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), class_filter_menu(vf));
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuitem);
	gq_gtk_container_add(menuitem, box);
	gq_gtk_widget_show_all(menuitem);

	return frame;
}

void vf_mark_filter_toggle(ViewFile *vf, gint mark)
{
	gint n = mark - 1;
	auto *filter_check = GTK_TOGGLE_BUTTON(vf->filter_check[n]);
	gtk_toggle_button_set_active(filter_check, !gtk_toggle_button_get_active(filter_check));
}

ViewFile *vf_new(FileViewType type, FileData *dir_fd)
{
	ViewFile *vf;

	vf = g_new0(ViewFile, 1);

	vf->type = type;
	vf->sort = { SORT_NAME, TRUE, FALSE };
	vf->read_metadata_in_idle_id = 0;

	vf->scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(vf->scrolled), GTK_SHADOW_IN);
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

#if !HAVE_GTK4
	vf_dnd_init(vf);
#endif

	g_signal_connect(G_OBJECT(vf->listview), "key_press_event",
			 G_CALLBACK(vf_press_key_cb), vf);
	g_signal_connect(G_OBJECT(vf->listview), "button_press_event",
			 G_CALLBACK(vf_press_cb), vf);
	g_signal_connect(G_OBJECT(vf->listview), "button_release_event",
			 G_CALLBACK(vf_release_cb), vf);

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
	vf_thumb_status(vf, vf_thumb_progress(vf), _("Loading thumbs..."));
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

	vf_thumb_status(vf, 0.0, _("Loading thumbs..."));
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
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vf->filter_check[i])))
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

	vf_thumb_status(vf, vf_read_metadata_in_idle_progress(vf), _("Loading meta..."));

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

	vf_thumb_status(vf, 0.0, _("Loading meta..."));
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
