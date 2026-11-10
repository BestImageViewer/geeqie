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

#include <functional>

#include <glib.h>
#include <gtk/gtk.h>

class FileData;
struct GenericDialog;

/** @addtogroup file_operations
 * @{
 */

enum class ClipboardAction {
	COPY,
	CUT,
};

void file_maint_renamed(FileData *fd);
void file_maint_moved(FileData *fd, GList *ignore_list);
void file_maint_copied(FileData *fd);

GenericDialog *file_util_gen_dlg(const gchar *title,
				 const gchar *role,
				 GtkWidget *parent, gint auto_close,
				 void (*cancel_cb)(GenericDialog *, gpointer), gpointer data);
GenericDialog *file_util_warning_dialog(const gchar *heading, const gchar *message,
					const gchar *icon_name, GtkWidget *parent);

/**
 * @file utilops.h
 * @ingroup file_operations
 *
 * User-facing operation entry points run on the GTK main thread. The delete,
 * copy, move, rename, metadata-writing, and editor/filter entry points consume
 * their input file lists, including the references held by the list elements,
 * even when setup fails. Pass filelist_copy() when retaining a selection.
 * A separate source_fd is borrowed and referenced internally as needed.
 * Destination strings are borrowed during the call and copied when retained.
 *
 * Operations can open dialogs and complete later, or finish during the call.
 * The parent widget provides presentation context; it is not a cancellation
 * token or a guarantee that callback captures stay alive. Callers must guard
 * access to views/windows that can close while an operation remains pending.
 * Cancel stops the remaining work where supported; it does not undo completed
 * filesystem changes. See file_tree_task_run() for threaded folder operations.
 * @see ownership_and_async
 */

/** Operation notification, copied by the entry point when retained.
 * Runs on the main thread, potentially before the initiating function returns.
 * The path is borrowed for this invocation and can be nullptr; copy it to keep it.
 * For the UtilityData workflow, TRUE means a non-CANCEL terminal phase (including
 * Discard), not proof that every requested file was processed successfully.
 * Early validation/setup failures can return without calling this function.
 * file_util_create_dir() has the narrower success-only contract documented below.
 */
using FileUtilDoneFunc = std::function<void(gboolean, const gchar *)>;

// force_dialog always confirms and displays full paths.
void file_util_delete(FileData *source_fd, GList *source_list, GtkWidget *parent, gboolean safe_delete, gboolean force_dialog = FALSE);
void file_util_delete_notify_done(FileData *source_fd, GList *source_list, GtkWidget *parent, gboolean safe_delete, const FileUtilDoneFunc &done_func);
void file_util_move(FileData *source_fd, GList *source_list, const gchar *dest_path, GtkWidget *parent);
void file_util_copy(FileData *source_fd, GList *source_list, const gchar *dest_path, GtkWidget *parent);
void file_util_rename(FileData *source_fd, GList *source_list, GtkWidget *parent);
void file_util_write_metadata(FileData *source_fd, GList *source_list, GtkWidget *parent, gboolean force_dialog, const FileUtilDoneFunc &done_func);

/** Show a folder chooser and copy the callback for later main-thread delivery.
 * The callback is invoked with TRUE and a borrowed selected folder path only
 * when an existing directory is selected. Cancelling/closing the chooser does
 * not invoke it; an empty callback is not supported by this entry point.
 */
void file_util_create_dir(const gchar *path, GtkWidget *parent, const FileUtilDoneFunc &done_func);

void file_util_rename_dir(FileData *source_fd, const gchar *new_path, GtkWidget *parent, const FileUtilDoneFunc &done_func);

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
/** Delete only this file, using the explicit trash mode and file confirmation preferences. */
void file_util_delete_single(FileData *source_fd, GtkWidget *parent, gboolean trash);
void file_util_delete_dir(FileData *source_fd, GtkWidget *parent, gboolean trash);

void file_util_copy_path_to_clipboard(FileData *fd, gboolean quoted, ClipboardAction action);
void file_util_path_list_to_clipboard(GList *list, gboolean quoted, ClipboardAction action);

gchar *new_folder(GtkWindow *window, gchar *path);
/** @} */

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
