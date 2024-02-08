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

#include "ui-pathsel.h"

#include <cstring>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <config.h>

#include "compat.h"
#include "debug.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "ui-tree-edit.h"
#include "uri-utils.h"
#include "utilops.h"


enum {
	DEST_WIDTH = 250,
	DEST_HEIGHT = 210
};

enum {
	RENAME_PRESS_DELAY = 333	/* 1/3 second, to allow double clicks */
};

#define PATH_SEL_USE_HEADINGS FALSE

enum {
	FILTER_COLUMN_NAME = 0,
	FILTER_COLUMN_FILTER
};

struct Dest_Data
{
	GtkWidget *d_view;
	GtkWidget *f_view;
	GtkWidget *entry;
	gchar *filter;
	gchar *path;

	GList *filter_list;
	GList *filter_text_list;
	GtkWidget *filter_combo;

	gboolean show_hidden;
	GtkWidget *hidden_button;

	GtkWidget *bookmark_list;

	GtkTreePath *right_click_path;

	void (*select_func)(const gchar *path, gpointer data);
	gpointer select_data;

	GenericDialog *gd;	/* any open confirm dialogs ? */
};

struct DestDel_Data
{
	Dest_Data *dd;
	gchar *path;
};


static void dest_view_delete_dlg_cancel(GenericDialog *gd, gpointer data);


/*
 *-----------------------------------------------------------------------------
 * (private)
 *-----------------------------------------------------------------------------
 */

static void dest_free_data(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);

	if (dd->gd)
		{
		GenericDialog *gd = dd->gd;
		dest_view_delete_dlg_cancel(dd->gd, dd->gd->data);
		generic_dialog_close(gd);
		}
	if (dd->right_click_path) gtk_tree_path_free(dd->right_click_path);

	g_free(dd->filter);
	g_free(dd->path);
	g_free(dd);
}

static gboolean dest_check_filter(const gchar *filter, const gchar *file)
{
	const gchar *f_ptr = filter;
	const gchar *strt_ptr;
	gint i;
	gint l;

	l = strlen(file);

	if (filter[0] == '*') return TRUE;
	while (f_ptr < filter + strlen(filter))
		{
		strt_ptr = f_ptr;
		i=0;
		while (*f_ptr != ';' && *f_ptr != '\0')
			{
			f_ptr++;
			i++;
			}
		if (*f_ptr != '\0' && f_ptr[1] == ' ') f_ptr++;	/* skip space immediately after separator */
		f_ptr++;
		/**
		 * @FIXME utf8 */
		if (l >= i && g_ascii_strncasecmp(file + l - i, strt_ptr, i) == 0) return TRUE;
		}
	return FALSE;
}

#ifndef CASE_SORT
#define CASE_SORT strcmp
#endif

static gint dest_sort_cb(gpointer a, gpointer b)
{
	return CASE_SORT((gchar *)a, (gchar *)b);
}

static gboolean is_hidden(const gchar *name)
{
	if (name[0] != '.') return FALSE;
	if (name[1] == '\0') return FALSE;
	if (name[1] == '.' && name[2] == '\0') return FALSE;
	return TRUE;
}

