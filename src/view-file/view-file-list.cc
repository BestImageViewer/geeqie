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

#include "view-file-list.h"

#include <cstring>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>

#include "collect.h"
#include "compat-deprecated.h"
#include "dnd.h"
#include "filedata.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-tree-edit.h"
#include "utilops.h"
#include "view-file.h"

/* Index to tree store */
enum {
	FILE_COLUMN_POINTER = 0,
	FILE_COLUMN_VERSION = 1,
	FILE_COLUMN_THUMB = 2,
	FILE_COLUMN_FORMATTED = 3,
	FILE_COLUMN_FORMATTED_WITH_STARS = 4,
	FILE_COLUMN_NAME = 5,
	FILE_COLUMN_SIDECARS = 6,
	FILE_COLUMN_STAR_RATING = 7,
	FILE_COLUMN_SIZE = 8,
	FILE_COLUMN_DATE = 9,
	FILE_COLUMN_EXPANDED = 10,
	FILE_COLUMN_COLOR = 11,
	FILE_COLUMN_MARKS = 12,
	FILE_COLUMN_MARKS_LAST = FILE_COLUMN_MARKS + FILEDATA_MARKS_SIZE - 1,
	FILE_COLUMN_COUNT = 22
};


/* Index to tree view */
enum {
	FILE_VIEW_COLUMN_MARKS = 0,
	FILE_VIEW_COLUMN_MARKS_LAST = FILE_VIEW_COLUMN_MARKS + FILEDATA_MARKS_SIZE - 1,
	FILE_VIEW_COLUMN_THUMB = 10,
	FILE_VIEW_COLUMN_FORMATTED = 11,
	FILE_VIEW_COLUMN_FORMATTED_WITH_STARS = 12,
	FILE_VIEW_COLUMN_STAR_RATING = 13,
	FILE_VIEW_COLUMN_SIZE = 14,
	FILE_VIEW_COLUMN_DATE = 15,
	FILE_VIEW_COLUMN_COUNT = 16
};



static gboolean vflist_row_is_selected(ViewFile *vf, FileData *fd);
static gboolean vflist_row_rename_cb(TreeEditData *td, const gchar *old_name, const gchar *new_name, gpointer data);
static void vflist_populate_view(ViewFile *vf, gboolean force);
static gboolean vflist_is_multiline(ViewFile *vf);
static gchar *vflist_get_formatted(ViewFile *vf, const gchar *name, const gchar *sidecars, const gchar *size, const gchar *time, gboolean expanded, const gchar *star_rating);


/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */
struct ViewFileFindRowData {
	const FileData *fd;
	GtkTreeIter *iter;
	gboolean found;
	gint row;
};

static gboolean vflist_find_row_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
	auto find = static_cast<ViewFileFindRowData *>(data);
	FileData *fd;
	gtk_tree_model_get(model, iter, FILE_COLUMN_POINTER, &fd, -1);
	if (fd == find->fd)
		{
		*find->iter = *iter;
		find->found = TRUE;
		return TRUE;
		}
	find->row++;
	return FALSE;
}

static gint vflist_find_row(const ViewFile *vf, const FileData *fd, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	ViewFileFindRowData data = {fd, iter, FALSE, 0};

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	gtk_tree_model_foreach(store, vflist_find_row_cb, &data);

	if (data.found)
		{
		return data.row;
		}

	return -1;
}

FileData *vflist_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *)
{
	g_autoptr(GtkTreePath) tpath = nullptr;
	GtkTreeViewColumn *column;

	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), x, y,
	                                   &tpath, &column, nullptr, nullptr))
		{
		return nullptr;
		}

	GtkTreeIter row;
	FileData *fd;

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	gtk_tree_model_get_iter(store, &row, tpath);
	gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &fd, -1);

	return fd;
}

static gboolean vflist_store_clear_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	FileData *fd;
	gtk_tree_model_get(model, iter, FILE_COLUMN_POINTER, &fd, -1);

	/* it seems that gtk_tree_store_clear may call some callbacks
	   that use the column. Set the pointer to NULL to be safe. */
	gtk_tree_store_set(GTK_TREE_STORE(model), iter, FILE_COLUMN_POINTER, NULL, -1);
	file_data_unref(fd);
	return FALSE;
}

static void vflist_store_clear(ViewFile *vf, gboolean unlock_files)
{
	GtkTreeModel *store;

	if (unlock_files && vf->marks_enabled)
		{
		// unlock locked files in this directory
		GList *files = nullptr;
		filelist_read(vf->dir_fd, &files, nullptr);
		GList *work = files;
		while (work)
			{
			auto fd = static_cast<FileData *>(work->data);
			work = work->next;
			file_data_unlock(fd);
			file_data_unref(fd);  // undo the ref that got added in filelist_read
			}
		g_list_free(files);
		}

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	gtk_tree_model_foreach(store, vflist_store_clear_cb, nullptr);
	gtk_tree_store_clear(GTK_TREE_STORE(store));
}

void vflist_color_set(ViewFile *vf, FileData *fd, gboolean color_set)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	if (vflist_find_row(vf, fd, &iter) < 0) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	gtk_tree_store_set(GTK_TREE_STORE(store), &iter, FILE_COLUMN_COLOR, color_set, -1);
}

static void vflist_move_cursor(ViewFile *vf, GtkTreeIter *iter)
{
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, iter);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(vf->listview), tpath, nullptr, FALSE);
}


/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

#if !HAVE_GTK4
void vflist_dnd_begin(ViewFile *vf, GtkWidget *widget, GdkDragContext *context)
{
	vflist_color_set(vf, vf->click_fd, TRUE);

	if (VFLIST(vf)->thumbs_enabled &&
	    vf->click_fd && vf->click_fd->thumb_pixbuf)
		{
		guint items;

		if (vflist_row_is_selected(vf, vf->click_fd))
			items = vflist_selection_count(vf);
		else
			items = 1;

		dnd_set_drag_icon(widget, context, vf->click_fd->thumb_pixbuf, items);
		}
}

void vflist_dnd_end(ViewFile *vf, GdkDragContext *context)
{
	vflist_color_set(vf, vf->click_fd, FALSE);

	if (gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE)
		{
		vflist_refresh(vf);
		}
}
#endif

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

GList *vflist_selection_get_one(ViewFile *vf, FileData *fd)
{
	GList *list = nullptr;

	if (fd->sidecar_files)
		{
		/* check if the row is expanded */
		GtkTreeModel *store;
		GtkTreeIter iter;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		if (vflist_find_row(vf, fd, &iter) >= 0)
			{
			g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
			if (!gtk_tree_view_row_expanded(GTK_TREE_VIEW(vf->listview), tpath))
				{
				/* unexpanded - add whole group */
				list = filelist_copy(fd->sidecar_files);
				}
			}
		}

	return g_list_prepend(list, file_data_ref(fd));
}

