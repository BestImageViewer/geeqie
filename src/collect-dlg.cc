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

#include "collect-dlg.h"

#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "collect-io.h"
#include "collect.h"
#include "compat.h"
#include "intl.h"
#include "main-defines.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "utilops.h"

static void collection_save_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
		g_autofree gchar *filename = g_file_get_path(file);

		cd->collection_path = g_strdup(filename);
		if (!collection_save(cd, cd->collection_path))
			{
			g_autofree gchar *buf = g_strdup_printf(_("Failed to save the collection:\n%s"), cd->collection_path);
			file_util_warning_dialog(_("Save Failed"), buf, GQ_ICON_DIALOG_ERROR, GTK_WIDGET(chooser));
			}

		collection_unref(cd);
		collection_window_close_by_collection(cd);
		}

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void collection_append_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
		g_autofree gchar *filename = g_file_get_path(file);

		if (!collection_load(cd, filename, COLLECTION_LOAD_APPEND))
			{
			return;
			}
		}

	collection_unref(cd);

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void collection_dialog_new(CollectionData *cd, const gchar *title, GtkFileChooserAction action, GCallback response_callback)
{
	if (!cd) return;
	collection_ref(cd);

	FileChooserDialogData fcdd{};

	fcdd.action = action;
	fcdd.accept_text = _("Save");
	fcdd.data = cd;
	fcdd.filename = get_collections_dir();
	fcdd.filter = GQ_COLLECTION_EXT;
	fcdd.filter_description = _("Collection files");
	fcdd.history_key = "open_collection";;
	fcdd.response_callback = response_callback;
	fcdd.shortcuts = get_collections_dir();
	fcdd.suggested_name = _("Untitled.gqv");
	fcdd.title = title;

	GtkFileChooserDialog *dialog = file_chooser_dialog_new(fcdd);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

void collection_dialog_save(CollectionData *cd)
{
	collection_dialog_new(cd, _("Save Collection As - Geeqie"), GTK_FILE_CHOOSER_ACTION_SAVE, G_CALLBACK(collection_save_cb));
}

void collection_dialog_append(CollectionData *cd)
{
	collection_dialog_new(cd, _("Append Collection - Geeqie"), GTK_FILE_CHOOSER_ACTION_OPEN, G_CALLBACK(collection_append_cb));
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
