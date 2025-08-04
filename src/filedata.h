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

#include <memory>
#include <mutex>
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

enum NotifyType : gint {
	NOTIFY_MARKS		= 1 << 1, /**< changed marks */
	NOTIFY_PIXBUF		= 1 << 2, /**< image was read into fd->pixbuf */
	NOTIFY_HISTMAP		= 1 << 3, /**< histmap was read into fd->histmap */
	NOTIFY_ORIENTATION	= 1 << 4, /**< image was rotated */
	NOTIFY_METADATA		= 1 << 5, /**< changed image metadata, not yet written */
	NOTIFY_GROUPING		= 1 << 6, /**< change in fd->sidecar_files or fd->parent */
	NOTIFY_REREAD		= 1 << 7, /**< changed file size, date, etc., file name remains unchanged */
	NOTIFY_CHANGE		= 1 << 8  /**< generic change described by fd->change */
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

class FileDataContext
{
    public:
	FileDataContext() :
		file_data_pool(g_hash_table_new(g_str_hash, g_str_equal)),
		planned_change_map(g_hash_table_new(g_str_hash, g_str_equal))
		{ }

	FileDataContext(FileDataContext &) = delete;  // Not copyable.
	FileDataContext &operator=(const FileDataContext &) = delete;  // Not assignable.
	~FileDataContext()
		{
		g_hash_table_destroy(planned_change_map);
		g_hash_table_destroy(file_data_pool);
		}

#ifdef DEBUG_FILEDATA
	gint global_file_data_count = 0;
#endif
	GHashTable *file_data_pool;
	GHashTable *planned_change_map;
};

class GlobalFileDataContext
{
    public:
	static GlobalFileDataContext &get_instance();

	FileDataContext &context() { return context_; }

    private:
	static std::mutex s_instance_mutex;
	// TODO(xsdg): Maybe use mutex annotations from absl?  clang has them but gcc doesn't.
	// GUARDED_BY(s_instance_mutex);
	static std::unique_ptr<GlobalFileDataContext> s_instance;
	FileDataContext context_;
};

class FileData {
    private:
	FileData() = delete;

	// TODO(xsdg): Switch FileData to using new/delete, so that FileDataContext
	// can be a non-const reference that's set by an initializer.
	FileDataContext *context = nullptr;
	static FileDataContext *DefaultFileDataContext()
		{
		return &(GlobalFileDataContext::get_instance().context());
		}

    public:
	// Child classes that encapsulate some functionality.
	class FileList;

	// Public members from the original API
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

	SelectionType selected;  /**< Used by view-file-icon. */

	gint page_num;
	gint page_total;

	static gchar *text_from_size(gint64 size);
	static gchar *text_from_size_abrev(gint64 size);
	static const gchar *text_from_time(time_t t);

	/**
	 * @headerfile file_data_new_group
	 * scan for sidecar files - expensive
	 */
	static FileData *file_data_new_group(const gchar *path_utf8, FileDataContext *context = nullptr);

	/**
	 * @headerfile file_data_new_no_grouping
	 * should be used on helper files which can't have sidecars
	 */
	static FileData *file_data_new_no_grouping(const gchar *path_utf8, FileDataContext *context = nullptr);

	/**
	 * @headerfile file_data_new_dir
	 * should be used on dirs
	 */
	static FileData *file_data_new_dir(const gchar *path_utf8, FileDataContext *context = nullptr);

	static FileData *file_data_new_simple(const gchar *path_utf8, FileDataContext *context = nullptr);

#ifdef DEBUG_FILEDATA
	FileData *file_data_ref(const gchar *file = __builtin_FILE(), gint line = __builtin_LINE());
	void file_data_unref(const gchar *file = __builtin_FILE(), gint line = __builtin_LINE());
#else
	FileData *file_data_ref();
	void file_data_unref();
#endif

	void file_data_lock(FileData *fd);
	void file_data_unlock(FileData *fd);
	static void file_data_lock_list(GList *list);
	static void file_data_unlock_list(GList *list);

	gboolean file_data_check_changed_files(FileData *fd);

	void file_data_increment_version(FileData *fd);

	void file_data_change_info_free(FileDataChangeInfo *fdci, FileData *fd);

	void file_data_disable_grouping(FileData *fd, gboolean disable);
	static void file_data_disable_grouping_list(GList *fd_list, gboolean disable);

