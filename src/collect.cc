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

#include "collect.h"

#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>

#include <glib-object.h>

#include "collect-dlg.h"
#include "collect-io.h"
#include "collect-table.h"
#include "compat.h"
#include "debug.h"
#include "filedata.h"
#include "img-view.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "print.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "window.h"

enum {
	COLLECT_DEF_WIDTH = 440,
	COLLECT_DEF_HEIGHT = 450
};

/**
 *  list of paths to collections */

/**
 * @brief  List of currently open Collections.
 *
 * Type ::_CollectionData
 */
static GList *collection_list = nullptr;

/**
 * @brief  List of currently open Collection windows.
 *
 * Type ::_CollectWindow
 */
static GList *collection_window_list = nullptr;

static void collection_window_get_geometry(CollectWindow *cw);
static void collection_window_refresh(CollectWindow *cw);
static void collection_window_update_title(CollectWindow *cw);
static void collection_window_add(CollectWindow *cw, CollectInfo *ci);
static void collection_window_insert(CollectWindow *cw, CollectInfo *ci);
static void collection_window_remove(CollectWindow *cw, CollectInfo *ci);
static void collection_window_update(CollectWindow *cw, CollectInfo *ci);

static void collection_window_close(CollectWindow *cw);

static void collection_notify_cb(FileData *fd, NotifyType type, gpointer data);

/*
 *-------------------------------------------------------------------
 * data, list handling
 *-------------------------------------------------------------------
 */

CollectInfo *collection_info_new(FileData *fd, struct stat *, GdkPixbuf *pixbuf)
{
	CollectInfo *ci;

	if (!fd) return nullptr;

	ci = g_new0(CollectInfo, 1);
	ci->fd = file_data_ref(fd);

	ci->pixbuf = pixbuf;
	if (ci->pixbuf) g_object_ref(ci->pixbuf);

	return ci;
}

static void collection_info_free_thumb(CollectInfo *ci)
{
	if (ci->pixbuf) g_object_unref(ci->pixbuf);
	ci->pixbuf = nullptr;
}

void collection_info_free(CollectInfo *ci)
{
	if (!ci) return;

	file_data_unref(ci->fd);
	collection_info_free_thumb(ci);
	g_free(ci);
}

void collection_info_set_thumb(CollectInfo *ci, GdkPixbuf *pixbuf)
{
	if (pixbuf) g_object_ref(pixbuf);
	collection_info_free_thumb(ci);
	ci->pixbuf = pixbuf;
}

/* an ugly static var, well what ya gonna do ? */
static SortType collection_list_sort_method = SORT_NAME;

static gint collection_list_sort_cb(gconstpointer a, gconstpointer b)
{
	auto cia = static_cast<const CollectInfo *>(a);
	auto cib = static_cast<const CollectInfo *>(b);

	switch (collection_list_sort_method)
		{
		case SORT_NAME:
			break;
		case SORT_NONE:
			return 0;
			break;
		case SORT_SIZE:
			if (cia->fd->size < cib->fd->size) return -1;
			if (cia->fd->size > cib->fd->size) return 1;
			return 0;
			break;
		case SORT_TIME:
			if (cia->fd->date < cib->fd->date) return -1;
			if (cia->fd->date > cib->fd->date) return 1;
			return 0;
			break;
		case SORT_CTIME:
			if (cia->fd->cdate < cib->fd->cdate) return -1;
			if (cia->fd->cdate > cib->fd->cdate) return 1;
			return 0;
			break;
		case SORT_EXIFTIME:
			if (cia->fd->exifdate < cib->fd->exifdate) return -1;
			if (cia->fd->exifdate > cib->fd->exifdate) return 1;
			break;
		case SORT_EXIFTIMEDIGITIZED:
			if (cia->fd->exifdate_digitized < cib->fd->exifdate_digitized) return -1;
			if (cia->fd->exifdate_digitized > cib->fd->exifdate_digitized) return 1;
			break;
		case SORT_RATING:
			if (cia->fd->rating < cib->fd->rating) return -1;
			if (cia->fd->rating > cib->fd->rating) return 1;
			break;
		case SORT_PATH:
			return utf8_compare(cia->fd->path, cib->fd->path, options->file_sort.case_sensitive);
			break;
		case SORT_CLASS:
			if (cia->fd->format_class < cib->fd->format_class) return -1;
			if (cia->fd->format_class > cib->fd->format_class) return 1;
			break;
		default:
			break;
		}

	if (options->file_sort.case_sensitive)
		return strcmp(cia->fd->collate_key_name, cib->fd->collate_key_name);

	return strcmp(cia->fd->collate_key_name_nocase, cib->fd->collate_key_name_nocase);
}

