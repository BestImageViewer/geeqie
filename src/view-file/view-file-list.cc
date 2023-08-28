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

#include "main.h"
#include "view-file-list.h"

#include "collect.h"
#include "dnd.h"
#include "img-view.h"
#include "layout.h"
#include "layout-image.h"
#include "metadata.h"
#include "misc.h"
#include "utilops.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "uri-utils.h"
#include "view-file.h"

#include <vector>

/* Index to tree store */
enum {
	FILE_COLUMN_POINTER = 0,
	FILE_COLUMN_VERSION,
	FILE_COLUMN_THUMB,
	FILE_COLUMN_FORMATTED,
	FILE_COLUMN_FORMATTED_WITH_STARS,
	FILE_COLUMN_NAME,
	FILE_COLUMN_SIDECARS,
	FILE_COLUMN_STAR_RATING,
	FILE_COLUMN_SIZE,
	FILE_COLUMN_DATE,
	FILE_COLUMN_EXPANDED,
	FILE_COLUMN_COLOR,
	FILE_COLUMN_MARKS,
	FILE_COLUMN_MARKS_LAST = FILE_COLUMN_MARKS + FILEDATA_MARKS_SIZE - 1,
	FILE_COLUMN_COUNT
};


/* Index to tree view */
enum {
	FILE_VIEW_COLUMN_MARKS = 0,
	FILE_VIEW_COLUMN_MARKS_LAST = FILE_VIEW_COLUMN_MARKS + FILEDATA_MARKS_SIZE - 1,
	FILE_VIEW_COLUMN_THUMB,
	FILE_VIEW_COLUMN_FORMATTED,
	FILE_VIEW_COLUMN_FORMATTED_WITH_STARS,
	FILE_VIEW_COLUMN_STAR_RATING,
	FILE_VIEW_COLUMN_SIZE,
	FILE_VIEW_COLUMN_DATE,
	FILE_VIEW_COLUMN_COUNT
};



static gboolean vflist_row_is_selected(ViewFile *vf, FileData *fd);
static gboolean vflist_row_rename_cb(TreeEditData *td, const gchar *old_name, const gchar *new_name, gpointer data);
static void vflist_populate_view(ViewFile *vf, gboolean force);
static gboolean vflist_is_multiline(ViewFile *vf);
static void vflist_set_expanded(ViewFile *vf, GtkTreeIter *iter, gboolean expanded);


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

static FileData *vflist_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *)
{
	GtkTreePath *tpath;
	GtkTreeViewColumn *column;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), x, y,
					  &tpath, &column, nullptr, nullptr))
		{
		GtkTreeModel *store;
		GtkTreeIter row;
		FileData *fd;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &row, tpath);
		gtk_tree_path_free(tpath);
		gtk_tree_model_get(store, &row, FILE_COLUMN_POINTER, &fd, -1);

		return fd;
		}

	return nullptr;
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
	GList *files = nullptr;

	if (unlock_files && vf->marks_enabled)
		{
		// unlock locked files in this directory
		filelist_read(vf->dir_fd, &files, nullptr);
		GList *work = files;
		while (work)
			{
			auto fd = static_cast<FileData *>(work->data);
			work = work->next;
			file_data_unlock(fd);
			file_data_unref(fd);  // undo the ref that got added in filelist_read
			}
		}

	g_list_free(files);
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
	GtkTreePath *tpath;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));

	tpath = gtk_tree_model_get_path(store, iter);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(vf->listview), tpath, nullptr, FALSE);
	gtk_tree_path_free(tpath);
}


/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

static void vflist_dnd_get(GtkWidget *, GdkDragContext *,
			   GtkSelectionData *selection_data, guint,
			   guint, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GList *list = nullptr;

	if (!VFLIST(vf)->click_fd) return;

	if (vflist_row_is_selected(vf, VFLIST(vf)->click_fd))
		{
		list = vf_selection_get_list(vf);
		}
	else
		{
		list = g_list_append(nullptr, file_data_ref(VFLIST(vf)->click_fd));
		}

	if (!list) return;
	uri_selection_data_set_uris_from_filelist(selection_data, list);
	filelist_free(list);
}