	using GetMarkFunc = gboolean (*)(FileData *, gint, gpointer);
	using SetMarkFunc = gboolean (*)(FileData *, gint, gboolean, gpointer);
	static gboolean file_data_register_mark_func(gint n, GetMarkFunc get_mark_func, SetMarkFunc set_mark_func, gpointer data, GDestroyNotify notify);
	static void file_data_get_registered_mark_func(gint n, GetMarkFunc *get_mark_func, SetMarkFunc *set_mark_func, gpointer *data);


	gboolean file_data_get_mark(FileData *fd, gint n);
	guint file_data_get_marks(FileData *fd);
	void file_data_set_mark(FileData *fd, gint n, gboolean value);
	gboolean file_data_filter_marks(FileData *fd, guint filter);
	static GList *file_data_filter_marks_list(GList *list, guint filter);

	gboolean file_data_mark_to_selection(FileData *fd, gint mark, MarkToSelectionMode mode, gboolean selected);
	void file_data_selection_to_mark(FileData *fd, gint mark, SelectionToMarkMode mode);

	gboolean file_data_filter_file_filter(FileData *fd, const GRegex *filter);
	static GList *file_data_filter_file_filter_list(GList *list, const GRegex *filter);

	static GList *file_data_filter_class_list(GList *list, guint filter);

	gchar *file_data_sc_list_to_string(FileData *fd);

	gchar *file_data_get_sidecar_path(FileData *fd, gboolean existing_only);


	gboolean file_data_add_ci(FileData *fd, FileDataChangeType type, const gchar *src, const gchar *dest);
	gboolean file_data_sc_add_ci_copy(FileData *fd, const gchar *dest_path);
	gboolean file_data_sc_add_ci_move(FileData *fd, const gchar *dest_path);
	gboolean file_data_sc_add_ci_rename(FileData *fd, const gchar *dest_path);
	gboolean file_data_sc_add_ci_delete(FileData *fd);
	gboolean file_data_sc_add_ci_unspecified(FileData *fd, const gchar *dest_path);

	static gboolean file_data_sc_add_ci_delete_list(GList *fd_list);
	static gboolean file_data_sc_add_ci_copy_list(GList *fd_list, const gchar *dest);
	static gboolean file_data_sc_add_ci_move_list(GList *fd_list, const gchar *dest);
	static gboolean file_data_sc_add_ci_rename_list(GList *fd_list, const gchar *dest);
	static gboolean file_data_sc_add_ci_unspecified_list(GList *fd_list, const gchar *dest);
	gboolean file_data_add_ci_write_metadata(FileData *fd);
	static gboolean file_data_add_ci_write_metadata_list(GList *fd_list);

	static gboolean file_data_sc_update_ci_copy_list(GList *fd_list, const gchar *dest);
	static gboolean file_data_sc_update_ci_move_list(GList *fd_list, const gchar *dest);
	static gboolean file_data_sc_update_ci_unspecified_list(GList *fd_list, const gchar *dest);


	gboolean file_data_sc_update_ci_copy(FileData *fd, const gchar *dest_path);
	gboolean file_data_sc_update_ci_move(FileData *fd, const gchar *dest_path);
	gboolean file_data_sc_update_ci_rename(FileData *fd, const gchar *dest_path);
	gboolean file_data_sc_update_ci_unspecified(FileData *fd, const gchar *dest_path);

	static gchar *file_data_get_error_string(gint error);

	gint file_data_verify_ci(FileData *fd, GList *list);
	static gint file_data_verify_ci_list(GList *list, gchar **desc, gboolean with_sidecars);

	gboolean file_data_perform_ci(FileData *fd);
	gboolean file_data_apply_ci(FileData *fd);
	void file_data_free_ci(FileData *fd);
	static void file_data_free_ci_list(GList *fd_list);

	void file_data_set_regroup_when_finished(FileData *fd, gboolean enable);

	gint file_data_sc_verify_ci(FileData *fd, GList *list);

	gboolean file_data_sc_perform_ci(FileData *fd);
	gboolean file_data_sc_apply_ci(FileData *fd);
	void file_data_sc_free_ci(FileData *fd);
	static void file_data_sc_free_ci_list(GList *fd_list);

	static GList *file_data_process_groups_in_selection(GList *list, gboolean ungroup, GList **ungrouped);