GList *collection_list_sort(GList *list, SortType method)
{
	if (method == SORT_NONE) return list;

	collection_list_sort_method = method;

	return g_list_sort(list, collection_list_sort_cb);
}

static GList *collection_list_randomize(GList *list)
{
	guint random;
	guint length;
	guint i;
	GList *nlist;
	GList *olist;

	length = g_list_length(list);
	if (!length) return nullptr;

	srand(static_cast<unsigned int>(time(nullptr))); // Initialize random generator (hasn't to be that much strong)

	for (i = 0; i < length; i++)
		{
		random = static_cast<guint>(1.0 * length * rand()/(RAND_MAX + 1.0));
		olist = g_list_nth(list, i);
		nlist = g_list_nth(list, random);
		std::swap(olist->data, nlist->data);
		}

	return list;
}

GList *collection_list_add(GList *list, CollectInfo *ci, SortType method)
{
	if (method != SORT_NONE)
		{
		collection_list_sort_method = method;
		list = g_list_insert_sorted(list, ci, collection_list_sort_cb);
		}
	else
		{
		list = g_list_append(list, ci);
		}

	return list;
}

GList *collection_list_insert(GList *list, CollectInfo *ci, CollectInfo *insert_ci, SortType method)
{
	if (method != SORT_NONE)
		{
		collection_list_sort_method = method;
		list = g_list_insert_sorted(list, ci, collection_list_sort_cb);
		}
	else
		{
		GList *point;

		point = g_list_find(list, insert_ci);
		list = uig_list_insert_link(list, point, ci);
		}

	return list;
}

GList *collection_list_remove(GList *list, CollectInfo *ci)
{
	list = g_list_remove(list, ci);
	collection_info_free(ci);
	return list;
}

CollectInfo *collection_list_find_fd(GList *list, FileData *fd)
{
	GList *work = list;

	while (work)
		{
		auto ci = static_cast<CollectInfo *>(work->data);
		if (ci->fd == fd) return ci;
		work = work->next;
		}

	return nullptr;
}

GList *collection_list_to_filelist(GList *list)
{
	GList *filelist = nullptr;
	GList *work = list;

	while (work)
		{
		auto info = static_cast<CollectInfo *>(work->data);
		filelist = g_list_prepend(filelist, file_data_ref(info->fd));
		work = work->next;
		}

	filelist = g_list_reverse(filelist);
	return filelist;
}

CollectWindow *collection_window_find(CollectionData *cd)
{
	GList *work;

	work = collection_window_list;
	while (work)
		{
		auto cw = static_cast<CollectWindow *>(work->data);
		if (cw->cd == cd) return cw;
		work = work->next;
		}

	return nullptr;
}

CollectWindow *collection_window_find_by_path(const gchar *path)
{
	if (!path) return nullptr;

	const auto collect_window_compare_data_path = [](gconstpointer data, gconstpointer user_data)
	{
		return g_strcmp0(static_cast<const CollectWindow *>(data)->cd->path, static_cast<const gchar *>(user_data));
	};

	GList *work = g_list_find_custom(collection_window_list, path, collect_window_compare_data_path);
	return work ? static_cast<CollectWindow *>(work->data) : nullptr;
}

/**
 * @brief Checks string for existence of Collection.
 * @param[in] param Filename, with or without extension of any collection
 * @returns full pathname if found or NULL
 *
 * Return value must be freed with g_free()
 */
gchar *collection_path(const gchar *param)
{
	gchar *path = nullptr;
	gchar *full_name = nullptr;

	if (file_extension_match(param, GQ_COLLECTION_EXT))
		{
		path = g_build_filename(get_collections_dir(), param, NULL);
		}
	else if (file_extension_match(param, nullptr))
		{
		full_name = g_strconcat(param, GQ_COLLECTION_EXT, NULL);
		path = g_build_filename(get_collections_dir(), full_name, NULL);
		}

	if (!isfile(path))
		{
		g_free(path);
		path = nullptr;
		}

	g_free(full_name);
	return path;
}