static void dest_populate(Dest_Data *dd, const gchar *path)
{
	DIR *dp;
	struct dirent *dir;
	struct stat ent_sbuf;
	GList *path_list = nullptr;
	GList *file_list = nullptr;
	GList *list;
	GtkListStore *store;
	gchar *pathl;

	if (!path) return;

	pathl = path_from_utf8(path);
	dp = opendir(pathl);
	if (!dp)
		{
		/* dir not found */
		g_free(pathl);
		return;
		}
	while ((dir = readdir(dp)) != nullptr)
		{
		if (!options->file_filter.show_dot_directory
		    && dir->d_name[0] == '.' && dir->d_name[1] == '\0')
			continue;
		if (dir->d_name[0] == '.' && dir->d_name[1] == '.' && dir->d_name[2] == '\0'
		    && pathl[0] == G_DIR_SEPARATOR && pathl[1] == '\0')
			continue; /* no .. for root directory */
		if (dd->show_hidden || !is_hidden(dir->d_name))
			{
			gchar *name = dir->d_name;
			gchar *filepath = g_build_filename(pathl, name, NULL);
			if (stat(filepath, &ent_sbuf) >= 0 && S_ISDIR(ent_sbuf.st_mode))
				{
				path_list = g_list_prepend(path_list, path_to_utf8(name));
				}
			else if (dd->f_view)
				{
				if (!dd->filter || (dd->filter && dest_check_filter(dd->filter, name)))
					file_list = g_list_prepend(file_list, path_to_utf8(name));
				}
			g_free(filepath);
			}
		}
	closedir(dp);
	g_free(pathl);

	path_list = g_list_sort(path_list, reinterpret_cast<GCompareFunc>(dest_sort_cb));
	file_list = g_list_sort(file_list, reinterpret_cast<GCompareFunc>(dest_sort_cb));

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dd->d_view)));
	gtk_list_store_clear(store);

	list = path_list;
	while (list)
		{
		GtkTreeIter iter;
		gchar *filepath;

		if (strcmp(static_cast<const gchar *>(list->data), ".") == 0)
			{
			filepath = g_strdup(path);
			}
		else if (strcmp(static_cast<const gchar *>(list->data), "..") == 0)
			{
			gchar *p;
			filepath = g_strdup(path);
			p = const_cast<gchar *>(filename_from_path(filepath));
			if (p - 1 != filepath) p--;
			p[0] = '\0';
			}
		else
			{
			filepath = g_build_filename(path, list->data, NULL);
			}

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, list->data, 1, filepath, -1);

		g_free(filepath);
		list = list->next;
		}

	g_list_free_full(path_list, g_free);


	if (dd->f_view)
		{
		store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dd->f_view)));
		gtk_list_store_clear(store);

		list = file_list;
		while (list)
			{
			GtkTreeIter iter;
			gchar *filepath;
			auto name = static_cast<const gchar *>(list->data);

			filepath = g_build_filename(path, name, NULL);

			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0, name, 1, filepath, -1);

			g_free(filepath);
			list = list->next;
			}

		g_list_free_full(file_list, g_free);
		}

	g_free(dd->path);
	dd->path = g_strdup(path);
}

static void dest_change_dir(Dest_Data *dd, const gchar *path, gboolean retain_name)
{
	const gchar *old_name = nullptr;
	gchar *full_path;
	gchar *new_directory;

	if (retain_name)
		{
		const gchar *buf = gq_gtk_entry_get_text(GTK_ENTRY(dd->entry));

		if (!isdir(buf)) old_name = filename_from_path(buf);
		}

	full_path = g_build_filename(path, old_name, NULL);
	if (old_name)
		new_directory = g_path_get_dirname(full_path);
	else
		new_directory = g_strdup(full_path);

	gq_gtk_entry_set_text(GTK_ENTRY(dd->entry), full_path);

	dest_populate(dd, new_directory);
	g_free(new_directory);

	if (old_name)
		{
		gchar *basename = g_path_get_basename(full_path);

		gtk_editable_select_region(GTK_EDITABLE(dd->entry), strlen(full_path) - strlen(basename), strlen(full_path));
		g_free(basename);
		}

	g_free(full_path);
}

/*
 *-----------------------------------------------------------------------------
 * drag and drop
 *-----------------------------------------------------------------------------
 */

enum {
	TARGET_URI_LIST,
	TARGET_TEXT_PLAIN
};

static GtkTargetEntry dest_drag_types[] = {
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"),    0, TARGET_TEXT_PLAIN }
};
enum {
	dest_drag_types_n = 2
};


static void dest_dnd_set_data(GtkWidget *view, GdkDragContext *,
				  GtkSelectionData *selection_data,
				  guint, guint, gpointer)
{
	gchar *path = nullptr;
	GList *list = nullptr;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;

	gtk_tree_model_get(model, &iter, 1, &path, -1);
	if (!path) return;

	list = g_list_append(list, path);

	gchar **uris = uris_from_pathlist(list);
	gboolean ret = gtk_selection_data_set_uris(selection_data, uris);
	if (!ret)
		{
		char *str = g_strjoinv("\r\n", uris);
		ret = gtk_selection_data_set_text(selection_data, str, -1);
		g_free(str);
		}

	g_list_free_full(list, g_free);
}

static void dest_dnd_init(Dest_Data *dd)
{
	gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(dd->d_view), GDK_BUTTON1_MASK,
					       dest_drag_types, dest_drag_types_n,
					       static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK | GDK_ACTION_ASK));
	g_signal_connect(G_OBJECT(dd->d_view), "drag_data_get",
			 G_CALLBACK(dest_dnd_set_data), dd);

	if (dd->f_view)
		{
		gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(dd->f_view), GDK_BUTTON1_MASK,
						       dest_drag_types, dest_drag_types_n,
						       static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK | GDK_ACTION_ASK));
		g_signal_connect(G_OBJECT(dd->f_view), "drag_data_get",
				 G_CALLBACK(dest_dnd_set_data), dd);
		}
}


