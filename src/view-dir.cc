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

#include "view-dir.h"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstring>

#include <gio/gio.h>
#include <glib-object.h>

#include <config.h>

#include "accelerators.h"
#include "actions.h"
#include "compat-deprecated.h"
#include "compat.h"
#include "dnd.h"
#include "dupe.h"
#include "editors.h"
#include "filedata.h"
#include "geometry.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "uri-utils.h"
#include "utilops.h"
#include "view-dir-list.h"
#include "view-dir-tree.h"

namespace
{
	
GIcon *load_icon(const gchar *icon_name)
{
	return g_themed_icon_new(icon_name);
}

GIcon *create_folder_icon_with_emblem(const gchar *emblem)
{
	g_autoptr(GIcon) icon_folder = g_themed_icon_new(GQ_ICON_DIRECTORY);
	g_autoptr(GIcon) icon_emblem = g_themed_icon_new(emblem);
	g_autoptr(GEmblem) emblem_new = g_emblem_new(icon_emblem);

	return g_emblemed_icon_new(icon_folder, emblem_new);
}

/* Folders icons to be used in tree or list directory view */
PixmapFolders *folder_icons_new()
{
	auto pf = g_new0(PixmapFolders, 1);

	pf->close  = load_icon(GQ_ICON_DIRECTORY);
	pf->open   = load_icon(GQ_ICON_OPEN);
	pf->parent = load_icon(GQ_ICON_GO_UP);

	pf->deny = create_folder_icon_with_emblem(GQ_ICON_UNREADABLE);

	pf->link = create_folder_icon_with_emblem(GQ_ICON_LINK);

	pf->read_only = create_folder_icon_with_emblem(GQ_ICON_READONLY);

	return pf;
}

void folder_icons_free(PixmapFolders *pf)
{
	if (!pf) return;

	g_clear_object(&pf->close);
	g_clear_object(&pf->open);
	g_clear_object(&pf->parent);
	g_clear_object(&pf->deny);
	g_clear_object(&pf->link);
	g_clear_object(&pf->read_only);

	g_free(pf);
}
}

static void vd_notify_cb(FileData *fd, NotifyType type, gpointer data);

static void vd_destroy_cb(GtkWidget *widget, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	file_data_unregister_notify_func(vd_notify_cb, vd);

	if (vd->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(vd->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, nullptr, nullptr, vd);
		gq_gtk_widget_destroy(vd->popup);
		}

	switch (vd->type)
		{
		case DIRVIEW_LIST: vdlist_destroy_cb(widget, data); break;
		case DIRVIEW_TREE: vdtree_destroy_cb(widget, data); break;
		}

	folder_icons_free(vd->pf);
	file_data_list_free(vd->drop_list);

	g_clear_pointer(&vd->info, g_free);

	delete vd;
}

void vd_set_select_func(ViewDir *vd,
			void (*func)(ViewDir *vd, FileData *fd, gpointer data), gpointer data)
{
	vd->select_func = func;
	vd->select_data = data;
}

gboolean vd_set_fd(ViewDir *vd, FileData *dir_fd)
{
	gboolean ret = FALSE;

	file_data_unregister_notify_func(vd_notify_cb, vd);

	switch (vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_set_fd(vd, dir_fd); break;
	case DIRVIEW_TREE: ret = vdtree_set_fd(vd, dir_fd); break;
	}

	file_data_register_notify_func(vd_notify_cb, vd, NOTIFY_PRIORITY_HIGH);

	return ret;
}

void vd_refresh(ViewDir *vd)
{
	switch (vd->type)
	{
	case DIRVIEW_LIST: vdlist_refresh(vd); break;
	case DIRVIEW_TREE: vdtree_refresh(vd); break;
	}
}

/* the calling stack is this:
   vd_select_row -> select_func -> layout_set_fd -> vd_set_fd
*/
static void vd_select_row(ViewDir *vd, FileData *fd)
{
	if (fd && vd->select_func)
		{
		vd->select_func(vd, fd, vd->select_data);
		}
}

