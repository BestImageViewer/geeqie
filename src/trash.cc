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

#include "trash.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "editors.h"
#include "file-tree-task.h"
#include "filedata.h"
#include "filefilter.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "window.h"

/*
 *--------------------------------------------------------------------------
 * Safe Delete
 *--------------------------------------------------------------------------
 */

static std::mutex trash_mutex;

static gboolean file_util_trash_lock(std::unique_lock<std::mutex> &lock, FileTreeOperation *operation, GError **error)
{
	while (!lock.try_lock())
		{
		if (!operation)
			{
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, _("Another trash operation is in progress. Please try again."));
			return FALSE;
			}
		if (g_cancellable_set_error_if_cancelled(operation->cancellable, error)) return FALSE;
		g_usleep(10000);
		}
	return !operation || !g_cancellable_set_error_if_cancelled(operation->cancellable, error);
}

struct TrashEntry
{
	std::string path;
	guint64 size;
	time_t mtime;
	gboolean has_trashinfo;
};

TrashSettings file_util_trash_settings()
{
	return {options->file_ops.no_trash || !options->file_ops.safe_delete_enable,
	        options->file_ops.use_system_trash,
	        options->file_ops.safe_delete_path ? options->file_ops.safe_delete_path : "",
	        static_cast<guint64>(options->file_ops.safe_delete_folder_maxsize) * 1048576};
}

static gchar *file_util_safe_subdir(const gchar *name, const TrashSettings &settings)
{
	return g_build_filename(settings.path.c_str(), name, nullptr);
}

static gchar *file_util_safe_subdir(const gchar *name)
{
	return file_util_safe_subdir(name, file_util_trash_settings());
}

static gboolean file_util_safe_read_dir(const gchar *path, gboolean has_trashinfo, gboolean clear, std::vector<TrashEntry> &entries, const TrashSettings &settings, FileTreeOperation *operation)
{
	g_autofree gchar *path_fs = path_from_utf8(path);
	g_autoptr(GDir) dir = g_dir_open(path_fs, 0, nullptr);
	if (!dir) return !isdir(path);

	gboolean complete = TRUE;
	const gchar *name;
	while ((name = g_dir_read_name(dir)))
		{
		if (operation && g_cancellable_is_cancelled(operation->cancellable)) return FALSE;
		g_autofree gchar *entry_fs = g_build_filename(path_fs, name, nullptr);
		g_autofree gchar *entry_path = path_to_utf8(entry_fs);
		GStatBuf stat_buf;
		if (g_lstat(entry_fs, &stat_buf) != 0)
			{
			complete = FALSE;
			continue;
			}
		if (!has_trashinfo && !S_ISREG(stat_buf.st_mode) && !S_ISLNK(stat_buf.st_mode)) continue;
		g_autoptr(GFile) file = g_file_new_for_path(entry_fs);
		FileTreeStats stats;
		// An unreadable tree must not be partially purged to enforce the size limit.
		if (!file_tree_stats(file, stats, nullptr, operation))
			{
			complete = FALSE;
			if (!clear) continue;
			}
		if (has_trashinfo)
			{
			g_autofree gchar *info_dir = file_util_safe_subdir("info", settings);
			g_autofree gchar *info_name = g_strconcat(filename_from_path(entry_path), ".trashinfo", nullptr);
			g_autofree gchar *info_path = g_build_filename(info_dir, info_name, nullptr);
			GStatBuf info_stat;
			if (stat_utf8(info_path, &info_stat)) stat_buf.st_mtime = info_stat.st_mtime;
			}
		entries.push_back({entry_path, stats.bytes, stat_buf.st_mtime, has_trashinfo});
		}
	return complete;
}

static void file_util_safe_remove_info(const gchar *path, const TrashSettings &settings = file_util_trash_settings())
{
	g_autofree gchar *info_dir = file_util_safe_subdir("info", settings);
	g_autofree gchar *info_name = g_strconcat(filename_from_path(path), ".trashinfo", nullptr);
	g_autofree gchar *info_path = g_build_filename(info_dir, info_name, nullptr);
	if (isfile(info_path)) unlink_file(info_path);
}