static void vflist_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vflist_color_set(vf, VFLIST(vf)->click_fd, TRUE);

	if (VFLIST(vf)->thumbs_enabled &&
	    VFLIST(vf)->click_fd && VFLIST(vf)->click_fd->thumb_pixbuf)
		{
		guint items;

		if (vflist_row_is_selected(vf, VFLIST(vf)->click_fd))
			items = vf_selection_count(vf, nullptr);
		else
			items = 1;

		dnd_set_drag_icon(widget, context, VFLIST(vf)->click_fd->thumb_pixbuf, items);
		}
}

static void vflist_dnd_end(GtkWidget *, GdkDragContext *context, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vflist_color_set(vf, VFLIST(vf)->click_fd, FALSE);

	if (gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE)
		{
		vf_refresh(vf);
		}
}

static void vflist_drag_data_received(GtkWidget *, GdkDragContext *,
				      int x, int y, GtkSelectionData *selection,
				      guint info, guint, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (info == TARGET_TEXT_PLAIN) {
		FileData *fd = vflist_find_data_by_coord(vf, x, y, nullptr);

		if (fd) {
			/* Add keywords to file */
			auto str = reinterpret_cast<gchar *>(gtk_selection_data_get_text(selection));
			GList *kw_list = string_to_keywords_list(str);

			metadata_append_list(fd, KEYWORD_KEY, kw_list);
			g_list_free_full(kw_list, g_free);
			g_free(str);
		}
	}
}

void vflist_dnd_init(ViewFile *vf)
{
	gtk_drag_source_set(vf->listview, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
			    dnd_file_drag_types, dnd_file_drag_types_count,
			    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gtk_drag_dest_set(vf->listview, GTK_DEST_DEFAULT_ALL,
			    dnd_file_drag_types, dnd_file_drag_types_count,
			    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));

	g_signal_connect(G_OBJECT(vf->listview), "drag_data_get",
			 G_CALLBACK(vflist_dnd_get), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_begin",
			 G_CALLBACK(vflist_dnd_begin), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_end",
			 G_CALLBACK(vflist_dnd_end), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_data_received",
			 G_CALLBACK(vflist_drag_data_received), vf);
}

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
			GtkTreePath *tpath;

			tpath = gtk_tree_model_get_path(store, &iter);
			if (!gtk_tree_view_row_expanded(GTK_TREE_VIEW(vf->listview), tpath))
				{
				/* unexpanded - add whole group */
				list = filelist_copy(fd->sidecar_files);
				}
			gtk_tree_path_free(tpath);
			}
		}

	return g_list_prepend(list, file_data_ref(fd));
}

GList *vflist_pop_menu_file_list(ViewFile *vf)
{
	if (!VFLIST(vf)->click_fd) return nullptr;

	if (vflist_row_is_selected(vf, VFLIST(vf)->click_fd))
		{
		return vf_selection_get_list(vf);
		}
	return vflist_selection_get_one(vf, VFLIST(vf)->click_fd);
}


void vflist_pop_menu_view_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (vflist_row_is_selected(vf, VFLIST(vf)->click_fd))
		{
		GList *list;

		list = vf_selection_get_list(vf);
		view_window_new_from_list(list);
		filelist_free(list);
		}
	else
		{
		view_window_new(VFLIST(vf)->click_fd);
		}
}

void vflist_pop_menu_rename_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GList *list;

	list = vf_pop_menu_file_list(vf);
	if (options->file_ops.enable_in_place_rename &&
	    list && !list->next && VFLIST(vf)->click_fd)
		{
		GtkTreeModel *store;
		GtkTreeIter iter;

		filelist_free(list);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		if (vflist_find_row(vf, VFLIST(vf)->click_fd, &iter) >= 0)
			{
			GtkTreePath *tpath;

			tpath = gtk_tree_model_get_path(store, &iter);
			tree_edit_by_path(GTK_TREE_VIEW(vf->listview), tpath,
					  FILE_VIEW_COLUMN_FORMATTED, VFLIST(vf)->click_fd->name,
					  vflist_row_rename_cb, vf);
			gtk_tree_path_free(tpath);
			}
		return;
		}

	file_util_rename(nullptr, list, vf->listview);
}