gboolean vd_find_row(ViewDir *vd, FileData *fd, GtkTreeIter *iter)
{
	gboolean ret = FALSE;

	switch (vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_find_row(vd, fd, iter); break;
	case DIRVIEW_TREE: ret = vdtree_find_row(vd, fd, iter, nullptr); break;
	}

	return ret;
}

static FileData *vd_get_fd_from_tree_path(ViewDir *vd, GtkTreeView *tview, GtkTreePath *tpath)
{
	GtkTreeIter iter;
	FileData *fd = nullptr;
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(tview);
	gtk_tree_model_get_iter(store, &iter, tpath);
	switch (vd->type)
		{
		case DIRVIEW_LIST:
			gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
			break;
		case DIRVIEW_TREE:
			{
			NodeData *nd;
			gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &nd, -1);
			fd = (nd) ? nd->fd : nullptr;
			};
			break;
		}

	return fd;
}

static gboolean vd_rename_cb(TreeEditData *td, const gchar *, const gchar *new_name, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	FileData *fd;

	fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(vd->view), td->path);
	if (!fd) return FALSE;

	g_autofree gchar *base = remove_level_from_path(fd->path);
	g_autofree gchar *new_path = g_build_filename(base, new_name, NULL);

	const auto vd_rename_finished_cb = [vd](gboolean success, const gchar *new_path)
	{
		if (!success) return;

		auto fd_ref = FileData::new_dir(new_path);

		GtkTreeIter iter;
		if (vd_find_row(vd, fd_ref, &iter))
			{
			tree_view_row_make_visible(GTK_TREE_VIEW(vd->view), &iter, TRUE);
			}
	};
	file_util_rename_dir(fd, new_path, vd->view, vd_rename_finished_cb);

	return FALSE;
}

static void vd_rename_by_data(ViewDir *vd, FileData *fd)
{
	GtkTreeIter iter;
	if (!fd || !vd_find_row(vd, fd, &iter)) return;

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);

	tree_edit_by_path(GTK_TREE_VIEW(vd->view), tpath, 0, fd->name,
	                  vd_rename_cb, vd);
}


void vd_color_set(ViewDir *vd, FileData *fd, gint color_set)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	if (!vd_find_row(vd, fd, &iter)) return;
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));

	switch (vd->type)
	{
	case DIRVIEW_LIST:
		gtk_list_store_set(GTK_LIST_STORE(store), &iter, DIR_COLUMN_COLOR, color_set, -1);
		break;
	case DIRVIEW_TREE:
		gtk_tree_store_set(GTK_TREE_STORE(store), &iter, DIR_COLUMN_COLOR, color_set, -1);
		break;
	}
}

void vd_popup_destroy_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd_color_set(vd, vd->click_fd, FALSE);
	vd->click_fd = nullptr;
	vd->popup = nullptr;

	vd_color_set(vd, vd->drop_fd, FALSE);
	file_data_list_free(vd->drop_list);
	vd->drop_list = nullptr;
	vd->drop_fd = nullptr;
}

/*
 *-----------------------------------------------------------------------------
 * drop menu (from dnd)
 *-----------------------------------------------------------------------------
 */

static void vd_drop_menu_copy_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	const gchar *path;
	GList *list;

	if (!vd->drop_fd) return;

	path = vd->drop_fd->path;
	list = vd->drop_list;
	vd->drop_list = nullptr;

	file_util_copy_simple(list, path, vd->widget);
}

static void vd_drop_menu_move_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	const gchar *path;
	GList *list;

	if (!vd->drop_fd) return;

	path = vd->drop_fd->path;
	list = vd->drop_list;

	vd->drop_list = nullptr;

	file_util_move_simple(list, path, vd->widget);
}

static void vd_drop_menu_filter_cb(GtkWidget *widget, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	const gchar *path;
	GList *list;
	const gchar *key;

	if (!vd->drop_fd) return;

	key = static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "filter_key"));

	path = vd->drop_fd->path;
	list = vd->drop_list;

	vd->drop_list = nullptr;

	file_util_start_filter_from_filelist(key, list, path, vd->widget);
}