static void file_util_safe_cleanup(guint64 incoming_size, gboolean clear, const TrashSettings &settings,
                                   FileTreeOperation *operation, TrashReport &report, GFile *protected_source = nullptr)
{
	std::vector<TrashEntry> entries;
	g_autofree gchar *files_dir = file_util_safe_subdir("files", settings);
	const gboolean files_complete = file_util_safe_read_dir(files_dir, TRUE, clear, entries, settings, operation);
	const gboolean legacy_complete = file_util_safe_read_dir(settings.path.c_str(), FALSE, clear, entries, settings, operation);
	if (!clear && (!files_complete || !legacy_complete))
		report.incomplete = TRUE;

	guint64 total = incoming_size;
	for (const auto &entry : entries) total += entry.size;

	std::sort(entries.begin(), entries.end(), [](const TrashEntry &a, const TrashEntry &b) { return a.mtime < b.mtime; });
	const guint64 limit = settings.limit;
	for (const auto &entry : entries)
		{
		if (operation && g_cancellable_is_cancelled(operation->cancellable)) break;
		if (!clear && (limit == 0 || total <= limit)) break;

		g_autofree gchar *entry_fs = path_from_utf8(entry.path.c_str());
		g_autoptr(GFile) trash_item = g_file_new_for_path(entry_fs);
		if (protected_source && (g_file_equal(trash_item, protected_source) || g_file_has_prefix(protected_source, trash_item))) continue;
		g_autoptr(GError) error = nullptr;
		if (rmdir_recursive(trash_item, operation ? operation->cancellable : nullptr, &error, operation))
			{
			if (entry.has_trashinfo) file_util_safe_remove_info(entry.path.c_str(), settings);
			total -= entry.size;
			report.removed_entries++;
			report.removed_bytes += entry.size;
			}
		else
			{
			report.cleanup_failed = TRUE;
			if (!report.cleanup_errors.empty()) report.cleanup_errors += "\n\n";
			report.cleanup_errors += entry.path;
			report.cleanup_errors += "\n";
			report.cleanup_errors += error ? error->message : _("Unknown error");
			}
		}
}

void file_util_trash_report(const TrashReport &report)
{
	if (report.incomplete)
		file_util_warning_dialog(_("Trash size is incomplete"), _("Some trash contents could not be read. The size limit may be exceeded."), GQ_ICON_DIALOG_WARNING, nullptr);
	if (report.cleanup_failed)
		{
		g_autofree gchar *message = report.cleanup_errors.empty() ?
		                          g_strdup(_("Another trash operation is in progress. No trash entries were removed by this operation.")) :
		                          g_strdup_printf(_("Unable to completely remove trash entries. Any remaining contents are at the locations below.\n\n%s"), report.cleanup_errors.c_str());
		file_util_warning_dialog(_("Trash cleanup failed"), message, GQ_ICON_DIALOG_WARNING, nullptr);
		}

	if (report.removed_entries > 0)
		{
		auto *app = g_application_get_default();
		if (app && g_application_get_is_registered(app))
			{
			g_autofree gchar *size = g_format_size_full(report.removed_bytes, G_FORMAT_SIZE_IEC_UNITS);
			g_autofree gchar *message = g_strdup_printf(_("Removed older trash entries to make room within the size limit.\nEntries permanently deleted: %u\nSpace reclaimed: %s"),
			                                          report.removed_entries, size);
			g_autoptr(GNotification) notification = g_notification_new(_("Geeqie trash cleanup"));
			g_notification_set_body(notification, message);
			g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
			g_application_send_notification(app, "trash-size-cleanup", notification);
			}
		}
}

static void file_util_trash_clear_full(const TrashSettings &settings, FileTreeOperation *operation, TrashReport &report)
{
	std::unique_lock<std::mutex> lock(trash_mutex, std::defer_lock);
	if (!file_util_trash_lock(lock, operation, nullptr))
		{
		report.cleanup_failed = !operation || !g_cancellable_is_cancelled(operation->cancellable);
		return;
		}
	file_util_safe_cleanup(0, TRUE, settings, operation, report);

	g_autofree gchar *info_dir = file_util_safe_subdir("info", settings);
	g_autoptr(GDir) dir = g_dir_open(info_dir, 0, nullptr);
	if (!dir) return;

	const gchar *name;
	while ((name = g_dir_read_name(dir)))
		{
		g_autofree gchar *path = g_build_filename(info_dir, name, nullptr);
		if (!g_str_has_suffix(name, ".trashinfo")) continue;
		g_autofree gchar *entry_name = g_strndup(name, strlen(name) - strlen(".trashinfo"));
		g_autofree gchar *files_dir = file_util_safe_subdir("files", settings);
		g_autofree gchar *entry = g_build_filename(files_dir, entry_name, nullptr);
		GStatBuf stat_buf;
		if (!lstat_utf8(entry, &stat_buf) && errno == ENOENT && isfile(path)) unlink_file(path);
		}
}

