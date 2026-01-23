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

constexpr std::array<GtkTargetEntry, 1> vd_dnd_drop_types{{
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST }
}};

GdkPixbuf *create_folder_icon_with_emblem(GtkIconTheme *icon_theme, const gchar *emblem, const gchar *fallback_icon, gint size)
{
	GdkPixbuf *pixbuf = nullptr;
	GtkIconInfo *info;

	GIcon *icon_folder = g_themed_icon_new(GQ_ICON_DIRECTORY);
	GIcon *icon_emblem = g_themed_icon_new(emblem);
	GEmblem *emblem_new = g_emblem_new(icon_emblem);
	GIcon *emblemed_icon = g_emblemed_icon_new(icon_folder, emblem_new);

	info = gtk_icon_theme_lookup_by_gicon(icon_theme, emblemed_icon, size, GTK_ICON_LOOKUP_USE_BUILTIN);

	if (info)
		{
		pixbuf = gtk_icon_info_load_icon(info, nullptr);
		}

	if (pixbuf == nullptr)
		{
		pixbuf = gq_gtk_icon_theme_load_icon_copy(icon_theme, fallback_icon, size, GTK_ICON_LOOKUP_USE_BUILTIN);
		}

	g_object_unref(emblem_new);
	g_object_unref(emblemed_icon);
	g_object_unref(icon_emblem);
	g_object_unref(icon_folder);
	g_object_unref(info);

	return pixbuf;
}

/* Folders icons to be used in tree or list directory view */
PixmapFolders *folder_icons_new()
{
	auto pf = g_new0(PixmapFolders, 1);
	GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

	gint size;
	if (!gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &size, &size))
		{
		size = 16;
		}

	pf->close  = gq_gtk_icon_theme_load_icon_copy(icon_theme, GQ_ICON_DIRECTORY, size, GTK_ICON_LOOKUP_USE_BUILTIN);
	pf->open   = gq_gtk_icon_theme_load_icon_copy(icon_theme, GQ_ICON_OPEN, size, GTK_ICON_LOOKUP_USE_BUILTIN);
	pf->parent = gq_gtk_icon_theme_load_icon_copy(icon_theme, GQ_ICON_GO_UP, size, GTK_ICON_LOOKUP_USE_BUILTIN);

	pf->deny = create_folder_icon_with_emblem(icon_theme, GQ_ICON_UNREADABLE, GQ_ICON_STOP, size);
	pf->link = create_folder_icon_with_emblem(icon_theme, GQ_ICON_LINK, GQ_ICON_REDO, size);
	pf->read_only = create_folder_icon_with_emblem(icon_theme, GQ_ICON_READONLY, GQ_ICON_DIRECTORY, size);

	return pf;
}

void folder_icons_free(PixmapFolders *pf)
{
	if (!pf) return;

	if (pf->close)
		{
		g_object_unref(pf->close);
		}

	if (pf->open)
		{
		g_object_unref(pf->open);
		}

	if (pf->deny)
		{
		g_object_unref(pf->deny);
		}

	if (pf->parent)
		{
		g_object_unref(pf->parent);
		}

	if (pf->link)
		{
		g_object_unref(pf->link);
		}

	if (pf->read_only)
		{
		g_object_unref(pf->read_only);
		}

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

	file_data_unref(vd->dir_fd);
	g_free(vd->info);

	g_free(vd);
}

ViewDir *vd_new(LayoutWindow *lw)
{
	auto vd = g_new0(ViewDir, 1);

	vd->widget = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(vd->widget), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vd->widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

	vd->layout = lw;
	vd->pf = folder_icons_new();

	switch (lw->options.dir_view_type)
		{
		case DIRVIEW_LIST: vd = vdlist_new(vd, lw->dir_fd); break;
		case DIRVIEW_TREE: vd = vdtree_new(vd, lw->dir_fd); break;
		}

	gq_gtk_container_add(vd->widget, vd->view);

	vd_dnd_init(vd);

	g_signal_connect(G_OBJECT(vd->view), "row_activated",
			 G_CALLBACK(vd_activate_cb), vd);
	g_signal_connect(G_OBJECT(vd->widget), "destroy",
			 G_CALLBACK(vd_destroy_cb), vd);
	g_signal_connect(G_OBJECT(vd->view), "key_press_event",
			 G_CALLBACK(vd_press_key_cb), vd);
	g_signal_connect(G_OBJECT(vd->view), "button_press_event",
			 G_CALLBACK(vd_press_cb), vd);
	g_signal_connect(G_OBJECT(vd->view), "button_release_event",
			 G_CALLBACK(vd_release_cb), vd);

	file_data_register_notify_func(vd_notify_cb, vd, NOTIFY_PRIORITY_HIGH);

	/* vd_set_fd expects that vd_notify_cb is already registered */
	if (lw->dir_fd) vd_set_fd(vd, lw->dir_fd);

	gtk_widget_show(vd->view);

	return vd;
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

		FileData *fd = file_data_new_dir(new_path);

		GtkTreeIter iter;
		if (vd_find_row(vd, fd, &iter))
			{
			tree_view_row_make_visible(GTK_TREE_VIEW(vd->view), &iter, TRUE);
			}

		file_data_unref(fd);
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

static void vd_pop_menu_up_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->dir_fd || strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) == 0) return;

	if (vd->select_func)
		{
		g_autofree gchar *path = remove_level_from_path(vd->dir_fd->path);
		FileData *fd = file_data_new_dir(path);
		vd->select_func(vd, fd, vd->select_data);
		file_data_unref(fd);
		}
}