/*
 *-----------------------------------------------------------------------------
 * destination widget file management utils
 *-----------------------------------------------------------------------------
 */

static void dest_view_store_selection(Dest_Data *dd, GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	if (dd->right_click_path) gtk_tree_path_free(dd->right_click_path);
	dd->right_click_path = nullptr;

	selection = gtk_tree_view_get_selection(view);
	if (!gtk_tree_selection_get_selected(selection, &model, &iter))
		{
		return;
		}

	dd->right_click_path = gtk_tree_model_get_path(model, &iter);
}

static gint dest_view_rename_cb(TreeEditData *ted, const gchar *old_name, const gchar *new_name, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *buf;
	gchar *old_path;
	gchar *new_path;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(ted->tree));
	gtk_tree_model_get_iter(model, &iter, dd->right_click_path);

	gtk_tree_model_get(model, &iter, 1, &old_path, -1);
	if (!old_path) return FALSE;

	buf = remove_level_from_path(old_path);
	new_path = g_build_filename(buf, new_name, NULL);
	g_free(buf);

	if (isname(new_path))
		{
		buf = g_strdup_printf(_("A file with name %s already exists."), new_name);
		warning_dialog(_("Rename failed"), buf, GQ_ICON_DIALOG_INFO, dd->entry);
		g_free(buf);
		}
	else if (!rename_file(old_path, new_path))
		{
		buf = g_strdup_printf(_("Failed to rename %s to %s."), old_name, new_name);
		warning_dialog(_("Rename failed"), buf, GQ_ICON_DIALOG_ERROR, dd->entry);
		g_free(buf);
		}
	else
		{
		const gchar *text;

		gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, new_name, 1, new_path, -1);

		text = gq_gtk_entry_get_text(GTK_ENTRY(dd->entry));
		if (text && old_path && strcmp(text, old_path) == 0)
			{
			gq_gtk_entry_set_text(GTK_ENTRY(dd->entry), new_path);
			}
		}

	g_free(old_path);
	g_free(new_path);

	return TRUE;
}

static void dest_view_rename(Dest_Data *dd, GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *text;

	if (!dd->right_click_path) return;

	model = gtk_tree_view_get_model(view);
	gtk_tree_model_get_iter(model, &iter, dd->right_click_path);
	gtk_tree_model_get(model, &iter, 0, &text, -1);

	tree_edit_by_path(view, dd->right_click_path, 0, text,
			  dest_view_rename_cb, dd);

	g_free(text);
}

static void dest_view_delete_dlg_cancel(GenericDialog *, gpointer data)
{
	auto dl = static_cast<DestDel_Data *>(data);

	dl->dd->gd = nullptr;
	g_free(dl->path);
	g_free(dl);
}

static void dest_view_delete_dlg_ok_cb(GenericDialog *gd, gpointer data)
{
	auto dl = static_cast<DestDel_Data *>(data);

	if (!unlink_file(dl->path))
		{
		gchar *text = g_strdup_printf(_("Unable to delete file:\n%s"), dl->path);
		warning_dialog(_("File deletion failed"), text, GQ_ICON_DIALOG_WARNING, dl->dd->entry);
		g_free(text);
		}
	else if (dl->dd->path)
		{
		/* refresh list */
		gchar *path = g_strdup(dl->dd->path);
		dest_populate(dl->dd, path);
		g_free(path);
		}

	dest_view_delete_dlg_cancel(gd, data);
}

static void dest_view_delete(Dest_Data *dd, GtkTreeView *view)
{
	gchar *path;
	gchar *text;
	DestDel_Data *dl;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (view != GTK_TREE_VIEW(dd->f_view)) return;
	if (!dd->right_click_path) return;

	model = gtk_tree_view_get_model(view);
	gtk_tree_model_get_iter(model, &iter, dd->right_click_path);
	gtk_tree_model_get(model, &iter, 1, &path, -1);

	if (!path) return;

	dl = g_new(DestDel_Data, 1);
	dl->dd = dd;
	dl->path = path;

	if (dd->gd)
		{
		GenericDialog *gd = dd->gd;
		dest_view_delete_dlg_cancel(dd->gd, dd->gd->data);
		generic_dialog_close(gd);
		}

	dd->gd = generic_dialog_new(_("Delete file"), "dlg_confirm",
				    dd->entry, TRUE,
				    dest_view_delete_dlg_cancel, dl);

	generic_dialog_add_button(dd->gd, GQ_ICON_DELETE, "Delete", dest_view_delete_dlg_ok_cb, TRUE);

	text = g_strdup_printf(_("About to delete the file:\n %s"), path);
	generic_dialog_add_message(dd->gd, GQ_ICON_DIALOG_QUESTION,
				   _("Delete file"), text, TRUE);
	g_free(text);

	gtk_widget_show(dd->gd->dialog);
}

