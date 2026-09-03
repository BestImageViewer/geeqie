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

#include "accelerators.h"
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

enum class DesktopFileField
{
	DISABLED,
	NAME,
	HIDDEN,
	KEY,
	PATH
};

constexpr gint CONFIG_WINDOW_DEF_WIDTH = 700;
constexpr gint CONFIG_WINDOW_DEF_HEIGHT = 400;

EditorListWindow *editor_list_window = nullptr;

gchar *plugin_shortcut_conflict(const gchar *desktop_text, const gchar *desktop_name)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();
	if (!g_key_file_load_from_data(key_file, desktop_text, -1, G_KEY_FILE_NONE, nullptr)) return nullptr;

	g_autofree gchar *hotkey = g_key_file_get_string(key_file, G_KEY_FILE_DESKTOP_GROUP,
	                                                "X-Geeqie-Hotkey", nullptr);
	if (!hotkey || !*hotkey || !accelerator_string_is_valid(hotkey)) return nullptr;

	g_auto(GStrv) shortcuts = g_strsplit(hotkey, ";", -1);
	for (const EditorDescription *editor : editor_list_get())
		{
		if (g_strcmp0(editor->key, desktop_name) == 0 || !editor->hotkey || !*editor->hotkey) continue;

		g_auto(GStrv) existing_shortcuts = g_strsplit(editor->hotkey, ";", -1);
		for (gchar **shortcut = shortcuts; *shortcut; shortcut++)
			{
			guint key = 0;
			GdkModifierType modifiers = GDK_NO_MODIFIER_MASK;
			gtk_accelerator_parse(g_strstrip(*shortcut), &key, &modifiers);
			if (!key) continue;

			for (gchar **existing = existing_shortcuts; *existing; existing++)
				{
				guint existing_key = 0;
				GdkModifierType existing_modifiers = GDK_NO_MODIFIER_MASK;
				gtk_accelerator_parse(g_strstrip(*existing), &existing_key, &existing_modifiers);
				if (key == existing_key && modifiers == existing_modifiers)
					{
					return g_strdup(editor->name && *editor->name ? editor->name : editor->key);
					}
				}
			}
		}

	return nullptr;
}

gboolean editor_window_save(EditorWindow *ew)
{
	gboolean ret = TRUE;

	const char *name = gtk_editable_get_text(GTK_EDITABLE(ew->entry));
	if (!name || !name[0])
		{
		file_util_warning_dialog(_("Can't save"), _("Please specify file name."), GQ_ICON_DIALOG_ERROR, nullptr);
		return FALSE;
		}

	g_autofree gchar *text = text_buffer_get_text(ew->buffer, FALSE);
	g_autofree gchar *conflicting_plugin = plugin_shortcut_conflict(text, ew->desktop_name);
	if (conflicting_plugin)
		{
		g_autofree gchar *message = g_strdup_printf(_("The shortcut is already assigned to plugin \"%s\".\n\nThe operation cannot be completed."), conflicting_plugin);
		file_util_warning_dialog(_("Shortcut conflict"), message, GQ_ICON_DIALOG_WARNING, ew->window);
		return FALSE;
		}

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
	gtk_window_destroy(GTK_WINDOW(ew->window));
	g_free(ew);
}

gint editor_window_delete_cb(GtkWidget *w, gpointer data)
{
	editor_window_close_cb(w, data);
	return TRUE;
}

