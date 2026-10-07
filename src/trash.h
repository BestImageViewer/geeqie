/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
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

#ifndef TRASH_H
#define TRASH_H

#include <string>

#include <glib.h>
#include <gtk/gtk.h>

struct FileTreeOperation;

struct TrashSettings
{
	gboolean permanent;
	gboolean system;
	std::string path;
	guint64 limit;
};

struct TrashReport
{
	guint removed_entries = 0;
	guint64 removed_bytes = 0;
	gboolean incomplete = FALSE;
	gboolean cleanup_failed = FALSE;
	std::string cleanup_errors;
};

TrashSettings file_util_trash_settings();
gboolean file_util_safe_unlink_full(const gchar *path, const TrashSettings &settings,
                                    FileTreeOperation *operation, TrashReport &report, GError **error);
void file_util_trash_report(const TrashReport &report);
void file_util_trash_clear_async(GtkWidget *parent);
void file_util_trash_clear();
gboolean file_util_safe_unlink(const gchar *path);
gchar *file_util_safe_delete_status();
gchar *file_util_safe_trash_original_path(const gchar *path);
gboolean file_util_safe_trash_restore(const gchar *path, gboolean move, GtkWidget *parent);

void file_util_safe_trash_restore_async(const gchar *path, gboolean move, GtkWidget *parent);
void file_util_safe_trash_restore_choose(const gchar *path, gboolean move, GtkWidget *parent);

#endif /* TRASH_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