void vflist_pop_menu_rename_cb(ViewFile *vf)
{
	GList *list;

	list = vf_pop_menu_file_list(vf);
	if (options->file_ops.enable_in_place_rename &&
	    list && !list->next && vf->click_fd)
		{
		GtkTreeModel *store;
		GtkTreeIter iter;

		file_data_list_free(list);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		if (vflist_find_row(vf, vf->click_fd, &iter) >= 0)
			{
			g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
			tree_edit_by_path(GTK_TREE_VIEW(vf->listview), tpath,
			                  FILE_VIEW_COLUMN_FORMATTED, vf->click_fd->name,
			                  vflist_row_rename_cb, vf);
			}
		return;
		}

	file_util_rename(nullptr, list, vf->listview);
}

static void vflist_pop_menu_thumbs_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vflist_color_set(vf, vf->click_fd, FALSE);
	if (vf->layout)
		{
		layout_thumb_set(vf->layout, !VFLIST(vf)->thumbs_enabled);
		}
	else
		{
		vflist_thumb_set(vf, !VFLIST(vf)->thumbs_enabled);
		}
}

void vflist_pop_menu_add_items(ViewFile *vf, GtkWidget *menu)
{
	menu_item_add_check(menu, _("Show _thumbnails"), VFLIST(vf)->thumbs_enabled,
	                    G_CALLBACK(vflist_pop_menu_thumbs_cb), vf);
}

static void vflist_star_rating_set(ViewFile *vf, gboolean enable)
{
	GList *columns;
	GList *work;

	columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(vf->listview));

	work = columns;
	while (work)
		{
		auto column = static_cast<GtkTreeViewColumn *>(work->data);
		gint col_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_store_idx"));
		work = work->next;

		if (vflist_is_multiline(vf))
			{
			if (col_idx == FILE_COLUMN_FORMATTED_WITH_STARS)
				{
				gtk_tree_view_column_set_visible(column, enable);
				}
			if (col_idx == FILE_COLUMN_FORMATTED)
				{
				gtk_tree_view_column_set_visible(column, !enable);
				}
			}
		else
			{
			if (col_idx == FILE_COLUMN_STAR_RATING)
				{
				gtk_tree_view_column_set_visible(column, enable);
				}
			}
		}
	g_list_free(columns);
}

void vflist_pop_menu_show_star_rating_cb(ViewFile *vf)
{
	vflist_populate_view(vf, TRUE);

	vflist_color_set(vf, vf->click_fd, FALSE);
	vflist_star_rating_set(vf, options->show_star_rating);
}

void vflist_pop_menu_refresh_cb(ViewFile *vf)
{
	vflist_color_set(vf, vf->click_fd, FALSE);
	vflist_refresh(vf);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
}

void vflist_popup_destroy_cb(ViewFile *vf)
{
	vflist_color_set(vf, vf->click_fd, FALSE);
}


/*
 *-----------------------------------------------------------------------------
 * callbacks
 *-----------------------------------------------------------------------------
 */

static gboolean vflist_row_rename_cb(TreeEditData *, const gchar *old_name, const gchar *new_name, gpointer data)
{
	if (!new_name || !new_name[0]) return FALSE;

	auto vf = static_cast<ViewFile *>(data);

	if (strchr(new_name, G_DIR_SEPARATOR) != nullptr)
		{
		g_autofree gchar *text = g_strdup_printf(_("Invalid file name:\n%s"), new_name);
		file_util_warning_dialog(_("Error renaming file"), text, GQ_ICON_DIALOG_ERROR, vf->listview);
		}
	else
		{
		g_autofree gchar *old_path = g_build_filename(vf->dir_fd->path, old_name, NULL);
		FileData *fd = file_data_new_group(old_path); /* get the fd from cache */

		g_autofree gchar *new_path = g_build_filename(vf->dir_fd->path, new_name, NULL);
		file_util_rename_simple(fd, new_path, vf->listview);

		file_data_unref(fd);
		}

	return FALSE;
}

gboolean vflist_press_key_cb(ViewFile *vf, GtkWidget *widget, GdkEventKey *event)
{
	if (event->keyval != GDK_KEY_Menu) return FALSE;

	g_autoptr(GtkTreePath) tpath = nullptr;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(vf->listview), &tpath, nullptr);
	if (tpath)
		{
		GtkTreeModel *store;
		GtkTreeIter iter;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &vf->click_fd, -1);
		}
	else
		{
		vf->click_fd = nullptr;
		}

	vf->popup = vf_pop_menu(vf);
	gtk_menu_popup_at_widget(GTK_MENU(vf->popup), widget, GDK_GRAVITY_EAST, GDK_GRAVITY_CENTER, nullptr);

	return TRUE;
}

gboolean vflist_press_cb(ViewFile *vf, GtkWidget *widget, GdkEventButton *bevent)
{
	GtkTreeIter iter;
	FileData *fd = nullptr;
	GtkTreeViewColumn *column;

	vf->clicked_mark = 0;

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
	                                  &tpath, &column, nullptr, nullptr))
		{
		GtkTreeModel *store;
		gint col_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_store_idx"));

		if (bevent->button == GDK_BUTTON_PRIMARY &&
		    col_idx >= FILE_COLUMN_MARKS && col_idx <= FILE_COLUMN_MARKS_LAST)
			return FALSE;

		if (col_idx >= FILE_COLUMN_MARKS && col_idx <= FILE_COLUMN_MARKS_LAST)
			vf->clicked_mark = 1 + (col_idx - FILE_COLUMN_MARKS);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);
		}

	vf->click_fd = fd;

	if (bevent->button == GDK_BUTTON_SECONDARY)
		{
		vf->popup = vf_pop_menu(vf);
		gtk_menu_popup_at_pointer(GTK_MENU(vf->popup), nullptr);
		return TRUE;
		}

	if (!fd) return FALSE;

	if (bevent->button == GDK_BUTTON_MIDDLE)
		{
		if (!vflist_row_is_selected(vf, fd))
			{
			vflist_color_set(vf, fd, TRUE);
			}
		return TRUE;
		}


	if (bevent->button == GDK_BUTTON_PRIMARY && bevent->type == GDK_BUTTON_PRESS &&
	    !(bevent->state & GDK_SHIFT_MASK ) &&
	    !(bevent->state & GDK_CONTROL_MASK ) &&
	    vflist_row_is_selected(vf, fd))
		{
		GtkTreeSelection *selection;

		gtk_widget_grab_focus(widget);


		/* returning FALSE and further processing of the event is needed for
		   correct operation of the expander, to show the sidecar files.
		   It however resets the selection of multiple files. With this condition
		   it should work for both cases */
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
		return (gtk_tree_selection_count_selected_rows(selection) > 1);
		}

	if (bevent->button == GDK_BUTTON_PRIMARY && bevent->type == GDK_2BUTTON_PRESS)
		{
		if (vf->click_fd->format_class == FORMAT_CLASS_COLLECTION)
			{
			collection_window_new(vf->click_fd->path);
			}
		else
			{
			if (vf->layout) layout_image_full_screen_start(vf->layout);
			}
		}

	return FALSE;
}

