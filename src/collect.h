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

#ifndef COLLECT_H
#define COLLECT_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

enum SortType : gint;

class FileData;
struct ThumbLoader;

struct CollectInfo
{
	FileData *fd;
	gchar *infotext;
};

void collection_info_free(CollectInfo *ci);

GList *collection_list_sort(GList *list, SortType method);
CollectInfo *collection_list_find_fd(GList *list, FileData *fd);

struct CollectionData
{
	gchar *path;
	gchar *name;
	GList *list;
	SortType sort_method;

	gint ref;

	gboolean changed; /**< contents changed since save flag */
	gboolean relative_paths; /**< Save paths relative to the collection directory */

	GHashTable *existence;
	GList *change_listeners;

};

using CollectionChangedFunc = void (*)(CollectionData *, gpointer);
void collection_add_listener(CollectionData *cd, CollectionChangedFunc func, gpointer data);
void collection_remove_listener(CollectionData *cd, CollectionChangedFunc func, gpointer data);
void collection_changed(CollectionData *cd);

CollectionData *collection_new(const gchar *path);

CollectionData *collection_ref(CollectionData *cd);
void collection_unref(CollectionData *cd);

void collection_path_changed(CollectionData *cd);

gint collection_to_number(const CollectionData *cd);
CollectionData *collection_from_number(gint n);

gint collection_info_valid(CollectionData *cd, CollectInfo *info);

CollectInfo *collection_next_by_info(CollectionData *cd, CollectInfo *info);
CollectInfo *collection_prev_by_info(CollectionData *cd, CollectInfo *info);
CollectInfo *collection_get_first(CollectionData *cd);
CollectInfo *collection_get_last(CollectionData *cd);
const gchar *collection_get_info_text(CollectionData *cd, FileData *fd);
gboolean collection_set_info_text(CollectionData *cd, FileData *fd, const gchar *infotext);

void collection_set_sort_method(CollectionData *cd, SortType method);
void collection_randomize(CollectionData *cd);

gboolean collection_add(CollectionData *cd, FileData *fd, gboolean sorted, const gchar *infotext = nullptr);
gboolean collection_insert(CollectionData *cd, FileData *fd, CollectInfo *insert_ci, gboolean sorted);
gboolean collection_remove(CollectionData *cd, FileData *fd);

gboolean is_collection(const gchar *param);
gchar *collection_path(const gchar *param);
[[nodiscard]] GString *collection_contents(const gchar *name, GString *contents);
GList *collection_contents_fd(const gchar *name);
void collection_by_index_add_filelist(gint index, GList *list);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