void vflist_pop_menu_thumbs_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vflist_color_set(vf, VFLIST(vf)->click_fd, FALSE);
	if (vf->layout)
		{
		layout_thumb_set(vf->layout, !VFLIST(vf)->thumbs_enabled);
		}
	else
		{
		vflist_thumb_set(vf, !VFLIST(vf)->thumbs_enabled);
		}
}

void vflist_star_rating_set(ViewFile *vf, gboolean enable)
{
	GList *columns, *work;

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

void vflist_pop_menu_show_star_rating_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	options->show_star_rating = !options->show_star_rating;

	vflist_populate_view(vf, TRUE);

	vflist_color_set(vf, VFLIST(vf)->click_fd, FALSE);
	vflist_star_rating_set(vf, options->show_star_rating);
}

void vflist_pop_menu_refresh_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vflist_color_set(vf, VFLIST(vf)->click_fd, FALSE);
	vf_refresh(vf);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
}

void vflist_popup_destroy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vflist_color_set(vf, VFLIST(vf)->click_fd, FALSE);
	VFLIST(vf)->click_fd = nullptr;
	vf->popup = nullptr;
}


/*
 *-----------------------------------------------------------------------------
 * callbacks
 *-----------------------------------------------------------------------------
 */

static gboolean vflist_row_rename_cb(TreeEditData *, const gchar *old_name, const gchar *new_name, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gchar *new_path;

	if (!new_name || !new_name[0]) return FALSE;

	new_path = g_build_filename(vf->dir_fd->path, new_name, NULL);

	if (strchr(new_name, G_DIR_SEPARATOR) != nullptr)
		{
		gchar *text = g_strdup_printf(_("Invalid file name:\n%s"), new_name);
		file_util_warning_dialog(_("Error renaming file"), text, GQ_ICON_DIALOG_ERROR, vf->listview);
		g_free(text);
		}
	else
		{
		gchar *old_path = g_build_filename(vf->dir_fd->path, old_name, NULL);
		FileData *fd = file_data_new_group(old_path); /* get the fd from cache */
		file_util_rename_simple(fd, new_path, vf->listview);
		file_data_unref(fd);
		g_free(old_path);
		}

	g_free(new_path);

	return FALSE;
}

gboolean vflist_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkTreePath *tpath;

	if (event->keyval != GDK_KEY_Menu) return FALSE;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(vf->listview), &tpath, nullptr);
	if (tpath)
		{
		GtkTreeModel *store;
		GtkTreeIter iter;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &VFLIST(vf)->click_fd, -1);
		gtk_tree_path_free(tpath);
		}
	else
		{
		VFLIST(vf)->click_fd = nullptr;
		}

	vf->popup = vf_pop_menu(vf);
	gtk_menu_popup_at_widget(GTK_MENU(vf->popup), widget, GDK_GRAVITY_EAST, GDK_GRAVITY_CENTER, nullptr);

	return TRUE;
}

gboolean vflist_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = nullptr;
	GtkTreeViewColumn *column;

	vf->clicked_mark = 0;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, &column, nullptr, nullptr))
		{
		GtkTreeModel *store;
		gint col_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "column_store_idx"));

		if (bevent->button == MOUSE_BUTTON_LEFT &&
		    col_idx >= FILE_COLUMN_MARKS && col_idx <= FILE_COLUMN_MARKS_LAST)
			return FALSE;

		if (col_idx >= FILE_COLUMN_MARKS && col_idx <= FILE_COLUMN_MARKS_LAST)
			vf->clicked_mark = 1 + (col_idx - FILE_COLUMN_MARKS);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	VFLIST(vf)->click_fd = fd;

	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		vf->popup = vf_pop_menu(vf);
		gtk_menu_popup_at_pointer(GTK_MENU(vf->popup), nullptr);
		return TRUE;
		}

	if (!fd) return FALSE;

	if (bevent->button == MOUSE_BUTTON_MIDDLE)
		{
		if (!vflist_row_is_selected(vf, fd))
			{
			vflist_color_set(vf, fd, TRUE);
			}
		return TRUE;
		}


	if (bevent->button == MOUSE_BUTTON_LEFT && bevent->type == GDK_BUTTON_PRESS &&
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

	if (bevent->button == MOUSE_BUTTON_LEFT && bevent->type == GDK_2BUTTON_PRESS)
		{
		if (VFLIST(vf)->click_fd->format_class == FORMAT_CLASS_COLLECTION)
			{
			collection_window_new(VFLIST(vf)->click_fd->path);
			}
		else
			{
			if (vf->layout) layout_image_full_screen_start(vf->layout);
			}
		}

	return FALSE;
}

