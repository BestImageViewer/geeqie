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
#include <cstdlib>
#include <string>
#include <vector>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "editors.h"
#include "filedata.h"
#include "filefilter.h"
#include "intl.h"
#include "main-defines.h"
#include "options.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "window.h"

/*
 *--------------------------------------------------------------------------
 * Safe Delete
 *--------------------------------------------------------------------------
 */

struct TrashEntry
{
	std::string path;
	gint64 size;
	time_t mtime;
	gboolean has_trashinfo;
};

static gchar *file_util_safe_subdir(const gchar *name)
{
	return g_build_filename(options->file_ops.safe_delete_path, name, nullptr);
}

static void file_util_safe_read_dir(const gchar *path, gboolean has_trashinfo, std::vector<TrashEntry> &entries)
{
	g_autoptr(GDir) dir = g_dir_open(path, 0, nullptr);
	if (!dir) return;

	const gchar *name;
	while ((name = g_dir_read_name(dir)))
		{
		g_autofree gchar *entry_path = g_build_filename(path, name, nullptr);
		GStatBuf stat_buf;
		if (g_stat(entry_path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode))
			entries.push_back({entry_path, stat_buf.st_size, stat_buf.st_mtime, has_trashinfo});
		}
}

static void file_util_safe_remove_info(const gchar *path)
{
	g_autofree gchar *info_dir = file_util_safe_subdir("info");
	g_autofree gchar *info_name = g_strconcat(filename_from_path(path), ".trashinfo", nullptr);
	g_autofree gchar *info_path = g_build_filename(info_dir, info_name, nullptr);
	if (isfile(info_path)) unlink_file(info_path);
}

static void file_util_safe_cleanup(gint64 incoming_size, gboolean clear)
{
	std::vector<TrashEntry> entries;
	g_autofree gchar *files_dir = file_util_safe_subdir("files");
	file_util_safe_read_dir(files_dir, TRUE, entries);
	file_util_safe_read_dir(options->file_ops.safe_delete_path, FALSE, entries);

	gint64 total = incoming_size;
	for (const auto &entry : entries) total += entry.size;

	std::sort(entries.begin(), entries.end(), [](const TrashEntry &a, const TrashEntry &b) { return a.mtime < b.mtime; });
	const gint64 limit = static_cast<gint64>(options->file_ops.safe_delete_folder_maxsize) * 1048576;
	gboolean warned = FALSE;
	for (const auto &entry : entries)
		{
		if (!clear && (limit == 0 || total <= limit)) break;

		DEBUG_1("expunging from trash for space: %s", entry.path.c_str());
		if (unlink_file(entry.path.c_str()))
			{
			if (entry.has_trashinfo) file_util_safe_remove_info(entry.path.c_str());
			total -= entry.size;
			}
		else if (!warned)
			{
			file_util_warning_dialog(_("Delete failed"), _("Unable to remove old file from trash folder"),
			                         GQ_ICON_DIALOG_WARNING, nullptr);
			warned = TRUE;
			}
		}
}

void file_util_trash_clear()
{
	file_util_safe_cleanup(0, TRUE);

	g_autofree gchar *info_dir = file_util_safe_subdir("info");
	g_autoptr(GDir) dir = g_dir_open(info_dir, 0, nullptr);
	if (!dir) return;

	const gchar *name;
	while ((name = g_dir_read_name(dir)))
		{
		g_autofree gchar *path = g_build_filename(info_dir, name, nullptr);
		if (isfile(path)) unlink_file(path);
		}
}

static gchar *file_util_safe_dest(const gchar *path)
{
	g_autofree gchar *files_dir = file_util_safe_subdir("files");
	const gchar *basename = filename_from_path(path);
	const gchar *extension = registered_extension_from_path(basename);
	g_autofree gchar *stem = g_strndup(basename, strlen(basename) - (extension ? strlen(extension) : 0));
	g_autofree gchar *dest = g_build_filename(files_dir, basename, nullptr);

	for (guint n = 2; isfile(dest); n++)
		{
		g_free(g_steal_pointer(&dest));
		g_autofree gchar *name = g_strdup_printf("%s.%u%s", stem, n, extension ? extension : "");
		dest = g_build_filename(files_dir, name, nullptr);
		}

	return g_steal_pointer(&dest);
}