static void vd_pop_menu_slide_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->layout) return;
	if (!vd->click_fd) return;

	layout_set_fd(vd->layout, vd->click_fd);
	layout_select_none(vd->layout);
	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start(vd->layout);
}

static void vd_pop_menu_slide_rec_cb(GtkWidget *, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd->layout || !vd->click_fd) return;

	GList *list = filelist_recursive_full(vd->click_fd, vd->layout->options.file_view_list_sort);

	layout_image_slideshow_stop(vd->layout);
	layout_image_slideshow_start_from_list(vd->layout, list);
}

template<gboolean recursive>
static void vd_pop_menu_dupe_cb(GtkWidget *, gpointer data)
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

static void vd_pop_menu_delete_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->click_fd) return;
	file_util_delete_dir(vd->click_fd, vd->widget);
}

template<gboolean quoted>
static void vd_pop_menu_copy_path_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->click_fd) return;

	file_util_copy_path_to_clipboard(vd->click_fd, quoted, ClipboardAction::COPY);
}

static void vd_pop_menu_cut_path_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (!vd->click_fd) return;

	file_util_copy_path_to_clipboard(vd->click_fd, FALSE, ClipboardAction::CUT);
}

template<DirViewType dir_view_type>
static void vd_pop_submenu_dir_view_as_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	layout_views_set(vd->layout, dir_view_type, vd->layout->options.file_view_type);
}

static void vd_pop_menu_refresh_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (vd->layout) layout_refresh(vd->layout);
}

static void vd_toggle_show_hidden_files_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	options->file_filter.show_hidden_files = !options->file_filter.show_hidden_files;
	if (vd->layout) layout_refresh(vd->layout);
}

static void vd_pop_menu_new_cb(GtkWidget *, gpointer data)
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

static void vd_pop_menu_rename_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd_rename_by_data(vd, vd->click_fd);
}

static void vd_pop_menu_sort_ascend_cb(GtkWidget *widget, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd || !vd->layout) return;

	auto sort = vd->layout->options.dir_view_list_sort;
	sort.ascending = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

	layout_views_set_sort_dir(vd->layout, sort);
	layout_refresh(vd->layout);
}

static void vd_pop_menu_sort_case_cb(GtkWidget *widget, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd || !vd->layout) return;

	auto sort = vd->layout->options.dir_view_list_sort;
	sort.case_sensitive = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

	layout_views_set_sort_dir(vd->layout, sort);
	layout_refresh(vd->layout);
}

static void vd_pop_menu_sort_cb(GtkWidget *widget, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	if (!vd || !vd->layout) return;

	auto sort = vd->layout->options.dir_view_list_sort;
	sort.method = static_cast<SortType>(GPOINTER_TO_INT(menu_item_radio_get_data(widget)));

	layout_views_set_sort_dir(vd->layout, sort);
	layout_refresh(vd->layout);
}