gboolean vflist_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = nullptr;

	if (defined_mouse_buttons(widget, bevent, vf->layout))
		{
		return TRUE;
		}

	if (bevent->button == MOUSE_BUTTON_MIDDLE)
		{
		vflist_color_set(vf, VFLIST(vf)->click_fd, FALSE);
		}

	if (bevent->button != MOUSE_BUTTON_LEFT && bevent->button != MOUSE_BUTTON_MIDDLE)
		{
		return TRUE;
		}

	if ((bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeModel *store;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);
		gtk_tree_path_free(tpath);
		}

	if (bevent->button == MOUSE_BUTTON_MIDDLE)
		{
		if (fd && VFLIST(vf)->click_fd == fd)
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

	if (fd && VFLIST(vf)->click_fd == fd &&
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

	if (sel_fd && options->image.enable_read_ahead && row >= 0)
		{
		if (row > g_list_index(vf->list, cur_fd) &&
		    static_cast<guint>(row + 1) < vf_count(vf, nullptr))
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

	if (!vf->layout)
		{
		VFLIST(vf)->select_idle_id = 0;
		return G_SOURCE_REMOVE;
		}

	vf_send_update(vf);

	if (VFLIST(vf)->select_fd)
		{
		vflist_select_image(vf, VFLIST(vf)->select_fd);
		VFLIST(vf)->select_fd = nullptr;
		}

	VFLIST(vf)->select_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void vflist_select_idle_cancel(ViewFile *vf)
{
	if (VFLIST(vf)->select_idle_id)
		{
		g_source_remove(VFLIST(vf)->select_idle_id);
		VFLIST(vf)->select_idle_id = 0;
		}
}

static gboolean vflist_select_cb(GtkTreeSelection *, GtkTreeModel *store, GtkTreePath *tpath, gboolean path_currently_selected, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkTreeIter iter;
	GtkTreePath *cursor_path;

	VFLIST(vf)->select_fd = nullptr;

	if (!path_currently_selected && gtk_tree_model_get_iter(store, &iter, tpath))
		{
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(vf->listview), &cursor_path, nullptr);
		if (cursor_path)
			{
			gtk_tree_model_get_iter(store, &iter, cursor_path);
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &VFLIST(vf)->select_fd, -1);
			gtk_tree_path_free(cursor_path);
			}
		}

	if (vf->layout &&
	    !VFLIST(vf)->select_idle_id)
		{
		VFLIST(vf)->select_idle_id = g_idle_add(vflist_select_idle_cb, vf);
		}

	return TRUE;
}

static void vflist_expand_cb(GtkTreeView *, GtkTreeIter *iter, GtkTreePath *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vflist_set_expanded(vf, iter, TRUE);
}

static void vflist_collapse_cb(GtkTreeView *, GtkTreeIter *iter, GtkTreePath *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vflist_set_expanded(vf, iter, FALSE);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */


static gchar* vflist_get_formatted(ViewFile *vf, const gchar *name, const gchar *sidecars, const gchar *size, const gchar *time, gboolean expanded, gboolean with_stars, const gchar *star_rating)
 {
	gboolean multiline = vflist_is_multiline(vf);
	gchar *text;

	if (multiline)
		{
		if (with_stars)
			{
					text = g_strdup_printf("%s %s\n%s\n%s\n%s", name, expanded ? "" : sidecars, size, time, star_rating);
			}
		else
			{
			text = g_strdup_printf("%s %s\n%s\n%s", name, expanded ? "" : sidecars, size, time);
			}
		}
	else
		{
		text = g_strdup_printf("%s %s", name, expanded ? "" : sidecars);
		}
	return text;
}

static void vflist_set_expanded(ViewFile *vf, GtkTreeIter *iter, gboolean expanded)
{
	GtkTreeStore *store;
	gchar *name;
	gchar *sidecars;
	gchar *size;
	gchar *time;
	gchar *formatted;
	gchar *formatted_with_stars;
	gchar *star_rating;
	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));

	gtk_tree_model_get(GTK_TREE_MODEL(store), iter,
					FILE_COLUMN_NAME, &name,
					FILE_COLUMN_SIDECARS, &sidecars,
					FILE_COLUMN_SIZE, &size,
					FILE_COLUMN_DATE, &time,
					FILE_COLUMN_STAR_RATING, &star_rating,
					-1);

	formatted = vflist_get_formatted(vf, name, sidecars, size, time, expanded, FALSE, nullptr);
	formatted_with_stars = vflist_get_formatted(vf, name, sidecars, size, time, expanded, TRUE, star_rating);

	gtk_tree_store_set(store, iter, FILE_COLUMN_FORMATTED, formatted,
					FILE_COLUMN_EXPANDED, expanded,
					-1);
	gtk_tree_store_set(store, iter, FILE_COLUMN_FORMATTED_WITH_STARS, formatted_with_stars,
					FILE_COLUMN_EXPANDED, expanded,
					-1);
	g_free(time);
	g_free(size);
	g_free(sidecars);
	g_free(name);
	g_free(formatted);
	g_free(formatted_with_stars);
}

