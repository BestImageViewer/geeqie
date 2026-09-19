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
#include "intl.h"
#include "main-defines.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "utilops.h"

static void collection_save_cb(GFile *file, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	if (file)
		{
		g_autofree gchar *filename = g_file_get_path(file);

		if (!collection_save(cd, filename))
			{
			g_autofree gchar *buf = g_strdup_printf(_("Failed to save the collection:\n%s"), filename);
			file_util_warning_dialog(_("Save Failed"), buf, GQ_ICON_DIALOG_ERROR, nullptr);
			}

		}
	collection_unref(cd);
}

struct CollectionSaveData
{
	CollectionData *collection;
	void (*callback)(gboolean, gpointer);
	gpointer data;
};

static void collection_save_with_callback_cb(GFile *file, gpointer data)
{
	auto *save_data = static_cast<CollectionSaveData *>(data);
	gboolean saved = FALSE;
	if (file)
		{
		g_autofree gchar *filename = g_file_get_path(file);
		saved = collection_save(save_data->collection, filename);
		if (!saved)
			{
			g_autofree gchar *message = g_strdup_printf(_("Failed to save the collection:\n%s"), filename);
			file_util_warning_dialog(_("Save Failed"), message, GQ_ICON_DIALOG_ERROR, nullptr);
			}
		}
	save_data->callback(saved, save_data->data);
	collection_unref(save_data->collection);
	delete save_data;
}

static void collection_append_cb(GFile *file, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	if (file)
		{
		g_autofree gchar *filename = g_file_get_path(file);

		if (!collection_load(cd, filename, COLLECTION_LOAD_APPEND))
			{
			collection_unref(cd);
			return;
			}
		}

	collection_unref(cd);
}

static void collection_dialog_new(CollectionData *cd, const gchar *title, FileDialogAction action, FileDialogCallback callback, gpointer callback_data = nullptr)
{
	if (!cd) return;

	FileDialogData fdd{};
	g_autofree gchar *directory = action == FileDialogAction::SAVE && cd->path ? g_path_get_dirname(cd->path) : nullptr;

	fdd.action = action;
	fdd.accept_text = _("Save");
	fdd.callback = callback;
	fdd.data = callback_data ? callback_data : collection_ref(cd);
	fdd.filename = directory ? directory : get_collections_dir();
	fdd.filter = GQ_COLLECTION_EXT;
	fdd.filter_description = _("Collection files");
	fdd.history_key = "open_collection";
	fdd.suggested_name = cd->path ? filename_from_path(cd->path) : _("Untitled.gqv");
	fdd.title = title;
	if (action == FileDialogAction::SAVE && !cd->path)
		{
		fdd.checkbox_text = _("Save paths relative to the collection file");
		fdd.checkbox_value = &cd->relative_paths;
		}

	file_dialog_show(fdd);
}

void collection_dialog_save(CollectionData *cd)
{
	collection_dialog_new(cd, _("Save Collection As - Geeqie"), FileDialogAction::SAVE, collection_save_cb);
}

void collection_dialog_save_with_callback(CollectionData *cd, void (*callback)(gboolean, gpointer), gpointer data)
{
	auto *save_data = new CollectionSaveData{collection_ref(cd), callback, data};
	collection_dialog_new(cd, _("Save Collection As - Geeqie"), FileDialogAction::SAVE,
	                      collection_save_with_callback_cb, save_data);
}

void collection_dialog_append(CollectionData *cd)
{
	collection_dialog_new(cd, _("Append Collection - Geeqie"), FileDialogAction::OPEN, collection_append_cb);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