static void dest_view_bookmark(Dest_Data *dd, GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *path;

	if (!dd->right_click_path) return;

	model = gtk_tree_view_get_model(view);
	gtk_tree_model_get_iter(model, &iter, dd->right_click_path);
	gtk_tree_model_get(model, &iter, 1, &path, -1);

	bookmark_list_add(dd->bookmark_list, filename_from_path(path), path);
	g_free(path);
}

static void dest_popup_dir_rename_cb(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	dest_view_rename(dd, GTK_TREE_VIEW(dd->d_view));
}

static void dest_popup_dir_bookmark_cb(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	dest_view_bookmark(dd, GTK_TREE_VIEW(dd->d_view));
}

static void dest_popup_file_rename_cb(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	dest_view_rename(dd, GTK_TREE_VIEW(dd->f_view));
}

static void dest_popup_file_delete_cb(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	dest_view_delete(dd, GTK_TREE_VIEW(dd->f_view));
}

static void dest_popup_file_bookmark_cb(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	dest_view_bookmark(dd, GTK_TREE_VIEW(dd->f_view));
}

static gboolean dest_popup_menu(Dest_Data *dd, GtkTreeView *view, guint, guint32, gboolean local)
{
	GtkWidget *menu;

	if (!dd->right_click_path) return FALSE;

	if (view == GTK_TREE_VIEW(dd->d_view))
		{
		GtkTreeModel *model;
		GtkTreeIter iter;
		gchar *text;
		gboolean normal_dir;

		model = gtk_tree_view_get_model(view);
		gtk_tree_model_get_iter(model, &iter, dd->right_click_path);
		gtk_tree_model_get(model, &iter, 0, &text, -1);

		if (!text) return FALSE;

		normal_dir = (strcmp(text, ".") == 0 || strcmp(text, "..") == 0);

		menu = popup_menu_short_lived();
		menu_item_add_sensitive(menu, _("_Rename"), !normal_dir,
			      G_CALLBACK(dest_popup_dir_rename_cb), dd);
		menu_item_add_icon(menu, _("Add _Bookmark"), GQ_ICON_GO_JUMP,
			      G_CALLBACK(dest_popup_dir_bookmark_cb), dd);
		}
	else
		{
		menu = popup_menu_short_lived();
		menu_item_add(menu, _("_Rename"),
				G_CALLBACK(dest_popup_file_rename_cb), dd);
		menu_item_add_icon(menu, _("_Delete"), GQ_ICON_DELETE,
				G_CALLBACK(dest_popup_file_delete_cb), dd);
		menu_item_add_icon(menu, _("Add _Bookmark"), GQ_ICON_GO_JUMP,
				G_CALLBACK(dest_popup_file_bookmark_cb), dd);
		}

	if (local)
		{
		g_object_set_data(G_OBJECT(menu), "active_view", view);
		gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(view), GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER, nullptr);
		}
	else
		{
		gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);

		}

	return TRUE;
}

static gboolean dest_press_cb(GtkWidget *view, GdkEventButton *event, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	GtkTreePath *tpath;
	GtkTreeViewColumn *column;
	gint cell_x;
	gint cell_y;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	if (event->button != MOUSE_BUTTON_RIGHT ||
	    !gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view), event->x, event->y,
					   &tpath, &column, &cell_x, &cell_y))
		{
		return FALSE;
		}

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
	gtk_tree_model_get_iter(model, &iter, tpath);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	gtk_tree_selection_select_iter(selection, &iter);

	if (dd->right_click_path) gtk_tree_path_free(dd->right_click_path);
	dd->right_click_path = tpath;

	return dest_popup_menu(dd, GTK_TREE_VIEW(view), 0, event->time, FALSE);
}