GtkWidget *vd_drop_menu(ViewDir *vd, gint active)
{
	GtkWidget *menu;

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vd_popup_destroy_cb), vd);

	menu_item_add_icon_sensitive(menu, _("_Copy"), GQ_ICON_COPY, active,
				      G_CALLBACK(vd_drop_menu_copy_cb), vd);
	menu_item_add_sensitive(menu, _("_Move"), active, G_CALLBACK(vd_drop_menu_move_cb), vd);

	EditorsList editors_list = editor_list_get();
	for (const EditorDescription *editor : editors_list)
		{
		if (!editor_is_filter(editor->key)) continue;

		GtkWidget *item = menu_item_add_sensitive(menu, editor->name, active,
		                                          G_CALLBACK(vd_drop_menu_filter_cb), vd);
		g_object_set_data_full(G_OBJECT(item), "filter_key", g_strdup(editor->key), g_free);
		}

	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("Cancel"), GQ_ICON_CANCEL, nullptr, vd);

	return menu;
}

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

static void vd_pop_menu_up_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->dir_fd || strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) == 0) return;

	if (vd->select_func)
		{
		g_autofree gchar *path = remove_level_from_path(vd->dir_fd->path);
		auto fd_ref = FileData::new_dir(path);
		vd->select_func(vd, fd_ref, vd->select_data);
		}
}

static void vd_pop_menu_slide_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->layout) return;
	if (!vd->click_fd) return;

	layout_set_fd(vd->layout, vd->click_fd);
	layout_select_none(vd->layout);
	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start(vd->layout);
}

static void vd_pop_menu_slide_rec_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd->layout || !vd->click_fd) return;

	GList *list = filelist_recursive_full(vd->click_fd, vd->layout->options.file_view_list_sort);

	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start_from_list(vd->layout, list);
}

template<gboolean recursive>
static void vd_pop_menu_dupe_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd->click_fd) return;

	g_autoptr(FileDataList) list = nullptr;

	if (recursive)
		{
		list = g_list_append(list, file_data_ref(vd->click_fd));
		}
	else
		{
		filelist_read(vd->click_fd, &list, nullptr);
		list = filelist_filter(list, FALSE);
		}

	DupeWindow *dw = dupe_window_new();
	dupe_window_add_files(dw, list, recursive);
}

static void vd_pop_menu_delete_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->click_fd) return;
	file_util_delete_dir(vd->click_fd, vd->widget);
}

template<gboolean quoted>
static void vd_pop_menu_copy_path_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->click_fd) return;

	file_util_copy_path_to_clipboard(vd->click_fd, quoted, ClipboardAction::COPY);
}

static void vd_pop_menu_cut_path_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->click_fd) return;

	file_util_copy_path_to_clipboard(vd->click_fd, FALSE, ClipboardAction::CUT);
}

static void vd_pop_submenu_dir_view_as_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	const char *value = g_variant_get_string(parameter, nullptr);

	DirViewType dir_view_type = DIRVIEW_LIST;

	if (g_str_equal(value, "tree"))
		{
		dir_view_type = DIRVIEW_TREE;
		}

	layout_views_set(vd->layout, dir_view_type, vd->layout->options.file_view_type);
}

static void vd_pop_menu_refresh_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (vd->layout) layout_refresh(vd->layout);
}

static void vd_toggle_show_hidden_files_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	options->file_filter.show_hidden_files = !options->file_filter.show_hidden_files;
	if (vd->layout) layout_refresh(vd->layout);
}

static void vd_pop_menu_new_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	FileData *dir_fd = nullptr;

	switch (vd->type)
		{
		case DIRVIEW_LIST:
			{
			if (!vd->dir_fd) return;
			dir_fd = vd->dir_fd;
			};
			break;
		case DIRVIEW_TREE:
			{
			if (!vd->click_fd) return;
			dir_fd = vd->click_fd;
			};
			break;
		}

	vd_new_folder(vd, dir_fd);
}

