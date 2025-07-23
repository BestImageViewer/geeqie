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

#ifndef UI_BOOKMARK_H
#define UI_BOOKMARK_H

#include <functional>

#include <glib.h>
#include <gtk/gtk.h>

/* bookmarks */

using BookmarkSelectFunc = std::function<void(const gchar *path)>;
GtkWidget *bookmark_list_new(const gchar *key, const BookmarkSelectFunc &select_func);
void bookmark_list_set_key(GtkWidget *list, const gchar *key);
void bookmark_list_set_no_defaults(GtkWidget *list, gint no_defaults);
void bookmark_list_set_editable(GtkWidget *list, gint editable);
void bookmark_list_set_only_directories(GtkWidget *list, gint only_directories);
void bookmark_list_add(GtkWidget *list, const gchar *name, const gchar *path);

void bookmark_add_default(const gchar *name, const gchar *path);

void bookmark_add_dialog(const gchar *title, GtkWidget *list);

/* history combo entry */

GtkWidget *history_combo_new(GtkWidget **entry, const gchar *text,
			     const gchar *history_key, gint max_levels);
void history_combo_append_history(GtkWidget *widget, const gchar *text);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