static gboolean dest_keypress_cb(GtkWidget *view, GdkEventKey *event, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);

	switch (event->keyval)
		{
		case GDK_KEY_F10:
			if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
			/* fall through */
		case GDK_KEY_Menu:
			dest_view_store_selection(dd, GTK_TREE_VIEW(view));
			dest_popup_menu(dd, GTK_TREE_VIEW(view), 0, event->time, TRUE);
			return TRUE;
			break;
		case 'R': case 'r':
			if (event->state & GDK_CONTROL_MASK)
				{
				dest_view_store_selection(dd, GTK_TREE_VIEW(view));
				dest_view_rename(dd, GTK_TREE_VIEW(view));
				return TRUE;
				}
			break;
		case GDK_KEY_Delete:
			dest_view_store_selection(dd, GTK_TREE_VIEW(view));
			dest_view_delete(dd, GTK_TREE_VIEW(view));
			return TRUE;
			break;
		case 'B' : case 'b':
			if (event->state & GDK_CONTROL_MASK)
				{
				dest_view_store_selection(dd, GTK_TREE_VIEW(view));
				dest_view_bookmark(dd, GTK_TREE_VIEW(view));
				return TRUE;
				}
			break;
		}

	return FALSE;
}

static void file_util_create_dir_cb(gboolean success, const gchar *new_path, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	const gchar *text;
	GtkListStore *store;
	GtkTreeIter iter;

	if (!success)
		{
		return;
		}

	if (new_path == nullptr)
		{
		return;
		}

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dd->d_view)));

	text = filename_from_path(new_path);

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, text, 1, new_path, -1);

	if (dd->right_click_path)
		{
		gtk_tree_path_free(dd->right_click_path);
		}
	dd->right_click_path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);

	gq_gtk_entry_set_text(GTK_ENTRY(dd->entry), new_path);

	gtk_widget_grab_focus(GTK_WIDGET(dd->entry));
}

static void dest_new_dir_cb(GtkWidget *widget, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);

/**
 * @FIXME on exit from the "new folder" modal dialog, focus returns to the main Geeqie
 * window rather than the file dialog window. gtk_window_present() does not seem to
 * function unless the window was previously minimized.
 */
	file_util_create_dir(gq_gtk_entry_get_text(GTK_ENTRY(dd->entry)), widget, file_util_create_dir_cb, data);
}

/*
 *-----------------------------------------------------------------------------
 * destination widget file selection, traversal, view options
 *-----------------------------------------------------------------------------
 */

static void dest_select_cb(GtkTreeSelection *selection, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	GtkTreeView *view;
	GtkTreeModel *store;
	GtkTreeIter iter;
	gchar *path;

	if (!gtk_tree_selection_get_selected(selection, nullptr, &iter)) return;

	view = gtk_tree_selection_get_tree_view(selection);
	store = gtk_tree_view_get_model(view);
	gtk_tree_model_get(store, &iter, 1, &path, -1);

	if (view == GTK_TREE_VIEW(dd->d_view))
		{
		dest_change_dir(dd, path, (dd->f_view != nullptr));
		}
	else
		{
		gq_gtk_entry_set_text(GTK_ENTRY(dd->entry), path);
		}

	g_free(path);
}

static void dest_activate_cb(GtkWidget *view, GtkTreePath *tpath, GtkTreeViewColumn *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	GtkTreeModel *store;
	GtkTreeIter iter;
	gchar *path;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
	gtk_tree_model_get_iter(store, &iter, tpath);
	gtk_tree_model_get(store, &iter, 1, &path, -1);

	if (view == dd->d_view)
		{
		dest_change_dir(dd, path, (dd->f_view != nullptr));
		}
	else
		{
		if (dd->select_func)
			{
			dd->select_func(path, dd->select_data);
			}
		}

	g_free(path);
}

static void dest_home_cb(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);

	dest_change_dir(dd, homedir(), (dd->f_view != nullptr));
}

static void dest_show_hidden_cb(GtkWidget *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	gchar *buf;

	dd->show_hidden = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dd->hidden_button));

	buf = g_strdup(dd->path);
	dest_populate(dd, buf);
	g_free(buf);
}