static gboolean file_util_safe_write_info(const gchar *source, const gchar *dest)
{
	g_autofree gchar *info_dir = file_util_safe_subdir("info");
	g_autofree gchar *info_name = g_strconcat(filename_from_path(dest), ".trashinfo", nullptr);
	g_autofree gchar *info_path = g_build_filename(info_dir, info_name, nullptr);
	g_autofree gchar *escaped_path = g_uri_escape_string(source, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, FALSE);
	g_autoptr(GDateTime) now = g_date_time_new_now_local();
	g_autofree gchar *date = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S");
	g_autofree gchar *contents = g_strdup_printf("[Trash Info]\nPath=%s\nDeletionDate=%s\n", escaped_path, date);
	g_autoptr(GError) error = nullptr;

	if (g_file_set_contents(info_path, contents, -1, &error)) return TRUE;

	log_printf("Unable to create trash information file %s: %s\n", info_path, error->message);
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

gboolean file_util_safe_trash_restore(const gchar *path, gboolean move, GtkWidget *parent)
{
	g_autofree gchar *original_path = file_util_safe_trash_original_path(path);
	if (!original_path) return FALSE;

	if (isfile(original_path) || isdir(original_path))
		{
		g_autofree gchar *message = g_strdup_printf(_("The original location already contains a file named:\n%s"), original_path);
		warning_dialog(_("Restore failed"), message, GQ_ICON_DIALOG_WARNING, parent);
		return FALSE;
		}

	g_autofree gchar *original_dir = remove_level_from_path(original_path);
	if (!isdir(original_dir))
		{
		g_autofree gchar *message = g_strdup_printf(_("The original folder no longer exists:\n%s"), original_dir);
		warning_dialog(_("Restore failed"), message, GQ_ICON_DIALOG_WARNING, parent);
		return FALSE;
		}

	const gboolean success = move ? move_file(path, original_path) : copy_file(path, original_path);
	if (success)
		{
		if (move) file_util_safe_remove_info(path);
		return TRUE;
		}

	g_autofree gchar *message = g_strdup_printf(_("Unable to restore file to:\n%s"), original_path);
	warning_dialog(_("Restore failed"), message, GQ_ICON_DIALOG_WARNING, parent);
	return FALSE;
}

static void move_to_trash_failed_cb(GenericDialog *, gpointer)
{
	help_window_show("TrashFailed.html");
}

gboolean file_util_safe_unlink(const gchar *path)
{
	static GenericDialog *gd = nullptr;
	gboolean success = TRUE;

	if (!isfile(path)) return FALSE;

	if (options->file_ops.no_trash)
		{
		if (!unlink_file(path))
			{
			file_util_warning_dialog(_("Delete failed"),
						 _("Unable to remove file"),
						 GQ_ICON_DIALOG_WARNING, nullptr);
			success = FALSE;
			}
		}
	else if (!options->file_ops.use_system_trash)
		{
		const gchar *result = nullptr;
		g_autofree gchar *files_dir = file_util_safe_subdir("files");
		g_autofree gchar *info_dir = file_util_safe_subdir("info");

		if (!isdir(options->file_ops.safe_delete_path) || !isdir(files_dir) || !isdir(info_dir))
			{
			DEBUG_1("creating trash: %s", options->file_ops.safe_delete_path);
			if (!options->file_ops.safe_delete_path ||
			    (!isdir(options->file_ops.safe_delete_path) && !mkdir_utf8(options->file_ops.safe_delete_path, 0755)) ||
			    (!isdir(files_dir) && !mkdir_utf8(files_dir, 0755)) ||
			    (!isdir(info_dir) && !mkdir_utf8(info_dir, 0755)))
				{
				result = _("Could not create folder");
				success = FALSE;
				}
			}

		if (success)
			{
			file_util_safe_cleanup(filesize(path), FALSE);
			g_autofree gchar *dest = file_util_safe_dest(path);
			if (dest)
				{
				DEBUG_1("safe deleting %s to %s", path, dest);
				success = file_util_safe_write_info(path, dest);
				if (success)
					{
					success = move_file(path, dest);
					if (!success) file_util_safe_remove_info(dest);
					}
				else
					{
					result = _("Could not create trash information file");
					}
				}
			else
				{
				success = FALSE;
				}

			if (!success && !access_file(path, W_OK))
				{
				result = _("Permission denied");
				}
			}

		if (result && !gd)
			{
			g_autofree gchar *buf = g_strdup_printf(_("Unable to access or create the trash folder.\n\"%s\""), options->file_ops.safe_delete_path);
			gd = file_util_warning_dialog(result, buf, GQ_ICON_DIALOG_WARNING, nullptr);
			}
		}
	else
		{
		g_autoptr(GFile) file = g_file_new_for_path(path);
		g_autoptr(GError) error = nullptr;

		success = g_file_trash(file, nullptr, &error);

		if (!success && !gd)
			{
			g_autofree gchar *message = g_strdup_printf( "%s\n\n%s", _("See the Help file for a possible workaround."), error ? error->message : _("Unknown error"));

			gd = warning_dialog(_("Move to trash failed"), message, GQ_ICON_DIALOG_ERROR, nullptr);

			generic_dialog_add_button(gd, GQ_ICON_HELP, _("Help"), move_to_trash_failed_cb, FALSE);
			}
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