/**
 * @brief Checks input string for existence of Collection.
 * @param[in] param Filename with or without extension of any collection
 * @returns TRUE if found
 *
 *
 */
gboolean is_collection(const gchar *param)
{
	gchar *name = nullptr;

	name = collection_path(param);
	if (name)
		{
		g_free(name);
		return TRUE;
		}
	return FALSE;
}

/**
 * @brief Creates a text list of the image paths of the contents of a Collection
 * @param[in] name The name of the collection, with or without extension
 * @param[inout] contents A GString to which the image paths are appended
 *
 *
 */
void collection_contents(const gchar *name, GString **contents)
{
	gchar *path;
	CollectionData *cd;
	CollectInfo *ci;
	GList *work;
	FileData *fd;

	if (is_collection(name))
		{
		path = collection_path(name);
		cd = collection_new("");
		collection_load(cd, path, COLLECTION_LOAD_APPEND);
		work = cd->list;
		while (work)
			{
			ci = static_cast<CollectInfo *>(work->data);
			fd = ci->fd;
			*contents = g_string_append(*contents, fd->path);
			*contents = g_string_append(*contents, "\n");

			work = work->next;
			}
		g_free(path);
		collection_free(cd);
		}
}

/**
 * @brief Returns a list of filedatas of the contents of a Collection
 * @param[in] name The name of the collection, with or without extension
 *
 *
 */
GList *collection_contents_fd(const gchar *name)
{
	gchar *path;
	CollectionData *cd;
	CollectInfo *ci;
	GList *work;
	GList *list = nullptr;

	if (is_collection(name))
		{
		path = collection_path(name);
		cd = collection_new("");
		collection_load(cd, path, COLLECTION_LOAD_APPEND);
		work = cd->list;
		while (work)
			{
			ci = static_cast<CollectInfo *>(work->data);
			list = g_list_append(list, ci->fd);

			work = work->next;
			}
		g_free(path);
		collection_free(cd);
		}

	return list;
}

/*
 *-------------------------------------------------------------------
 * please use these to actually add/remove stuff
 *-------------------------------------------------------------------
 */

CollectionData *collection_new(const gchar *path)
{
	CollectionData *cd;
	static gint untitled_counter = 0;

	cd = g_new0(CollectionData, 1);

	cd->ref = 1;	/* starts with a ref of 1 */
	cd->sort_method = SORT_NONE;
	cd->window.width = COLLECT_DEF_WIDTH;
	cd->window.height = COLLECT_DEF_HEIGHT;
	cd->existence = g_hash_table_new(nullptr, nullptr);

	if (path)
		{
		cd->path = g_strdup(path);
		cd->name = g_strdup(filename_from_path(cd->path));
		/* load it */
		}
	else
		{
		if (untitled_counter == 0)
			{
			cd->name = g_strdup(_("Untitled"));
			}
		else
			{
			cd->name = g_strdup_printf(_("Untitled (%d)"), untitled_counter + 1);
			}

		untitled_counter++;
		}

	file_data_register_notify_func(collection_notify_cb, cd, NOTIFY_PRIORITY_MEDIUM);


	collection_list = g_list_append(collection_list, cd);

	return cd;
}

void collection_free(CollectionData *cd)
{
	if (!cd) return;

	DEBUG_1("collection \"%s\" freed", cd->name);

	collection_load_stop(cd);
	g_list_free_full(cd->list, reinterpret_cast<GDestroyNotify>(collection_info_free));

	file_data_unregister_notify_func(collection_notify_cb, cd);

	collection_list = g_list_remove(collection_list, cd);

	g_hash_table_destroy(cd->existence);

	g_free(cd->collection_path);
	g_free(cd->path);
	g_free(cd->name);

	g_free(cd);
}

void collection_ref(CollectionData *cd)
{
	cd->ref++;

	DEBUG_1("collection \"%s\" ref count = %d", cd->name, cd->ref);
}

void collection_unref(CollectionData *cd)
{
	cd->ref--;

	DEBUG_1("collection \"%s\" ref count = %d", cd->name, cd->ref);

	if (cd->ref < 1)
		{
		collection_free(cd);
		}
}

void collection_path_changed(CollectionData *cd)
{
	collection_window_update_title(collection_window_find(cd));
}

gint collection_to_number(const CollectionData *cd)
{
	return g_list_index(collection_list, cd);
}