GtkWidget *vd_pop_menu(ViewDir *vd, FileData *fd)
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

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vd_popup_destroy_cb), vd);

	menu_item_add_icon_sensitive(menu, _("_Up to parent"), GQ_ICON_GO_UP,
				      (vd->dir_fd && strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) != 0),
				      G_CALLBACK(vd_pop_menu_up_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("_Slideshow"), active,
				G_CALLBACK(vd_pop_menu_slide_cb), vd);
	menu_item_add_sensitive(menu, _("Slideshow recursive"), active,
				G_CALLBACK(vd_pop_menu_slide_rec_cb), vd);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("Find _duplicates..."), GQ_ICON_FIND, active,
	                             G_CALLBACK(vd_pop_menu_dupe_cb<FALSE>), vd);
	menu_item_add_icon_sensitive(menu, _("Find duplicates recursive..."), GQ_ICON_FIND, active,
	                             G_CALLBACK(vd_pop_menu_dupe_cb<TRUE>), vd);

	menu_item_add_divider(menu);

	menu_item_add_sensitive(menu, _("_New folder..."), new_folder_active,
				G_CALLBACK(vd_pop_menu_new_cb), vd);

	menu_item_add_sensitive(menu, _("_Rename..."), rename_delete_active,
				G_CALLBACK(vd_pop_menu_rename_cb), vd);

	menu_item_add(menu, _("_Copy to clipboard"),
	              G_CALLBACK(vd_pop_menu_copy_path_cb<TRUE>), vd);

	menu_item_add(menu, _("_Copy to clipboard (unquoted)"),
	              G_CALLBACK(vd_pop_menu_copy_path_cb<FALSE>), vd);

	menu_item_add(menu, _("_Cut to clipboard"),
		      G_CALLBACK(vd_pop_menu_cut_path_cb), vd);

	menu_item_add_icon_sensitive(menu, _("_Delete..."), GQ_ICON_DELETE, rename_delete_active,
				      G_CALLBACK(vd_pop_menu_delete_cb), vd);
	menu_item_add_divider(menu);


	menu_item_add_radio(menu, _("View as _List"), nullptr, vd->type == DIRVIEW_LIST,
	                    G_CALLBACK(vd_pop_submenu_dir_view_as_cb<DIRVIEW_LIST>), vd);
	menu_item_add_radio(menu, _("View as _Tree"), nullptr, vd->type == DIRVIEW_TREE,
	                    G_CALLBACK(vd_pop_submenu_dir_view_as_cb<DIRVIEW_TREE>), vd);

	GtkWidget *submenu = gtk_menu_new();
	for (const SortType sort_type : { SORT_NAME, SORT_NUMBER, SORT_TIME })
		{
		menu_item_add_radio(submenu, sort_type_get_text(sort_type), GINT_TO_POINTER(sort_type),
		                    vd->layout->options.dir_view_list_sort.method == sort_type,
		                    G_CALLBACK(vd_pop_menu_sort_cb), vd);
		}
	if (vd->type == DIRVIEW_LIST)
		{
		menu_item_add_check(submenu, _("Ascending"), vd->layout->options.dir_view_list_sort.ascending, G_CALLBACK(vd_pop_menu_sort_ascend_cb), (vd));
		menu_item_add_check(submenu, _("Case"), vd->layout->options.dir_view_list_sort.case_sensitive, G_CALLBACK(vd_pop_menu_sort_case_cb), (vd));
		}
	GtkWidget *item = menu_item_add(menu, _("_Sort"), nullptr, nullptr);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);

	menu_item_add_divider(menu);

	menu_item_add_check(menu, _("Show _hidden files"), options->file_filter.show_hidden_files,
			    G_CALLBACK(vd_toggle_show_hidden_files_cb), vd);

	menu_item_add_icon(menu, _("Re_fresh"), GQ_ICON_REFRESH,
			    G_CALLBACK(vd_pop_menu_refresh_cb), vd);

	return menu;
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
				FileData *new_fd = file_data_new_dir(new_path);
				fd = vdtree_populate_path(vd, new_fd, TRUE, TRUE);
				file_data_unref(new_fd);
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

static void vd_dest_set(ViewDir *vd, gint enable)
{
	if (enable)
		{
		gq_gtk_drag_dest_set(vd->view,
		                  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP),
		                  vd_dnd_drop_types.data(), vd_dnd_drop_types.size(),
		                  static_cast<GdkDragAction>(GDK_ACTION_MOVE | GDK_ACTION_COPY));
		}
	else
		{
		gq_gtk_drag_dest_unset(vd->view);
		}
}

#if !HAVE_GTK4
static void vd_dnd_get(GtkWidget *, GdkDragContext *,
			   GtkSelectionData *selection_data, guint info,
			   guint, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	GList *list;

	if (!vd->click_fd) return;

	switch (info)
		{
		case TARGET_URI_LIST:
		case TARGET_TEXT_PLAIN:
			list = g_list_prepend(nullptr, vd->click_fd);
			uri_selection_data_set_uris_from_filelist(selection_data, list);
			g_list_free(list);
			break;
		default:
			break;
		}
}

static void vd_dnd_begin(GtkWidget *, GdkDragContext *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd_color_set(vd, vd->click_fd, TRUE);
	vd_dest_set(vd, FALSE);
}