void file_util_trash_clear()
{
	TrashReport report;
	file_util_trash_clear_full(file_util_trash_settings(), nullptr, report);
	report.removed_entries = 0; // Manual clearing does not produce a size-limit notification.
	file_util_trash_report(report);
}

void file_util_trash_clear_async(GtkWidget *parent)
{
	const auto settings = file_util_trash_settings();
	auto report = std::make_shared<TrashReport>();
	file_tree_task_run(_("Clear trash"), parent,
		[settings, report](FileTreeOperation *operation, GError **error)
			{
			file_util_trash_clear_full(settings, operation, *report);
			return !g_cancellable_set_error_if_cancelled(operation->cancellable, error);
			},
		[report](gboolean, const GError *)
			{
			report->removed_entries = 0;
			file_util_trash_report(*report);
			});
}

static gchar *file_util_safe_dest(const gchar *path, const TrashSettings &settings)
{
	g_autofree gchar *files_dir = file_util_safe_subdir("files", settings);
	g_autofree gchar *info_dir = file_util_safe_subdir("info", settings);
	const gchar *basename = filename_from_path(path);
	const gchar *extension = strrchr(basename, '.');
	g_autofree gchar *stem = g_strndup(basename, strlen(basename) - (extension ? strlen(extension) : 0));
	for (guint n = 1; ; n++)
		{
		g_autofree gchar *name = n == 1 ? g_strdup(basename) : g_strdup_printf("%s.%u%s", stem, n, extension ? extension : "");
		g_autofree gchar *dest = g_build_filename(files_dir, name, nullptr);
		g_autofree gchar *info_name = g_strconcat(name, ".trashinfo", nullptr);
		g_autofree gchar *info = g_build_filename(info_dir, info_name, nullptr);
		GStatBuf stat_buf;
		if (!lstat_utf8(dest, &stat_buf) && !lstat_utf8(info, &stat_buf)) return g_steal_pointer(&dest);
		}
}

static gboolean file_util_safe_write_info(const gchar *source, const gchar *dest, const TrashSettings &settings)
{
	g_autofree gchar *info_dir = file_util_safe_subdir("info", settings);
	g_autofree gchar *info_name = g_strconcat(filename_from_path(dest), ".trashinfo", nullptr);
	g_autofree gchar *info_path = g_build_filename(info_dir, info_name, nullptr);
	g_autofree gchar *escaped_path = g_uri_escape_string(source, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, FALSE);
	g_autoptr(GDateTime) now = g_date_time_new_now_local();
	g_autofree gchar *date = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S");
	g_autofree gchar *contents = g_strdup_printf("[Trash Info]\nPath=%s\nDeletionDate=%s\n", escaped_path, date);
	g_autofree gchar *info_fs = path_from_utf8(info_path);
	g_autoptr(GFile) info_file = g_file_new_for_path(info_fs);
	// Reserve the metadata name exclusively; never overwrite another trash entry.
	g_autoptr(GFileOutputStream) stream = g_file_create(info_file, G_FILE_CREATE_PRIVATE, nullptr, nullptr);
	if (!stream) return FALSE;
	if (g_output_stream_write_all(G_OUTPUT_STREAM(stream), contents, strlen(contents), nullptr, nullptr, nullptr) &&
	    g_output_stream_close(G_OUTPUT_STREAM(stream), nullptr, nullptr)) return TRUE;
	g_file_delete(info_file, nullptr, nullptr);
	return FALSE;
}