CollectionData *collection_from_number(gint n)
{
	return static_cast<CollectionData *>(g_list_nth_data(collection_list, n));
}

/**
 * @brief Pass a NULL pointer to whatever you don't need
 * use free_selected_list to free list, and
 * g_list_free to free info_list, which is a list of
 * CollectInfo pointers into CollectionData
 */
 CollectionData *collection_from_dnd_data(const gchar *data, GList **list, GList **info_list)
{
	if (list) *list = nullptr;
	if (info_list) *info_list = nullptr;

	if (strncmp(data, "COLLECTION:", 11) != 0) return nullptr;

	data += 11;

	gint collection_number = atoi(data);
	CollectionData *cd = collection_from_number(collection_number);
	if (!cd) return nullptr;

	if (!list && !info_list) return cd;

	GStrv numbers = g_strsplit(data, "\n", -1);
	for (gint i = 1; numbers[i] != nullptr; i++)
		{
		if (!numbers[i + 1]) break; // numbers[i] is data after last \n, skip it

		auto item_number = static_cast<guint>(atoi(numbers[i]));
		auto *info = static_cast<CollectInfo *>(g_list_nth_data(cd->list, item_number));
		if (!info) continue;

		if (list) *list = g_list_append(*list, file_data_ref(info->fd));
		if (info_list) *info_list = g_list_append(*info_list, info);
		}

	g_strfreev(numbers);
	return cd;
}

gchar *collection_info_list_to_dnd_data(const CollectionData *cd, const GList *list, gint &length)
{
	length = 0;
	if (!list) return nullptr;

	gint collection_number = collection_to_number(cd);
	if (collection_number < 0) return nullptr;

	GString *text = g_string_new(nullptr);
	g_string_printf(text, "COLLECTION:%d\n", collection_number);

	for (const GList *work = list; work; work = work->next)
		{
		gint item_number = g_list_index(cd->list, work->data);

		if (item_number < 0) continue;

		g_string_append_printf(text, "%d\n", item_number);
		}

	length = text->len + 1; /* ending nul char */

	return g_string_free(text, FALSE);
}

gint collection_info_valid(CollectionData *cd, CollectInfo *info)
{
	if (collection_to_number(cd) < 0) return FALSE;

	return (g_list_index(cd->list, info) != 0);
}

CollectInfo *collection_next_by_info(CollectionData *cd, CollectInfo *info)
{
	GList *work;

	work = g_list_find(cd->list, info);

	if (!work) return nullptr;
	work = work->next;
	if (work) return static_cast<CollectInfo *>(work->data);
	return nullptr;
}

CollectInfo *collection_prev_by_info(CollectionData *cd, CollectInfo *info)
{
	GList *work;

	work = g_list_find(cd->list, info);

	if (!work) return nullptr;
	work = work->prev;
	if (work) return static_cast<CollectInfo *>(work->data);
	return nullptr;
}

CollectInfo *collection_get_first(CollectionData *cd)
{
	if (cd->list) return static_cast<CollectInfo *>(cd->list->data);

	return nullptr;
}

CollectInfo *collection_get_last(CollectionData *cd)
{
	GList *list;

	list = g_list_last(cd->list);

	if (list) return static_cast<CollectInfo *>(list->data);

	return nullptr;
}

void collection_set_sort_method(CollectionData *cd, SortType method)
{
	if (!cd) return;

	if (cd->sort_method == method) return;

	cd->sort_method = method;
	cd->list = collection_list_sort(cd->list, cd->sort_method);
	if (cd->list) cd->changed = TRUE;

	collection_window_refresh(collection_window_find(cd));
}

void collection_randomize(CollectionData *cd)
{
	if (!cd) return;

	cd->list = collection_list_randomize(cd->list);
	cd->sort_method = SORT_NONE;
	if (cd->list) cd->changed = TRUE;

	collection_window_refresh(collection_window_find(cd));
}

void collection_set_update_info_func(CollectionData *cd,
				     void (*func)(CollectionData *, CollectInfo *, gpointer), gpointer data)
{
	cd->info_updated_func = func;
	cd->info_updated_data = data;
}

static CollectInfo *collection_info_new_if_not_exists(CollectionData *cd, struct stat *st, FileData *fd)
{
	CollectInfo *ci;

	if (!options->collections_duplicates)
		{
		if (g_hash_table_lookup(cd->existence, fd->path)) return nullptr;
		}

	ci = collection_info_new(fd, st, nullptr);
	if (ci) g_hash_table_insert(cd->existence, fd->path, g_strdup(""));
	return ci;
}

