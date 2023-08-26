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

#ifndef EDITORS_H
#define EDITORS_H

struct FileData;

enum EditorFlags {
	EDITOR_KEEP_FS            = 0x00000001,
	EDITOR_VERBOSE            = 0x00000002,
	EDITOR_VERBOSE_MULTI      = 0x00000004,
	EDITOR_TERMINAL		  = 0x00000008,

	EDITOR_DEST               = 0x00000100,
	EDITOR_FOR_EACH           = 0x00000200,
	EDITOR_SINGLE_COMMAND     = 0x00000400,
	EDITOR_NO_PARAM           = 0x00000800,
	/**< below are errors */
	EDITOR_ERROR_EMPTY        = 0x00020000,
	EDITOR_ERROR_SYNTAX       = 0x00040000,
	EDITOR_ERROR_INCOMPATIBLE = 0x00080000,
	EDITOR_ERROR_NO_FILE      = 0x00100000,
	EDITOR_ERROR_CANT_EXEC    = 0x00200000,
	EDITOR_ERROR_STATUS       = 0x00400000,
	EDITOR_ERROR_SKIPPED      = 0x00800000,
	/**< mask to match errors only */
	EDITOR_ERROR_MASK         = ~0xffff,
};

struct EditorDescription {
	gchar *key; 		/**< desktop file name, not including path, including extension */
	gchar *name; 		/**< Name, localized name presented to user */
	gchar *icon;		/**< Icon */
	gchar *exec;		/**< Exec */
	gchar *menu_path;
	gchar *hotkey;
	GList *ext_list;
	gchar *file;
	gchar *comment;		/**< .desktop Comment key, used to show a tooltip */
	EditorFlags flags;
	gboolean hidden;	/**< explicitly hidden, shown in configuration dialog */
	gboolean ignored;	/**< not interesting, do not show at all */
	gboolean disabled;	/**< display disabled by user */
};

#define EDITOR_ERRORS(flags) ((flags) & EDITOR_ERROR_MASK)
#define EDITOR_ERRORS_BUT_SKIPPED(flags) (!!(((flags) & EDITOR_ERROR_MASK) && !((flags) & EDITOR_ERROR_SKIPPED)))


/**
 * @note EDITOR_CB_*:
 * Return values from callback function
 */
enum {
	EDITOR_CB_CONTINUE = 0, /**< continue multiple editor execution on remaining files*/
	EDITOR_CB_SKIP,         /**< skip the remaining files */
	EDITOR_CB_SUSPEND       /**< suspend execution, one of editor_resume or editor_skip
				   must be called later */
};

enum {
	DESKTOP_FILE_COLUMN_KEY,
	DESKTOP_FILE_COLUMN_DISABLED,
	DESKTOP_FILE_COLUMN_NAME,
	DESKTOP_FILE_COLUMN_HIDDEN,
	DESKTOP_FILE_COLUMN_WRITABLE,
	DESKTOP_FILE_COLUMN_PATH,
	DESKTOP_FILE_COLUMN_COUNT
};

extern GtkListStore *desktop_file_list;


extern GHashTable *editors;

void editor_table_finish();
void editor_table_clear();
GList *editor_get_desktop_files();
gboolean editor_read_desktop_file(const gchar *path);

GList *editor_list_get();


/**
 * @typedef EditorCallback
 *
 * Callback is called even on skipped files, with the #EDITOR_ERROR_SKIPPED flag set.
 * It is a good place to call file_data_change_info_free().
 *
 * @param ed - pointer that can be used for editor_resume/editor_skip or NULL if all files were already processed \n
 * @param flags - flags above \n
 * @param list - list of processed #FileData structures, typically single file or whole list passed to start_editor_* @n
 * @param data - generic pointer
*/
using EditorCallback = gint (*)(gpointer, EditorFlags, GList *, gpointer);


void editor_resume(gpointer ed);
void editor_skip(gpointer ed);



EditorFlags start_editor(const gchar *key, const gchar *working_directory);
EditorFlags start_editor_from_file(const gchar *key, FileData *fd);
EditorFlags start_editor_from_filelist(const gchar *key, GList *list);
EditorFlags start_editor_from_file_full(const gchar *key, FileData *fd, EditorCallback cb, gpointer data);
EditorFlags start_editor_from_filelist_full(const gchar *key, GList *list, const gchar *working_directory, EditorCallback cb, gpointer data);
gboolean editor_window_flag_set(const gchar *key);
gboolean editor_is_filter(const gchar *key);
gboolean editor_no_param(const gchar *key);
const gchar *editor_get_error_str(EditorFlags flags);

gboolean is_valid_editor_command(const gchar *key);
gboolean editor_blocks_file(const gchar *key);

EditorFlags editor_command_parse(const EditorDescription *editor, GList *list, gboolean consider_sidecars, gchar **output);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