	using NotifyFunc = void (*)(FileData *, NotifyType, gpointer);
	static gboolean file_data_register_notify_func(NotifyFunc func, gpointer data, NotifyPriority priority);
	static gboolean file_data_unregister_notify_func(NotifyFunc func, gpointer data);
	void file_data_send_notification(FileData *fd, NotifyType type);

	gboolean file_data_register_real_time_monitor(FileData *fd);
	gboolean file_data_unregister_real_time_monitor(FileData *fd);

	void read_exif_time_data(FileData *file);
	void read_exif_time_digitized_data(FileData *file);

	static gboolean marks_list_save(gchar *path, gboolean save);
	static gboolean marks_list_load(const gchar *path);
	static void marks_clear_all();
	void read_rating_data(FileData *file);

	void file_data_inc_page_num(FileData *fd);
	void file_data_dec_page_num(FileData *fd);
	void file_data_set_page_total(FileData *fd, gint page_total);
	void file_data_set_page_num(FileData *fd, gint page_num);

	static void file_data_dump();

    protected:
	static FileData *file_data_new(const gchar *path_utf8, struct stat *st, gboolean disable_sidecars, FileDataContext *context = nullptr);
	static FileData *file_data_new_local(const gchar *path, struct stat *st, gboolean disable_sidecars, FileDataContext *context = nullptr);

	static GHashTable *file_data_basename_hash_new();
	static GList *file_data_basename_hash_insert(GHashTable *basename_hash, FileData *fd);
	static void file_data_basename_hash_insert_cb(gpointer fd, gpointer basename_hash);
	static void file_data_basename_hash_remove_list(gpointer, gpointer value, gpointer);
	static void file_data_basename_hash_free(GHashTable *basename_hash);
	static void file_data_basename_hash_to_sidecars(gpointer, gpointer value, gpointer);
	static void file_data_free(FileData *fd);
	static void file_data_consider_free(FileData *fd);
	static void file_data_update_ci_dest(FileData *fd, const gchar *dest_path);
	static void file_data_update_ci_dest_preserve_ext(FileData *fd, const gchar *dest_path);
	static void file_data_sc_update_ci(FileData *fd, const gchar *dest_path);
	static gboolean file_data_sc_check_update_ci(FileData *fd, const gchar *dest_path, FileDataChangeType type);


	// static methods that have already been switched to C++-style.
	void set_path(const gchar *new_path);
	void planned_change_remove();
	void update_planned_change_hash(const gchar *old_path, gchar *new_path);
};

class FileData::FileList
{
    private:
	FileList() = delete;
	friend class FileData;  // Allows FileData to access protected API.

    public:
	// Note that this struct will be moved to a new Util class in a subsequent commit.
	struct SortSettings
	{
		bool operator==(const SortSettings &other)
		{
			return method == other.method
			    && ascending == other.ascending
			    && case_sensitive == other.case_sensitive;
		}

		SortType method = SORT_NONE;
		gboolean ascending = TRUE;
		gboolean case_sensitive = TRUE;
	};

	static gint sort_compare_filedata(const FileData *fa, const FileData *fb, SortSettings *settings);
	static gint sort_compare_filedata_full(const FileData *fa, const FileData *fb, SortType method, gboolean ascend);
	static GList *sort(GList *list, SortSettings settings);
	static GList *sort_full(GList *list, SortSettings settings, GCompareDataFunc cb);

	static gboolean read_list(FileData *dir_fd, GList **files, GList **dirs);
	static gboolean read_list_lstat(FileData *dir_fd, GList **files, GList **dirs);
	static void free_list(GList *list);
	static GList *copy(GList *list);
	static GList *from_path_list(GList *list);
	static GList *to_path_list(GList *list);

	static bool has_dir(GList *list);
	static GList *filter(GList *list, gboolean is_dir_list);

	static GList *sort_path(GList *list);
	static GList *recursive(FileData *dir_fd);
	static GList *recursive_full(FileData *dir_fd, SortSettings settings);

    protected:
	static GList *filter_out_sidecars(GList *flist);
	static gboolean is_hidden_file(const gchar *filepath);
	static gboolean read_list_real(const gchar *dir_path, GList **files, GList **dirs, gboolean follow_symlinks);
	static gint sort_file_cb(gconstpointer a, gconstpointer b, gpointer data);
	static gint sort_path_cb(gconstpointer a, gconstpointer b);
	static void recursive_append(GList **list, GList *dirs);
	static void recursive_append_full(GList **list, GList *dirs, SortSettings settings);
};

/**
 * @class FileDataRef
 * @brief RAII object that holds a ref to the specified FileData.
 */
class FileDataRef
{
    public:
	explicit FileDataRef(FileData &fd, gboolean skip_ref = FALSE);
	FileDataRef(FileDataRef &) = delete;  // Not copyable.
	FileDataRef &operator=(const FileDataRef &) = delete;  // Not assignable.
	~FileDataRef();

