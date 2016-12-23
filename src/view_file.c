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

#include "main.h"
#include "view_file.h"

#include "collect.h"
#include "collect-table.h"
#include "editors.h"
#include "layout.h"
#include "menu.h"
#include "thumb.h"
#include "ui_menu.h"
#include "ui_fileops.h"
#include "utilops.h"
#include "view_file_list.h"
#include "view_file_icon.h"

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

void vf_sort_set(ViewFile *vf, SortType type, gboolean ascend)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_sort_set(vf, type, ascend); break;
	case FILEVIEW_ICON: vficon_sort_set(vf, type, ascend); break;
	}
}

/*
 *-----------------------------------------------------------------------------
 * row stuff
 *-----------------------------------------------------------------------------
 */

FileData *vf_index_get_data(ViewFile *vf, gint row)
{
	FileData *fd = NULL;

	switch (vf->type)
	{
	case FILEVIEW_LIST: fd = vflist_index_get_data(vf, row); break;
	case FILEVIEW_ICON: fd = vficon_index_get_data(vf, row); break;
	}

	return fd;
}

gint vf_index_by_fd(ViewFile *vf, FileData *fd)
{
	gint index = -1;

	switch (vf->type)
	{
	case FILEVIEW_LIST: index = vflist_index_by_fd(vf, fd); break;
	case FILEVIEW_ICON: index = vficon_index_by_fd(vf, fd); break;
	}

	return index;
}

guint vf_count(ViewFile *vf, gint64 *bytes)
{
	guint count = 0;

	switch (vf->type)
	{
	case FILEVIEW_LIST: count = vflist_count(vf, bytes); break;
	case FILEVIEW_ICON: count = vficon_count(vf, bytes); break;
	}

	return count;
}

GList *vf_get_list(ViewFile *vf)
{
	GList *list = NULL;

	switch (vf->type)
	{
	case FILEVIEW_LIST: list = vflist_get_list(vf); break;
	case FILEVIEW_ICON: list = vficon_get_list(vf); break;
	}

	return list;
}


/*
 *-------------------------------------------------------------------
 * keyboard
 *-------------------------------------------------------------------
 */

static gboolean vf_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	ViewFile *vf = data;
	gboolean ret = FALSE;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_press_key_cb(widget, event, data); break;
	case FILEVIEW_ICON: ret = vficon_press_key_cb(widget, event, data); break;
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
	ViewFile *vf = data;
	gboolean ret = FALSE;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_press_cb(widget, bevent, data); break;
	case FILEVIEW_ICON: ret = vficon_press_cb(widget, bevent, data); break;
	}

	return ret;
}

static gboolean vf_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	ViewFile *vf = data;
	gboolean ret = FALSE;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_release_cb(widget, bevent, data); break;
	case FILEVIEW_ICON: ret = vficon_release_cb(widget, bevent, data); break;
	}

	return ret;
}


/*
 *-----------------------------------------------------------------------------
 * selections
 *-----------------------------------------------------------------------------
 */

gboolean vf_index_is_selected(ViewFile *vf, gint row)
{
	gboolean ret = FALSE;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_index_is_selected(vf, row); break;
	case FILEVIEW_ICON: ret = vficon_index_is_selected(vf, row); break;
	}

	return ret;
}


guint vf_selection_count(ViewFile *vf, gint64 *bytes)
{
	guint count = 0;

	switch (vf->type)
	{
	case FILEVIEW_LIST: count = vflist_selection_count(vf, bytes); break;
	case FILEVIEW_ICON: count = vficon_selection_count(vf, bytes); break;
	}

	return count;
}

GList *vf_selection_get_list(ViewFile *vf)
{
	GList *list = NULL;

	switch (vf->type)
	{
	case FILEVIEW_LIST: list = vflist_selection_get_list(vf); break;
	case FILEVIEW_ICON: list = vficon_selection_get_list(vf); break;
	}

	return list;
}

GList *vf_selection_get_list_by_index(ViewFile *vf)
{
	GList *list = NULL;

	switch (vf->type)
	{
	case FILEVIEW_LIST: list = vflist_selection_get_list_by_index(vf); break;
	case FILEVIEW_ICON: list = vficon_selection_get_list_by_index(vf); break;
	}

	return list;
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


static void vf_dnd_init(ViewFile *vf)
{
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_dnd_init(vf); break;
	case FILEVIEW_ICON: vficon_dnd_init(vf); break;
	}
}

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