static void vflist_setup_iter(ViewFile *vf, GtkTreeStore *store, GtkTreeIter *iter, FileData *fd)
{
	gchar *size;
	gchar *sidecars = nullptr;
	gchar *name;
	const gchar *time = text_from_time(fd->date);
	const gchar *link = islink(fd->path) ? GQ_LINK_STR : "";
	const gchar *disabled_grouping;
	gchar *formatted;
	gchar *formatted_with_stars;
	gboolean expanded = FALSE;
	gchar *star_rating;

	if (options->show_star_rating && fd->rating != STAR_RATING_NOT_READ)
		{
 		star_rating = convert_rating_to_stars(fd->rating);
		}
	else
		{
		star_rating = nullptr;
		}

	if (fd->sidecar_files) /* expanded has no effect on files without sidecars */
		{
		gtk_tree_model_get(GTK_TREE_MODEL(store), iter, FILE_COLUMN_EXPANDED, &expanded, -1);
		}

	sidecars = file_data_sc_list_to_string(fd);

	disabled_grouping = fd->disable_grouping ? _(" [NO GROUPING]") : "";
	name = g_strdup_printf("%s%s%s", link, fd->name, disabled_grouping);
	size = text_from_size(fd->size);

	formatted = vflist_get_formatted(vf, name, sidecars, size, time, expanded, FALSE, nullptr);
	formatted_with_stars = vflist_get_formatted(vf, name, sidecars, size, time, expanded, TRUE, star_rating);

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
	g_free(size);
	g_free(sidecars);
	g_free(name);
	g_free(formatted);
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
						match = filelist_sort_compare_filedata_full(fd, old_fd, vf->sort_method, vf->sort_ascend);

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

void vflist_sort_set(ViewFile *vf, SortType type, gboolean ascend, gboolean case_sensitive)
{
	gint i;
	GHashTable *fd_idx_hash = g_hash_table_new(nullptr, nullptr);
	GtkTreeStore *store;
	GList *work;

	if (vf->sort_method == type && vf->sort_ascend == ascend && vf->sort_case == case_sensitive) return;
	if (!vf->list) return;

	work = vf->list;
	i = 0;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		g_hash_table_insert(fd_idx_hash, fd, GINT_TO_POINTER(i));
		i++;
		work = work->next;
		}

	vf->sort_method = type;
	vf->sort_ascend = ascend;
	vf->sort_case = case_sensitive;

	vf->list = filelist_sort(vf->list, vf->sort_method, vf->sort_ascend, vf->sort_case);

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


void vflist_thumb_progress_count(GList *list, gint *count, gint *done)
{
	GList *work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (fd->thumb_pixbuf) (*done)++;

		if (fd->sidecar_files)
			{
			vflist_thumb_progress_count(fd->sidecar_files, count, done);
			}
		(*count)++;
		}
}

