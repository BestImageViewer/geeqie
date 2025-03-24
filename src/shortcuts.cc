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

#include "shortcuts.h"

#include <glib-object.h>
#include <glib.h>

#include "collect.h"
#include "compat.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"
#include "ui-misc.h"

namespace
{

struct ShortcutsData
{
	GtkWidget *vbox;
	GtkWidget *bookmarks;
	LayoutWindow *lw;

	gchar *name;
	GtkPopover *name_popover;

	GtkWidget *add_button;
};

#define SHORTCUTS     "shortcuts"

void shortcuts_bookmark_select(const gchar *path, gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);

	if (file_extension_match(path, GQ_COLLECTION_EXT))
		{
		collection_window_new(path);
		}
	else
		{
		layout_set_path(scd->lw, path);
		}

}

void name_entry_activate_cb(GtkEntry *entry, gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);

	g_free(scd->name);
	scd->name = g_strdup(gtk_entry_get_text(entry));

	gtk_popover_popdown(GTK_POPOVER(scd->name_popover));
}

void add_shortcut_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		gboolean empty_name = (scd->name == nullptr);

		g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
		g_autofree gchar *selected_dir = g_file_get_path(file);

		bookmark_list_add(scd->bookmarks, empty_name ? filename_from_path(selected_dir) : scd->name, selected_dir);

		g_free(scd->name);
		scd->name = nullptr;
		}

	if (response_id == GQ_RESPONSE_NAME_CLICKED)
		{
		gtk_popover_popup(GTK_POPOVER(scd->name_popover));
		gq_gtk_widget_show_all(GTK_WIDGET(scd->name_popover));
		gtk_widget_grab_focus(GTK_WIDGET(scd->name_popover));

		return;
		}

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

void shortcuts_add_cb(GtkWidget *, gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);
	GtkFileChooserDialog *dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

	dialog = GTK_FILE_CHOOSER_DIALOG(gtk_file_chooser_dialog_new(_("Add Shortcut - Geeqie"), nullptr, action, _("_Cancel"), GTK_RESPONSE_CANCEL, _("Add"), GTK_RESPONSE_ACCEPT, _("Name"), GQ_RESPONSE_NAME_CLICKED, nullptr));

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), layout_get_path(get_current_layout()));
	gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dialog), layout_get_path(get_current_layout()), nullptr);

	gtk_widget_set_tooltip_text(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GQ_RESPONSE_NAME_CLICKED), _("Optional alias name for the shortcut.\nThis may be amended or added from the Shortcuts pane.\nIf none given, the basename of the folder is usedddd"));

	GtkWidget *entry = gtk_entry_new();
	GtkWidget *name_popover = gtk_popover_new(GTK_WIDGET(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GQ_RESPONSE_NAME_CLICKED)));
	scd->name_popover = GTK_POPOVER(name_popover);

	g_signal_connect(GTK_ENTRY(entry), "activate", G_CALLBACK(name_entry_activate_cb), scd);

	gtk_popover_set_position(GTK_POPOVER(name_popover), GTK_POS_BOTTOM);
	gq_gtk_container_add(GTK_WIDGET(name_popover), entry);
	gtk_container_set_border_width(GTK_CONTAINER(name_popover), 6);

	gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);

	g_signal_connect(dialog, "response", G_CALLBACK(add_shortcut_cb), scd);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

void shortcuts_destroy(gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);

	g_free(scd->name);
	g_free(scd);
}

} // namespace

GtkWidget *shortcuts_new(LayoutWindow *lw)
{
	ShortcutsData *scd;
	GtkWidget *tbar;

	if (!lw) return nullptr;

	scd = g_new0(ShortcutsData, 1);

	scd->lw = lw;

	scd->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	g_object_set_data_full(G_OBJECT(scd->vbox), "shortcuts_data", scd, shortcuts_destroy);

	scd->bookmarks = bookmark_list_new(SHORTCUTS, shortcuts_bookmark_select, scd);
	gq_gtk_box_pack_start(GTK_BOX(scd->vbox), scd->bookmarks, TRUE, TRUE, 0);
	gtk_widget_show(scd->bookmarks);

	tbar = pref_toolbar_new(scd->vbox);

	scd->add_button = pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE,
					_("Add Shortcut"),
					G_CALLBACK(shortcuts_add_cb), scd);

	gtk_widget_show(scd->vbox);
	return scd->vbox;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