static void vd_dnd_end(GtkWidget *, GdkDragContext *context, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd_color_set(vd, vd->click_fd, FALSE);

	if (vd->type == DIRVIEW_LIST && gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE)
		{
		vd_refresh(vd);
		}
	vd_dest_set(vd, TRUE);
}

static void vd_dnd_drop_receive(GtkWidget *widget, GdkDragContext *context,
				gint x, gint y,
				GtkSelectionData *selection_data, guint info,
				guint, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	FileData *fd = nullptr;

	vd->click_fd = nullptr;

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x, y,
	                                  &tpath, nullptr, nullptr, nullptr))
		{
		fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(widget), tpath);
		}

	if (!fd) return;

	if (info == TARGET_URI_LIST)
		{
		GList *list;
		gint active;
		gboolean done = FALSE;

		list = uri_filelist_from_gtk_selection_data(selection_data);
		if (!list) return;

		active = access_file(fd->path, W_OK | X_OK);

		vd_color_set(vd, fd, TRUE);

		if (active)
			{
			/** @FIXME With GTK2 gdk_drag_context_get_actions() shows the state of the
			 * shift and control keys during the drag operation. With GTK3 this is not
			 * so. This is a workaround.
			 */
			GdkModifierType mask;
			DnDAction action = options->dnd_default_action;

			get_pointer_position(widget, gdk_drag_context_get_device(context), nullptr, nullptr, &mask);
			if (mask & GDK_CONTROL_MASK)
				{
				action = DND_ACTION_COPY;
				}
			else if (mask & GDK_SHIFT_MASK)
				{
				action = DND_ACTION_MOVE;
				}

			if (action == DND_ACTION_COPY)
				{
				file_util_copy_simple(list, fd->path, vd->widget);
				done = TRUE;
				list = nullptr;
				}
			else if (action == DND_ACTION_MOVE)
				{
				file_util_move_simple(list, fd->path, vd->widget);
				done = TRUE;
				list = nullptr;
				}
			}

		if (done == FALSE)
			{
			vd->popup = vd_drop_menu(vd, active);
			gtk_menu_popup_at_pointer(GTK_MENU(vd->popup), nullptr);
			}

		vd->drop_fd = fd;
		vd->drop_list = list;
		}
}

static void vd_dnd_drop_update(ViewDir *vd, gint x, gint y)
{
	FileData *fd = nullptr;

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(vd->view), x, y,
	                                  &tpath, nullptr, nullptr, nullptr))
		{
		fd = vd_get_fd_from_tree_path(vd, GTK_TREE_VIEW(vd->view), tpath);
		}

	if (fd != vd->drop_fd)
		{
		vd_color_set(vd, vd->drop_fd, FALSE);
		vd_color_set(vd, fd, TRUE);
		if (fd && vd->dnd_drop_update_func) vd->dnd_drop_update_func(vd);
		}

	vd->drop_fd = fd;
}

void vd_dnd_drop_scroll_cancel(ViewDir *vd)
{
	g_clear_handle_id(&vd->drop_scroll_id, g_source_remove);
}
#endif

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

static gboolean vd_dnd_drop_motion(GtkWidget *, GdkDragContext *context, gint x, gint y, guint time, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd->click_fd = nullptr;

	if (gtk_drag_get_source_widget(context) == vd->view)
		{
		/* from same window */
		gdk_drag_status(context, GDK_ACTION_DEFAULT, time);
		return TRUE;
		}

	gdk_drag_status(context, gdk_drag_context_get_suggested_action(context), time);

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

	return FALSE;
}

static void vd_dnd_drop_leave(GtkWidget *, GdkDragContext *, guint, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (vd->drop_fd != vd->click_fd) vd_color_set(vd, vd->drop_fd, FALSE);

	vd->drop_fd = nullptr;

	if (vd->dnd_drop_leave_func) vd->dnd_drop_leave_func(vd);
}

void vd_dnd_init(ViewDir *vd)
{
	gq_gtk_drag_source_set(vd->view, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
	                    dnd_file_drag_types.data(), dnd_file_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK));
	gq_drag_g_signal_connect(G_OBJECT(vd->view), "drag_data_get",
			 G_CALLBACK(vd_dnd_get), vd);
	gq_drag_g_signal_connect(G_OBJECT(vd->view), "drag_begin",
			 G_CALLBACK(vd_dnd_begin), vd);
	gq_drag_g_signal_connect(G_OBJECT(vd->view), "drag_end",
			 G_CALLBACK(vd_dnd_end), vd);

	vd_dest_set(vd, TRUE);
	gq_drag_g_signal_connect(G_OBJECT(vd->view), "drag_data_received",
			 G_CALLBACK(vd_dnd_drop_receive), vd);
	gq_drag_g_signal_connect(G_OBJECT(vd->view), "drag_motion",
			 G_CALLBACK(vd_dnd_drop_motion), vd);
	gq_drag_g_signal_connect(G_OBJECT(vd->view), "drag_leave",
			 G_CALLBACK(vd_dnd_drop_leave), vd);
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