static void vd_pop_menu_rename_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd_rename_by_data(vd, vd->click_fd);
}

static void vd_pop_menu_sort_ascend_cb(GSimpleAction *, GVariant *state, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd || !vd->layout) return;

	bool enabled = g_variant_get_boolean(state);

	auto sort = vd->layout->options.dir_view_list_sort;
	sort.ascending = enabled;

	layout_views_set_sort_dir(vd->layout, sort);
	layout_refresh(vd->layout);
}

static void vd_pop_menu_sort_case_cb(GSimpleAction *, GVariant *state, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd || !vd->layout) return;

	bool enabled = g_variant_get_boolean(state);

	auto sort = vd->layout->options.dir_view_list_sort;
	sort.case_sensitive = enabled;

	layout_views_set_sort_dir(vd->layout, sort);
	layout_refresh(vd->layout);
}

static void vd_pop_menu_sort_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd || !vd->layout || !parameter) return;

	const char *value = g_variant_get_string(parameter, nullptr);

	auto sort = vd->layout->options.dir_view_list_sort;

	if (g_str_equal(value, "name"))
		{
		sort.method = SORT_NAME;
		}
	else if (g_str_equal(value, "number"))
		{
		sort.method = SORT_NUMBER;
		}
	else
		{
		sort.method = SORT_TIME;
		}

	layout_views_set_sort_dir(vd->layout, sort);
	layout_refresh(vd->layout);
}

void vd_pop_menu(ViewDir *vd, FileData *fd)
{
	GtkWidget *menu;
	gboolean active;
	gboolean rename_delete_active = FALSE;
	gboolean new_folder_active = FALSE;

	active = (fd != nullptr);
	switch (vd->type)
		{
		case DIRVIEW_LIST:
			{
			/* check using . (always row 0) */
			new_folder_active = (vd->dir_fd && access_file(vd->dir_fd->path , W_OK | X_OK));

			/* ignore .. and . */
			rename_delete_active = (new_folder_active && fd &&
				strcmp(fd->name, ".") != 0 &&
				strcmp(fd->name, "..") != 0 &&
				access_file(fd->path, W_OK | X_OK));
			};
			break;
		case DIRVIEW_TREE:
			{
			if (fd)
				{
				new_folder_active = access_file(fd->path, W_OK | X_OK);

				g_autofree gchar *parent = remove_level_from_path(fd->path);
				rename_delete_active = access_file(parent, W_OK | X_OK);
				};
			}
			break;
		}

	GtkBuilder *builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-dir-popup.ui");
	GMenu *menu_model = G_MENU(gtk_builder_get_object(builder, "menu-dir-popup"));

	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-up");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), (vd->dir_fd && strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) != 0));

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-slideshow");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), active);

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-slideshow-recursive");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), active);

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-find-duplicates");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), active);

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-find-duplicates-recursive");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), active);

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-new-folder");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), new_folder_active);

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-rename");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), rename_delete_active);

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-delete");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), rename_delete_active);

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-sort-ascending");
	if (vd->layout->options.dir_view_type == DIRVIEW_LIST)
		{
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), TRUE);
		}
	else
		{
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), FALSE);
		}

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-sort-case");
	if (vd->layout->options.dir_view_type == DIRVIEW_LIST)
		{
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), TRUE);
		}
	else
		{
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), FALSE);
		}

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-view-type");
	if (vd->layout->options.dir_view_type == DIRVIEW_LIST)
		{
		g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("list"));
		}
	else
		{
		g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("tree"));
		}

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-sort");
	const char *value = nullptr;
	auto sort = vd->layout->options.dir_view_list_sort;
	if (sort.method == SORT_NAME)
		{
		value = "name";
		}
	else if (sort.method == SORT_NUMBER)
		{
		value = "number";
		}
	else
		{
		value = "date";
		}
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string(value));

	action = g_action_map_lookup_action(G_ACTION_MAP(vd->layout->window), "dir-win-show-hidden-files");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->file_filter.show_hidden_files));

	popup_menu(menu_model, vd->layout->window);
}

