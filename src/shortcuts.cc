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
#include "ui-utildlg.h"
#include "utilops.h"

namespace
{

struct ShortcutsData
{
	GtkWidget *vbox;
	GtkWidget *bookmarks;
	LayoutWindow *lw;

	FileDialog *dialog;
	GtkWidget *dialog_name_entry;

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

void shortcuts_add_close(ShortcutsData *scd)
{
	if (scd->dialog) file_dialog_close(scd->dialog);
	scd->dialog_name_entry = nullptr;
	scd->dialog = nullptr;
}

void add_shortcut_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		GList *list = gtk_container_get_children(GTK_CONTAINER(gtk_file_chooser_get_extra_widget(chooser)));

		GList *work = list;
		while (work)
			{
			if (GTK_IS_ENTRY(work->data))
				{
				auto scd = static_cast<ShortcutsData *>(data);

				const gchar *alias_name = gtk_entry_get_text(static_cast<GtkEntry *>(work->data));

				gboolean empty_name = (alias_name[0] == '\0');
				g_autofree gchar *selected_dir = gtk_file_chooser_get_filename(chooser);

				bookmark_list_add(scd->bookmarks, empty_name ? filename_from_path(selected_dir) : alias_name, selected_dir);
				}
			work = work->next;
			}
		g_list_free(list);
		}

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

void shortcuts_add_cb(GtkWidget *, gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);
	GtkFileChooserDialog *dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

	dialog = GTK_FILE_CHOOSER_DIALOG(gtk_file_chooser_dialog_new(_("Add Shortcut - Geeqie"), nullptr, action, _("_Cancel"), GTK_RESPONSE_CANCEL, _("Add"), GTK_RESPONSE_ACCEPT, nullptr));

	GtkWidget *name_widget_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

	GtkWidget *name_label = gtk_label_new(_("Shortcut name (optional):"));
	gq_gtk_box_pack_start(GTK_BOX(name_widget_box), name_label, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(name_label, _("If none given, the basename of the folder is used"));

	GtkWidget *entry = gtk_entry_new();
	gq_gtk_box_pack_start(GTK_BOX(name_widget_box), entry, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(entry, _("If none given, the basename of the folder is used"));

	gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), name_widget_box);
	gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);

	g_signal_connect(dialog, "response", G_CALLBACK(add_shortcut_cb), scd);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

void shortcuts_destroy(gpointer data)
{
	auto scd = static_cast<ShortcutsData *>(data);

	shortcuts_add_close(scd);

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
