/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: John Ellis, Vladimir Nadvornik, Laurent Monin
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

#ifndef HISTORY_LIST_H
#define HISTORY_LIST_H

#include <glib.h>

/* history lists */

gboolean history_list_load(const gchar *path);
gboolean history_list_save(const gchar *path);

void history_list_free_key(const gchar *key);

void history_list_add_to_key(const gchar *key, const gchar *path, gint max);

void history_list_item_change(const gchar *key, const gchar *oldpath, const gchar *newpath);
void history_list_item_move(const gchar *key, const gchar *path, gint direction);
void history_list_item_remove(const gchar *key, const gchar *path);
gchar *get_recent_viewed_folder_image(gchar *path);

const gchar *history_list_find_last_path_by_key(const gchar *key);

const gchar *history_chain_back();
const gchar *history_chain_forward();
void history_chain_append_end(const gchar *path);

const gchar *image_chain_back();
const gchar *image_chain_forward();
void image_chain_append_end(const gchar *path);


/**
 * @headerfile history_list_get_by_key
 * the returned GList is internal, don't free it
 */
GList *history_list_get_by_key(const gchar *key);


#endif /* HISTORY_LIST_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