void vd_new_folder(ViewDir *vd, FileData *dir_fd)
{
	const auto vd_pop_menu_new_folder_cb = [vd](gboolean success, const gchar *new_path)
	{
		if (!success) return;

		FileData *fd = nullptr;
		switch (vd->type)
			{
			case DIRVIEW_LIST:
				{
				vd_refresh(vd);
				fd = vdlist_row_by_path(vd, new_path, nullptr);
				}
				break;
			case DIRVIEW_TREE:
				{
				auto new_fd_ref = FileData::new_dir(new_path);
				fd = vdtree_populate_path(vd, new_fd_ref, TRUE, TRUE);
				}
				break;
			}

		GtkTreeIter iter;
		if (!fd || !vd_find_row(vd, fd, &iter)) return;

		GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
		g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, &iter);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(vd->view), tpath, nullptr, FALSE);
	};
	file_util_create_dir(dir_fd->path, vd->layout->window, vd_pop_menu_new_folder_cb);
}

/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

static void vd_dnd_drop_update(ViewDir *vd, gint x, gint y);

static gboolean vd_auto_scroll_idle_cb(gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (vd->drop_fd)
		{
		GqPoint pos;
		if (widget_get_pointer_position(vd->view, pos))
			{
			vd_dnd_drop_update(vd, pos.x, pos.y);
			}
		}

	vd->drop_scroll_id = 0;
	return G_SOURCE_REMOVE;
}

void vd_dnd_drop_scroll_cancel(ViewDir *vd)
{
	g_clear_handle_id(&vd->drop_scroll_id, g_source_remove);
}

static void vd_dnd_drop_update(ViewDir *vd, gint x, gint y)
{
	FileData *fd = nullptr;

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vd->view), x, y, &tpath, nullptr, nullptr, nullptr))
		{
		fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(vd->view), tpath);
		}

	if (fd == vd->drop_fd) return;

	if (vd->drop_fd != vd->click_fd)
		{
		vd_color_set(vd, vd->drop_fd, FALSE);
		}

	vd->drop_fd = fd;

	if (vd->drop_fd)
		{
		vd_color_set(vd, vd->drop_fd, TRUE);
		}

	if (vd->dnd_drop_update_func)
		{
		vd->dnd_drop_update_func(vd);
		}
}

static GdkDragAction vd_dnd_select_action(GdkDrop *drop)
{
	GdkDragAction actions = gdk_drop_get_actions(drop);

	if (actions & GDK_ACTION_COPY) return GDK_ACTION_COPY;
	if (actions & GDK_ACTION_MOVE) return GDK_ACTION_MOVE;
	if (actions & GDK_ACTION_LINK) return GDK_ACTION_LINK;

	return GDK_ACTION_NONE;
}

static GdkDragAction vd_dnd_drop_motion(GtkDropTargetAsync *, GdkDrop *drop, gdouble x, gdouble y, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd->click_fd = nullptr;

	vd_dnd_drop_update(vd, x, y);

	if (vd->drop_fd)
		{
		const auto vd_auto_scroll_notify_cb = [vd](GtkWidget *, GqPoint)
		{
			if (!vd->drop_fd || vd->drop_list) return false;

			if (!vd->drop_scroll_id)
				{
				vd->drop_scroll_id = g_idle_add(vd_auto_scroll_idle_cb, vd);
				}

			return true;
		};
		widget_auto_scroll_start(vd->view, -1, -1, vd_auto_scroll_notify_cb);
		}
	else
		{
		widget_auto_scroll_stop(vd->view);
		}

	return vd->drop_fd ? vd_dnd_select_action(drop) : GDK_ACTION_NONE;
}

static void vd_dnd_drop_leave(GtkDropTargetAsync *, GdkDrop *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (vd->drop_fd != vd->click_fd) vd_color_set(vd, vd->drop_fd, FALSE);

	vd->drop_fd = nullptr;
	vd_dnd_drop_scroll_cancel(vd);
	widget_auto_scroll_stop(vd->view);

	if (vd->dnd_drop_leave_func) vd->dnd_drop_leave_func(vd);
}