gboolean vflist_release_cb(ViewFile *vf, GtkWidget *widget, GdkEventButton *bevent)
{
	GtkTreeIter iter;
	FileData *fd = nullptr;

	if (layout_handle_user_defined_mouse_buttons(vf->layout, bevent))
		{
		return TRUE;
		}

	if (bevent->button == GDK_BUTTON_MIDDLE)
		{
		vflist_color_set(vf, vf->click_fd, FALSE);
		}

	if (bevent->button != GDK_BUTTON_PRIMARY && bevent->button != GDK_BUTTON_MIDDLE)
		{
		return TRUE;
		}

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    (bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);
		}

	if (bevent->button == GDK_BUTTON_MIDDLE)
		{
		if (fd && vf->click_fd == fd)
			{
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
			if (vflist_row_is_selected(vf, fd))
				{
				gtk_tree_selection_unselect_iter(selection, &iter);
				}
			else
				{
				gtk_tree_selection_select_iter(selection, &iter);
				}
			}
		return TRUE;
		}

	if (fd && vf->click_fd == fd &&
	    !(bevent->state & GDK_SHIFT_MASK ) &&
	    !(bevent->state & GDK_CONTROL_MASK ) &&
	    vflist_row_is_selected(vf, fd))
		{
		GtkTreeSelection *selection;

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
		gtk_tree_selection_unselect_all(selection);
		gtk_tree_selection_select_iter(selection, &iter);
		vflist_move_cursor(vf, &iter);
		}

	return FALSE;
}

