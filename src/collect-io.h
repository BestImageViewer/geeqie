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

#ifndef COLLECT_IO_H
#define COLLECT_IO_H

enum CollectionLoadFlags {
	COLLECTION_LOAD_NONE	= 0,
	COLLECTION_LOAD_APPEND	= 1 << 0,
	COLLECTION_LOAD_FLUSH	= 1 << 1,
	COLLECTION_LOAD_GEOMETRY= 1 << 2,
};

gboolean collection_load(CollectionData *cd, const gchar *path, CollectionLoadFlags flags);

gboolean collection_load_begin(CollectionData *cd, const gchar *path, CollectionLoadFlags flags);
void collection_load_stop(CollectionData *cd);

void collection_load_thumb_idle(CollectionData *cd);

gboolean collection_save(CollectionData *cd, const gchar *path);

gboolean collection_load_only_geometry(CollectionData *cd, const gchar *path);


/**
 * @headerfile collect_manager_moved
 * these are used to update collections contained in user's collection
 * folder when moving or renaming files.
 * also handles:
 *   deletes file when newpath == NULL
 *   adds file when oldpath == NULL
 */
void collect_manager_moved(FileData *fd);

/**
 * @headerfile collect_manager_add
 * add from a specific collection
 */
void collect_manager_add(FileData *fd, const gchar *collection);

/**
 * @headerfile collect_manager_remove
 * removing from a specific collection
 */
void collect_manager_remove(FileData *fd, const gchar *collection);

/**
 * @headerfile collect_manager_flush
 * commit pending operations to disk
 */
void collect_manager_flush();

void collect_manager_notify_cb(FileData *fd, NotifyType type, gpointer data);
void collect_manager_list(GList **names_exc, GList **names_inc, GList **paths);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