void vflist_read_metadata_progress_count(GList *list, gint *count, gint *done)
{
	GList *work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (fd->metadata_in_idle_loaded) (*done)++;

		if (fd->sidecar_files)
			{
			vflist_read_metadata_progress_count(fd->sidecar_files, count, done);
			}
		(*count)++;
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
	GtkTreePath *tpath;
	FileData *fd = nullptr;

	/* first check the visible files */

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid = TRUE;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_path_free(tpath);
		tpath = nullptr;

		while (!fd && valid && tree_view_row_get_visibility(GTK_TREE_VIEW(vf->listview), &iter, FALSE) == 0)
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
	gchar *name;
	gchar *sidecars;
	gchar *size;
	gchar *time;
	gchar *star_rating;
	gchar *formatted_with_stars;
	gboolean expanded;

	if (!fd || vflist_find_row(vf, fd, &iter) < 0) return;

	star_rating = metadata_read_rating_stars(fd);

	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	gtk_tree_store_set(store, &iter, FILE_COLUMN_STAR_RATING, star_rating, -1);

	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
					FILE_COLUMN_NAME, &name,
					FILE_COLUMN_SIDECARS, &sidecars,
					FILE_COLUMN_SIZE, &size,
					FILE_COLUMN_DATE, &time,
					FILE_COLUMN_EXPANDED, &expanded,
					-1);

	formatted_with_stars = vflist_get_formatted(vf, name, sidecars, size, time, expanded, TRUE, star_rating);

	gtk_tree_store_set(store, &iter, FILE_COLUMN_FORMATTED_WITH_STARS, formatted_with_stars,
					FILE_COLUMN_EXPANDED, expanded,
					-1);

	g_free(star_rating);
	g_free(formatted_with_stars);
}

FileData *vflist_star_next_fd(ViewFile *vf)
{
	GtkTreePath *tpath;
	FileData *fd = nullptr;
	FileData *nfd = nullptr;

	/* first check the visible files */

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vf->listview), 0, 0, &tpath, nullptr, nullptr, nullptr))
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid = TRUE;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_path_free(tpath);
		tpath = nullptr;

		while (!fd && valid && tree_view_row_get_visibility(GTK_TREE_VIEW(vf->listview), &iter, FALSE) == 0)
			{
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &nfd, -1);

			if (nfd && nfd->rating == STAR_RATING_NOT_READ)
				{
				fd = nfd;
				}

			valid = gtk_tree_model_iter_next(store, &iter);
			}

		if (fd)
			{
			vf->stars_filedata = fd;

			if (vf->stars_id == 0)
				{
				vf->stars_id = g_idle_add_full(G_PRIORITY_LOW, vf_stars_cb, vf, nullptr);
				}
			}
		}

	/* then find first undone */

	if (!fd)
		{
		GList *work = vf->list;

		while (work && !fd)
			{
			auto fd_p = static_cast<FileData *>(work->data);

			if (fd_p && fd_p->rating == STAR_RATING_NOT_READ)
				{
				fd = fd_p;
				}
			else
				{
				fd = nullptr;
				}

			work = work->next;
			}

		if (fd)
			{
			vf->stars_filedata = fd;

			if (vf->stars_id == 0)
				{
				vf->stars_id = g_idle_add_full(G_PRIORITY_LOW, vf_stars_cb, vf, nullptr);
				}
			}
		}

	return fd;
}

