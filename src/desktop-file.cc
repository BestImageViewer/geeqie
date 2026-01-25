/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#include "desktop-file.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>

#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "compat.h"
#include "editors.h"
#include "intl.h"
#include "layout-util.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "window.h"

namespace
{

struct EditorWindow
{
	GtkWidget *window;
	GtkWidget *entry;
	GtkWidget *save_button;
	GtkTextBuffer *buffer;
	gchar *desktop_name;
	gboolean modified;
};

struct EditorListWindow
{
	GtkWidget *window;
	GtkWidget *view;
	GenericDialog *gd;	/* any open confirm dialogs ? */
	GtkWidget *delete_button;
	GtkWidget *edit_button;
};

struct EditorWindowDelData
{
	EditorListWindow *ewl;
	gchar *path;
};

constexpr gint CONFIG_WINDOW_DEF_WIDTH = 700;
constexpr gint CONFIG_WINDOW_DEF_HEIGHT = 400;

EditorListWindow *editor_list_window = nullptr;

gboolean editor_window_save(EditorWindow *ew)
{
	GtkTextIter start;
	GtkTextIter end;
	gboolean ret = TRUE;
	const gchar *name = gq_gtk_entry_get_text(GTK_ENTRY(ew->entry));

	if (!name || !name[0])
		{
		file_util_warning_dialog(_("Can't save"), _("Please specify file name."), GQ_ICON_DIALOG_ERROR, nullptr);
		return FALSE;
		}

	gtk_text_buffer_get_bounds(ew->buffer, &start, &end);
	g_autofree gchar *text = gtk_text_buffer_get_text(ew->buffer, &start, &end, FALSE);

	g_autofree gchar *dir = g_build_filename(get_rc_dir(), "applications", NULL);
	g_autofree gchar *path = g_build_filename(dir, name, NULL);

	if (!recursive_mkdir_if_not_exists(dir, 0755))
		{
		file_util_warning_dialog(_("Can't save"), _("Could not create directory"), GQ_ICON_DIALOG_ERROR, nullptr);
		ret = FALSE;
		}

	g_autoptr(GError) error = nullptr;
	if (ret && !g_file_set_contents(path, text, -1, &error))
		{
		file_util_warning_dialog(_("Can't save"), error->message, GQ_ICON_DIALOG_ERROR, nullptr);
		ret = FALSE;
		}

	layout_editors_reload_start();
	/* idle function is not needed, everything should be cached */
	layout_editors_reload_finish();
	return ret;
}

void editor_window_close_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<EditorWindow *>(data);

	g_free(ew->desktop_name);
	gq_gtk_widget_destroy(ew->window);
	g_free(ew);
}

gint editor_window_delete_cb(GtkWidget *w, GdkEventAny *, gpointer data)
{
	editor_window_close_cb(w, data);
	return TRUE;
}

void editor_window_save_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<EditorWindow *>(data);

	if (ew->modified)
		{
		editor_window_save(ew);
		}

	gtk_widget_set_sensitive(ew->save_button, FALSE);
	gtk_text_buffer_set_modified(ew->buffer, FALSE);
	ew->modified = FALSE;
}

void editor_window_text_modified_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<EditorWindow *>(data);

	if (gtk_text_buffer_get_modified(ew->buffer))
		{
		gtk_widget_set_sensitive(ew->save_button, TRUE);
		ew->modified = TRUE;
		}
}

void editor_window_entry_changed_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<EditorWindow *>(data);
	const gchar *content = gq_gtk_entry_get_text(GTK_ENTRY(ew->entry));
	gboolean modified = ew->modified;

	if (!modified)
		{
		modified = (!ew->desktop_name && *content);
		}

	if (!modified)
		{
		modified = strcmp(ew->desktop_name, content) != 0;
		}

	gtk_widget_set_sensitive(ew->save_button, modified);
	ew->modified = modified;
}