gboolean collection_add_check(CollectionData *cd, FileData *fd, gboolean sorted, gboolean must_exist)
{
	struct stat st;
	gboolean valid;

	if (!fd) return FALSE;

	g_assert(fd->magick == FD_MAGICK);

	if (must_exist)
		{
		valid = (stat_utf8(fd->path, &st) && !S_ISDIR(st.st_mode));
		}
	else
		{
		valid = TRUE;
		st.st_size = 0;
		st.st_mtime = 0;
		}

	if (valid)
		{
		CollectInfo *ci;

		ci = collection_info_new_if_not_exists(cd, &st, fd);
		if (!ci) return FALSE;
		DEBUG_3("add to collection: %s", fd->path);

		cd->list = collection_list_add(cd->list, ci, sorted ? cd->sort_method : SORT_NONE);
		cd->changed = TRUE;

		if (!sorted || cd->sort_method == SORT_NONE)
			{
			collection_window_add(collection_window_find(cd), ci);
			}
		else
			{
			collection_window_insert(collection_window_find(cd), ci);
			}
		}

	return valid;
}

gboolean collection_add(CollectionData *cd, FileData *fd, gboolean sorted)
{
	return collection_add_check(cd, fd, sorted, TRUE);
}

gboolean collection_insert(CollectionData *cd, FileData *fd, CollectInfo *insert_ci, gboolean sorted)
{
	struct stat st;

	if (!insert_ci) return collection_add(cd, fd, sorted);

	if (stat_utf8(fd->path, &st) >= 0 && !S_ISDIR(st.st_mode))
		{
		CollectInfo *ci;

		ci = collection_info_new_if_not_exists(cd, &st, fd);
		if (!ci) return FALSE;

		DEBUG_3("insert in collection: %s", fd->path);

		cd->list = collection_list_insert(cd->list, ci, insert_ci, sorted ? cd->sort_method : SORT_NONE);
		cd->changed = TRUE;

		collection_window_insert(collection_window_find(cd), ci);

		return TRUE;
		}

	return FALSE;
}

gboolean collection_remove(CollectionData *cd, FileData *fd)
{
	CollectInfo *ci;

	ci = collection_list_find_fd(cd->list, fd);

	if (!ci) return FALSE;

	g_hash_table_remove(cd->existence, fd->path);

	cd->list = g_list_remove(cd->list, ci);
	cd->changed = TRUE;

	collection_window_remove(collection_window_find(cd), ci);
	collection_info_free(ci);

	return TRUE;
}

static void collection_remove_by_info(CollectionData *cd, CollectInfo *info)
{
	if (!info || !g_list_find(cd->list, info)) return;

	cd->list = g_list_remove(cd->list, info);
	cd->changed = (cd->list != nullptr);

	collection_window_remove(collection_window_find(cd), info);
	collection_info_free(info);
}

void collection_remove_by_info_list(CollectionData *cd, GList *list)
{
	GList *work;

	if (!list) return;

	if (!list->next)
		{
		/* more efficient (in collect-table) to remove a single item this way */
		collection_remove_by_info(cd, static_cast<CollectInfo *>(list->data));
		return;
		}

	work = list;
	while (work)
		{
		cd->list = collection_list_remove(cd->list, static_cast<CollectInfo *>(work->data));
		work = work->next;
		}
	cd->changed = (cd->list != nullptr);

	collection_window_refresh(collection_window_find(cd));
}

gboolean collection_rename(CollectionData *cd, FileData *fd)
{
	CollectInfo *ci;
	ci = collection_list_find_fd(cd->list, fd);

	if (!ci) return FALSE;

	cd->changed = TRUE;

	collection_window_update(collection_window_find(cd), ci);

	return TRUE;
}

void collection_update_geometry(CollectionData *cd)
{
	collection_window_get_geometry(collection_window_find(cd));
}

/*
 *-------------------------------------------------------------------
 * simple maintenance for renaming, deleting
 *-------------------------------------------------------------------
 */

static void collection_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify collection: %s %04x", fd->path, type);

	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
			collection_rename(cd, fd);
			break;
		case FILEDATA_CHANGE_COPY:
			break;
		case FILEDATA_CHANGE_DELETE:
			while (collection_remove(cd, fd));
			break;
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}

}