static void vflist_select_image(ViewFile *vf, FileData *sel_fd)
{
	FileData *read_ahead_fd = nullptr;
	gint row;
	FileData *cur_fd;

	if (!sel_fd) return;

	cur_fd = layout_image_get_fd(vf->layout);
	if (sel_fd == cur_fd) return; /* no change */

	row = g_list_index(vf->list, sel_fd);
	/** @FIXME sidecar data */

	if (options->image.enable_read_ahead && row >= 0)
		{
		if (row > g_list_index(vf->list, cur_fd) &&
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

static gboolean vflist_select_idle_cb(gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (vf->layout)
		{
		vf_send_update(vf);

		if (VFLIST(vf)->select_fd)
			{
			vflist_select_image(vf, VFLIST(vf)->select_fd);
			VFLIST(vf)->select_fd = nullptr;
			}
		}

	VFLIST(vf)->select_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static gboolean vflist_select_cb(GtkTreeSelection *, GtkTreeModel *store, GtkTreePath *tpath, gboolean path_currently_selected, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkTreeIter iter;

	VFLIST(vf)->select_fd = nullptr;

	if (!path_currently_selected && gtk_tree_model_get_iter(store, &iter, tpath))
		{
		g_autoptr(GtkTreePath) cursor_path = nullptr;
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(vf->listview), &cursor_path, nullptr);
		if (cursor_path)
			{
			gtk_tree_model_get_iter(store, &iter, cursor_path);
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &VFLIST(vf)->select_fd, -1);
			}
		}

	if (vf->layout &&
	    !VFLIST(vf)->select_idle_id)
		{
		VFLIST(vf)->select_idle_id = g_idle_add(vflist_select_idle_cb, vf);
		}

	return TRUE;
}

template<gboolean expanded>
static void vflist_expand_cb(GtkTreeView *listview, GtkTreeIter *iter, GtkTreePath *, gpointer data)
{
	GtkTreeModel *store = gtk_tree_view_get_model(listview);

	g_autofree gchar *name = nullptr;
	g_autofree gchar *sidecars = nullptr;
	g_autofree gchar *size = nullptr;
	g_autofree gchar *time = nullptr;
	g_autofree gchar *star_rating = nullptr;
	gtk_tree_model_get(store, iter,
	                   FILE_COLUMN_NAME, &name,
	                   FILE_COLUMN_SIDECARS, &sidecars,
	                   FILE_COLUMN_SIZE, &size,
	                   FILE_COLUMN_DATE, &time,
	                   FILE_COLUMN_STAR_RATING, &star_rating,
	                   -1);

	auto *vf = static_cast<ViewFile *>(data);
	g_autofree gchar *formatted = vflist_get_formatted(vf, name, sidecars, size, time, expanded, nullptr);
	g_autofree gchar *formatted_with_stars = vflist_get_formatted(vf, name, sidecars, size, time, expanded, star_rating);

	gtk_tree_store_set(GTK_TREE_STORE(store), iter,
	                   FILE_COLUMN_FORMATTED, formatted,
	                   FILE_COLUMN_FORMATTED_WITH_STARS, formatted_with_stars,
	                   FILE_COLUMN_EXPANDED, expanded,
	                   -1);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

static gchar *vflist_get_formatted(ViewFile *vf, const gchar *name, const gchar *sidecars, const gchar *size, const gchar *time, gboolean expanded, const gchar *star_rating)
{
	gboolean multiline = vflist_is_multiline(vf);
	GString *text = g_string_new(nullptr);

	g_string_printf(text, "%s %s", name, expanded ? "" : sidecars);

	if (multiline)
		{
		g_string_append_printf(text, "\n%s\n%s", size, time);

		if (star_rating)
			{
			g_string_append_printf(text, "\n%s", star_rating);
			}
		}

	return g_string_free(text, FALSE);
}

static void vflist_setup_iter(ViewFile *vf, GtkTreeStore *store, GtkTreeIter *iter, FileData *fd)
{
	const gchar *time = text_from_time(fd->date);
	const gchar *link = islink(fd->path) ? GQ_LINK_STR : "";
	const gchar *disabled_grouping;
	gboolean expanded = FALSE;
	g_autofree gchar *star_rating = nullptr;

	if (options->show_star_rating && fd->rating != STAR_RATING_NOT_READ)
		{
 		star_rating = convert_rating_to_stars(fd->rating);
		}

	if (fd->sidecar_files) /* expanded has no effect on files without sidecars */
		{
		gtk_tree_model_get(GTK_TREE_MODEL(store), iter, FILE_COLUMN_EXPANDED, &expanded, -1);
		}

	g_autofree gchar *sidecars = file_data_sc_list_to_string(fd);

	disabled_grouping = fd->disable_grouping ? _(" [NO GROUPING]") : "";
	g_autofree gchar *name = g_strdup_printf("%s%s%s", link, fd->name, disabled_grouping);
	g_autofree gchar *size = text_from_size(fd->size);

	g_autofree gchar *formatted = vflist_get_formatted(vf, name, sidecars, size, time, expanded, nullptr);
	g_autofree gchar *formatted_with_stars = vflist_get_formatted(vf, name, sidecars, size, time, expanded, star_rating);

	gtk_tree_store_set(store, iter, FILE_COLUMN_POINTER, fd,
					FILE_COLUMN_VERSION, fd->version,
					FILE_COLUMN_THUMB, fd->thumb_pixbuf,
					FILE_COLUMN_FORMATTED, formatted,
					FILE_COLUMN_FORMATTED_WITH_STARS, formatted_with_stars,
					FILE_COLUMN_SIDECARS, sidecars,
					FILE_COLUMN_NAME, name,
					FILE_COLUMN_STAR_RATING, star_rating,
					FILE_COLUMN_SIZE, size,
					FILE_COLUMN_DATE, time,
#define STORE_SET_IS_SLOW 1
#if STORE_SET_IS_SLOW
/* this is 3x faster on a directory with 20000 files */
					FILE_COLUMN_MARKS + 0, file_data_get_mark(fd, 0),
					FILE_COLUMN_MARKS + 1, file_data_get_mark(fd, 1),
					FILE_COLUMN_MARKS + 2, file_data_get_mark(fd, 2),
					FILE_COLUMN_MARKS + 3, file_data_get_mark(fd, 3),
					FILE_COLUMN_MARKS + 4, file_data_get_mark(fd, 4),
					FILE_COLUMN_MARKS + 5, file_data_get_mark(fd, 5),
					FILE_COLUMN_MARKS + 6, file_data_get_mark(fd, 6),
					FILE_COLUMN_MARKS + 7, file_data_get_mark(fd, 7),
					FILE_COLUMN_MARKS + 8, file_data_get_mark(fd, 8),
					FILE_COLUMN_MARKS + 9, file_data_get_mark(fd, 9),
#if FILEDATA_MARKS_SIZE != 10
#error this needs to be updated
#endif
#endif
					FILE_COLUMN_COLOR, FALSE, -1);

#if !STORE_SET_IS_SLOW
	{
	gint i;
	for (i = 0; i < FILEDATA_MARKS_SIZE; i++)
		gtk_tree_store_set(store, iter, FILE_COLUMN_MARKS + i, file_data_get_mark(fd, i), -1);
	}
#endif
}

static void vflist_setup_iter_recursive(ViewFile *vf, GtkTreeStore *store, GtkTreeIter *parent_iter, GList *list, GList *selected, gboolean force)
{
	GList *work;
	GtkTreeIter iter;
	gboolean valid;
	gint num_ordered = 0;
	gint num_prepended = 0;

	valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &iter, parent_iter);

	work = list;
	while (work)
		{
		gint match;
		auto fd = static_cast<FileData *>(work->data);
		gboolean done = FALSE;

		while (!done)
			{
			FileData *old_fd = nullptr;
			gint old_version = 0;

			if (valid)
				{
				gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
						   FILE_COLUMN_POINTER, &old_fd,
						   FILE_COLUMN_VERSION, &old_version,
						   -1);

				if (fd == old_fd)
					{
					match = 0;
					}
				else
					{
					if (parent_iter)
						match = filelist_sort_compare_filedata_full(fd, old_fd, SORT_NAME, TRUE); /* always sort sidecars by name */
					else
						match = filelist_sort_compare_filedata_full(fd, old_fd, vf->sort.method, vf->sort.ascending);

					if (match == 0) g_warning("multiple fd for the same path");
					}

				}
			else
				{
				match = -1;
				}

			if (match < 0)
				{
				GtkTreeIter new_iter;

				if (valid)
					{
					num_ordered++;
					gtk_tree_store_insert_before(store, &new_iter, parent_iter, &iter);
					}
				else
					{
					/*
					    here should be used gtk_tree_store_append, but this function seems to be O(n)
					    and it seems to be much faster to add new entries to the beginning and reorder later
					*/
					num_prepended++;
					gtk_tree_store_prepend(store, &new_iter, parent_iter);
					}

				vflist_setup_iter(vf, store, &new_iter, file_data_ref(fd));
				vflist_setup_iter_recursive(vf, store, &new_iter, fd->sidecar_files, selected, force);

				if (g_list_find(selected, fd))
					{
					/* renamed files - the same fd appears at different position - select it again*/
					GtkTreeSelection *selection;
					selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
					gtk_tree_selection_select_iter(selection, &new_iter);
					}

				done = TRUE;
				}
			else if (match > 0)
				{
				file_data_unref(old_fd);
				valid = gtk_tree_store_remove(store, &iter);
				}
			else
				{
				num_ordered++;
				if (fd->version != old_version || force)
					{
					vflist_setup_iter(vf, store, &iter, fd);
					vflist_setup_iter_recursive(vf, store, &iter, fd->sidecar_files, selected, force);
					}

				if (valid) valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);

				done = TRUE;
				}
			}
		work = work->next;
		}

	while (valid)
		{
		FileData *old_fd;
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, FILE_COLUMN_POINTER, &old_fd, -1);
		file_data_unref(old_fd);

		valid = gtk_tree_store_remove(store, &iter);
		}

	/* move the prepended entries to the correct position */
	if (num_prepended)
		{
		gint num_total = num_prepended + num_ordered;
		std::vector<gint> new_order;
		new_order.reserve(num_total);

		for (gint i = 0; i < num_ordered; i++)
			{
			new_order.push_back(num_prepended + i);
			}
		for (gint i = num_ordered; i < num_total; i++)
			{
			new_order.push_back(num_total - 1 - i);
			}
		gtk_tree_store_reorder(store, parent_iter, new_order.data());
		}
}

void vflist_sort_set(ViewFile *vf, FileData::FileList::SortSettings settings)
{
	gint i;
	GHashTable *fd_idx_hash = g_hash_table_new(nullptr, nullptr);
	GtkTreeStore *store;
	GList *work;

	if (!vf->list || vf->sort == settings) return;

	work = vf->list;
	i = 0;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		g_hash_table_insert(fd_idx_hash, fd, GINT_TO_POINTER(i));
		i++;
		work = work->next;
		}

	vf->sort = settings;
	vf->list = filelist_sort(vf->list, vf->sort);

	std::vector<gint> new_order;
	new_order.reserve(i);

	work = vf->list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		new_order.push_back(GPOINTER_TO_INT(g_hash_table_lookup(fd_idx_hash, fd)));
		work = work->next;
		}

	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	gtk_tree_store_reorder(store, nullptr, new_order.data());

	g_hash_table_destroy(fd_idx_hash);
}