static void dest_entry_changed_cb(GtkEditable *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	const gchar *path;
	gchar *buf;

	path = gq_gtk_entry_get_text(GTK_ENTRY(dd->entry));
	if (dd->path && strcmp(path, dd->path) == 0) return;

	buf = remove_level_from_path(path);

	if (buf && (!dd->path || strcmp(buf, dd->path) != 0))
		{
		gchar *tmp = remove_trailing_slash(path);
		if (isdir(tmp))
			{
			dest_populate(dd, tmp);
			}
		else if (isdir(buf))
			{
			dest_populate(dd, buf);
			}
		g_free(tmp);
		}
	g_free(buf);
}

static void dest_filter_list_sync(Dest_Data *dd)
{
	GtkWidget *entry;
	GtkListStore *store;
	gchar *old_text;
	GList *fwork;
	GList *twork;

	if (!dd->filter_list || !dd->filter_combo) return;

	entry = gtk_bin_get_child(GTK_BIN(dd->filter_combo));
	old_text = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(entry)));

	store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(dd->filter_combo)));
	gtk_list_store_clear(store);

	fwork = dd->filter_list;
	twork = dd->filter_text_list;
	while (fwork && twork)
		{
		GtkTreeIter iter;
		gchar *name;
		gchar *filter;

		name = static_cast<gchar *>(twork->data);
		filter = static_cast<gchar *>(fwork->data);

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, FILTER_COLUMN_NAME, name,
						 FILTER_COLUMN_FILTER, filter, -1);

		if (strcmp(old_text, filter) == 0)
			{
			gtk_combo_box_set_active_iter(GTK_COMBO_BOX(dd->filter_combo), &iter);
			}

		fwork = fwork->next;
		twork = twork->next;
		}

	g_free(old_text);
}

static void dest_filter_add(Dest_Data *dd, const gchar *filter, const gchar *description, gboolean set)
{
	GList *work;
	gchar *buf;
	gint c = 0;

	if (!filter) return;

	work = dd->filter_list;
	while (work)
		{
		auto f = static_cast<gchar *>(work->data);

		if (strcmp(f, filter) == 0)
			{
			if (set) gtk_combo_box_set_active(GTK_COMBO_BOX(dd->filter_combo), c);
			return;
			}
		work = work->next;
		c++;
		}

	dd->filter_list = uig_list_insert_link(dd->filter_list, g_list_last(dd->filter_list), g_strdup(filter));

	if (description)
		{
		buf = g_strdup_printf("%s  ( %s )", description, filter);
		}
	else
		{
		buf = g_strdup_printf("( %s )", filter);
		}
	dd->filter_text_list = uig_list_insert_link(dd->filter_text_list, g_list_last(dd->filter_text_list), buf);

	if (set) gq_gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(dd->filter_combo))), filter);
	dest_filter_list_sync(dd);
}

static void dest_filter_clear(Dest_Data *dd)
{
	g_list_free_full(dd->filter_list, g_free);
	dd->filter_list = nullptr;

	g_list_free_full(dd->filter_text_list, g_free);
	dd->filter_text_list = nullptr;

	dest_filter_add(dd, "*", _("All Files"), TRUE);
}

static void dest_filter_changed_cb(GtkEditable *, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);
	GtkWidget *entry;
	const gchar *buf;
	gchar *path;

	entry = gtk_bin_get_child(GTK_BIN(dd->filter_combo));
	buf = gq_gtk_entry_get_text(GTK_ENTRY(entry));

	g_free(dd->filter);
	dd->filter = nullptr;
	if (strlen(buf) > 0) dd->filter = g_strdup(buf);

	path = g_strdup(dd->path);
	dest_populate(dd, path);
	g_free(path);
}

static void dest_bookmark_select_cb(const gchar *path, gpointer data)
{
	auto dd = static_cast<Dest_Data *>(data);

	if (isdir(path))
		{
		dest_change_dir(dd, path, (dd->f_view != nullptr));
		}
	else if (isfile(path) && dd->f_view)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(dd->entry), path);
		}
}

/*
 *-----------------------------------------------------------------------------
 * destination widget setup routines (public)
 *-----------------------------------------------------------------------------
 */