void editor_window_save_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<EditorWindow *>(data);

	if (ew->modified && !editor_window_save(ew)) return;

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
	const char *content = gtk_editable_get_text(GTK_EDITABLE(ew->entry));
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
	GtkWidget *text_view;
	gchar *text;
	gsize size;

	ew = g_new0(EditorWindow, 1);

	ew->window = window_new("Desktop", PIXBUF_INLINE_ICON_CONFIG, _("Desktop file"));
	DEBUG_NAME(ew->window);

	g_signal_connect(G_OBJECT(ew->window), "close-request",
			 G_CALLBACK(editor_window_delete_cb), ew);

	gtk_window_set_default_size(GTK_WINDOW(ew->window), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(ew->window), TRUE);
	gtk_widget_set_margin_top(ew->window, PREF_PAD_BORDER);
	gtk_widget_set_margin_bottom(ew->window, PREF_PAD_BORDER);
	gtk_widget_set_margin_start(ew->window, PREF_PAD_BORDER);
	gtk_widget_set_margin_end(ew->window, PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gtk_window_set_child(GTK_WINDOW(ew->window), win_vbox);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_widget_set_valign(hbox, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(win_vbox), hbox);

	ew->entry = gtk_entry_new();
	gtk_widget_set_hexpand(ew->entry, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(ew->entry, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox), ew->entry);
	ew->desktop_name = nullptr;
	if (desktop_name)
		{
		entry_set_text(GTK_ENTRY(ew->entry), desktop_name);
		ew->desktop_name = g_strdup(desktop_name);
		}
	g_signal_connect(G_OBJECT(ew->entry), "changed", G_CALLBACK(editor_window_entry_changed_cb), ew);

	GtkWidget *button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	gtk_widget_set_halign(button_hbox, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(hbox), button_hbox);

	pref_button_new(button_hbox, GQ_ICON_CLOSE, _("Close"),
	                G_CALLBACK(editor_window_close_cb), ew);

	ew->save_button = pref_button_new(button_hbox, GQ_ICON_SAVE, _("Save"),
	                                  G_CALLBACK(editor_window_save_cb), ew);
	gtk_window_set_default_widget(GTK_WINDOW(ew->window), ew->save_button);
	gtk_widget_set_sensitive(ew->save_button, FALSE);

	GtkWidget *scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_HORIZONTAL)
		{
		gtk_widget_set_margin_end(scrolled, 5);
		}
	else
		{
		gtk_widget_set_margin_bottom(scrolled, 5);
		}
	gtk_box_append(GTK_BOX(win_vbox), scrolled);
	gtk_box_reorder_child_after(GTK_BOX(win_vbox), hbox, scrolled);

	text_view = gtk_text_view_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), text_view);

	ew->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
	if (g_file_get_contents(src_path, &text, &size, nullptr))
		{
		gtk_text_buffer_set_text(ew->buffer, text, size);
		}
	gtk_text_buffer_set_modified(ew->buffer, FALSE);
	g_signal_connect(G_OBJECT(ew->buffer), "modified-changed",
			 G_CALLBACK(editor_window_text_modified_cb), ew);

	gtk_window_present(GTK_WINDOW(ew->window));
}


void editor_list_window_close_cb(GtkWidget *, gpointer)
{
	gtk_window_destroy(GTK_WINDOW(editor_list_window->window));
	g_free(editor_list_window);
	editor_list_window = nullptr;
}

gboolean editor_list_window_delete(GtkWidget *, gpointer)
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
	auto *selection = GTK_SINGLE_SELECTION(gtk_column_view_get_model(GTK_COLUMN_VIEW(ewl->view)));
	auto *item = static_cast<DesktopFileListItem *>(gtk_single_selection_get_selected_item(selection));

	if (item)
		{
		auto *ewdl = g_new(EditorWindowDelData, 1);
		ewdl->ewl = ewl;
		ewdl->path = g_strdup(item->path);

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

		g_autofree gchar *text = g_strdup_printf(_("About to delete the file:\n %s"), item->path);
		generic_dialog_add_message(ewl->gd, GQ_ICON_DIALOG_QUESTION,
					   _("Delete file"), text, TRUE);

		gtk_window_present(GTK_WINDOW(ewl->gd->dialog));
		}
}

void editor_list_window_edit_cb(GtkWidget *, gpointer data)
{
	auto ewl = static_cast<EditorListWindow *>(data);
	auto *selection = GTK_SINGLE_SELECTION(gtk_column_view_get_model(GTK_COLUMN_VIEW(ewl->view)));
	auto *item = static_cast<DesktopFileListItem *>(gtk_single_selection_get_selected_item(selection));
	if (!item) return;

	editor_window_new(item->path, item->key);
}

void editor_list_window_new_cb(GtkWidget *, gpointer)
{
	editor_window_new(desktop_file_template, _("new.desktop"));
}

void editor_list_window_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuidePluginsConfig.html");
}

void editor_list_window_selection_changed_cb(GtkSingleSelection *selection, GParamSpec *, gpointer data)
{
	auto *ewl = static_cast<EditorListWindow *>(data);
	auto *item = static_cast<DesktopFileListItem *>(gtk_single_selection_get_selected_item(selection));

	gtk_widget_set_sensitive(ewl->delete_button, item && access_file(item->path, W_OK));
	gtk_widget_set_sensitive(ewl->edit_button, item != nullptr);
}