gchar *file_util_safe_trash_original_path(const gchar *path)
{
	if (!path || !options->file_ops.safe_delete_path) return nullptr;

	g_autofree gchar *files_dir = file_util_safe_subdir("files");
	g_autofree gchar *canonical_files_dir = g_canonicalize_filename(files_dir, nullptr);
	g_autofree gchar *canonical_path = g_canonicalize_filename(path, nullptr);
	g_autofree gchar *path_dir = remove_level_from_path(canonical_path);
	if (!path_dir || g_strcmp0(path_dir, canonical_files_dir) != 0) return nullptr;

	g_autofree gchar *info_dir = file_util_safe_subdir("info");
	g_autofree gchar *info_name = g_strconcat(filename_from_path(canonical_path), ".trashinfo", nullptr);
	g_autofree gchar *info_path = g_build_filename(info_dir, info_name, nullptr);
	g_autoptr(GKeyFile) key_file = g_key_file_new();
	if (!g_key_file_load_from_file(key_file, info_path, G_KEY_FILE_NONE, nullptr)) return nullptr;

	g_autofree gchar *escaped_path = g_key_file_get_string(key_file, "Trash Info", "Path", nullptr);
	if (!escaped_path) return nullptr;

	gchar *original_path = g_uri_unescape_string(escaped_path, nullptr);
	if (!original_path || !g_path_is_absolute(original_path))
		{
		g_free(original_path);
		return nullptr;
		}

	return original_path;
}

static gboolean file_util_safe_trash_restore_to(const gchar *path, const gchar *dest_path, gboolean move, GtkWidget *parent)
{
	std::unique_lock<std::mutex> lock(trash_mutex, std::defer_lock);
	g_autoptr(GError) lock_error = nullptr;
	if (!file_util_trash_lock(lock, nullptr, &lock_error))
		{
		warning_dialog(_("Restore failed"), lock_error->message, GQ_ICON_DIALOG_WARNING, parent);
		return FALSE;
		}
	GStatBuf stat_buf;
	if (lstat_utf8(dest_path, &stat_buf))
		{
		g_autofree gchar *message = g_strdup_printf(_("The destination already contains an item named:\n%s"), dest_path);
		warning_dialog(_("Restore failed"), message, GQ_ICON_DIALOG_WARNING, parent);
		return FALSE;
		}

	g_autofree gchar *source_fs = path_from_utf8(path);
	g_autofree gchar *dest_fs = path_from_utf8(dest_path);
	g_autoptr(GFile) source = g_file_new_for_path(source_fs);
	g_autoptr(GFile) dest = g_file_new_for_path(dest_fs);
	g_autoptr(GError) error = nullptr;
	const gboolean success = move ? file_tree_move(source, dest, &error) : file_tree_copy(source, dest, &error);
	if (success)
		{
		if (move) file_util_safe_remove_info(path);
		return TRUE;
		}

	g_autofree gchar *message = g_strdup_printf(_("Unable to finish restoring to:\n%s\n\n%s"), dest_path, error ? error->message : _("Unknown error"));
	log_printf("Error: %s\n", message);
	warning_dialog(_("Restore failed"), message, GQ_ICON_DIALOG_WARNING, parent);
	return FALSE;
}

static void file_util_trash_notify_parent(const std::string &path)
{
	g_autofree gchar *parent = remove_level_from_path(path.c_str());
	if (!parent) return;
	auto fd = FileData::new_dir(parent);
	file_data_send_notification(fd, NOTIFY_REREAD);
}

static void file_util_safe_trash_restore_to_async(const gchar *path, const gchar *dest_path, gboolean move, GtkWidget *parent)
{
	const std::string source_path = path;
	const std::string destination_path = dest_path;
	const auto settings = file_util_trash_settings();
	file_tree_task_run(_("Restore from Trash"), parent,
		[source_path, destination_path, move, settings](FileTreeOperation *operation, GError **error)
			{
			std::unique_lock<std::mutex> lock(trash_mutex, std::defer_lock);
			if (!file_util_trash_lock(lock, operation, error)) return FALSE;
			g_autofree gchar *source_fs = path_from_utf8(source_path.c_str());
			g_autofree gchar *dest_fs = path_from_utf8(destination_path.c_str());
			g_autoptr(GFile) source = g_file_new_for_path(source_fs);
			g_autoptr(GFile) dest = g_file_new_for_path(dest_fs);
			const gboolean success = move ? file_tree_move(source, dest, error, operation) : file_tree_copy(source, dest, error, operation);
			if (success && move) file_util_safe_remove_info(source_path.c_str(), settings);
			return success;
			},
		[source_path, destination_path](gboolean success, const GError *error)
			{
			file_util_trash_notify_parent(source_path);
			file_util_trash_notify_parent(destination_path);
			if (!success)
				{
				g_autofree gchar *message = g_strdup_printf(_("Unable to finish restoring to:\n%s\n\n%s"),
				                                          destination_path.c_str(), error ? error->message : _("Unknown error"));
				warning_dialog(_("Restore failed"), message, GQ_ICON_DIALOG_WARNING, nullptr);
				}
			});
}

