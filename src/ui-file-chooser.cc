/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ui-file-chooser.h"

#include <config.h>

#include "history-list.h"
#include "intl.h"
#include "layout.h"
#include "ui-fileops.h"

namespace {

struct PendingFileDialog
{
	FileDialogAction action;
	FileDialogCallback callback;
	gpointer data;
};

GtkFileDialog *create_file_dialog(const FileDialogData &fdd)
{
	GtkFileDialog *dialog = gtk_file_dialog_new();

	if (fdd.accept_text)
		{
		gtk_file_dialog_set_accept_label(dialog, fdd.accept_text);
		}
	if (fdd.title)
		{
		gtk_file_dialog_set_title(dialog, fdd.title);
		}
	gtk_file_dialog_set_modal(dialog, TRUE);

	auto *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

	GtkFileFilter *all_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(all_filter, _("All files"));
	gtk_file_filter_add_pattern(all_filter, "*");
	g_list_store_append(filters, all_filter);
	g_object_unref(all_filter);

	GtkFileFilter *default_filter = all_filter;

	if (fdd.filter)
		{
		g_auto(GStrv) extension_list = g_strsplit(fdd.filter, ";", -1);
		GtkFileFilter *sub_filter = gtk_file_filter_new();
		gtk_file_filter_set_name(sub_filter, fdd.filter_description ? fdd.filter_description : fdd.filter);

		for (gint i = 0; extension_list[i] != nullptr; i++)
			{
			g_autofree gchar *ext_pattern = g_strconcat("*", extension_list[i], nullptr);
			gtk_file_filter_add_pattern(sub_filter, ext_pattern);
			}

		g_list_store_append(filters, sub_filter);
		default_filter = sub_filter;
		g_object_unref(sub_filter);
		}

	gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
	gtk_file_dialog_set_default_filter(dialog, default_filter);
	g_object_unref(filters);

	const gchar *initial_folder = nullptr;
	if (fdd.history_key)
		{
		initial_folder = history_list_find_last_path_by_key(fdd.history_key);
		}

	if (fdd.filename && isfile(fdd.filename))
		{
		g_autoptr(GFile) file = g_file_new_for_path(fdd.filename);
		gtk_file_dialog_set_initial_file(dialog, file);
		}
	else if (fdd.filename && isdir(fdd.filename))
		{
		g_autoptr(GFile) folder = g_file_new_for_path(fdd.filename);
		gtk_file_dialog_set_initial_folder(dialog, folder);
		}
	else if (initial_folder && isdir(initial_folder))
		{
		g_autoptr(GFile) folder = g_file_new_for_path(initial_folder);
		gtk_file_dialog_set_initial_folder(dialog, folder);
		}
	else
		{
		g_autoptr(GFile) folder = g_file_new_for_path(layout_get_path(get_current_layout()));
		gtk_file_dialog_set_initial_folder(dialog, folder);
		}

	if (fdd.action == FileDialogAction::SAVE && fdd.suggested_name)
		{
		gtk_file_dialog_set_initial_name(dialog, fdd.suggested_name);
		}

	return dialog;
}

void pending_file_dialog_free(PendingFileDialog *pending)
{
	g_free(pending);
}

void file_dialog_finish_cb(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	auto *pending = static_cast<PendingFileDialog *>(user_data);
	auto *dialog = GTK_FILE_DIALOG(source_object);

	g_autoptr(GError) error = nullptr;
	g_autoptr(GFile) file = nullptr;

	switch (pending->action)
		{
		case FileDialogAction::OPEN:
			file = gtk_file_dialog_open_finish(dialog, result, &error);
			break;
		case FileDialogAction::SAVE:
			file = gtk_file_dialog_save_finish(dialog, result, &error);
			break;
		case FileDialogAction::SELECT_FOLDER:
			file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
			break;
		}

	if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
		g_printerr("file dialog failed: %s\n", error->message);
		}

	pending->callback(file, pending->data);
	pending_file_dialog_free(pending);
}

} // namespace

void file_dialog_show(const FileDialogData &fdd)
{
	GtkWindow *parent = fdd.parent;
	if (!parent)
		{
		parent = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
		}

	g_autoptr(GtkFileDialog) dialog = create_file_dialog(fdd);

	auto *pending = g_new0(PendingFileDialog, 1);
	pending->action = fdd.action;
	pending->callback = fdd.callback;
	pending->data = fdd.data;

	switch (fdd.action)
		{
		case FileDialogAction::OPEN:
			gtk_file_dialog_open(dialog, parent, nullptr, file_dialog_finish_cb, pending);
			break;
		case FileDialogAction::SAVE:
			gtk_file_dialog_save(dialog, parent, nullptr, file_dialog_finish_cb, pending);
			break;
		case FileDialogAction::SELECT_FOLDER:
			gtk_file_dialog_select_folder(dialog, parent, nullptr, file_dialog_finish_cb, pending);
			break;
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