gint editor_list_window_sort_cb(gconstpointer item_a, gconstpointer item_b, gpointer data)
{
	auto *a = static_cast<const DesktopFileListItem *>(item_a);
	auto *b = static_cast<const DesktopFileListItem *>(item_b);
	const auto field = static_cast<DesktopFileField>(GPOINTER_TO_INT(data));

	if (field == DesktopFileField::DISABLED)
		{
		if (a->disabled == b->disabled) return GTK_ORDERING_EQUAL;
		return a->disabled ? GTK_ORDERING_SMALLER : GTK_ORDERING_LARGER;
		}

	const gchar *str_a = nullptr;
	const gchar *str_b = nullptr;
	switch (field)
		{
		case DesktopFileField::NAME: str_a = a->name; str_b = b->name; break;
		case DesktopFileField::HIDDEN: str_a = a->hidden; str_b = b->hidden; break;
		case DesktopFileField::KEY: str_a = a->key; str_b = b->key; break;
		case DesktopFileField::PATH: str_a = a->path; str_b = b->path; break;
		case DesktopFileField::DISABLED: g_assert_not_reached();
		}

	if (!str_a || !str_b)
		{
		if (str_a == str_b) return GTK_ORDERING_EQUAL;
		return str_a ? GTK_ORDERING_LARGER : GTK_ORDERING_SMALLER;
		}

	return std::clamp(g_utf8_collate(str_a, str_b), -1, 1);
}

void plugin_disable_cb(GtkCheckButton *button, gpointer)
{
	auto *list_item = static_cast<GtkListItem *>(g_object_get_data(G_OBJECT(button), "editor-list-item"));
	auto *item = static_cast<DesktopFileListItem *>(gtk_list_item_get_item(list_item));
	const gboolean disabled = gtk_check_button_get_active(button);
	if (!item || item->disabled == disabled) return;

	item->disabled = disabled;
	if (item->path)
		{
		if (disabled)
			{
			options->disabled_plugins.emplace_back(item->path);
			}
		else
			{
			options->disabled_plugins.erase(std::remove(options->disabled_plugins.begin(), options->disabled_plugins.end(), item->path),
			                                options->disabled_plugins.end());
			}
		}

	layout_editors_reload_start();
	layout_editors_reload_finish();
}

void editor_list_toggle_factory_setup(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	GtkWidget *button = gtk_check_button_new();
	g_object_set_data(G_OBJECT(button), "editor-list-item", list_item);
	g_signal_connect(button, "toggled", G_CALLBACK(plugin_disable_cb), nullptr);
	gtk_list_item_set_child(list_item, button);
}

void editor_list_toggle_factory_bind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	auto *item = static_cast<DesktopFileListItem *>(gtk_list_item_get_item(list_item));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(gtk_list_item_get_child(list_item)), item->disabled);
}

void editor_list_text_factory_setup(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	GtkWidget *label = gtk_label_new(nullptr);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_list_item_set_child(list_item, label);
}

void editor_list_text_factory_bind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer data)
{
	auto *item = static_cast<DesktopFileListItem *>(gtk_list_item_get_item(list_item));
	const auto field = static_cast<DesktopFileField>(GPOINTER_TO_INT(data));
	const gchar *text = nullptr;
	switch (field)
		{
		case DesktopFileField::NAME: text = item->name; break;
		case DesktopFileField::HIDDEN: text = item->hidden; break;
		case DesktopFileField::KEY: text = item->key; break;
		case DesktopFileField::PATH: text = item->path; break;
		case DesktopFileField::DISABLED: g_assert_not_reached();
		}
	gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(list_item)), text ? text : "");
}

GtkColumnViewColumn *editor_list_column_new(const gchar *title, DesktopFileField field)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	if (field == DesktopFileField::DISABLED)
		{
		g_signal_connect(factory, "setup", G_CALLBACK(editor_list_toggle_factory_setup), nullptr);
		g_signal_connect(factory, "bind", G_CALLBACK(editor_list_toggle_factory_bind), nullptr);
		}
	else
		{
		g_signal_connect(factory, "setup", G_CALLBACK(editor_list_text_factory_setup), nullptr);
		g_signal_connect(factory, "bind", G_CALLBACK(editor_list_text_factory_bind), GINT_TO_POINTER(static_cast<gint>(field)));
		}

	GtkColumnViewColumn *column = gtk_column_view_column_new(title, factory);
	gtk_column_view_column_set_resizable(column, TRUE);
	GtkSorter *sorter = GTK_SORTER(gtk_custom_sorter_new(editor_list_window_sort_cb,
								       GINT_TO_POINTER(static_cast<gint>(field)), nullptr));
	gtk_column_view_column_set_sorter(column, sorter);
	g_object_unref(sorter);

	return column;
}