/*
 *-----------------------------------------------------------------------------
 * thumb updates
 *-----------------------------------------------------------------------------
 */


void vflist_thumb_progress_count(const GList *list, gint &count, gint &done)
{
	for (const GList *work = list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (fd->thumb_pixbuf) done++;

		if (fd->sidecar_files)
			{
			vflist_thumb_progress_count(fd->sidecar_files, count, done);
			}
		count++;
		}
}

void vflist_read_metadata_progress_count(const GList *list, gint &count, gint &done)
{
	for (const GList *work = list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (fd->metadata_in_idle_loaded) done++;

		if (fd->sidecar_files)
			{
			vflist_read_metadata_progress_count(fd->sidecar_files, count, done);
			}
		count++;
		}
}

void vflist_set_thumb_fd(ViewFile *vf, FileData *fd)
{
	GtkTreeStore *store;
	GtkTreeIter iter;

	if (!fd || vflist_find_row(vf, fd, &iter) < 0) return;

	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	gtk_tree_store_set(store, &iter, FILE_COLUMN_THUMB, fd->thumb_pixbuf, -1);
}

FileData *vflist_thumb_next_fd(ViewFile *vf)
{
	FileData *fd = nullptr;

	/* first check the visible files */

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid = TRUE;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);

		while (!fd && valid && tree_view_row_is_visible(GTK_TREE_VIEW(vf->listview), &iter, FALSE))
			{
			FileData *nfd;

			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &nfd, -1);

			if (!nfd->thumb_pixbuf) fd = nfd;

			valid = gtk_tree_model_iter_next(store, &iter);
			}
		}

	/* then find first undone */

	if (!fd)
		{
		GList *work = vf->list;
		while (work && !fd)
			{
			auto fd_p = static_cast<FileData *>(work->data);
			if (!fd_p->thumb_pixbuf)
				fd = fd_p;
			else
				{
				GList *work2 = fd_p->sidecar_files;

				while (work2 && !fd)
					{
					fd_p = static_cast<FileData *>(work2->data);
					if (!fd_p->thumb_pixbuf) fd = fd_p;
					work2 = work2->next;
					}
				}
			work = work->next;
			}
		}

	return fd;
}

void vflist_set_star_fd(ViewFile *vf, FileData *fd)
{
	GtkTreeStore *store;
	GtkTreeIter iter;
	gboolean expanded;

	if (!fd || vflist_find_row(vf, fd, &iter) < 0) return;

	g_autofree gchar *star_rating = metadata_read_rating_stars(fd);

	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	gtk_tree_store_set(store, &iter, FILE_COLUMN_STAR_RATING, star_rating, -1);

	g_autofree gchar *name = nullptr;
	g_autofree gchar *sidecars = nullptr;
	g_autofree gchar *size = nullptr;
	g_autofree gchar *time = nullptr;
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
					FILE_COLUMN_NAME, &name,
					FILE_COLUMN_SIDECARS, &sidecars,
					FILE_COLUMN_SIZE, &size,
					FILE_COLUMN_DATE, &time,
					FILE_COLUMN_EXPANDED, &expanded,
					-1);

	g_autofree gchar *formatted_with_stars = vflist_get_formatted(vf, name, sidecars, size, time, expanded, star_rating);

	gtk_tree_store_set(store, &iter, FILE_COLUMN_FORMATTED_WITH_STARS, formatted_with_stars,
					FILE_COLUMN_EXPANDED, expanded,
					-1);
}

FileData *vflist_star_next_fd(ViewFile *vf)
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
			FileData *fd = nullptr;
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);

			if (fd && fd->rating == STAR_RATING_NOT_READ)
				{
				return fd;
				}

			valid = gtk_tree_model_iter_next(store, &iter);
			}
		}

	/* then find first undone */

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

gint vflist_index_by_fd(const ViewFile *vf, const FileData *fd)
{
	gint p = 0;

	for (const GList *work = vf->list; work; work = work->next)
		{
		auto list_fd = static_cast<FileData *>(work->data);
		if (list_fd == fd) return p;

		/** @FIXME return the same index also for sidecars
		   it is sufficient for next/prev navigation but it should be rewritten
		   without using indexes at all
		*/
		if (g_list_find(list_fd->sidecar_files, fd)) return p;

		p++;
		}

	return -1;
}

/*
 *-----------------------------------------------------------------------------
 * selections
 *-----------------------------------------------------------------------------
 */

static gboolean vflist_row_is_selected(ViewFile *vf, FileData *fd)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	GtkTreeModel *store;
	g_autolist(GtkTreePath) slist = gtk_tree_selection_get_selected_rows(selection, &store);

	for (GList *work = slist; work; work = work->next)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		FileData *fd_n;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd_n, -1);

		if (fd_n == fd) return TRUE;
		}

	return FALSE;
}

gboolean vflist_is_selected(ViewFile *vf, FileData *fd)
{
	return vflist_row_is_selected(vf, fd);
}

guint vflist_selection_count(ViewFile *vf, gint64 *bytes)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));

	if (bytes)
		{
		gint64 b = 0;

		GtkTreeModel *store;
		g_autolist(GtkTreePath) slist = gtk_tree_selection_get_selected_rows(selection, &store);

		for (GList *work = slist; work; work = work->next)
			{
			auto tpath = static_cast<GtkTreePath *>(work->data);
			GtkTreeIter iter;
			gtk_tree_model_get_iter(store, &iter, tpath);

			FileData *fd;
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);
			b += fd->size;
			}

		*bytes = b;
		}

	return gtk_tree_selection_count_selected_rows(selection);
}

GList *vflist_selection_get_list(ViewFile *vf)
{
	GList *list = nullptr;

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	GtkTreeModel *store;
	g_autolist(GtkTreePath) slist = gtk_tree_selection_get_selected_rows(selection, &store);

	for (GList *work = g_list_last(slist); work; work = work->prev)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		FileData *fd;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);

		if (!fd->parent && !gtk_tree_view_row_expanded(GTK_TREE_VIEW(vf->listview), tpath))
			{
			/* unexpanded - add whole group */
			list = g_list_concat(filelist_copy(fd->sidecar_files), list);
			}

		list = g_list_prepend(list, file_data_ref(fd));
		}

	return list;
}