struct VdDropReadData
{
	GtkWidget *view;
	GdkDrop *drop;
};

static GList *vd_dnd_file_list_from_uri_list(const gchar *data)
{
	g_auto(GStrv) uris = g_uri_list_extract_uris(data);
	GList *path_list = nullptr;

	for (gint i = 0; uris && uris[i]; i++)
		{
		g_autofree gchar *path = g_filename_from_uri(uris[i], nullptr, nullptr);
		if (path)
			{
			path_list = g_list_prepend(path_list, g_steal_pointer(&path));
			}
		}

	path_list = g_list_reverse(path_list);
	GList *file_list = filelist_from_path_list(path_list);
	g_list_free_full(path_list, g_free);

	return file_list;
}

static GList *vd_dnd_file_list_from_stream(GInputStream *stream)
{
	GString *text = g_string_new(nullptr);
	gchar buffer[4096];
	gssize bytes_read;

	while ((bytes_read = g_input_stream_read(stream, buffer, sizeof(buffer), nullptr, nullptr)) > 0)
		{
		g_string_append_len(text, buffer, bytes_read);
		}

	GList *file_list = (bytes_read < 0) ? nullptr : vd_dnd_file_list_from_uri_list(text->str);
	g_string_free(text, TRUE);

	return file_list;
}

static void vd_dnd_drop_read_cb(GObject *source_object, GAsyncResult *result, gpointer data)
{
	g_autofree auto *drop_data = static_cast<VdDropReadData *>(data);
	GdkDrop *drop = GDK_DROP(source_object);
	GError *error = nullptr;
	const gchar *mime_type = nullptr;
	g_autoptr(GInputStream) stream = gdk_drop_read_finish(drop, result, &mime_type, &error);
	GdkDragAction action = GDK_ACTION_NONE;

	if (stream && g_strcmp0(mime_type, "text/uri-list") == 0)
		{
		GList *list = vd_dnd_file_list_from_stream(stream);
		auto *vd = static_cast<ViewDir *>(g_object_get_data(G_OBJECT(drop_data->view), "view-dir"));

		if (vd && vd->drop_fd && list)
			{
			file_data_list_free(vd->drop_list);
			vd->drop_list = list;

			GtkWidget *menu = vd_drop_menu(vd, access_file(vd->drop_fd->path, W_OK | X_OK));
			(void)menu;
			action = vd_dnd_select_action(drop);
			}
		else
			{
			file_data_list_free(list);
			}
		}

	if (error)
		{
		DEBUG_1("Directory drop read failed: %s", error->message);
		g_error_free(error);
		}

	gdk_drop_finish(drop_data->drop, action);
	g_object_unref(drop_data->drop);
	g_object_unref(drop_data->view);
}

static gboolean vd_dnd_drop(GtkDropTargetAsync *, GdkDrop *drop, gdouble x, gdouble y, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd_dnd_drop_update(vd, x, y);
	vd_dnd_drop_scroll_cancel(vd);
	widget_auto_scroll_stop(vd->view);

	if (!vd->drop_fd) return FALSE;

	static const gchar *mime_types[] = {"text/uri-list", nullptr};
	auto *drop_data = g_new0(VdDropReadData, 1);
	drop_data->view = GTK_WIDGET(g_object_ref(vd->view));
	drop_data->drop = GDK_DROP(g_object_ref(drop));

	gdk_drop_read_async(drop, mime_types, G_PRIORITY_DEFAULT, nullptr, vd_dnd_drop_read_cb, drop_data);

	return TRUE;
}

