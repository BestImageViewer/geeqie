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

#ifndef FILEDATA_H
#define FILEDATA_H

#include <sys/types.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include <config.h>

#include "typedefs.h"

struct ExifData;
struct HistMap;

#ifdef DEBUG
#define DEBUG_FILEDATA
#endif

#define FD_MAGICK 0x12345678u

gchar *text_from_size(gint64 size);
gchar *text_from_size_abrev(gint64 size);
const gchar *text_from_time(time_t t);

enum FileDataChangeType {
	FILEDATA_CHANGE_DELETE,
	FILEDATA_CHANGE_MOVE,
	FILEDATA_CHANGE_RENAME,
	FILEDATA_CHANGE_COPY,
	FILEDATA_CHANGE_UNSPECIFIED,
	FILEDATA_CHANGE_WRITE_METADATA
};

enum NotifyPriority {
	NOTIFY_PRIORITY_HIGH = 0,
	NOTIFY_PRIORITY_MEDIUM,
	NOTIFY_PRIORITY_LOW
};

enum SelectionType {
	SELECTION_NONE		= 0,
	SELECTION_SELECTED	= 1 << 0,
	SELECTION_PRELIGHT	= 1 << 1,
	SELECTION_FOCUS		= 1 << 2
};

struct FileDataChangeInfo {
	FileDataChangeType type;
	gchar *source;
	gchar *dest;
	gint error;
	gboolean regroup_when_finished;
};

struct FileData {
	guint magick;
	gint type;
	gchar *original_path; /**< key to file_data_pool hash table */
	gchar *path;
	const gchar *name;
	const gchar *extension;
	gchar *extended_extension;
	FileFormatClass format_class;
	gchar *format_name; /**< set by the image loader */
	gchar *collate_key_name;
	gchar *collate_key_name_nocase;
	gchar *collate_key_name_natural;
	gchar *collate_key_name_nocase_natural;
	gint64 size;
	time_t date;
	time_t cdate;
	mode_t mode; /**< this is needed at least for notification in view_dir because it is preserved after the file/directory is deleted */
	gint sidecar_priority;

	guint marks; /**< each bit represents one mark */
	guint valid_marks; /**< zero bit means that the corresponding mark needs to be reread */


	GList *sidecar_files;
	FileData *parent; /**< parent file if this is a sidecar file, NULL otherwise */
	FileDataChangeInfo *change; /**< for rename, move ... */
	GdkPixbuf *thumb_pixbuf;

	GdkPixbuf *pixbuf; /**< full-size image, only complete images, NULL during loading
			      all FileData with non-NULL pixbuf are referenced by image_cache */

	HistMap *histmap;

	gboolean locked;
	gint ref;
	gint version; /**< increased when any field in this structure is changed */
	gboolean disable_grouping;

	gint user_orientation;
	gint exif_orientation;

	ExifData *exif;
	time_t exifdate;
	time_t exifdate_digitized;
	GHashTable *modified_xmp; /**< hash table which contains unwritten xmp metadata in format: key->list of string values */
	GList *cached_metadata;
	gint rating;
	gboolean metadata_in_idle_loaded;

	gchar *owner;
	gchar *group;
	gchar *sym_link;

	SelectionType selected;  /**< Used by view-file-icon. */

	gint page_num;
	gint page_total;
};

/**
 * @headerfile file_data_new_group
 * scan for sidecar files - expensive
 */
FileData *file_data_new_group(const gchar *path_utf8);

/**
 * @headerfile file_data_new_no_grouping
 * should be used on helper files which can't have sidecars
 */
FileData *file_data_new_no_grouping(const gchar *path_utf8);

/**
 * @headerfile file_data_new_dir
 * should be used on dirs
 */
FileData *file_data_new_dir(const gchar *path_utf8);

FileData *file_data_new_simple(const gchar *path_utf8);

#ifdef DEBUG_FILEDATA
FileData *file_data_ref_debug(const gchar *file, gint line, FileData *fd);
void file_data_unref_debug(const gchar *file, gint line, FileData *fd);
#define file_data_ref(fd) file_data_ref_debug(__FILE__, __LINE__, fd)
#define file_data_unref(fd) file_data_unref_debug(__FILE__, __LINE__, fd)
#else
FileData *file_data_ref(FileData *fd);
void file_data_unref(FileData *fd);
#endif

void file_data_lock(FileData *fd);
void file_data_unlock(FileData *fd);
void file_data_lock_list(GList *list);
void file_data_unlock_list(GList *list);

gboolean file_data_check_changed_files(FileData *fd);

void file_data_increment_version(FileData *fd);

gboolean file_data_add_change_info(FileData *fd, FileDataChangeType type, const gchar *src, const gchar *dest);
void file_data_change_info_free(FileDataChangeInfo *fdci, FileData *fd);

void file_data_disable_grouping(FileData *fd, gboolean disable);
void file_data_disable_grouping_list(GList *fd_list, gboolean disable);