EditorListWindow *editor_list_window_new()
{
	GtkWidget *win_vbox;
	GtkWidget *hbox;
	GtkWidget *scrolled;

	auto *ewl = g_new0(EditorListWindow, 1);

	ewl->window = window_new("editors", PIXBUF_INLINE_ICON_CONFIG, _("Plugins"));
	DEBUG_NAME(ewl->window);
	g_signal_connect(G_OBJECT(ewl->window), "close-request",
			 G_CALLBACK(editor_list_window_delete), NULL);
	gtk_window_set_default_size(GTK_WINDOW(ewl->window), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(ewl->window), TRUE);
	gtk_widget_set_margin_top(ewl->window, PREF_PAD_BORDER);
	gtk_widget_set_margin_bottom(ewl->window, PREF_PAD_BORDER);
	gtk_widget_set_margin_start(ewl->window, PREF_PAD_BORDER);
	gtk_widget_set_margin_end(ewl->window, PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gtk_window_set_child(GTK_WINDOW(ewl->window), win_vbox);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	gtk_widget_set_halign(hbox, GTK_ALIGN_END);
	gtk_widget_set_valign(hbox, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(win_vbox), hbox);

	pref_button_new(hbox, GQ_ICON_HELP, _("Help"),
	                G_CALLBACK(editor_list_window_help_cb), ewl);

	pref_button_new(hbox, GQ_ICON_NEW, _("New"),
	                G_CALLBACK(editor_list_window_new_cb), ewl);

	ewl->edit_button = pref_button_new(hbox, GQ_ICON_EDIT, _("Edit"),
	                                   G_CALLBACK(editor_list_window_edit_cb), ewl);
	gtk_widget_set_sensitive(ewl->edit_button, FALSE);

	ewl->delete_button = pref_button_new(hbox, GQ_ICON_DELETE, _("Delete"),
	                                     G_CALLBACK(editor_list_window_delete_cb), ewl);
	gtk_widget_set_sensitive(ewl->delete_button, FALSE);

	pref_button_new(hbox, GQ_ICON_CLOSE, _("Close"),
	                G_CALLBACK(editor_list_window_close_cb), ewl);

	scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_HORIZONTAL)
		{
		gtk_widget_set_margin_end(scrolled, 5);
		}
	else
		{
		gtk_widget_set_margin_bottom(scrolled, 5);
		}
	gtk_box_append(GTK_BOX(win_vbox), scrolled);
	gtk_box_reorder_child_after(GTK_BOX(win_vbox), hbox, scrolled);

	ewl->view = gtk_column_view_new(nullptr);
	GtkColumnViewColumn *column = editor_list_column_new(_("Disabled"), DesktopFileField::DISABLED);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(ewl->view), column);
	g_object_unref(column);

	column = editor_list_column_new(_("Name"), DesktopFileField::NAME);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(ewl->view), column);
	gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(ewl->view), column, GTK_SORT_ASCENDING);
	g_object_unref(column);

	column = editor_list_column_new(_("Hidden"), DesktopFileField::HIDDEN);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(ewl->view), column);
	g_object_unref(column);

	column = editor_list_column_new(_("Desktop file"), DesktopFileField::KEY);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(ewl->view), column);
	g_object_unref(column);

	column = editor_list_column_new(_("Path"), DesktopFileField::PATH);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(ewl->view), column);
	g_object_unref(column);

	GtkSortListModel *sort_model = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(desktop_file_list)),
									GTK_SORTER(g_object_ref(gtk_column_view_get_sorter(GTK_COLUMN_VIEW(ewl->view)))));
	GtkSingleSelection *selection = gtk_single_selection_new(G_LIST_MODEL(sort_model));
	gtk_single_selection_set_autoselect(selection, FALSE);
	gtk_single_selection_set_can_unselect(selection, TRUE);
	g_signal_connect(selection, "notify::selected-item", G_CALLBACK(editor_list_window_selection_changed_cb), ewl);
	gtk_column_view_set_model(GTK_COLUMN_VIEW(ewl->view), GTK_SELECTION_MODEL(selection));
	g_object_unref(selection);

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), ewl->view);

	return ewl;
}

} // namespace

/*
 *-----------------------------------------------------------------------------
 * config window show (public)
 *-----------------------------------------------------------------------------
 */

void show_editor_list_window()
{
	if (!editor_list_window)
		{
		editor_list_window = editor_list_window_new();
		}

	gtk_window_present(GTK_WINDOW(editor_list_window->window));
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