void vd_dnd_init(ViewDir *vd)
{
	static const char *mime_types[] = {"text/uri-list"};

	g_object_set_data(G_OBJECT(vd->view), "view-dir", vd);

	GdkContentFormats *formats = gdk_content_formats_new(mime_types, G_N_ELEMENTS(mime_types));
	GtkDropTargetAsync *drop_target = gtk_drop_target_async_new(formats, static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));

	g_signal_connect(drop_target, "drag-motion", G_CALLBACK(vd_dnd_drop_motion), vd);
	g_signal_connect(drop_target, "drag-leave", G_CALLBACK(vd_dnd_drop_leave), vd);
	g_signal_connect(drop_target, "drop", G_CALLBACK(vd_dnd_drop), vd);

	gtk_widget_add_controller(vd->view, GTK_EVENT_CONTROLLER(drop_target));
}

/*
 *----------------------------------------------------------------------------
 * callbacks
 *----------------------------------------------------------------------------
 */

void vd_activate_cb(GtkTreeView *tview, GtkTreePath *tpath, GtkTreeViewColumn *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	FileData *fd = vd_get_fd_from_tree_path(vd, tview, tpath);

	vd_select_row(vd, fd);
}

static GdkRGBA *vd_color_shifted()
{
	static GdkRGBA color;
	static GtkWidget *done = nullptr;

/* @FIXME GTK4 no background color */

	return &color;
}

void vd_color_cb(GtkTreeViewColumn *, GtkCellRenderer *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	gboolean set;

	gtk_tree_model_get(tree_model, iter, DIR_COLUMN_COLOR, &set, -1);
	g_object_set(cell,
	             "cell-background-rgba", vd_color_shifted(),
	             "cell-background-set", set,
	             NULL);
}

gboolean vd_release_cb(GtkWidget *widget, const GqMouseButtonEvent *event, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	FileData *fd = nullptr;

	if (layout_handle_user_defined_mouse_buttons(vd->layout, event->button))
		{
		return TRUE;
		}

	if (vd->type == DIRVIEW_LIST && !options->view_dir_list_single_click_enter)
		return FALSE;

	if (!vd->click_fd) return FALSE;
	vd_color_set(vd, vd->click_fd, FALSE);

	if (event->button != GDK_BUTTON_PRIMARY) return TRUE;

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    (event->x != 0 || event->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
					  &tpath, nullptr, nullptr, nullptr))
		{
		fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(widget), tpath);
		}

	if (fd && vd->click_fd == fd)
		{
		vd_select_row(vd, vd->click_fd);
		}

	return FALSE;
}

static void vd_gesture_release_cb(GtkGestureClick *gesture,  gint, gdouble x, gdouble y, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GqMouseButtonEvent event = {
		gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)),
		x,
		y,
		gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture)),
		1
	};
	if (vd_release_cb(widget, &event, data))
		{
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		}
}

static gboolean vd_key_pressed_cb(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	const GqKeyEvent event{keyval, keycode, state, 0};
	return vd_press_key_cb(widget, &event, data);
}

gboolean vd_press_key_cb(GtkWidget *widget, const GqKeyEvent *event, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	gboolean ret = FALSE;

	switch (vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_press_key_cb(widget, event, data); break;
	case DIRVIEW_TREE: ret = vdtree_press_key_cb(widget, event, data); break;
	}

	return ret;
}

gboolean vd_press_cb(GtkWidget *widget, const GqMouseButtonEvent *event, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (event->button == GDK_BUTTON_SECONDARY)
		{
		if (g_autoptr(GtkTreePath) tpath = nullptr;
		    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y, &tpath, nullptr, nullptr, nullptr))
			{
			GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
			GtkTreeIter iter;
			gtk_tree_model_get_iter(model, &iter, tpath);

			switch (vd->type)
				{
				case DIRVIEW_LIST:
					{
					FileData *fd = nullptr;
					gtk_tree_model_get(model, &iter, DIR_COLUMN_POINTER, &fd, -1);
					vd->click_fd = fd;
					}
					break;
				case DIRVIEW_TREE:
					{
					NodeData *nd = nullptr;
					gtk_tree_model_get(model, &iter, DIR_COLUMN_POINTER, &nd, -1);
					vd->click_fd = nd ? nd->fd : nullptr;
					}
					break;
				}

			if (vd->click_fd)
				{
				vd_color_set(vd, vd->click_fd, TRUE);
				}
			}

		vd_pop_menu(vd, vd->click_fd);

		return TRUE;
		}

	gboolean ret = FALSE;

	switch (vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_press_cb(widget, event, data); break;
	case DIRVIEW_TREE: ret = vdtree_press_cb(widget, event, data); break;
	}

	return ret;
}