GList *vf_pop_menu_file_list(ViewFile *vf)
{
	GList *ret = NULL;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_pop_menu_file_list(vf); break;
	case FILEVIEW_ICON: ret = vficon_pop_menu_file_list(vf); break;
	}

	return ret;
}

GList *vf_selection_get_one(ViewFile *vf, FileData *fd)
{
	GList *ret = NULL;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_selection_get_one(vf, fd); break;
	case FILEVIEW_ICON: ret = vficon_selection_get_one(vf, fd); break;
	}

	return ret;
}

static void vf_pop_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf;
	const gchar *key = data;

	vf = submenu_item_get_data(widget);

	if (!vf) return;

	file_util_start_editor_from_filelist(key, vf_pop_menu_file_list(vf), vf->dir_fd->path, vf->listview);
}

static void vf_pop_menu_view_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_pop_menu_view_cb(widget, data); break;
	case FILEVIEW_ICON: vficon_pop_menu_view_cb(widget, data); break;
	}
}

static void vf_pop_menu_copy_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	file_util_copy(NULL, vf_pop_menu_file_list(vf), NULL, vf->listview);
}

static void vf_pop_menu_move_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	file_util_move(NULL, vf_pop_menu_file_list(vf), NULL, vf->listview);
}

static void vf_pop_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_pop_menu_rename_cb(widget, data); break;
	case FILEVIEW_ICON: vficon_pop_menu_rename_cb(widget, data); break;
	}
}

static void vf_pop_menu_delete_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	file_util_delete(NULL, vf_pop_menu_file_list(vf), vf->listview);
}

static void vf_pop_menu_copy_path_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	file_util_copy_path_list_to_clipboard(vf_pop_menu_file_list(vf));
}

static void vf_pop_menu_enable_grouping_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	file_data_disable_grouping_list(vf_pop_menu_file_list(vf), FALSE);
}

static void vf_pop_menu_disable_grouping_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	file_data_disable_grouping_list(vf_pop_menu_file_list(vf), TRUE);
}

static void vf_pop_menu_sort_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf;
	SortType type;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	vf = submenu_item_get_data(widget);
	if (!vf) return;

	type = (SortType)GPOINTER_TO_INT(data);

	if (vf->layout)
		{
		layout_sort_set(vf->layout, type, vf->sort_ascend);
		}
	else
		{
		vf_sort_set(vf, type, vf->sort_ascend);
		}
}

static void vf_pop_menu_sort_ascend_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	if (vf->layout)
		{
		layout_sort_set(vf->layout, vf->sort_method, !vf->sort_ascend);
		}
	else
		{
		vf_sort_set(vf, vf->sort_method, !vf->sort_ascend);
		}
}

static void vf_pop_menu_sel_mark_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_SET);
}

static void vf_pop_menu_sel_mark_and_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_AND);
}

static void vf_pop_menu_sel_mark_or_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_OR);
}

static void vf_pop_menu_sel_mark_minus_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_mark_to_selection(vf, vf->active_mark, MTS_MODE_MINUS);
}

static void vf_pop_menu_set_mark_sel_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_selection_to_mark(vf, vf->active_mark, STM_MODE_SET);
}

static void vf_pop_menu_res_mark_sel_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_selection_to_mark(vf, vf->active_mark, STM_MODE_RESET);
}

static void vf_pop_menu_toggle_mark_sel_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_selection_to_mark(vf, vf->active_mark, STM_MODE_TOGGLE);
}

static void vf_pop_menu_toggle_view_type_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	FileViewType new_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "menu_item_radio_data"));
	if (!vf->layout) return;

	layout_views_set(vf->layout, vf->layout->options.dir_view_type, new_type);
}

static void vf_pop_menu_refresh_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_pop_menu_refresh_cb(widget, data); break;
	case FILEVIEW_ICON: vficon_pop_menu_refresh_cb(widget, data); break;
	}
}

static void vf_popup_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_popup_destroy_cb(widget, data); break;
	case FILEVIEW_ICON: vficon_popup_destroy_cb(widget, data); break;
	}

	filelist_free(vf->editmenu_fd_list);
	vf->editmenu_fd_list = NULL;
}