void editor_window_new(const gchar *src_path, const gchar *desktop_name)
{
	EditorWindow *ew;
	GtkWidget *win_vbox;
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *text_view;
	gchar *text;
	gsize size;

	ew = g_new0(EditorWindow, 1);

	ew->window = window_new("Desktop", PIXBUF_INLINE_ICON_CONFIG, _("Desktop file"));
	DEBUG_NAME(ew->window);
	gtk_window_set_type_hint(GTK_WINDOW(ew->window), GDK_WINDOW_TYPE_HINT_DIALOG);

	g_signal_connect(G_OBJECT(ew->window), "delete_event",
			 G_CALLBACK(editor_window_delete_cb), ew);

	gtk_window_set_default_size(GTK_WINDOW(ew->window), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(ew->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(ew->window), PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gq_gtk_container_add(ew->window, win_vbox);
	gtk_widget_show(win_vbox);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gq_gtk_box_pack_end(GTK_BOX(win_vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	ew->entry = gtk_entry_new();
	gq_gtk_box_pack_start(GTK_BOX(hbox), ew->entry, TRUE, TRUE, 0);
	ew->desktop_name = nullptr;
	if (desktop_name)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(ew->entry), desktop_name);
		ew->desktop_name = g_strdup(desktop_name);
		}
	gtk_widget_show(ew->entry);
	g_signal_connect(G_OBJECT(ew->entry), "changed", G_CALLBACK(editor_window_entry_changed_cb), ew);

	GtkWidget *button_hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(button_hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(button_hbox), PREF_PAD_BUTTON_GAP);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button_hbox, FALSE, FALSE, 0);
	gtk_widget_show(button_hbox);

	ew->save_button = pref_button_new(nullptr, GQ_ICON_SAVE, _("Save"),
				 G_CALLBACK(editor_window_save_cb), ew);
	gq_gtk_container_add(button_hbox, ew->save_button);
	gtk_widget_set_can_default(ew->save_button, TRUE);
	gtk_widget_set_sensitive(ew->save_button, FALSE);
	gtk_widget_show(ew->save_button);
	ct_button = ew->save_button;

	button = pref_button_new(nullptr, GQ_ICON_CLOSE, _("Close"),
				 G_CALLBACK(editor_window_close_cb), ew);
	gq_gtk_container_add(button_hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	if (!generic_dialog_get_alternative_button_order(ew->window))
		{
		gtk_box_reorder_child(GTK_BOX(button_hbox), ct_button, -1);
		}

	GtkWidget *scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(win_vbox), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	text_view = gtk_text_view_new();
	gq_gtk_container_add(scrolled, text_view);
	gtk_widget_show(text_view);

	ew->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
	if (g_file_get_contents(src_path, &text, &size, nullptr))
		{
		gtk_text_buffer_set_text(ew->buffer, text, size);
		}
	gtk_text_buffer_set_modified(ew->buffer, FALSE);
	g_signal_connect(G_OBJECT(ew->buffer), "modified-changed",
			 G_CALLBACK(editor_window_text_modified_cb), ew);

	gtk_widget_show(ew->window);
}


void editor_list_window_close_cb(GtkWidget *, gpointer)
{
	gq_gtk_widget_destroy(editor_list_window->window);
	g_free(editor_list_window);
	editor_list_window = nullptr;
}

gboolean editor_list_window_delete(GtkWidget *, GdkEventAny *, gpointer)
{
	editor_list_window_close_cb(nullptr, nullptr);
	return TRUE;
}

void editor_list_window_delete_dlg_cancel(GenericDialog *, gpointer data)
{
	auto ewdl = static_cast<EditorWindowDelData *>(data);

	ewdl->ewl->gd = nullptr;
	g_free(ewdl->path);
	g_free(ewdl);
}

void editor_list_window_delete_dlg_ok_cb(GenericDialog *gd, gpointer data)
{
	auto ewdl = static_cast<EditorWindowDelData *>(data);

	if (!unlink_file(ewdl->path))
		{
		g_autofree gchar *text = g_strdup_printf(_("Unable to delete file:\n%s"), ewdl->path);
		warning_dialog(_("File deletion failed"), text, GQ_ICON_DIALOG_WARNING, nullptr);
		}
	else
		{
		/* refresh list */
		layout_editors_reload_start();
		/* idle function is not needed, everything should be cached */
		layout_editors_reload_finish();
		}

	editor_list_window_delete_dlg_cancel(gd, data);
}

void editor_list_window_delete_cb(GtkWidget *, gpointer data)
{
	auto ewl = static_cast<EditorListWindow *>(data);
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ewl->view));
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(sel, nullptr, &iter))
		{
		GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(ewl->view));
		gchar *path;

		gtk_tree_model_get(store, &iter,
		                   DESKTOP_FILE_COLUMN_PATH, &path,
		                   -1);

		auto *ewdl = g_new(EditorWindowDelData, 1);
		ewdl->ewl = ewl;
		ewdl->path = path;

		if (ewl->gd)
			{
			GenericDialog *gd = ewl->gd;
			editor_list_window_delete_dlg_cancel(ewl->gd, ewl->gd->data);
			generic_dialog_close(gd);
			}

		ewl->gd = generic_dialog_new(_("Delete file"), "dlg_confirm",
					    nullptr, TRUE,
					    editor_list_window_delete_dlg_cancel, ewdl);

		generic_dialog_add_button(ewl->gd, GQ_ICON_DELETE, _("Delete"), editor_list_window_delete_dlg_ok_cb, TRUE);

		g_autofree gchar *text = g_strdup_printf(_("About to delete the file:\n %s"), path);
		generic_dialog_add_message(ewl->gd, GQ_ICON_DIALOG_QUESTION,
					   _("Delete file"), text, TRUE);

		gtk_widget_show(ewl->gd->dialog);
		}
}