void file_util_safe_trash_restore_async(const gchar *path, gboolean move, GtkWidget *parent)
{
	g_autofree gchar *original = file_util_safe_trash_original_path(path);
	if (original) file_util_safe_trash_restore_to_async(path, original, move, parent);
	else warning_dialog(_("Restore failed"), _("The original location is unavailable. Use Restore to another folder."), GQ_ICON_DIALOG_WARNING, parent);
}

struct TrashRestoreData
{
	gchar *path;
	gchar *name;
	gboolean move;
};

static void file_util_safe_trash_restore_choose_cb(GFile *folder, gpointer data)
{
	auto *restore = static_cast<TrashRestoreData *>(data);
	if (folder)
		{
		g_autofree gchar *folder_fs = g_file_get_path(folder);
		g_autofree gchar *folder_path = path_to_utf8(folder_fs);
		g_autofree gchar *dest_path = g_build_filename(folder_path, restore->name, nullptr);
		file_util_safe_trash_restore_to_async(restore->path, dest_path, restore->move, nullptr);
		}
	g_free(restore->path);
	g_free(restore->name);
	g_free(restore);
}

void file_util_safe_trash_restore_choose(const gchar *path, gboolean move, GtkWidget *parent)
{
	g_autofree gchar *original = file_util_safe_trash_original_path(path);
	if (!original) return;
	auto *restore = g_new0(TrashRestoreData, 1);
	restore->path = g_strdup(path);
	restore->name = g_strdup(filename_from_path(original));
	restore->move = move;
	FileDialogData dialog{};
	dialog.action = FileDialogAction::SELECT_FOLDER;
	dialog.title = _("Restore to another folder");
	dialog.accept_text = _("Restore");
	dialog.callback = file_util_safe_trash_restore_choose_cb;
	dialog.data = restore;
	auto *toplevel = parent ? widget_get_toplevel(parent) : nullptr;
	dialog.parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : nullptr;
	file_dialog_show(dialog);
}

gboolean file_util_safe_trash_restore(const gchar *path, gboolean move, GtkWidget *parent)
{
	g_autofree gchar *original_path = file_util_safe_trash_original_path(path);
	if (!original_path) return FALSE;
	g_autofree gchar *original_dir = remove_level_from_path(original_path);
	if (!isdir(original_dir))
		{
		g_autofree gchar *message = g_strdup_printf(_("The original folder no longer exists:\n%s\n\nUse Restore to another folder to choose a destination."), original_dir);
		warning_dialog(_("Restore failed"), message, GQ_ICON_DIALOG_WARNING, parent);
		return FALSE;
		}
	return file_util_safe_trash_restore_to(path, original_path, move, parent);
}

static void move_to_trash_failed_cb(GenericDialog *, gpointer)
{
	help_window_show("TrashFailed.html");
}

