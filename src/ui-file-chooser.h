/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UI_FILE_CHOOSER_H
#define UI_FILE_CHOOSER_H

#include <gtk/gtk.h>

struct FileChooserDialogData {
	GCallback response_callback;
	GtkFileChooserAction action;
	const gchar *accept_text;
	const gchar *entry_text;
	const gchar *entry_tooltip;
	const gchar *filter_description;
	const gchar *history_key;
	const gchar *suggested_name;
	const gchar *title;
	const gchar *filename;
	const gchar *filter;
	const gchar *shortcuts;
	gpointer data;
};

GtkFileChooserDialog *file_chooser_dialog_new(const FileChooserDialogData &fcdd);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