    private:
	FileData &fd_;
};

// C-style compatibility API.
gchar *text_from_size(gint64 size);
gchar *text_from_size_abrev(gint64 size);
const gchar *text_from_time(time_t t);

FileData *file_data_new_group(const gchar *path_utf8);

FileData *file_data_new_no_grouping(const gchar *path_utf8);

FileData *file_data_new_dir(const gchar *path_utf8);

FileData *file_data_new_simple(const gchar *path_utf8);

#ifdef DEBUG_FILEDATA
FileData *file_data_ref(FileData *fd, const gchar *file = __builtin_FILE(), gint line = __builtin_LINE());
void file_data_unref(FileData *fd, const gchar *file = __builtin_FILE(), gint line = __builtin_LINE());
#else
FileData *file_data_ref(FileData *fd);
void file_data_unref(FileData *fd);
#endif

using FileDataList = GList; // element is FileData

void file_data_lock(FileData *fd);
void file_data_unlock(FileData *fd);
void file_data_lock_list(GList *list);
void file_data_unlock_list(GList *list);

gboolean file_data_check_changed_files(FileData *fd);

void file_data_increment_version(FileData *fd);

void file_data_change_info_free(FileDataChangeInfo *fdci, FileData *fd);

void file_data_disable_grouping(FileData *fd, gboolean disable);
void file_data_disable_grouping_list(GList *fd_list, gboolean disable);

gint filelist_sort_compare_filedata(const FileData *fa, const FileData *fb, FileData::FileList::SortSettings *settings);
gint filelist_sort_compare_filedata_full(const FileData *fa, const FileData *fb, SortType method, gboolean ascend);
GList *filelist_sort(GList *list, FileData::FileList::SortSettings settings);
GList *filelist_sort_full(GList *list, FileData::FileList::SortSettings settings, GCompareDataFunc cb);

gboolean filelist_read(FileData *dir_fd, GList **files, GList **dirs);
gboolean filelist_read_lstat(FileData *dir_fd, GList **files, GList **dirs);

void file_data_list_free(FileDataList *list);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FileDataList, file_data_list_free)

GList *filelist_copy(GList *list);
GList *filelist_from_path_list(GList *list);
GList *filelist_to_path_list(GList *list);

bool file_data_list_has_dir(FileDataList *list);
GList *filelist_filter(GList *list, gboolean is_dir_list);

GList *filelist_sort_path(GList *list);
GList *filelist_recursive(FileData *dir_fd);
GList *filelist_recursive_full(FileData *dir_fd, FileData::FileList::SortSettings settings);

gboolean file_data_register_mark_func(gint n, FileData::GetMarkFunc get_mark_func, FileData::SetMarkFunc set_mark_func, gpointer data, GDestroyNotify notify);
void file_data_get_registered_mark_func(gint n, FileData::GetMarkFunc *get_mark_func, FileData::SetMarkFunc *set_mark_func, gpointer *data);


gboolean file_data_get_mark(FileData *fd, gint n);
guint file_data_get_marks(FileData *fd);
void file_data_set_mark(FileData *fd, gint n, gboolean value);
gboolean file_data_filter_marks(FileData *fd, guint filter);
GList *file_data_filter_marks_list(GList *list, guint filter);

gboolean file_data_mark_to_selection(FileData *fd, gint mark, MarkToSelectionMode mode, gboolean selected);
void file_data_selection_to_mark(FileData *fd, gint mark, SelectionToMarkMode mode);

gboolean file_data_filter_file_filter(FileData *fd, const GRegex *filter);
GList *file_data_filter_file_filter_list(GList *list, const GRegex *filter);

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
gboolean file_data_add_ci_write_metadata(FileData *fd);
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


gboolean file_data_register_notify_func(FileData::NotifyFunc func, gpointer data, NotifyPriority priority);
gboolean file_data_unregister_notify_func(FileData::NotifyFunc func, gpointer data);
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