/*
 *-------------------------------------------------------------------
 * window key presses
 *-------------------------------------------------------------------
 */

static gboolean collection_window_keypress(GtkWidget *, GdkEventKey *event, gpointer data)
{
	auto cw = static_cast<CollectWindow *>(data);
	gboolean stop_signal = FALSE;
	GList *list;

	if (event->state & GDK_CONTROL_MASK)
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case '0':
				break;
			case 'A': case 'a':
				if (event->state & GDK_SHIFT_MASK)
					{
					collection_table_unselect_all(cw->table);
					}
				else
					{
					collection_table_select_all(cw->table);
					}
				break;
			case 'L': case 'l':
				list = layout_list(nullptr);
				if (list)
					{
					collection_table_add_filelist(cw->table, list);
					filelist_free(list);
					}
				break;
			case 'C': case 'c':
				file_util_copy(nullptr, collection_table_selection_get_list(cw->table), nullptr, cw->window);
				break;
			case 'M': case 'm':
				file_util_move(nullptr, collection_table_selection_get_list(cw->table), nullptr, cw->window);
				break;
			case 'R': case 'r':
				file_util_rename(nullptr, collection_table_selection_get_list(cw->table), cw->window);
				break;
			case 'D': case 'd':
				options->file_ops.safe_delete_enable = TRUE;
				file_util_delete(nullptr, collection_table_selection_get_list(cw->table), cw->window);
				break;
			case 'S': case 's':
				collection_dialog_save_as(cw->cd);
				break;
			case 'W': case 'w':
				collection_window_close(cw);
				break;
			default:
				stop_signal = FALSE;
				break;
			}
		}
	else
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case GDK_KEY_Return: case GDK_KEY_KP_Enter:
				layout_image_set_collection(nullptr, cw->cd,
					collection_table_get_focus_info(cw->table));
				break;
			case 'V': case 'v':
				view_window_new_from_collection(cw->cd,
					collection_table_get_focus_info(cw->table));
				break;
			case 'S': case 's':
				if (!cw->cd->path)
					{
					collection_dialog_save_as(cw->cd);
					}
				else if (!collection_save(cw->cd, cw->cd->path))
					{
					log_printf("failed saving to collection path: %s\n", cw->cd->path);
					}
				break;
			case 'A': case 'a':
				collection_dialog_append(cw->cd);
				break;
			case 'N': case 'n':
				collection_set_sort_method(cw->cd, SORT_NAME);
				break;
			case 'D': case 'd':
				collection_set_sort_method(cw->cd, SORT_TIME);
				break;
			case 'B': case 'b':
				collection_set_sort_method(cw->cd, SORT_SIZE);
				break;
			case 'P': case 'p':
				if (event->state & GDK_SHIFT_MASK)
					{
					CollectInfo *info;

					info = collection_table_get_focus_info(cw->table);

					print_window_new(info->fd, collection_table_selection_get_list(cw->table),
							 collection_list_to_filelist(cw->cd->list), cw->window);
					}
				else
					{
					collection_set_sort_method(cw->cd, SORT_PATH);
					}
				break;
			case 'R': case 'r':
				if (event->state & GDK_MOD1_MASK)
					{
						options->collections.rectangular_selection = !(options->collections.rectangular_selection);
					}
				break;
			case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
				list = g_list_copy(cw->table->selection);
				if (list)
					{
					collection_remove_by_info_list(cw->cd, list);
					collection_table_refresh(cw->table);
					g_list_free(list);
					}
				else
					{
					collection_remove_by_info(cw->cd, collection_table_get_focus_info(cw->table));
					}
				break;
			default:
				stop_signal = FALSE;
				break;
			}
		}
	if (!stop_signal && is_help_key(event))
		{
		help_window_show("GuideCollections.html");
		stop_signal = TRUE;
		}

	return stop_signal;
}

/*
 *-------------------------------------------------------------------
 * window
 *-------------------------------------------------------------------
 */
static void collection_window_get_geometry(CollectWindow *cw)
{
	CollectionData *cd;
	GdkWindow *window;

	if (!cw) return;

	cd = cw->cd;
	window = gtk_widget_get_window(cw->window);
	cd->window = window_get_position_geometry(window);
	cd->window_read = TRUE;
}

static void collection_window_refresh(CollectWindow *cw)
{
	if (!cw) return;

	collection_table_refresh(cw->table);
}