/*
 *-----------------------------------------------------------------------------
 * row stuff
 *-----------------------------------------------------------------------------
 */

gint vflist_index_by_fd(ViewFile *vf, FileData *fd)
{
	gint p = 0;
	GList *work, *work2;

	work = vf->list;
	while (work)
		{
		auto list_fd = static_cast<FileData *>(work->data);
		if (list_fd == fd) return p;

		work2 = list_fd->sidecar_files;
		while (work2)
			{
			/** @FIXME return the same index also for sidecars
			   it is sufficient for next/prev navigation but it should be rewritten
			   without using indexes at all
			*/
			auto sidecar_fd = static_cast<FileData *>(work2->data);
			if (sidecar_fd == fd) return p;
			work2 = work2->next;
			}

		work = work->next;
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
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *work;
	gboolean found = FALSE;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (!found && work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		FileData *fd_n;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd_n, -1);
		if (fd_n == fd) found = TRUE;
		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	return found;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
gboolean vflist_index_is_selected_unused(ViewFile *vf, gint row)
{
	FileData *fd;

	fd = vf_index_get_data(vf, row);
	return vflist_row_is_selected(vf, fd);
}
#pragma GCC diagnostic pop

guint vflist_selection_count(ViewFile *vf, gint64 *bytes)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	guint count;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);

	if (bytes)
		{
		gint64 b = 0;
		GList *work;

		work = slist;
		while (work)
			{
			auto tpath = static_cast<GtkTreePath *>(work->data);
			GtkTreeIter iter;
			FileData *fd;

			gtk_tree_model_get_iter(store, &iter, tpath);
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);
			b += fd->size;

			work = work->next;
			}

		*bytes = b;
		}

	count = g_list_length(slist);
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	return count;
}

GList *vflist_selection_get_list(ViewFile *vf)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *list = nullptr;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
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
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	return list;
}

GList *vflist_selection_get_list_by_index(ViewFile *vf)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *list = nullptr;
	GList *work;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		FileData *fd;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);

		list = g_list_prepend(list, GINT_TO_POINTER(g_list_index(vf->list, fd)));

		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	return g_list_reverse(list);
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
	GtkTreePath *tpath;
	gboolean result;

	tpath = gtk_tree_model_get_path(store, iter);
	result = gtk_tree_path_prev(tpath);
	if (result)
		gtk_tree_model_get_iter(store, iter, tpath);

	gtk_tree_path_free(tpath);

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

	if (!vflist_row_is_selected(vf, fd))
		{
		GtkTreeSelection *selection;
		GtkTreeModel *store;
		GtkTreePath *tpath;

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
		gtk_tree_selection_unselect_all(selection);
		gtk_tree_selection_select_iter(selection, &iter);
		vflist_move_cursor(vf, &iter);

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		tpath = gtk_tree_model_get_path(store, &iter);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(vf->listview), tpath, nullptr, FALSE);
		gtk_tree_path_free(tpath);
		}
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

		match = filelist_sort_compare_filedata_full(fd, sel_fd, vf->sort_method, vf->sort_ascend);

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
	gint n = mark - 1;

	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));

	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		FileData *fd;
		gboolean mark_val, selected;
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, FILE_COLUMN_POINTER, &fd, -1);

		mark_val = file_data_get_mark(fd, n);
		selected = gtk_tree_selection_iter_is_selected(selection, &iter);

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

		if (selected)
			gtk_tree_selection_select_iter(selection, &iter);
		else
			gtk_tree_selection_unselect_iter(selection, &iter);

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}
}