GtkWidget *path_selection_new_with_files(GtkWidget *entry, const gchar *path,
					 const gchar *filter, const gchar *filter_desc)
{
	Dest_Data *dd;
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkWidget *hbox1; // home, new folder, hidden, filter
	GtkWidget *hbox2; // files paned
	GtkWidget *hbox3; // filter
	GtkWidget *paned;
	GtkWidget *scrolled;
	GtkWidget *table; // main box

	dd = g_new0(Dest_Data, 1);

	table = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	dd->entry = entry;
	g_object_set_data(G_OBJECT(dd->entry), "destination_data", dd);

	hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gtk_box_set_spacing(GTK_BOX(hbox1), PREF_PAD_BUTTON_GAP);
	pref_button_new(hbox1, nullptr, _("Home"),
			G_CALLBACK(dest_home_cb), dd);
	pref_button_new(hbox1, nullptr, _("New folder"),
			G_CALLBACK(dest_new_dir_cb), dd);

	dd->hidden_button = gtk_check_button_new_with_label(_("Show hidden"));
	g_signal_connect(G_OBJECT(dd->hidden_button), "clicked",
			 G_CALLBACK(dest_show_hidden_cb), dd);
	gq_gtk_box_pack_end(GTK_BOX(hbox1), dd->hidden_button, FALSE, FALSE, 0);
	gtk_widget_show(dd->hidden_button);

	gq_gtk_box_pack_start(GTK_BOX(table), hbox1, FALSE, FALSE, 0);
	gtk_widget_show_all(hbox1);

	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	if (filter)
		{
		paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
		DEBUG_NAME(paned);
		gq_gtk_box_pack_end(GTK_BOX(table), paned, TRUE , TRUE, 0);
		gtk_widget_show(paned);
		gtk_paned_add1(GTK_PANED(paned), hbox2);
		}
	else
		{
		paned = nullptr;
		gq_gtk_box_pack_end(GTK_BOX(table), hbox2, TRUE, TRUE, 0);
		}
	gtk_widget_show(hbox2);

	/* bookmarks */
	scrolled = bookmark_list_new(nullptr, dest_bookmark_select_cb, dd);
	gq_gtk_box_pack_start(GTK_BOX(hbox2), scrolled, FALSE, FALSE, 0);
	gtk_widget_show(scrolled);

	dd->bookmark_list = scrolled;

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gq_gtk_box_pack_start(GTK_BOX(hbox2), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	dd->d_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dd->d_view), PATH_SEL_USE_HEADINGS);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dd->d_view));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_SINGLE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Folders"));
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", 0);

	gtk_tree_view_append_column(GTK_TREE_VIEW(dd->d_view), column);

#if 0
	/* only for debugging */
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Path"));
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", 1);
	gtk_tree_view_append_column(GTK_TREE_VIEW(dd->d_view), column);