void editor_list_window_edit_cb(GtkWidget *, gpointer data)
{
	auto ewl = static_cast<EditorListWindow *>(data);
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ewl->view));
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected(sel, nullptr, &iter)) return;

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(ewl->view));
	g_autofree gchar *path = nullptr;
	g_autofree gchar *key = nullptr;

	gtk_tree_model_get(store, &iter,
	                   DESKTOP_FILE_COLUMN_PATH, &path,
	                   DESKTOP_FILE_COLUMN_KEY, &key,
	                   -1);
	editor_window_new(path, key);
}

void editor_list_window_new_cb(GtkWidget *, gpointer)
{
	editor_window_new(desktop_file_template, _("new.desktop"));
}

void editor_list_window_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuidePluginsConfig.html");
}

void editor_list_window_selection_changed_cb(GtkTreeSelection *sel, gpointer user_data)
{
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(sel, nullptr, &iter)) return;

	auto *ewl = static_cast<EditorListWindow *>(user_data);
	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(ewl->view));

	g_autofree gchar *path = nullptr;
	gtk_tree_model_get(store, &iter,
	                   DESKTOP_FILE_COLUMN_PATH, &path,
	                   -1);

	gtk_widget_set_sensitive(ewl->delete_button, access_file(path, W_OK));
	gtk_widget_set_sensitive(ewl->edit_button, TRUE);
}

gint editor_list_window_sort_cb(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gint n = GPOINTER_TO_INT(data);
	gint ret = 0;

	switch (n)
		{
		case DESKTOP_FILE_COLUMN_KEY:
		case DESKTOP_FILE_COLUMN_NAME:
		case DESKTOP_FILE_COLUMN_PATH:
		case DESKTOP_FILE_COLUMN_HIDDEN:
			{
			ret = gq_gtk_tree_iter_utf8_collate(model, a, b, n);
			}
			break;
		case DESKTOP_FILE_COLUMN_DISABLED:
			{
			gboolean bool1;
			gboolean bool2;
			gtk_tree_model_get(model, a, n, &bool1, -1);
			gtk_tree_model_get(model, b, n, &bool2, -1);

			ret = bool2 - bool1;
			break;
			}

		default:
			g_return_val_if_reached(0);
		}

	return ret;
}

void plugin_disable_cb(GtkCellRendererToggle *, gchar *path_str, gpointer data)
{
	auto ewl = static_cast<EditorListWindow *>(data);

	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(ewl->view));
	GtkTreeIter iter;
	g_autoptr(GtkTreePath) tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(model, &iter, tpath);

	gboolean disabled;
	g_autofree gchar *path = nullptr;
	gtk_tree_model_get(model, &iter,
	                   DESKTOP_FILE_COLUMN_DISABLED, &disabled,
	                   DESKTOP_FILE_COLUMN_PATH, &path,
	                   -1);

	gtk_list_store_set(GTK_LIST_STORE(desktop_file_list), &iter,
	                   DESKTOP_FILE_COLUMN_DISABLED, !disabled,
	                   -1);

	if (path)
		{
		if (!disabled)
			{
			options->disabled_plugins.emplace_back(path);
			}
		else
			{
			options->disabled_plugins.erase(std::remove(options->disabled_plugins.begin(), options->disabled_plugins.end(), path),
			                                options->disabled_plugins.end());
			}
		}

	layout_editors_reload_start();
	layout_editors_reload_finish();
}

void plugin_disable_set_func(GtkTreeViewColumn *, GtkCellRenderer *cell,
                             GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer)
{
	gboolean disabled;

	gtk_tree_model_get(tree_model, iter, DESKTOP_FILE_COLUMN_DISABLED, &disabled, -1);

	if (disabled)
		{
		g_object_set(GTK_CELL_RENDERER(cell), "active", TRUE, NULL);
		}
	else
		{
		g_object_set(GTK_CELL_RENDERER(cell), "active", FALSE, NULL);
		}
}