gboolean file_util_safe_unlink_full(const gchar *path, const TrashSettings &settings,
                                    FileTreeOperation *operation, TrashReport &report, GError **error)
{

	g_autofree gchar *path_fs = path_from_utf8(path);
	g_autoptr(GFile) source = g_file_new_for_path(path_fs);
	if (settings.permanent) return rmdir_recursive(source, nullptr, error, operation);
	FileTreeStats stats;
	// Reject mount boundaries before either trash implementation changes anything.
	if (!file_tree_stats(source, stats, error, operation)) return FALSE;
	if (settings.system) return g_file_trash(source, operation ? operation->cancellable : nullptr, error);
	// Reject an oversized item before creating metadata or evicting existing entries.
	if (settings.limit > 0 && stats.bytes > settings.limit)
		{
		g_autofree gchar *size = g_format_size_full(stats.bytes, G_FORMAT_SIZE_IEC_UNITS);
		g_autofree gchar *limit = g_format_size_full(settings.limit, G_FORMAT_SIZE_IEC_UNITS);
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
		            _("The item is too large for Geeqie Trash.\nItem size: %s\nTrash size limit: %s\n\nThe item has not been moved and existing trash entries have not been removed."), size, limit);
		return FALSE;
		}
	std::unique_lock<std::mutex> lock(trash_mutex, std::defer_lock);
	if (!file_util_trash_lock(lock, operation, error)) return FALSE;
	g_autofree gchar *files_dir = file_util_safe_subdir("files", settings);
	g_autofree gchar *info_dir = file_util_safe_subdir("info", settings);
	g_autofree gchar *files_fs = path_from_utf8(files_dir);
	g_autofree gchar *info_fs = path_from_utf8(info_dir);
	if (settings.path.empty() || g_mkdir_with_parents(files_fs, 0755) != 0 || g_mkdir_with_parents(info_fs, 0755) != 0)
		{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Could not create trash folder"));
		return FALSE;
		}
	g_autofree gchar *real_source = realpath(path_fs, nullptr);
	g_autofree gchar *real_trash = realpath(files_fs, nullptr);
	g_autoptr(GFile) resolved_source = real_source ? g_file_new_for_path(real_source) : nullptr;
	g_autoptr(GFile) resolved_trash = real_trash ? g_file_new_for_path(real_trash) : nullptr;
	if (g_file_query_file_type(source, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr) == G_FILE_TYPE_DIRECTORY &&
	    resolved_source && resolved_trash && (g_file_equal(resolved_trash, resolved_source) || g_file_has_prefix(resolved_trash, resolved_source)))
		{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("The trash location is inside the folder being deleted."));
		return FALSE;
		}
	file_util_safe_cleanup(stats.bytes, FALSE, settings, operation, report, source);
	if (operation && g_cancellable_set_error_if_cancelled(operation->cancellable, error)) return FALSE;
	g_autofree gchar *dest = file_util_safe_dest(path, settings);
	if (!file_util_safe_write_info(path, dest, settings))
		{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Could not create trash information file"));
		return FALSE;
		}
	g_autofree gchar *dest_fs = path_from_utf8(dest);
	g_autoptr(GFile) destination = g_file_new_for_path(dest_fs);
	const gboolean success = file_tree_move(source, destination, error, operation);
	GStatBuf stat_buf;
	if (!success && !lstat_utf8(dest, &stat_buf) && errno == ENOENT) file_util_safe_remove_info(dest, settings);

	return success;
}

gboolean file_util_safe_unlink(const gchar *path)
{
	TrashReport report;
	const auto settings = file_util_trash_settings();
	g_autoptr(GError) error = nullptr;
	const gboolean success = file_util_safe_unlink_full(path, settings, nullptr, report, &error);
	file_util_trash_report(report);
	if (!success)
		{
		auto *dialog = warning_dialog(settings.permanent ? _("Delete failed") : _("Move to trash failed"),
		                              error ? error->message : _("Unknown error"), GQ_ICON_DIALOG_ERROR, nullptr);
		if (settings.system && !settings.permanent)
			generic_dialog_add_button(dialog, GQ_ICON_HELP, _("Help"), move_to_trash_failed_cb, FALSE);
		}
	return success;
}

gchar *file_util_safe_delete_status()
{
	gchar *buf = nullptr;

	if (is_valid_editor_command(CMD_DELETE))
		{
		buf = g_strdup(_("Deletion by external command"));
		}
	else if (options->file_ops.no_trash)
		{
		buf = g_strdup(_("Deleting without trash"));
		}
	else if (options->file_ops.safe_delete_enable)
		{
		if (!options->file_ops.use_system_trash)
			{
			g_autofree gchar *buf2 = nullptr;
			if (options->file_ops.safe_delete_folder_maxsize > 0)
				buf2 = g_strdup_printf(_(" (max. %d MiB)"), options->file_ops.safe_delete_folder_maxsize);
			else
				buf2 = g_strdup("");

			buf = g_strdup_printf(_("Using Geeqie Trash bin\n%s"), buf2);
			}
		else
			{
			buf = g_strdup(_("Using system Trash bin"));
			}
		}

	return buf;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