static void collection_window_update_title(CollectWindow *cw)
{
	gboolean free_name = FALSE;
	gchar *name;
	gchar *buf;

	if (!cw) return;

	if (file_extension_match(cw->cd->name, GQ_COLLECTION_EXT))
		{
		name = remove_extension_from_path(cw->cd->name);
		free_name = TRUE;
		}
	else
		{
		name = cw->cd->name;
		}

	buf = g_strdup_printf(_("%s - Collection - %s"), name, GQ_APPNAME);
	if (free_name) g_free(name);
	gtk_window_set_title(GTK_WINDOW(cw->window), buf);
	g_free(buf);
}

static void collection_window_update_info(CollectionData *, CollectInfo *ci, gpointer data)
{
	auto cw = static_cast<CollectWindow *>(data);

	collection_table_file_update(cw->table, ci);
}

static void collection_window_add(CollectWindow *cw, CollectInfo *ci)
{
	if (!cw) return;

	if (!ci->pixbuf) collection_load_thumb_idle(cw->cd);
	collection_table_file_add(cw->table, ci);
}

static void collection_window_insert(CollectWindow *cw, CollectInfo *ci)
{
	if (!cw) return;

	if (!ci->pixbuf) collection_load_thumb_idle(cw->cd);
	collection_table_file_insert(cw->table, ci);
}

static void collection_window_remove(CollectWindow *cw, CollectInfo *ci)
{
	if (!cw) return;

	collection_table_file_remove(cw->table, ci);
}

static void collection_window_update(CollectWindow *cw, CollectInfo *ci)
{
	if (!cw) return;

	collection_table_file_update(cw->table, ci);
	collection_table_file_update(cw->table, nullptr);
}

static void collection_window_close_final(CollectWindow *cw)
{
	if (cw->close_dialog) return;

	collection_window_list = g_list_remove(collection_window_list, cw);
	collection_window_get_geometry(cw);

	gq_gtk_widget_destroy(cw->window);

	collection_set_update_info_func(cw->cd, nullptr, nullptr);
	collection_unref(cw->cd);

	g_free(cw);
}

static void collection_close_save_cb(GenericDialog *gd, gpointer data)
{
	auto cw = static_cast<CollectWindow *>(data);

	cw->close_dialog = nullptr;
	generic_dialog_close(gd);

	if (!cw->cd->path)
		{
		collection_dialog_save_close(cw->cd);
		return;
		}

	if (!collection_save(cw->cd, cw->cd->path))
		{
		gchar *buf;
		buf = g_strdup_printf(_("Failed to save the collection:\n%s"), cw->cd->path);
		warning_dialog(_("Save Failed"), buf, GQ_ICON_DIALOG_ERROR, cw->window);
		g_free(buf);
		return;
		}

	collection_window_close_final(cw);
}

static void collection_close_close_cb(GenericDialog *gd, gpointer data)
{
	auto cw = static_cast<CollectWindow *>(data);

	cw->close_dialog = nullptr;
	generic_dialog_close(gd);

	collection_window_close_final(cw);
}

static void collection_close_cancel_cb(GenericDialog *gd, gpointer data)
{
	auto cw = static_cast<CollectWindow *>(data);

	cw->close_dialog = nullptr;
	generic_dialog_close(gd);
}

static void collection_close_dlg_show(CollectWindow *cw)
{
	GenericDialog *gd;

	if (cw->close_dialog)
		{
		gtk_window_present(GTK_WINDOW(cw->close_dialog));
		return;
		}

	gd = generic_dialog_new(_("Close collection"),
				"close_collection", cw->window, FALSE,
				collection_close_cancel_cb, cw);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION,
				   _("Close collection"),
				   _("Collection has been modified.\nSave first?"), TRUE);

	generic_dialog_add_button(gd, GQ_ICON_SAVE, _("Save"), collection_close_save_cb, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_DELETE, _("_Discard"), collection_close_close_cb, FALSE);

	cw->close_dialog = gd->dialog;

	gtk_widget_show(gd->dialog);
}

static void collection_window_close(CollectWindow *cw)
{
	if (!cw->cd->changed && !cw->close_dialog)
		{
		collection_window_close_final(cw);
		return;
		}

	collection_close_dlg_show(cw);
}

void collection_window_close_by_collection(CollectionData *cd)
{
	CollectWindow *cw;

	cw = collection_window_find(cd);
	if (cw) collection_window_close_final(cw);
}