GtkWidget *vf_pop_menu(ViewFile *vf)
{
	GtkWidget *menu;
	GtkWidget *item;
	GtkWidget *submenu;
	gboolean active = FALSE;

	switch (vf->type)
	{
	case FILEVIEW_LIST:
		vflist_color_set(vf, VFLIST(vf)->click_fd, TRUE);
		active = (VFLIST(vf)->click_fd != NULL);
		break;
	case FILEVIEW_ICON:
		active = (VFICON(vf)->click_id != NULL);
		break;
	}

	menu = popup_menu_short_lived();

	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vf_popup_destroy_cb), vf);

	if (vf->clicked_mark > 0)
		{
		gint mark = vf->clicked_mark;
		gchar *str_set_mark = g_strdup_printf(_("_Set mark %d"), mark);
		gchar *str_res_mark = g_strdup_printf(_("_Reset mark %d"), mark);
		gchar *str_toggle_mark = g_strdup_printf(_("_Toggle mark %d"), mark);
		gchar *str_sel_mark = g_strdup_printf(_("_Select mark %d"), mark);
		gchar *str_sel_mark_or = g_strdup_printf(_("_Add mark %d"), mark);
		gchar *str_sel_mark_and = g_strdup_printf(_("_Intersection with mark %d"), mark);
		gchar *str_sel_mark_minus = g_strdup_printf(_("_Unselect mark %d"), mark);

		g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

		vf->active_mark = mark;
		vf->clicked_mark = 0;

		menu_item_add_sensitive(menu, str_set_mark, active,
					G_CALLBACK(vf_pop_menu_set_mark_sel_cb), vf);

		menu_item_add_sensitive(menu, str_res_mark, active,
					G_CALLBACK(vf_pop_menu_res_mark_sel_cb), vf);

		menu_item_add_sensitive(menu, str_toggle_mark, active,
					G_CALLBACK(vf_pop_menu_toggle_mark_sel_cb), vf);

		menu_item_add_divider(menu);

		menu_item_add_sensitive(menu, str_sel_mark, active,
					G_CALLBACK(vf_pop_menu_sel_mark_cb), vf);
		menu_item_add_sensitive(menu, str_sel_mark_or, active,
					G_CALLBACK(vf_pop_menu_sel_mark_or_cb), vf);
		menu_item_add_sensitive(menu, str_sel_mark_and, active,
					G_CALLBACK(vf_pop_menu_sel_mark_and_cb), vf);
		menu_item_add_sensitive(menu, str_sel_mark_minus, active,
					G_CALLBACK(vf_pop_menu_sel_mark_minus_cb), vf);

		menu_item_add_divider(menu);

		g_free(str_set_mark);
		g_free(str_res_mark);
		g_free(str_toggle_mark);
		g_free(str_sel_mark);
		g_free(str_sel_mark_and);
		g_free(str_sel_mark_or);
		g_free(str_sel_mark_minus);
		}

	vf->editmenu_fd_list = vf_pop_menu_file_list(vf);
	submenu_add_edit(menu, &item, G_CALLBACK(vf_pop_menu_edit_cb), vf, vf->editmenu_fd_list);
	gtk_widget_set_sensitive(item, active);

	menu_item_add_stock_sensitive(menu, _("View in _new window"), GTK_STOCK_NEW, active,
				      G_CALLBACK(vf_pop_menu_view_cb), vf);

	menu_item_add_divider(menu);
	menu_item_add_stock_sensitive(menu, _("_Copy..."), GTK_STOCK_COPY, active,
				      G_CALLBACK(vf_pop_menu_copy_cb), vf);
	menu_item_add_sensitive(menu, _("_Move..."), active,
				G_CALLBACK(vf_pop_menu_move_cb), vf);
	menu_item_add_sensitive(menu, _("_Rename..."), active,
				G_CALLBACK(vf_pop_menu_rename_cb), vf);
	menu_item_add_stock_sensitive(menu, _("_Delete..."), GTK_STOCK_DELETE, active,
				      G_CALLBACK(vf_pop_menu_delete_cb), vf);
	menu_item_add_sensitive(menu, _("_Copy path"), active,
				G_CALLBACK(vf_pop_menu_copy_path_cb), vf);

	menu_item_add_sensitive(menu, _("Enable file _grouping"), active,
				G_CALLBACK(vf_pop_menu_enable_grouping_cb), vf);
	menu_item_add_sensitive(menu, _("Disable file groupi_ng"), active,
				G_CALLBACK(vf_pop_menu_disable_grouping_cb), vf);

	menu_item_add_divider(menu);

	submenu = submenu_add_sort(NULL, G_CALLBACK(vf_pop_menu_sort_cb), vf,
				   FALSE, FALSE, TRUE, vf->sort_method);
	menu_item_add_divider(submenu);
	menu_item_add_check(submenu, _("Ascending"), vf->sort_ascend,
			    G_CALLBACK(vf_pop_menu_sort_ascend_cb), vf);

	item = menu_item_add(menu, _("_Sort"), NULL, NULL);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);

	item = menu_item_add_radio(menu, _("View as _List"), GINT_TO_POINTER(FILEVIEW_LIST), vf->type == FILEVIEW_LIST,
                                           G_CALLBACK(vf_pop_menu_toggle_view_type_cb), vf);

	item = menu_item_add_radio(menu, _("View as _Icons"), GINT_TO_POINTER(FILEVIEW_ICON), vf->type == FILEVIEW_ICON,
                                           G_CALLBACK(vf_pop_menu_toggle_view_type_cb), vf);

	switch (vf->type)
	{
	case FILEVIEW_LIST:
		menu_item_add_check(menu, _("Show _thumbnails"), VFLIST(vf)->thumbs_enabled,
				    G_CALLBACK(vflist_pop_menu_thumbs_cb), vf);
		break;
	case FILEVIEW_ICON:
		menu_item_add_check(menu, _("Show filename _text"), VFICON(vf)->show_text,
				    G_CALLBACK(vficon_pop_menu_show_names_cb), vf);
		break;
	}

	menu_item_add_stock(menu, _("Re_fresh"), GTK_STOCK_REFRESH, G_CALLBACK(vf_pop_menu_refresh_cb), vf);

	return menu;
}

