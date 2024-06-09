/*
 * Copyright (C) 2006 John Ellis
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

#ifndef UTILOPS_H
#define UTILOPS_H

#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

class FileData;
struct FileDialog;
struct GenericDialog;

void file_maint_renamed(FileData *fd);
void file_maint_moved(FileData *fd, GList *ignore_list);
void file_maint_copied(FileData *fd);

GenericDialog *file_util_gen_dlg(const gchar *title,
				 const gchar *role,
				 GtkWidget *parent, gint auto_close,
				 void (*cancel_cb)(GenericDialog *, gpointer), gpointer data);
FileDialog *file_util_file_dlg(const gchar *title,
			       const gchar *role,
			       GtkWidget *parent,
			       void (*cancel_cb)(FileDialog *, gpointer), gpointer data);
GenericDialog *file_util_warning_dialog(const gchar *heading, const gchar *message,
					const gchar *icon_name, GtkWidget *parent);

/* all functions takes over the filelist and frees it when done */

void file_util_delete(FileData *source_fd, GList *source_list, GtkWidget *parent);
void file_util_delete_notify_done(FileData *source_fd, GList *source_list, GtkWidget *parent, FileUtilDoneFunc done_func, gpointer done_data);
void file_util_move(FileData *source_fd, GList *source_list, const gchar *dest_path, GtkWidget *parent);
void file_util_copy(FileData *source_fd, GList *source_list, const gchar *dest_path, GtkWidget *parent);
void file_util_rename(FileData *source_fd, GList *source_list, GtkWidget *parent);
void file_util_write_metadata(FileData *source_fd, GList *source_list, GtkWidget *parent, gboolean force_dialog, FileUtilDoneFunc done_func, gpointer done_data);

void file_util_create_dir(const gchar *path, GtkWidget *parent, FileUtilDoneFunc done_func, gpointer done_data);

void file_util_rename_dir(FileData *source_fd, const gchar *new_path, GtkWidget *parent, FileUtilDoneFunc done_func, gpointer done_data);

/* these avoid the location entry dialog, list must be files only and
 * dest_path must be a valid directory path
*/
void file_util_move_simple(GList *list, const gchar *dest_path, GtkWidget *parent);
void file_util_copy_simple(GList *list, const gchar *dest_path, GtkWidget *parent);
void file_util_rename_simple(FileData *fd, const gchar *dest_path, GtkWidget *parent);

void file_util_start_editor_from_file(const gchar *key, FileData *fd, GtkWidget *parent);

/* working directory is used only as a fallback when the filelist is empty */
void file_util_start_editor_from_filelist(const gchar *key, GList *list, const gchar *working_directory, GtkWidget *parent);
void file_util_start_filter_from_filelist(const gchar *key, GList *list, const gchar *dest_path, GtkWidget *parent);

void file_util_delete_dir(FileData *source_fd, GtkWidget *parent);

void file_util_copy_path_to_clipboard(FileData *fd, gboolean quoted, gboolean action_copy);
void file_util_path_list_to_clipboard(GList *list, gboolean quoted, gboolean action_copy);

gchar *new_folder(GtkWindow *window, gchar *path);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