GList *vflist_selection_get_list_by_index(ViewFile *vf)
{
	GList *list = nullptr;

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	GtkTreeModel *store;
	g_autolist(GtkTreePath) slist = gtk_tree_selection_get_selected_rows(selection, &store);

	for (GList *work = slist; work; work = work->next)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		FileData *fd;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);

		list = g_list_prepend(list, GINT_TO_POINTER(g_list_index(vf->list, fd)));
		}

	return g_list_reverse(list);
}

void vflist_selection_foreach(ViewFile *vf, const ViewFile::SelectionCallback &func)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	GtkTreeModel *store;
	g_autolist(GtkTreePath) slist = gtk_tree_selection_get_selected_rows(selection, &store);

	for (GList *work = slist; work; work = work->next)
		{
		auto *tpath = static_cast<GtkTreePath *>(work->data);

		GtkTreeIter iter;
		gtk_tree_model_get_iter(store, &iter, tpath);

		FileData *fd_n;
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd_n, -1);

		func(fd_n);
		}
}

void vflist_select_all(ViewFile *vf)
{
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	gtk_tree_selection_select_all(selection);

	VFLIST(vf)->select_fd = nullptr;
}

void vflist_select_none(ViewFile *vf)
{
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	gtk_tree_selection_unselect_all(selection);
}

static gboolean tree_model_iter_prev(GtkTreeModel *store, GtkTreeIter *iter)
{
	g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, iter);

	gboolean result = gtk_tree_path_prev(tpath);
	if (result)
		gtk_tree_model_get_iter(store, iter, tpath);

	return result;
}

static gboolean tree_model_get_iter_last(GtkTreeModel *store, GtkTreeIter *iter)
{
	if (!gtk_tree_model_get_iter_first(store, iter))
		return FALSE;

	while (TRUE)
		{
		GtkTreeIter next = *iter;

		if (gtk_tree_model_iter_next(store, &next))
			*iter = next;
		else
			break;
		}

	return TRUE;
}

void vflist_select_invert(ViewFile *vf)
{
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	GtkTreeModel *store;
	gboolean valid;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));

	/* Backward iteration prevents scrolling to the end of the list,
	 * it scrolls to the first selected row instead. */
	valid = tree_model_get_iter_last(store, &iter);

	while (valid)
		{
		gboolean selected = gtk_tree_selection_iter_is_selected(selection, &iter);

		if (selected)
			gtk_tree_selection_unselect_iter(selection, &iter);
		else
			gtk_tree_selection_select_iter(selection, &iter);

		valid = tree_model_iter_prev(store, &iter);
		}
}

void vflist_select_by_fd(ViewFile *vf, FileData *fd)
{
	GtkTreeIter iter;

	if (vflist_find_row(vf, fd, &iter) < 0) return;

	tree_view_row_make_visible(GTK_TREE_VIEW(vf->listview), &iter, TRUE);

	if (vflist_row_is_selected(vf, fd)) return;

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	gtk_tree_selection_unselect_all(selection);
	gtk_tree_selection_select_iter(selection, &iter);
	vflist_move_cursor(vf, &iter);

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(vf->listview), tpath, nullptr, FALSE);
}

void vflist_select_list(ViewFile *vf, GList *list)
{
	GtkTreeIter iter;
	GList *work;

	work = list;

	while (work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);

		if (vflist_find_row(vf, fd, &iter) < 0) return;
		if (!vflist_row_is_selected(vf, fd))
			{
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
			gtk_tree_selection_select_iter(selection, &iter);
			}
		work = work->next;
		}
}

static void vflist_select_closest(ViewFile *vf, FileData *sel_fd)
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

	if (fd) vflist_select_by_fd(vf, fd);

}

void vflist_mark_to_selection(ViewFile *vf, gint mark, MarkToSelectionMode mode)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gboolean valid;

	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));

	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		FileData *fd;
		gboolean selected;
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, FILE_COLUMN_POINTER, &fd, -1);

		selected = file_data_mark_to_selection(fd, mark, mode, gtk_tree_selection_iter_is_selected(selection, &iter));

		if (selected)
			gtk_tree_selection_select_iter(selection, &iter);
		else
			gtk_tree_selection_unselect_iter(selection, &iter);

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}
}

void vflist_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode)
{
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	GtkTreeModel *store;
	g_autolist(GtkTreePath) slist = gtk_tree_selection_get_selected_rows(selection, &store);

	for (GList *work = slist; work; work = work->next)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		FileData *fd;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);

		/* the change has a very limited range and the standard notification would trigger
		   complete re-read of the directory - try to do only minimal update instead */
		file_data_unregister_notify_func(vf_notify_cb, vf); /* we don't need the notification */

		file_data_selection_to_mark(fd, mark, mode);

		if (!file_data_filter_marks(fd, vf_marks_get_filter(vf))) /* file no longer matches the filter -> remove it */
			{
			vf_refresh_idle(vf);
			}
		else
			{
			/* mark functions can have various side effects - update all columns to be sure */
			vflist_setup_iter(vf, GTK_TREE_STORE(store), &iter, fd);
			/* mark functions can change sidecars too */
			vflist_setup_iter_recursive(vf, GTK_TREE_STORE(store), &iter, fd->sidecar_files, nullptr, FALSE);
			}

		file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);
		}
}

/*
 *-----------------------------------------------------------------------------
 * core (population)
 *-----------------------------------------------------------------------------
 */

static void vflist_listview_set_columns(GtkWidget *listview, gboolean thumb, gboolean multiline)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
	GList *list;

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_THUMB);
	if (!column) return;

	gtk_tree_view_column_set_fixed_width(column, options->thumbnails.max_width + 4);

	list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
	if (!list) return;
	cell = static_cast<GtkCellRenderer *>(list->data);
	g_list_free(list);

	g_object_set(G_OBJECT(cell), "height", options->thumbnails.max_height, NULL);
	gtk_tree_view_column_set_visible(column, thumb);

	if (options->show_star_rating)
		{
		column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_FORMATTED_WITH_STARS);
		if (!column) return;
		gtk_tree_view_set_expander_column(GTK_TREE_VIEW(listview), column);
		gtk_tree_view_column_set_visible(column, TRUE);

		column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_FORMATTED);
		if (!column) return;
		gtk_tree_view_column_set_visible(column, FALSE);
		}
	else
		{
		column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_FORMATTED);
		if (!column) return;
		gtk_tree_view_set_expander_column(GTK_TREE_VIEW(listview), column);
		gtk_tree_view_column_set_visible(column, TRUE);

		column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_FORMATTED_WITH_STARS);
		if (!column) return;
		gtk_tree_view_column_set_visible(column, FALSE);
		}

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_STAR_RATING);
	if (!column) return;
	gtk_tree_view_column_set_visible(column, !multiline && options->show_star_rating);

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_SIZE);
	if (!column) return;
	gtk_tree_view_column_set_visible(column, !multiline);

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), FILE_VIEW_COLUMN_DATE);
	if (!column) return;
	gtk_tree_view_column_set_visible(column, !multiline);
}