gboolean vf_refresh(ViewFile *vf)
{
	gboolean ret = FALSE;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_refresh(vf); break;
	case FILEVIEW_ICON: ret = vficon_refresh(vf); break;
	}

	return ret;
}

gboolean vf_set_fd(ViewFile *vf, FileData *dir_fd)
{
	gboolean ret = FALSE;

	switch (vf->type)
	{
	case FILEVIEW_LIST: ret = vflist_set_fd(vf, dir_fd); break;
	case FILEVIEW_ICON: ret = vficon_set_fd(vf, dir_fd); break;
	}

	return ret;
}

static void vf_destroy_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_destroy_cb(widget, data); break;
	case FILEVIEW_ICON: vficon_destroy_cb(widget, data); break;
	}

	if (vf->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(vf->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, 0, NULL, vf);
		gtk_widget_destroy(vf->popup);
		}

	file_data_unref(vf->dir_fd);
	g_free(vf->info);
	g_free(vf);
}

static void vf_marks_filter_toggle_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf = data;
	vf_refresh_idle(vf);
}


static GtkWidget *vf_marks_filter_init(ViewFile *vf)
{
	GtkWidget *frame = gtk_frame_new(NULL);
	GtkWidget *hbox = gtk_hbox_new(FALSE, 0);

	gint i;

	for (i = 0; i < FILEDATA_MARKS_SIZE ; i++)
		{
		GtkWidget *check = gtk_check_button_new();
		gtk_box_pack_start(GTK_BOX(hbox), check, FALSE, FALSE, 0);
		g_signal_connect(G_OBJECT(check), "toggled",
			 G_CALLBACK(vf_marks_filter_toggle_cb), vf);

		gtk_widget_show(check);
		vf->filter_check[i] = check;
		}
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	gtk_widget_show(hbox);
	return frame;
}

void vf_mark_filter_toggle(ViewFile *vf, gint mark)
{
	gint n = mark - 1;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vf->filter_check[n]),
				     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vf->filter_check[n])));
}

ViewFile *vf_new(FileViewType type, FileData *dir_fd)
{
	ViewFile *vf;

	vf = g_new0(ViewFile, 1);

	vf->type = type;
	vf->sort_method = SORT_NAME;
	vf->sort_ascend = TRUE;

	vf->scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(vf->scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vf->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	vf->filter = vf_marks_filter_init(vf);

	vf->widget = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vf->widget), vf->filter, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vf->widget), vf->scrolled, TRUE, TRUE, 0);
	gtk_widget_show(vf->scrolled);

	g_signal_connect(G_OBJECT(vf->widget), "destroy",
			 G_CALLBACK(vf_destroy_cb), vf);

	switch (type)
	{
	case FILEVIEW_LIST: vf = vflist_new(vf, dir_fd); break;
	case FILEVIEW_ICON: vf = vficon_new(vf, dir_fd); break;
	}

	vf_dnd_init(vf);

	g_signal_connect(G_OBJECT(vf->listview), "key_press_event",
			 G_CALLBACK(vf_press_key_cb), vf);
	g_signal_connect(G_OBJECT(vf->listview), "button_press_event",
			 G_CALLBACK(vf_press_cb), vf);
	g_signal_connect(G_OBJECT(vf->listview), "button_release_event",
			 G_CALLBACK(vf_release_cb), vf);

	gtk_container_add(GTK_CONTAINER(vf->scrolled), vf->listview);
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
	case FILEVIEW_LIST: vflist_thumb_progress_count(vf->list, &count, &done); break;
	case FILEVIEW_ICON: vficon_thumb_progress_count(vf->list, &count, &done); break;
	}

	DEBUG_1("thumb progress: %d of %d", done, count);
	return (gdouble)done / count;
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
	vf_thumb_status(vf, 0.0, NULL);

	vf->thumbs_running = FALSE;

	thumb_loader_free(vf->thumbs_loader);
	vf->thumbs_loader = NULL;

	vf->thumbs_filedata = NULL;
}