static void vd_gesture_press_cb(GtkGestureClick *gesture, gint, gdouble x, gdouble y, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GqMouseButtonEvent event = {
		gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)),
		x,
		y,
		gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture)),
		1
	};
	if (vd_press_cb(widget, &event, data))
		{
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		}
}

static void vd_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->dir_fd) return;
	if (!S_ISDIR(fd->mode)) return; /* this gives correct results even on recently deleted files/directories */

	DEBUG_1("Notify vd: %s %04x", fd->path, type);

	g_autofree gchar *base = remove_level_from_path(fd->path);

	if (vd->type == DIRVIEW_LIST)
		{
		bool should_refresh = (fd == vd->dir_fd);

		if (!should_refresh)
			{
			should_refresh = (strcmp(base, vd->dir_fd->path) == 0);
			}

		if ((type & NOTIFY_CHANGE) && fd->change)
			{
			if (!should_refresh && fd->change->dest)
				{
				g_autofree gchar *dest_base = remove_level_from_path(fd->change->dest);
				should_refresh = (strcmp(dest_base, vd->dir_fd->path) == 0);
				}

			if (!should_refresh && fd->change->source)
				{
				g_autofree gchar *source_base = remove_level_from_path(fd->change->source);
				should_refresh = (strcmp(source_base, vd->dir_fd->path) == 0);
				}
			}

		if (should_refresh) vd_refresh(vd);
		}

	if (vd->type == DIRVIEW_TREE)
		{
		GtkTreeIter iter;
		auto base_fd_ref = FileData::new_dir(base);

		if (vd_find_row(vd, base_fd_ref, &iter))
			{
			vdtree_populate_path_by_iter(vd, &iter, TRUE, vd->dir_fd);
			}
		}
}

#include "view-dir-actions.inc"

ViewDir *vd_new(LayoutWindow *lw)
{
	auto vd = g_new0(ViewDir, 1);

	vd->widget = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(vd->widget), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vd->widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

	vd->layout = lw;
	vd->pf = folder_icons_new();

	switch (lw->options.dir_view_type)
		{
		case DIRVIEW_LIST: vd = vdlist_new(vd); break;
		case DIRVIEW_TREE: vd = vdtree_new(vd); break;
		}

	gq_gtk_container_add(vd->widget, vd->view);

	vd_dnd_init(vd);

	g_signal_connect(G_OBJECT(vd->view), "row_activated",
			 G_CALLBACK(vd_activate_cb), vd);
	g_signal_connect(G_OBJECT(vd->widget), "destroy",
			 G_CALLBACK(vd_destroy_cb), vd);
	GtkEventController *key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(vd_key_pressed_cb), vd);
	gtk_widget_add_controller(vd->view, key_controller);

	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);

	g_signal_connect(gesture, "pressed",  G_CALLBACK(vd_gesture_press_cb), vd);
	g_signal_connect(gesture, "released",  G_CALLBACK(vd_gesture_release_cb), vd);

	gtk_widget_add_controller(vd->view, GTK_EVENT_CONTROLLER(gesture));

	file_data_register_notify_func(vd_notify_cb, vd, NOTIFY_PRIORITY_HIGH);

	/* vd_set_fd expects that vd_notify_cb is already registered */
	if (lw->dir_fd) vd_set_fd(vd, lw->dir_fd);

	GApplication *app = g_application_get_default();
	register_actions_from_table(GTK_APPLICATION(app), lw->window, view_dir_actions, get_keyfile_merged(), vd);

	gtk_widget_show(vd->view);

	return vd;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