void vflist_selection_to_mark(ViewFile *vf, gint mark, SelectionToMarkMode mode)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *work;
	gint n = mark - 1;

	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		FileData *fd;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd, -1);

		/* the change has a very limited range and the standard notification would trigger
		   complete re-read of the directory - try to do only minimal update instead */
		file_data_unregister_notify_func(vf_notify_cb, vf); /* we don't need the notification */

		switch (mode)
			{
			case STM_MODE_SET: file_data_set_mark(fd, n, 1);
				break;
			case STM_MODE_RESET: file_data_set_mark(fd, n, 0);
				break;
			case STM_MODE_TOGGLE: file_data_set_mark(fd, n, !file_data_get_mark(fd, n));
				break;
			}

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

		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));
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

	if (selected && vflist_selection_count(vf, nullptr) == 0)
		{
		/* all selected files disappeared */
		vflist_select_closest(vf, static_cast<FileData *>(selected->data));
		}

	filelist_free(selected);

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
		vf->list = g_list_first(vf->list);
		vf->list = file_data_filter_file_filter_list(vf->list, vf_file_filter_get_filter(vf));

		vf->list = g_list_first(vf->list);
		vf->list = file_data_filter_class_list(vf->list, vf_class_get_filter(vf));

		file_data_register_notify_func(vf_notify_cb, vf, NOTIFY_PRIORITY_MEDIUM);

		DEBUG_1("%s vflist_refresh: sort", get_exec_time());
		vf->list = filelist_sort(vf->list, vf->sort_method, vf->sort_ascend, vf->sort_case);
		}

	DEBUG_1("%s vflist_refresh: populate view", get_exec_time());

	vflist_populate_view(vf, FALSE);

	DEBUG_1("%s vflist_refresh: free filelist", get_exec_time());

	filelist_free(old_list);
	DEBUG_1("%s vflist_refresh: done", get_exec_time());

	return ret;
}



/* this overrides the low default of a GtkCellRenderer from 100 to CELL_HEIGHT_OVERRIDE, something sane for our purposes */

#define CELL_HEIGHT_OVERRIDE 512

static void cell_renderer_height_override(GtkCellRenderer *renderer)
{
	GParamSpec *spec;

	spec = g_object_class_find_property(G_OBJECT_GET_CLASS(G_OBJECT(renderer)), "height");
	if (spec && G_IS_PARAM_SPEC_INT(spec))
		{
		GParamSpecInt *spec_int;

		spec_int = G_PARAM_SPEC_INT(spec);
		if (spec_int->maximum < CELL_HEIGHT_OVERRIDE) spec_int->maximum = CELL_HEIGHT_OVERRIDE;
		}
}

static GdkColor *vflist_listview_color_shifted(GtkWidget *widget)
{
	static GdkColor color;
	static GtkWidget *done = nullptr;

	if (done != widget)
		{
		GtkStyle *style;

		style = gtk_widget_get_style(widget);
		memcpy(&color, &style->base[GTK_STATE_NORMAL], sizeof(color));
		shift_color(&color, -1, 0);
		done = widget;
		}

	return &color;
}

static void vflist_listview_color_cb(GtkTreeViewColumn *, GtkCellRenderer *cell,
				     GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean set;

	gtk_tree_model_get(tree_model, iter, FILE_COLUMN_COLOR, &set, -1);
	g_object_set(G_OBJECT(cell),
		     "cell-background-gdk", vflist_listview_color_shifted(vf->listview),
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
	GtkTreeStore *store;
	GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
	GtkTreeIter iter;
	FileData *fd;
	gboolean marked;
	guint col_idx;

	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview)));
	if (!path || !gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, path))
		return;

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

	gtk_tree_path_free(path);
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

	filelist_free(vf->list);
	vf->list = nullptr;

	ret = vf_refresh(vf);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(vf->listview));
	return ret;
}

void vflist_destroy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_data_unregister_notify_func(vf_notify_cb, vf);

	vflist_select_idle_cancel(vf);
	vf_refresh_idle_cancel(vf);
	vf_thumb_stop(vf);
	vf_star_stop(vf);

	filelist_free(vf->list);
}

ViewFile *vflist_new(ViewFile *vf, FileData *)
{
	GtkTreeStore *store;
	GtkTreeSelection *selection;
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
			 G_CALLBACK(vflist_expand_cb), vf);

	g_signal_connect(G_OBJECT(vf->listview), "row-collapsed",
			 G_CALLBACK(vflist_collapse_cb), vf);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf->listview));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_MULTIPLE);
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

	/* vflist_populate_view is better than vf_refresh:
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
	GList *columns, *work;

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
