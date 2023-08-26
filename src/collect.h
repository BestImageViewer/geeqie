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

struct CollectTable;
struct FileData;
struct ThumbLoader;

struct CollectInfo
{
	FileData *fd;
	GdkPixbuf *pixbuf;
	guint flag_mask;
};

CollectInfo *collection_info_new(FileData *fd, struct stat *st, GdkPixbuf *pixbuf);

void collection_info_free_thumb(CollectInfo *ci);
void collection_info_free(CollectInfo *ci);

void collection_info_set_thumb(CollectInfo *ci, GdkPixbuf *pixbuf);

GList *collection_list_sort(GList *list, SortType method);
GList *collection_list_add(GList *list, CollectInfo *ci, SortType method);
GList *collection_list_insert(GList *list, CollectInfo *ci, CollectInfo *insert_ci, SortType method);
GList *collection_list_remove(GList *list, CollectInfo *ci);
CollectInfo *collection_list_find_fd(GList *list, FileData *fd);
GList *collection_list_to_filelist(GList *list);

struct CollectionData
{
	gchar *path;
	gchar *name;
	GList *list;
	SortType sort_method;

	ThumbLoader *thumb_loader;
	CollectInfo *thumb_info;

	void (*info_updated_func)(CollectionData *, CollectInfo *, gpointer);
	gpointer info_updated_data;

	gint ref;

	/* geometry */
	gint window_read;
	gint window_x;
	gint window_y;
	gint window_w;
	gint window_h;

	gboolean changed; /**< contents changed since save flag */

	GHashTable *existence;

	GtkWidget *dialog_name_entry;
	gchar *collection_path; /**< Full path to collection including extension */
	gint collection_append_index;
};

CollectionData *collection_new(const gchar *path);
void collection_free(CollectionData *cd);

void collection_ref(CollectionData *cd);
void collection_unref(CollectionData *cd);

void collection_path_changed(CollectionData *cd);

gint collection_to_number(CollectionData *cd);
CollectionData *collection_from_number(gint n);

/**
 * @headerfile collection_from_dnd_data
 * pass a NULL pointer to whatever you don't need
 * use free_selected_list to free list, and
 * g_list_free to free info_list, which is a list of
 * CollectInfo pointers into CollectionData
 */
CollectionData *collection_from_dnd_data(const gchar *data, GList **list, GList **info_list);
gchar *collection_info_list_to_dnd_data(CollectionData *cd, GList *list, gint *length);

gint collection_info_valid(CollectionData *cd, CollectInfo *info);

CollectInfo *collection_next_by_info(CollectionData *cd, CollectInfo *info);
CollectInfo *collection_prev_by_info(CollectionData *cd, CollectInfo *info);
CollectInfo *collection_get_first(CollectionData *cd);
CollectInfo *collection_get_last(CollectionData *cd);

void collection_set_sort_method(CollectionData *cd, SortType method);
void collection_randomize(CollectionData *cd);
void collection_set_update_info_func(CollectionData *cd,
				     void (*func)(CollectionData *, CollectInfo *, gpointer), gpointer data);

gboolean collection_add(CollectionData *cd, FileData *fd, gboolean sorted);
gboolean collection_add_check(CollectionData *cd, FileData *fd, gboolean sorted, gboolean must_exist);
gboolean collection_insert(CollectionData *cd, FileData *fd, CollectInfo *insert_ci, gboolean sorted);
gboolean collection_remove(CollectionData *cd, FileData *fd);
void collection_remove_by_info_list(CollectionData *cd, GList *list);
gboolean collection_rename(CollectionData *cd, FileData *fd);

void collection_update_geometry(CollectionData *cd);

struct CollectWindow
{
	GtkWidget *window;
	CollectTable *table;
	GtkWidget *status_box;

	GtkWidget *close_dialog;

	CollectionData *cd;
};

CollectWindow *collection_window_new(const gchar *path);
void collection_window_close_by_collection(CollectionData *cd);
CollectWindow *collection_window_find(CollectionData *cd);
CollectWindow *collection_window_find_by_path(const gchar *path);
gboolean collection_window_modified_exists();

gboolean is_collection(const gchar *param);
gchar *collection_path(const gchar *param);
void collection_contents(const gchar *name, GString **contents);
GList *collection_contents_fd(const gchar *name);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