gint filelist_sort_compare_filedata(FileData *fa, FileData *fb);
gint filelist_sort_compare_filedata_full(FileData *fa, FileData *fb, SortType method, gboolean ascend);
GList *filelist_sort(GList *list, SortType method, gboolean ascend, gboolean case_sensitive);
GList *filelist_sort_full(GList *list, SortType method, gboolean ascend, gboolean case_sensitive, GCompareFunc cb);
GList *filelist_insert_sort_full(GList *list, gpointer data, SortType method, gboolean ascend, gboolean case_sensitive, GCompareFunc cb);

gboolean filelist_read(FileData *dir_fd, GList **files, GList **dirs);
gboolean filelist_read_lstat(FileData *dir_fd, GList **files, GList **dirs);
void filelist_free(GList *list);
GList *filelist_copy(GList *list);
GList *filelist_from_path_list(GList *list);
GList *filelist_to_path_list(GList *list);

GList *filelist_filter(GList *list, gboolean is_dir_list);

GList *filelist_sort_path(GList *list);
GList *filelist_recursive(FileData *dir_fd);
GList *filelist_recursive_full(FileData *dir_fd, SortType method, gboolean ascend, gboolean case_sensitive);

using FileDataGetMarkFunc = gboolean (*)(FileData *, gint, gpointer);
using FileDataSetMarkFunc = gboolean (*)(FileData *, gint, gboolean, gpointer);
gboolean file_data_register_mark_func(gint n, FileDataGetMarkFunc get_mark_func, FileDataSetMarkFunc set_mark_func, gpointer data, GDestroyNotify notify);
void file_data_get_registered_mark_func(gint n, FileDataGetMarkFunc *get_mark_func, FileDataSetMarkFunc *set_mark_func, gpointer *data);


gboolean file_data_get_mark(FileData *fd, gint n);
guint file_data_get_marks(FileData *fd);
void file_data_set_mark(FileData *fd, gint n, gboolean value);
gboolean file_data_filter_marks(FileData *fd, guint filter);
GList *file_data_filter_marks_list(GList *list, guint filter);

gboolean file_data_filter_file_filter(FileData *fd, GRegex *filter);
GList *file_data_filter_file_filter_list(GList *list, GRegex *filter);

GList *file_data_filter_class_list(GList *list, guint filter);

gchar *file_data_sc_list_to_string(FileData *fd);

gchar *file_data_get_sidecar_path(FileData *fd, gboolean existing_only);


gboolean file_data_add_ci(FileData *fd, FileDataChangeType type, const gchar *src, const gchar *dest);
gboolean file_data_sc_add_ci_copy(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_add_ci_move(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_add_ci_rename(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_add_ci_delete(FileData *fd);
gboolean file_data_sc_add_ci_unspecified(FileData *fd, const gchar *dest_path);

gboolean file_data_sc_add_ci_delete_list(GList *fd_list);
gboolean file_data_sc_add_ci_copy_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_add_ci_move_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_add_ci_rename_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_add_ci_unspecified_list(GList *fd_list, const gchar *dest);
gboolean file_data_add_ci_write_metadata_list(GList *fd_list);

gboolean file_data_sc_update_ci_copy_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_update_ci_move_list(GList *fd_list, const gchar *dest);
gboolean file_data_sc_update_ci_unspecified_list(GList *fd_list, const gchar *dest);


gboolean file_data_sc_update_ci_copy(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_update_ci_move(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_update_ci_rename(FileData *fd, const gchar *dest_path);
gboolean file_data_sc_update_ci_unspecified(FileData *fd, const gchar *dest_path);

gchar *file_data_get_error_string(gint error);

gint file_data_verify_ci(FileData *fd, GList *list);
gint file_data_verify_ci_list(GList *list, gchar **desc, gboolean with_sidecars);

gboolean file_data_perform_ci(FileData *fd);
gboolean file_data_apply_ci(FileData *fd);
void file_data_free_ci(FileData *fd);
void file_data_free_ci_list(GList *fd_list);

void file_data_set_regroup_when_finished(FileData *fd, gboolean enable);

gint file_data_sc_verify_ci(FileData *fd, GList *list);

gboolean file_data_sc_perform_ci(FileData *fd);
gboolean file_data_sc_apply_ci(FileData *fd);
void file_data_sc_free_ci(FileData *fd);
void file_data_sc_free_ci_list(GList *fd_list);

GList *file_data_process_groups_in_selection(GList *list, gboolean ungroup, GList **ungrouped);


using FileDataNotifyFunc = void (*)(FileData *, NotifyType, gpointer);
gboolean file_data_register_notify_func(FileDataNotifyFunc func, gpointer data, NotifyPriority priority);
gboolean file_data_unregister_notify_func(FileDataNotifyFunc func, gpointer data);
void file_data_send_notification(FileData *fd, NotifyType type);

gboolean file_data_register_real_time_monitor(FileData *fd);
gboolean file_data_unregister_real_time_monitor(FileData *fd);

void read_exif_time_data(FileData *file);
void read_exif_time_digitized_data(FileData *file);

gboolean marks_list_save(gchar *path, gboolean save);
gboolean marks_list_load(const gchar *path);
void marks_clear_all();
void read_rating_data(FileData *file);

void file_data_inc_page_num(FileData *fd);
void file_data_dec_page_num(FileData *fd);
void file_data_set_page_total(FileData *fd, gint page_total);
void file_data_set_page_num(FileData *fd, gint page_num);

void file_data_dump();
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