#endif

	gtk_widget_set_size_request(dd->d_view, DEST_WIDTH, DEST_HEIGHT);
	gq_gtk_container_add(GTK_WIDGET(scrolled), dd->d_view);
	gtk_widget_show(dd->d_view);

	g_signal_connect(G_OBJECT(dd->d_view), "button_press_event",
			 G_CALLBACK(dest_press_cb), dd);
	g_signal_connect(G_OBJECT(dd->d_view), "key_press_event",
			 G_CALLBACK(dest_keypress_cb), dd);
	g_signal_connect(G_OBJECT(dd->d_view), "row_activated",
			 G_CALLBACK(dest_activate_cb), dd);
	g_signal_connect(G_OBJECT(dd->d_view), "destroy",
			 G_CALLBACK(dest_free_data), dd);

	if (filter)
		{
		GtkListStore *store;

		hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		pref_label_new(hbox3, _("Filter:"));

		store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

		dd->filter_combo = gtk_combo_box_new_with_model_and_entry(GTK_TREE_MODEL(store));
		gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(dd->filter_combo),
														FILTER_COLUMN_FILTER);
		gtk_widget_set_tooltip_text(dd->filter_combo, _("File extension.\nAll files: *\nOr, e.g. png;jpg\nOr, e.g. png; jpg"));

		g_object_unref(store);
		gtk_cell_layout_clear(GTK_CELL_LAYOUT(dd->filter_combo));
		renderer = gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dd->filter_combo), renderer, TRUE);
		gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dd->filter_combo), renderer, "text", FILTER_COLUMN_NAME, NULL);
		gq_gtk_box_pack_start(GTK_BOX(hbox3), dd->filter_combo, TRUE, TRUE, 0);
		gtk_widget_show(dd->filter_combo);

		gq_gtk_box_pack_end(GTK_BOX(hbox1), hbox3, FALSE, FALSE, 0);
		gtk_widget_show(hbox3);

		scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
		gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
					       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
		if (paned)
			{
			gtk_paned_add2(GTK_PANED(paned), scrolled);
			}
		else
			{
			gq_gtk_box_pack_end(GTK_BOX(table), paned, FALSE, FALSE, 0);
			}
		gtk_widget_show(scrolled);

		store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
		dd->f_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
		g_object_unref(store);

		gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dd->f_view), PATH_SEL_USE_HEADINGS);

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dd->f_view));
		gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_SINGLE);

		column = gtk_tree_view_column_new();
		gtk_tree_view_column_set_title(column, _("Files"));
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

		renderer = gtk_cell_renderer_text_new();
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "text", 0);

		gtk_tree_view_append_column(GTK_TREE_VIEW(dd->f_view), column);

		gtk_widget_set_size_request(dd->f_view, DEST_WIDTH, DEST_HEIGHT);
		gq_gtk_container_add(GTK_WIDGET(scrolled), dd->f_view);
		gtk_widget_show(dd->f_view);

		g_signal_connect(G_OBJECT(dd->f_view), "button_press_event",
				 G_CALLBACK(dest_press_cb), dd);
		g_signal_connect(G_OBJECT(dd->f_view), "key_press_event",
				 G_CALLBACK(dest_keypress_cb), dd);
		g_signal_connect(G_OBJECT(dd->f_view), "row_activated",
				 G_CALLBACK(dest_activate_cb), dd);
		g_signal_connect(selection, "changed",
				 G_CALLBACK(dest_select_cb), dd);

		dest_filter_clear(dd);
		dest_filter_add(dd, filter, filter_desc, TRUE);

		dd->filter = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(dd->filter_combo)))));
		}

	if (path && path[0] == G_DIR_SEPARATOR && isdir(path))
		{
		dest_populate(dd, path);
		}
	else
		{
		gchar *buf = remove_level_from_path(path);
		if (buf && buf[0] == G_DIR_SEPARATOR && isdir(buf))
			{
			dest_populate(dd, buf);
			}
		else
			{
			gint pos = -1;

			dest_populate(dd, const_cast<gchar *>(homedir()));
			if (path) gtk_editable_insert_text(GTK_EDITABLE(dd->entry), G_DIR_SEPARATOR_S, -1, &pos);
			if (path) gtk_editable_insert_text(GTK_EDITABLE(dd->entry), path, -1, &pos);
			}
		g_free(buf);
		}

	if (dd->filter_combo)
		{
		g_signal_connect(G_OBJECT(gtk_bin_get_child(GTK_BIN(dd->filter_combo))), "changed",
				 G_CALLBACK(dest_filter_changed_cb), dd);
		}
	g_signal_connect(G_OBJECT(dd->entry), "changed",
			 G_CALLBACK(dest_entry_changed_cb), dd);

	dest_dnd_init(dd);

	return table;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
GtkWidget *path_selection_new_unused(const gchar *path, GtkWidget *entry)
{
	return path_selection_new_with_files(entry, path, nullptr, nullptr);
}

void path_selection_sync_to_entry_unused(GtkWidget *entry)
{
	auto *dd = static_cast<Dest_Data *>(g_object_get_data(G_OBJECT(entry), "destination_data"));
	const gchar *path;

	if (!dd) return;

	path = gq_gtk_entry_get_text(GTK_ENTRY(entry));

	if (isdir(path) && (!dd->path || strcmp(path, dd->path) != 0))
		{
		dest_populate(dd, path);
		}
	else
		{
		gchar *buf = remove_level_from_path(path);
		if (isdir(buf) && (!dd->path || strcmp(buf, dd->path) != 0))
			{
			dest_populate(dd, buf);
			}
		g_free(buf);
		}
}
#pragma GCC diagnostic pop

void path_selection_add_select_func(GtkWidget *entry,
				    void (*func)(const gchar *, gpointer), gpointer data)
{
	auto dd = static_cast<Dest_Data *>(g_object_get_data(G_OBJECT(entry), "destination_data"));

	if (!dd) return;

	dd->select_func = func;
	dd->select_data = data;
}

void path_selection_add_filter(GtkWidget *entry, const gchar *filter, const gchar *description, gboolean set)
{
	auto dd = static_cast<Dest_Data *>(g_object_get_data(G_OBJECT(entry), "destination_data"));

	if (!dd) return;
	if (!filter) return;

	dest_filter_add(dd, filter, description, set);
}

void path_selection_clear_filter(GtkWidget *entry)
{
	auto dd = static_cast<Dest_Data *>(g_object_get_data(G_OBJECT(entry), "destination_data"));

	if (!dd) return;

	dest_filter_clear(dd);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