void editor_list_window_create()
{
	GtkWidget *win_vbox;
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *scrolled;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *store;
	GtkTreeSortable *sortable;
	EditorListWindow *ewl;

	editor_list_window = ewl = g_new0(EditorListWindow, 1);

	ewl->window = window_new("editors", PIXBUF_INLINE_ICON_CONFIG, _("Plugins"));
	DEBUG_NAME(ewl->window);
	gtk_window_set_type_hint(GTK_WINDOW(ewl->window), GDK_WINDOW_TYPE_HINT_DIALOG);
	g_signal_connect(G_OBJECT(ewl->window), "delete_event",
			 G_CALLBACK(editor_list_window_delete), NULL);
	gtk_window_set_default_size(GTK_WINDOW(ewl->window), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(ewl->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(ewl->window), PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gq_gtk_container_add(ewl->window, win_vbox);
	gtk_widget_show(win_vbox);

	hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_BUTTON_GAP);
	gq_gtk_box_pack_end(GTK_BOX(win_vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(nullptr, GQ_ICON_HELP, _("Help"),
				 G_CALLBACK(editor_list_window_help_cb), ewl);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_NEW, _("New"),
				 G_CALLBACK(editor_list_window_new_cb), ewl);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_EDIT, _("Edit"),
				 G_CALLBACK(editor_list_window_edit_cb), ewl);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_show(button);
	ewl->edit_button = button;

	button = pref_button_new(nullptr, GQ_ICON_DELETE, _("Delete"),
				 G_CALLBACK(editor_list_window_delete_cb), ewl);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_show(button);
	ewl->delete_button = button;

	button = pref_button_new(nullptr, GQ_ICON_CLOSE, _("Close"),
				 G_CALLBACK(editor_list_window_close_cb), ewl);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(win_vbox), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	ewl->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(desktop_file_list));
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ewl->view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
 	g_signal_connect(selection, "changed", G_CALLBACK(editor_list_window_selection_changed_cb), ewl);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(ewl->view), FALSE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Disabled"));
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(plugin_disable_cb), ewl);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, plugin_disable_set_func,
						nullptr, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(ewl->view), column);
	gtk_tree_view_column_set_sort_column_id(column, DESKTOP_FILE_COLUMN_DISABLED);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Name"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", DESKTOP_FILE_COLUMN_NAME);
	gtk_tree_view_append_column(GTK_TREE_VIEW(ewl->view), column);
	gtk_tree_view_column_set_sort_column_id(column, DESKTOP_FILE_COLUMN_NAME);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Hidden"));
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", DESKTOP_FILE_COLUMN_HIDDEN);
	gtk_tree_view_append_column(GTK_TREE_VIEW(ewl->view), column);
	gtk_tree_view_column_set_sort_column_id(column, DESKTOP_FILE_COLUMN_HIDDEN);
	gtk_tree_view_column_set_alignment(column, 0.5);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Desktop file"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", DESKTOP_FILE_COLUMN_KEY);
	gtk_tree_view_append_column(GTK_TREE_VIEW(ewl->view), column);
	gtk_tree_view_column_set_sort_column_id(column, DESKTOP_FILE_COLUMN_KEY);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Path"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", DESKTOP_FILE_COLUMN_PATH);
	gtk_tree_view_append_column(GTK_TREE_VIEW(ewl->view), column);
	gtk_tree_view_column_set_sort_column_id(column, DESKTOP_FILE_COLUMN_PATH);

	/* set up sorting */
	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ewl->view));
	sortable = GTK_TREE_SORTABLE(store);
	gtk_tree_sortable_set_sort_func(sortable, DESKTOP_FILE_COLUMN_KEY, editor_list_window_sort_cb,
					GINT_TO_POINTER(DESKTOP_FILE_COLUMN_KEY), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, DESKTOP_FILE_COLUMN_HIDDEN, editor_list_window_sort_cb,
					GINT_TO_POINTER(DESKTOP_FILE_COLUMN_HIDDEN), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, DESKTOP_FILE_COLUMN_NAME, editor_list_window_sort_cb,
					GINT_TO_POINTER(DESKTOP_FILE_COLUMN_NAME), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, DESKTOP_FILE_COLUMN_PATH, editor_list_window_sort_cb,
					GINT_TO_POINTER(DESKTOP_FILE_COLUMN_PATH), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, DESKTOP_FILE_COLUMN_DISABLED, editor_list_window_sort_cb,
					GINT_TO_POINTER(DESKTOP_FILE_COLUMN_DISABLED), nullptr);

	/* set initial sort order */
    gtk_tree_sortable_set_sort_column_id(sortable, DESKTOP_FILE_COLUMN_NAME, GTK_SORT_ASCENDING);

	gq_gtk_container_add(scrolled, ewl->view);
	gtk_widget_show(ewl->view);

	gtk_widget_show(ewl->window);
}

} // namespace

/*
 *-----------------------------------------------------------------------------
 * config window show (public)
 *-----------------------------------------------------------------------------
 */

void show_editor_list_window()
{
	if (editor_list_window)
		{
		gtk_window_present(GTK_WINDOW(editor_list_window));
		return;
		}

	editor_list_window_create();
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