static gboolean vflist_is_multiline(ViewFile *vf)
{
	return (VFLIST(vf)->thumbs_enabled && options->thumbnails.max_height >= 48);
}


static void vflist_populate_view(ViewFile *vf, gboolean force)
{
	GtkTreeStore *store;
	GList *selected;

	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));

	vf_thumb_stop(vf);
	vf_star_stop(vf);

	if (!vf->list)
		{
		vflist_store_clear(vf, FALSE);
		vf_send_update(vf);
		return;
		}

	vflist_listview_set_columns(vf->listview, VFLIST(vf)->thumbs_enabled, vflist_is_multiline(vf));

	selected = vflist_selection_get_list(vf);

	vflist_setup_iter_recursive(vf, store, nullptr, vf->list, selected, force);

	if (selected && vflist_selection_count(vf) == 0)
		{
		/* all selected files disappeared */
		vflist_select_closest(vf, static_cast<FileData *>(selected->data));
		}

	file_data_list_free(selected);

	vf_send_update(vf);
	vf_thumb_update(vf);
	vf_star_update(vf);
}

gboolean vflist_refresh(ViewFile *vf)
{
	GList *old_list;
	gboolean ret = TRUE;

	old_list = vf->list;
	vf->list = nullptr;

	DEBUG_1("%s vflist_refresh: read dir", get_exec_time());
	if (vf->dir_fd)
		{
		file_data_unregister_notify_func(vf_notify_cb, vf); /* we don't need the notification of changes detected by filelist_read */

		ret = filelist_read(vf->dir_fd, &vf->list, nullptr);

		if (vf->marks_enabled)
			{
			// When marks are enabled, lock FileDatas so that we don't end up re-parsing XML
			// each time a mark is changed.
			file_data_lock_list(vf->list);
			}
		else
			{
			/** @FIXME only do this when needed (aka when we just switched from */
			/** @FIXME marks-enabled to marks-disabled) */
			file_data_unlock_list(vf->list);
			}

		vf->list = file_data_filter_marks_list(vf->list, vf_marks_get_filter(vf));

		g_autoptr(GRegex) filter = vf_file_filter_get_filter(vf);
		vf->list = g_list_first(vf->list);
		vf->list = file_data_filter_file_filter_list(vf->list, filter);

		vf->list = g_list_first(vf->list);
		vf->list = file_data_filter_class_list(vf->list, vf_class_get_filter(vf));

		file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);

		DEBUG_1("%s vflist_refresh: sort", get_exec_time());
		vf->list = filelist_sort(vf->list, vf->sort);
		}

	DEBUG_1("%s vflist_refresh: populate view", get_exec_time());

	vflist_populate_view(vf, FALSE);

	DEBUG_1("%s vflist_refresh: free filelist", get_exec_time());

	file_data_list_free(old_list);
	DEBUG_1("%s vflist_refresh: done", get_exec_time());

	return ret;
}


static GdkRGBA *vflist_listview_color_shifted(GtkWidget *widget)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return nullptr;
#else
	static GdkRGBA color;
	static GtkWidget *done = nullptr;

	if (done != widget)
		{
		GtkStyle *style;

		style = deprecated_gtk_widget_get_style(widget);
		color = convert_gdkcolor_to_gdkrgba(&style->base[GTK_STATE_NORMAL]);

		shift_color(color);
		done = widget;
		}

	return &color;
#endif
}

static void vflist_listview_color_cb(GtkTreeViewColumn *, GtkCellRenderer *cell,
				     GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean set;

	gtk_tree_model_get(tree_model, iter, FILE_COLUMN_COLOR, &set, -1);
	g_object_set(G_OBJECT(cell),
		     "cell-background-rgba", vflist_listview_color_shifted(vf->listview),
		     "cell-background-set", set, NULL);
}

static void vflist_listview_add_column(ViewFile *vf, gint n, const gchar *title, gboolean image, gboolean right_justify, gboolean expand)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, title);
	gtk_tree_view_column_set_min_width(column, 4);

	if (!image)
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
		renderer = gtk_cell_renderer_text_new();
		if (right_justify)
			{
			g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
			}
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "text", n);
		if (expand)
			gtk_tree_view_column_set_expand(column, TRUE);
		}
	else
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
		renderer = gtk_cell_renderer_pixbuf_new();
		cell_renderer_height_override(renderer);
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "pixbuf", n);
		}

	gtk_tree_view_column_set_cell_data_func(column, renderer, vflist_listview_color_cb, vf, nullptr);
	g_object_set_data(G_OBJECT(column), "column_store_idx", GUINT_TO_POINTER(n));
	g_object_set_data(G_OBJECT(renderer), "column_store_idx", GUINT_TO_POINTER(n));

	gtk_tree_view_append_column(GTK_TREE_VIEW(vf->listview), column);
}

static void vflist_listview_mark_toggled_cb(GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkTreeIter iter;
	FileData *fd;
	gboolean marked;
	guint col_idx;

	g_autoptr(GtkTreePath) path = gtk_tree_path_new_from_string(path_str);
	if (!path) return;

	auto *store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, path)) return;

	col_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), "column_store_idx"));

	g_assert(col_idx >= FILE_COLUMN_MARKS && col_idx <= FILE_COLUMN_MARKS_LAST);

	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, FILE_COLUMN_POINTER, &fd, col_idx, &marked, -1);
	marked = !marked;

	/* the change has a very limited range and the standard notification would trigger
	   complete re-read of the directory - try to do only minimal update instead */
	file_data_unregister_notify_func(vf_notify_cb, vf);
	file_data_set_mark(fd, col_idx - FILE_COLUMN_MARKS, marked);
	if (!file_data_filter_marks(fd, vf_marks_get_filter(vf))) /* file no longer matches the filter -> remove it */
		{
		vf_refresh_idle(vf);
		}
	else
		{
		/* mark functions can have various side effects - update all columns to be sure */
		vflist_setup_iter(vf, GTK_TREE_STORE(store), &iter, fd);
		/* mark functions can change sidecars too */
		vflist_setup_iter_recursive(vf, GTK_TREE_STORE(store), &iter, fd->sidecar_files, nullptr, FALSE);
		}
	file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);
}