/**
 * @brief Check if any Collection windows have unsaved data
 * @returns TRUE if unsaved data exists
 *
 * Also saves window geometry for Collection windows that have
 * no unsaved data
 */
gboolean collection_window_modified_exists()
{
	GList *work;
	gboolean ret;

	ret = FALSE;

	work = collection_window_list;
	while (work)
		{
		auto cw = static_cast<CollectWindow *>(work->data);
		if (cw->cd->changed)
			{
			ret = TRUE;
			}
		else
			{
			if (!collection_save(cw->table->cd, cw->table->cd->path))
				{
				log_printf("failed saving to collection path: %s\n", cw->table->cd->path);
				}
			}
		work = work->next;
		}

	return ret;
}

static gboolean collection_window_delete(GtkWidget *, GdkEvent *, gpointer data)
{
	auto cw = static_cast<CollectWindow *>(data);
	collection_window_close(cw);

	return TRUE;
}

CollectWindow *collection_window_new(const gchar *path)
{
	CollectWindow *cw;
	GtkWidget *vbox;
	GtkWidget *frame;
	GtkWidget *status_label;
	GtkWidget *extra_label;
	GdkGeometry geometry;

	/* If the collection is already opened in another window, return that one */
	cw = collection_window_find_by_path(path);
	if (cw)
		{
		return cw;
		}

	cw = g_new0(CollectWindow, 1);

	collection_window_list = g_list_append(collection_window_list, cw);

	cw->cd = collection_new(path);

	cw->window = window_new("collection", PIXBUF_INLINE_ICON_BOOK, nullptr, nullptr);
	DEBUG_NAME(cw->window);

	geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.base_width = COLLECT_DEF_WIDTH;
	geometry.base_height = COLLECT_DEF_HEIGHT;
	gtk_window_set_geometry_hints(GTK_WINDOW(cw->window), nullptr, &geometry,
				      static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE));

	if (options->collections_on_top)
		{
		gq_gtk_window_set_keep_above(GTK_WINDOW(cw->window), TRUE);
		}

	if (options->save_window_positions && path && collection_load_only_geometry(cw->cd, path))
		{
		gtk_window_set_default_size(GTK_WINDOW(cw->window), cw->cd->window.width, cw->cd->window.height);
		gq_gtk_window_move(GTK_WINDOW(cw->window), cw->cd->window.x, cw->cd->window.y);
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(cw->window), COLLECT_DEF_WIDTH, COLLECT_DEF_HEIGHT);
		}

	gtk_window_set_resizable(GTK_WINDOW(cw->window), TRUE);
	collection_window_update_title(cw);
	gtk_container_set_border_width(GTK_CONTAINER(cw->window), 0);

	g_signal_connect(G_OBJECT(cw->window), "delete_event",
			 G_CALLBACK(collection_window_delete), cw);

	g_signal_connect(G_OBJECT(cw->window), "key_press_event",
			 G_CALLBACK(collection_window_keypress), cw);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_container_add(GTK_WIDGET(cw->window), vbox);
	gtk_widget_show(vbox);

	cw->table = collection_table_new(cw->cd);
	gq_gtk_box_pack_start(GTK_BOX(vbox), cw->table->scrolled, TRUE, TRUE, 0);
	gtk_widget_show(cw->table->scrolled);

	cw->status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(vbox), cw->status_box, FALSE, FALSE, 0);
	gtk_widget_show(cw->status_box);

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gq_gtk_box_pack_start(GTK_BOX(cw->status_box), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	status_label = gtk_label_new("");
	gq_gtk_container_add(GTK_WIDGET(frame), status_label);
	gtk_widget_show(status_label);

	extra_label = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(extra_label), 0.0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(extra_label), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(extra_label), TRUE);

	gq_gtk_box_pack_start(GTK_BOX(cw->status_box), extra_label, TRUE, TRUE, 0);
	gtk_widget_show(extra_label);

	collection_table_set_labels(cw->table, status_label, extra_label);

	gtk_widget_show(cw->window);
	gtk_widget_grab_focus(cw->table->listview);

	collection_set_update_info_func(cw->cd, collection_window_update_info, cw);

	if (path && *path == G_DIR_SEPARATOR) collection_load_begin(cw->cd, nullptr, COLLECTION_LOAD_NONE);

	return cw;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