static GdkRGBA *vd_color_shifted(GtkWidget *widget)
{
	static GdkRGBA color;
	static GtkWidget *done = nullptr;

#if HAVE_GTK4
/* @FIXME GTK4 no background color */
#else
	if (done != widget)
		{
		GtkStyleContext *style_context;

		style_context = gtk_widget_get_style_context(widget);
		deprecated_gtk_style_context_get_background_color(style_context, GTK_STATE_FLAG_NORMAL, &color);

		shift_color(color);
		done = widget;
		}
#endif

	return &color;
}

void vd_color_cb(GtkTreeViewColumn *, GtkCellRenderer *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	gboolean set;

	gtk_tree_model_get(tree_model, iter, DIR_COLUMN_COLOR, &set, -1);
	g_object_set(G_OBJECT(cell),
		     "cell-background-rgba", vd_color_shifted(vd->view),
		     "cell-background-set", set, NULL);
}

gboolean vd_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	FileData *fd = nullptr;

	if (layout_handle_user_defined_mouse_buttons(vd->layout, bevent))
		{
		return TRUE;
		}

	if (vd->type == DIRVIEW_LIST && !options->view_dir_list_single_click_enter)
		return FALSE;

	if (!vd->click_fd) return FALSE;
	vd_color_set(vd, vd->click_fd, FALSE);

	if (bevent->button != GDK_BUTTON_PRIMARY) return TRUE;

	if (g_autoptr(GtkTreePath) tpath = nullptr;
	    (bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
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

gboolean vd_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
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

gboolean vd_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	gboolean ret = FALSE;
	FileData *fd;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	NodeData *nd = nullptr;
	GtkTreeModel *store;

	if (bevent->button == GDK_BUTTON_SECONDARY)
		{
		if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y, &tpath, nullptr, nullptr, nullptr))
			{
			store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
			gtk_tree_model_get_iter(store, &iter, tpath);

			switch (vd->type)
				{
				case DIRVIEW_LIST:
					gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &fd, -1);
					vd->click_fd = fd;
					break;
				case DIRVIEW_TREE:
					gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &nd, -1);
					vd->click_fd = (nd) ? nd->fd : nullptr;
				}

			if (vd->click_fd)
				{
				vd_color_set(vd, vd->click_fd, TRUE);
				}
			}

		vd->popup = vd_pop_menu(vd, vd->click_fd);
		gtk_menu_popup_at_pointer(GTK_MENU(vd->popup), nullptr);

		return TRUE;
		}

	switch (vd->type)
	{
	case DIRVIEW_LIST: ret = vdlist_press_cb(widget, bevent, data); break;
	case DIRVIEW_TREE: ret = vdtree_press_cb(widget, bevent, data); break;
	}

	return ret;
}

static void vd_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	gboolean refresh;

	if (!S_ISDIR(fd->mode)) return; /* this gives correct results even on recently deleted files/directories */

	DEBUG_1("Notify vd: %s %04x", fd->path, type);

	g_autofree gchar *base = remove_level_from_path(fd->path);

	if (vd->type == DIRVIEW_LIST)
		{
		refresh = (fd == vd->dir_fd);

		if (!refresh)
			{
			refresh = (strcmp(base, vd->dir_fd->path) == 0);
			}

		if ((type & NOTIFY_CHANGE) && fd->change)
			{
			if (!refresh && fd->change->dest)
				{
				g_autofree gchar *dest_base = remove_level_from_path(fd->change->dest);
				refresh = (strcmp(dest_base, vd->dir_fd->path) == 0);
				}

			if (!refresh && fd->change->source)
				{
				g_autofree gchar *source_base = remove_level_from_path(fd->change->source);
				refresh = (strcmp(source_base, vd->dir_fd->path) == 0);
				}
			}

		if (refresh) vd_refresh(vd);
		}

	if (vd->type == DIRVIEW_TREE)
		{
		GtkTreeIter iter;
		FileData *base_fd = file_data_new_dir(base);

		if (vd_find_row(vd, base_fd, &iter))
			{
			vdtree_populate_path_by_iter(vd, &iter, TRUE, vd->dir_fd);
			}

		file_data_unref(base_fd);
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