void vf_thumb_stop(ViewFile *vf)
{
	if (vf->thumbs_running) vf_thumb_cleanup(vf);
}

static void vf_thumb_common_cb(ThumbLoader *tl, gpointer data)
{
	ViewFile *vf = data;

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
	FileData *fd = NULL;

	if (!gtk_widget_get_realized(vf->listview))
		{
		vf_thumb_status(vf, 0.0, NULL);
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
				   NULL,
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
	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_thumb_reset_all(vf); break;
	case FILEVIEW_ICON: vficon_thumb_reset_all(vf); break;
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


void vf_marks_set(ViewFile *vf, gboolean enable)
{
	if (vf->marks_enabled == enable) return;

	vf->marks_enabled = enable;

	switch (vf->type)
	{
	case FILEVIEW_LIST: vflist_marks_set(vf, enable); break;
	case FILEVIEW_ICON: vficon_marks_set(vf, enable); break;
	}
	if (enable)
		gtk_widget_show(vf->filter);
	else
		gtk_widget_hide(vf->filter);

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
	ViewFile *vf = data;

	vf_refresh(vf);
	vf->refresh_idle_id = 0;
	return FALSE;
}

void vf_refresh_idle_cancel(ViewFile *vf)
{
	if (vf->refresh_idle_id)
		{
		g_source_remove(vf->refresh_idle_id);
		vf->refresh_idle_id = 0;
		}
}


void vf_refresh_idle(ViewFile *vf)
{
	if (!vf->refresh_idle_id)
		{
		vf->time_refresh_set = time(NULL);
		/* file operations run with G_PRIORITY_DEFAULT_IDLE */
		vf->refresh_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE + 50, vf_refresh_idle_cb, vf, NULL);
		}
	else if (time(NULL) - vf->time_refresh_set > 1)
		{
		/* more than 1 sec since last update - increase priority */
		vf_refresh_idle_cancel(vf);
		vf->time_refresh_set = time(NULL);
		vf->refresh_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE - 50, vf_refresh_idle_cb, vf, NULL);
		}
}

void vf_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	ViewFile *vf = data;
	gboolean refresh;

	NotifyType interested = NOTIFY_CHANGE | NOTIFY_REREAD | NOTIFY_GROUPING;
	if (vf->marks_enabled) interested |= NOTIFY_MARKS | NOTIFY_METADATA;
	/* FIXME: NOTIFY_METADATA should be checked by the keyword-to-mark functions and converted to NOTIFY_MARKS only if there was a change */

	if (!(type & interested) || vf->refresh_idle_id || !vf->dir_fd) return;

	refresh = (fd == vf->dir_fd);

	if (!refresh)
		{
		gchar *base = remove_level_from_path(fd->path);
		refresh = (strcmp(base, vf->dir_fd->path) == 0);
		g_free(base);
		}

	if ((type & NOTIFY_CHANGE) && fd->change)
		{
		if (!refresh && fd->change->dest)
			{
			gchar *dest_base = remove_level_from_path(fd->change->dest);
			refresh = (strcmp(dest_base, vf->dir_fd->path) == 0);
			g_free(dest_base);
			}

		if (!refresh && fd->change->source)
			{
			gchar *source_base = remove_level_from_path(fd->change->source);
			refresh = (strcmp(source_base, vf->dir_fd->path) == 0);
			g_free(source_base);
			}
		}

	if (refresh)
		{
		DEBUG_1("Notify vf: %s %04x", fd->path, type);
		vf_refresh_idle(vf);
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