static void vflist_listview_add_column_toggle(ViewFile *vf, gint n, const gchar *title)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	renderer = gtk_cell_renderer_toggle_new();
	column = gtk_tree_view_column_new_with_attributes(title, renderer, "active", n, NULL);

	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	g_object_set_data(G_OBJECT(column), "column_store_idx", GUINT_TO_POINTER(n));
	g_object_set_data(G_OBJECT(renderer), "column_store_idx", GUINT_TO_POINTER(n));

	gtk_tree_view_append_column(GTK_TREE_VIEW(vf->listview), column);
	gtk_tree_view_column_set_fixed_width(column, 22);
	gtk_tree_view_column_set_visible(column, vf->marks_enabled);


	g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(vflist_listview_mark_toggled_cb), vf);
}

/*
 *-----------------------------------------------------------------------------
 * base
 *-----------------------------------------------------------------------------
 */

gboolean vflist_set_fd(ViewFile *vf, FileData *dir_fd)
{
	gboolean ret;
	if (!dir_fd) return FALSE;
	if (vf->dir_fd == dir_fd) return TRUE;

	file_data_unref(vf->dir_fd);
	vf->dir_fd = file_data_ref(dir_fd);

	/* force complete reload */
	vflist_store_clear(vf, TRUE);

	file_data_list_free(vf->list);
	vf->list = nullptr;

	ret = vflist_refresh(vf);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
	return ret;
}

void vflist_destroy_cb(ViewFile *vf)
{
	file_data_unregister_notify_func(vf_notify_cb, vf);

	g_clear_handle_id(&(VFLIST(vf)->select_idle_id), g_source_remove);
	vf_refresh_idle_cancel(vf);
	vf_thumb_stop(vf);
	vf_star_stop(vf);

	file_data_list_free(vf->list);
}

ViewFile *vflist_new(ViewFile *vf)
{
	GtkTreeStore *store;
	GType flist_types[FILE_COLUMN_COUNT];
	gint i;
	gint column;

	vf->info = g_new0(ViewFileInfoList, 1);

	flist_types[FILE_COLUMN_POINTER] = G_TYPE_POINTER;
	flist_types[FILE_COLUMN_VERSION] = G_TYPE_INT;
	flist_types[FILE_COLUMN_THUMB] = GDK_TYPE_PIXBUF;
	flist_types[FILE_COLUMN_FORMATTED] = G_TYPE_STRING;
	flist_types[FILE_COLUMN_FORMATTED_WITH_STARS] = G_TYPE_STRING;
	flist_types[FILE_COLUMN_NAME] = G_TYPE_STRING;
	flist_types[FILE_COLUMN_STAR_RATING] = G_TYPE_STRING;
	flist_types[FILE_COLUMN_SIDECARS] = G_TYPE_STRING;
	flist_types[FILE_COLUMN_SIZE] = G_TYPE_STRING;
	flist_types[FILE_COLUMN_DATE] = G_TYPE_STRING;
	flist_types[FILE_COLUMN_EXPANDED] = G_TYPE_BOOLEAN;
	flist_types[FILE_COLUMN_COLOR] = G_TYPE_BOOLEAN;
	for (i = FILE_COLUMN_MARKS; i < FILE_COLUMN_MARKS + FILEDATA_MARKS_SIZE; i++)
		flist_types[i] = G_TYPE_BOOLEAN;

	store = gtk_tree_store_newv(FILE_COLUMN_COUNT, flist_types);

	vf->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	g_signal_connect(G_OBJECT(vf->listview), "row-expanded",
	                 G_CALLBACK(vflist_expand_cb<TRUE>), vf);

	g_signal_connect(G_OBJECT(vf->listview), "row-collapsed",
	                 G_CALLBACK(vflist_expand_cb<FALSE>), vf);

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_select_function(selection, vflist_select_cb, vf, nullptr);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(vf->listview), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(vf->listview), FALSE);

	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(vf->listview), -1);

	column = 0;

	for (i = 0; i < FILEDATA_MARKS_SIZE; i++)
		{
		vflist_listview_add_column_toggle(vf, i + FILE_COLUMN_MARKS, "");
		g_assert(column == FILE_VIEW_COLUMN_MARKS + i);
		column++;
		}

	vflist_listview_add_column(vf, FILE_COLUMN_THUMB, "", TRUE, FALSE, FALSE);
	g_assert(column == FILE_VIEW_COLUMN_THUMB);
	column++;

	vflist_listview_add_column(vf, FILE_COLUMN_FORMATTED, _("Name"), FALSE, FALSE, TRUE);
	g_assert(column == FILE_VIEW_COLUMN_FORMATTED);
	column++;

	vflist_listview_add_column(vf, FILE_COLUMN_FORMATTED_WITH_STARS, _("NameStars"), FALSE, FALSE, TRUE);
	g_assert(column == FILE_VIEW_COLUMN_FORMATTED_WITH_STARS);
	column++;

	vflist_listview_add_column(vf, FILE_COLUMN_STAR_RATING, _("Stars"), FALSE, FALSE, FALSE);
	g_assert(column == FILE_VIEW_COLUMN_STAR_RATING);
	column++;

	vflist_listview_add_column(vf, FILE_COLUMN_SIZE, _("Size"), FALSE, TRUE, FALSE);
	g_assert(column == FILE_VIEW_COLUMN_SIZE);
	column++;

	vflist_listview_add_column(vf, FILE_COLUMN_DATE, _("Date"), FALSE, TRUE, FALSE);
	g_assert(column == FILE_VIEW_COLUMN_DATE);
	column++;

	file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);
	return vf;
}

void vflist_thumb_set(ViewFile *vf, gboolean enable)
{
	if (VFLIST(vf)->thumbs_enabled == enable) return;

	VFLIST(vf)->thumbs_enabled = enable;

	/* vflist_populate_view is better than vflist_refresh:
	   - no need to re-read the directory
	   - force update because the formatted string has changed
	*/
	if (vf->layout)
		{
		vflist_populate_view(vf, TRUE);
		gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
		}
}

void vflist_marks_set(ViewFile *vf, gboolean enable)
{
	GList *columns;
	GList *work;

	columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(vf->listview));

	work = columns;
	while (work)
		{
		auto column = static_cast<GtkTreeViewColumn *>(work->data);
		gint col_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_store_idx"));
		work = work->next;

		if (col_idx <= FILE_COLUMN_MARKS_LAST && col_idx >= FILE_COLUMN_MARKS)
			gtk_tree_view_column_set_visible(column, enable);
		}

	if (enable)
		{
		// Previously disabled, which means that vf->list is complete
		file_data_lock_list(vf->list);
		}
	else
		{
		// Previously enabled, which means that vf->list is incomplete
		}

	g_list_free(columns);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
