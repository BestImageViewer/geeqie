/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UI_FILE_CHOOSER_H
#define UI_FILE_CHOOSER_H

#include <gtk/gtk.h>

enum class FileDialogAction
{
	OPEN,
	SAVE,
	SELECT_FOLDER
};

using FileDialogCallback = void (*)(GFile *file, gpointer data);

struct FileDialogData
{
	FileDialogCallback callback;
	FileDialogAction action;
	const gchar *accept_text;
	const gchar *filter_description;
	const gchar *history_key;
	const gchar *suggested_name;
	const gchar *title;
	const gchar *filename;
	const gchar *filter;
	gpointer data;
	GtkWindow *parent;
};

void file_dialog_show(const FileDialogData &fdd);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
