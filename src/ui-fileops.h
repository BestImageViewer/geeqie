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

#ifndef UI_FILEOPS_H
#define UI_FILEOPS_H

#include <sys/types.h>

#include <ctime>

#include <gio/gio.h>
#include <glib.h>

#include <config.h>

#ifdef DEBUG
#define GQ_DEBUG_PATH_UTF8 1
#endif

#if GQ_DEBUG_PATH_UTF8
#define path_to_utf8(path) path_to_utf8_debug(path, __FILE__, __LINE__)
#define path_from_utf8(utf8) path_from_utf8_debug(utf8, __FILE__, __LINE__)
gchar *path_to_utf8_debug(const gchar *path, const gchar *file, gint line);
gchar *path_from_utf8_debug(const gchar *utf8, const gchar *file, gint line);
#else
gchar *path_to_utf8(const gchar *path);
gchar *path_from_utf8(const gchar *utf8);
#endif

const gchar *xdg_data_home_get();
const gchar *xdg_config_home_get();
const gchar *xdg_cache_home_get();
const gchar *homedir();
const gchar *get_rc_dir();
const gchar *get_collections_dir();
const gchar *get_trash_dir();
const gchar *get_window_layouts_dir();

gboolean stat_utf8(const gchar *s, struct stat *st);
gboolean lstat_utf8(const gchar *s, struct stat *st);

gboolean isname(const gchar *s);
gboolean isfile(const gchar *s);
gboolean isdir(const gchar *s);
gboolean islink(const gchar *s);
gint64 filesize(const gchar *s);
time_t filetime(const gchar *s);
gboolean filetime_set(const gchar *s, time_t tval);
gboolean is_readable_file(const gchar *s);
gboolean access_file(const gchar *s, gint mode);
gboolean unlink_file(const gchar *s);
gboolean mkdir_utf8(const gchar *s, gint mode);
gboolean rmdir_utf8(const gchar *s);
gboolean copy_file_attributes(const gchar *s, const gchar *t, gint perms, gint mtime);
gboolean copy_file(const gchar *s, const gchar *t);
gboolean move_file(const gchar *s, const gchar *t);
gboolean rename_file(const gchar *s, const gchar *t);
gchar *get_current_dir();

GList *string_list_copy(const GList *list);
GString *string_list_join(const GList *list, const gchar *sep) G_GNUC_WARN_UNUSED_RESULT;

gchar *unique_filename(const gchar *path, const gchar *ext, const gchar *divider, gboolean pad);

const gchar *filename_from_path(const gchar *path);
gchar *remove_trailing_slash(const gchar *path);
gchar *remove_level_from_path(const gchar *path);

gchar *remove_extension_from_path(const gchar *path);

gboolean file_extension_match(const gchar *path, const gchar *ext);

void parse_out_relatives(gchar *path);

gboolean file_in_path(const gchar *name);

gboolean recursive_mkdir_if_not_exists(const gchar *path, mode_t mode);

gchar *md5_text_from_file_utf8(const gchar *path, const gchar *error_text);
gboolean md5_get_digest_from_file_utf8(const gchar *path, guchar digest[16]);

gchar *download_web_file(const gchar *text, gboolean minimized, gpointer data);
gboolean rmdir_recursive(GFile *file, GCancellable *cancellable, GError **error);

guchar *map_file(const gchar *path, gsize &map_len);

GList *pixbuf_gdk_known_extensions();

gboolean secure_save(const gchar *file_name, const gchar *contents, gsize length);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
