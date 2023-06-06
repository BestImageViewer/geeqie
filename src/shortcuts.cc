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
#include "shortcuts.h"

#include "collect.h"
#include "layout.h"
#include "utilops.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"
#include "ui-misc.h"

typedef struct _ShortcutsData ShortcutsData;
struct _ShortcutsData
{
	GtkWidget *vbox;
	GtkWidget *bookmarks;
	LayoutWindow *lw;

	FileDialog *dialog;
	GtkWidget *dialog_name_entry;

	GtkWidget *add_button;
};

#define SHORTCUTS     "shortcuts"

static void shortcuts_bookmark_select(const gchar *path, gpointer data)
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

static void shortcuts_add_close(ShortcutsData *scd)
{
	if (scd->dialog) file_dialog_close(scd->dialog);
	scd->dialog_name_entry = nullptr;
	scd->dialog = nullptr;
}

static void shortcuts_add_ok_cb(FileDialog *fd, gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);
	const gchar *name = gtk_entry_get_text(GTK_ENTRY(scd->dialog_name_entry));
	gboolean empty_name = (name[0] == '\0');

	name = gtk_entry_get_text(GTK_ENTRY(scd->dialog_name_entry));

	if (empty_name)
		{
		name = filename_from_path(fd->dest_path);
		}

	bookmark_list_add(scd->bookmarks, name, fd->dest_path);

	shortcuts_add_close(scd);
}

static void shortcuts_add_cancel_cb(FileDialog *UNUSED(fd), gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);

	shortcuts_add_close(scd);
}

static void shortcuts_add_cb(GtkWidget *button, gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);
	GtkWidget *hbox;
	const gchar *title;

	if (scd->dialog)
		{
		gtk_window_present(GTK_WINDOW(GENERIC_DIALOG(scd->dialog)->dialog));
		return;
		}

	title = _("Add Shortcut");
	scd->dialog = file_util_file_dlg(title,
					"add_shortcuts", button,
					shortcuts_add_cancel_cb, scd);
	file_dialog_add_button(scd->dialog, GTK_STOCK_OK, nullptr, shortcuts_add_ok_cb, TRUE);

	generic_dialog_add_message(GENERIC_DIALOG(scd->dialog), nullptr, title, nullptr, FALSE);

	file_dialog_add_path_widgets(scd->dialog, nullptr, nullptr, "add_shortcuts", nullptr, nullptr);

	hbox = pref_box_new(GENERIC_DIALOG(scd->dialog)->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

	pref_label_new(hbox, _("Name:"));

	scd->dialog_name_entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), scd->dialog_name_entry, TRUE, TRUE, 0);
	generic_dialog_attach_default(GENERIC_DIALOG(scd->dialog), scd->dialog_name_entry);
	gtk_widget_show(scd->dialog_name_entry);

	gtk_widget_show(GENERIC_DIALOG(scd->dialog)->dialog);
}

static void shortcuts_destroy(GtkWidget *UNUSED(widget), gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);

	shortcuts_add_close(scd);

	g_free(scd);
}

static GtkWidget *shortcuts_new(LayoutWindow *lw)
{
	ShortcutsData *scd;
	GtkWidget *tbar;

	if (!lw) return nullptr;

	scd = g_new0(ShortcutsData, 1);

	scd->lw = lw;

	scd->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	g_object_set_data(G_OBJECT(scd->vbox), "shortcuts_data", scd);
	g_signal_connect(G_OBJECT(scd->vbox), "destroy",
			G_CALLBACK(shortcuts_destroy), scd);

	scd->bookmarks = bookmark_list_new(SHORTCUTS, shortcuts_bookmark_select, scd);
	gtk_box_pack_start(GTK_BOX(scd->vbox), scd->bookmarks, TRUE, TRUE, 0);
	gtk_widget_show(scd->bookmarks);

	tbar = pref_toolbar_new(scd->vbox, GTK_TOOLBAR_ICONS);

	scd->add_button = pref_toolbar_button(tbar, GTK_STOCK_ADD, nullptr, FALSE,
					_("Add Shortcut"),
					G_CALLBACK(shortcuts_add_cb), scd);

	return scd->vbox;
}

GtkWidget *shortcuts_new_from_config(LayoutWindow *lw, const gchar **UNUSED(attribute_names), const gchar **UNUSED(attribute_values))
{
	GtkWidget *shortcuts_bar;

	shortcuts_bar = shortcuts_new(lw);
	gtk_widget_show(shortcuts_bar);

	return shortcuts_bar;
}

GtkWidget *shortcuts_new_default(LayoutWindow *lw)
{
	return shortcuts_new_from_config(lw, nullptr, nullptr);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
