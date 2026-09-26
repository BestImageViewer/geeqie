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
#include <memory>
#include <utility>
#include <vector>

#include <glib-object.h>

#include "collect-io.h"
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
#include "ui-fileops.h"
#include "utilops.h"
#include "view-file.h"

namespace
{

/**
 *  list of paths to collections */

/**
 * @brief  List of currently open Collections.
 *
 * Type ::_CollectionData
 */
GList *collection_list = nullptr;

} // namespace

static void collection_free(CollectionData *cd);
static void collection_notify_cb(FileData *fd, NotifyType type, gpointer data);

/*
 *-------------------------------------------------------------------
 * data, list handling
 *-------------------------------------------------------------------
 */

static CollectInfo *collection_info_new(FileData *fd, struct stat *, const gchar *infotext)
{
	CollectInfo *ci;

	if (!fd) return nullptr;

	ci = g_new0(CollectInfo, 1);
	ci->fd = file_data_ref(fd);

	ci->infotext = g_strdup(infotext);

	return ci;
}

void collection_info_free(CollectInfo *ci)
{
	if (!ci) return;

	file_data_unref(ci->fd);
	g_free(ci->infotext);
	g_free(ci);
}

static gint collection_list_sort_cb(gconstpointer a, gconstpointer b,
                                    gpointer user_data)
{
	auto cia = static_cast<const CollectInfo *>(a);
	auto cib = static_cast<const CollectInfo *>(b);

	switch (GPOINTER_TO_INT(user_data))
		{
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
		case SORT_MEDIA_TIME:
			if (cia->fd->media_date < cib->fd->media_date) return -1;
			if (cia->fd->media_date > cib->fd->media_date) return 1;
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

	return g_list_sort_with_data(list, collection_list_sort_cb, GINT_TO_POINTER(method));
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

static GList *collection_list_add(GList *list, CollectInfo *ci, SortType method)
{
	if (method != SORT_NONE)
		{
		list = g_list_insert_sorted_with_data(list, ci, collection_list_sort_cb, GINT_TO_POINTER(method));
		}
	else
		{
		list = g_list_append(list, ci);
		}

	return list;
}

static GList *collection_list_insert(GList *list, CollectInfo *ci, CollectInfo *insert_ci, SortType method)
{
	if (method != SORT_NONE)
		{
		list = g_list_insert_sorted_with_data(list, ci, collection_list_sort_cb, GINT_TO_POINTER(method));
		}
	else
		{
		GList *point;

		point = g_list_find(list, insert_ci);
		list = g_list_insert_before(list, point, ci);
		}

	return list;
}

static GList *collection_list_remove(GList *list, CollectInfo *ci)
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

	if (file_extension_match(param, GQ_COLLECTION_EXT))
		{
		path = g_build_filename(get_collections_dir(), param, NULL);
		}
	else if (file_extension_match(param, nullptr))
		{
		g_autofree gchar *full_name = g_strconcat(param, GQ_COLLECTION_EXT, NULL);
		path = g_build_filename(get_collections_dir(), full_name, NULL);
		}

	if (!isfile(path))
		{
		g_free(path);
		path = nullptr;
		}

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
	g_autofree gchar *name = collection_path(param);

	return name != nullptr;
}

/**
 * @brief Creates a text list of the image paths of the contents of a Collection
 * @param[in] name The name of the collection, with or without extension
 * @param[inout] contents A GString to which the image paths are appended
 *
 *
 */
GString *collection_contents(const gchar *name, GString *contents)
{
	if (!is_collection(name)) return contents;

	CollectionData *cd = collection_new("");
	g_autofree gchar *path = collection_path(name);
	collection_load(cd, path, COLLECTION_LOAD_APPEND);

	for (GList *work = cd->list; work; work = work->next)
		{
		auto *ci = static_cast<CollectInfo *>(work->data);

		contents = g_string_append(contents, ci->fd->path);
		contents = g_string_append(contents, "\n");
		}

	collection_free(cd);

	return contents;
}

/**
 * @brief Returns a list of filedatas of the contents of a Collection
 * @param[in] name The name of the collection, with or without extension
 *
 *
 */
GList *collection_contents_fd(const gchar *name)
{
	CollectionData *cd;
	CollectInfo *ci;
	GList *work;
	GList *list = nullptr;

	if (!is_collection(name)) return nullptr;

	g_autofree gchar *path = collection_path(name);
	cd = collection_new("");
	collection_load(cd, path, COLLECTION_LOAD_APPEND);
	work = cd->list;
	while (work)
		{
		ci = static_cast<CollectInfo *>(work->data);
		list = g_list_append(list, file_data_ref(ci->fd));

		work = work->next;
		}

	collection_unref(cd);

	return list;
}

/**
 * @brief Add file selection list to a collection
 * @param[in] index Index to the collection list, or -1 for new collection
 * @param[in] list List of ::_FileData
 *
 */
void collection_by_index_add_filelist(gint index, GList *list)
{
	if (!list) return;
	if (index < 0)
		{
			LayoutWindow *lw = layout_new_from_default();
			CollectionData *cd = collection_new(nullptr);
			if (layout_set_collection(lw, cd))
				for (GList *work = list; work; work = work->next)
					collection_add(cd, static_cast<FileData *>(work->data), FALSE);
			collection_unref(cd);
			return;
		}
	LayoutWindow *lw = get_current_layout();
	if (!lw) return;
	g_autofree gchar *path = collection_manager_path_by_index(index);
	CollectionData *current = lw->vf ? lw->vf->collection : nullptr;
	if (current && (index < 0 || g_strcmp0(current->path, path) != 0))
		{
			auto files = std::shared_ptr<GList>(filelist_copy(list), [](GList *items) { file_data_list_free(items); });
			if (!layout_confirm_collection_leave(lw, [index, files]() { collection_by_index_add_filelist(index, files.get()); }, FALSE)) return;
		}
	CollectionData *cd = nullptr;
	for (gint i = 0; (cd = collection_from_number(i)); i++)
		if (path && g_strcmp0(cd->path, path) == 0) break;
	if (cd)
		collection_ref(cd);
	else
		{
			cd = collection_new(path);
			if (path && isfile(path) && !collection_load(cd, path, COLLECTION_LOAD_NONE))
				{
				collection_unref(cd);
				return;
				}
		}
	if (layout_set_collection(lw, cd))
		for (GList *work = list; work; work = work->next)
			collection_add(cd, static_cast<FileData *>(work->data), FALSE);
	collection_unref(cd);
}

/*
 *-------------------------------------------------------------------
 * please use these to actually add/remove stuff
 *-------------------------------------------------------------------
 */

namespace
{
struct CollectionListener
{
	CollectionChangedFunc func;
	gpointer data;
};
}

void collection_add_listener(CollectionData *cd, CollectionChangedFunc func, gpointer data)
{
	auto *listener = g_new(CollectionListener, 1);
	*listener = {func, data};
	cd->change_listeners = g_list_append(cd->change_listeners, listener);
}

void collection_remove_listener(CollectionData *cd, CollectionChangedFunc func, gpointer data)
{
	for (GList *work = cd->change_listeners; work; work = work->next)
		{
		auto *listener = static_cast<CollectionListener *>(work->data);
		if (listener->func == func && listener->data == data)
			{
			cd->change_listeners = g_list_delete_link(cd->change_listeners, work);
			g_free(listener);
			return;
			}
		}
}

void collection_changed(CollectionData *cd)
{
	std::vector<CollectionListener> listeners;
	for (GList *work = cd->change_listeners; work; work = work->next)
		listeners.push_back(*static_cast<CollectionListener *>(work->data));
	for (const auto &listener : listeners) listener.func(cd, listener.data);
}

CollectionData *collection_new(const gchar *path)
{
	CollectionData *cd;
	static gint untitled_counter = 0;

	cd = g_new0(CollectionData, 1);

	cd->ref = 1;	/* starts with a ref of 1 */
	cd->sort_method = SORT_NONE;
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

	g_list_free_full(cd->list, reinterpret_cast<GDestroyNotify>(collection_info_free));

	file_data_unregister_notify_func(collection_notify_cb, cd);

	collection_list = g_list_remove(collection_list, cd);

	g_hash_table_destroy(cd->existence);
	g_list_free_full(cd->change_listeners, g_free);

	g_free(cd->path);
	g_free(cd->name);

	g_free(cd);
}

CollectionData *collection_ref(CollectionData *cd)
{
	cd->ref++;

	DEBUG_1("collection \"%s\" ref count = %d", cd->name, cd->ref);
	return cd;
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
	collection_changed(cd);
}

gint collection_to_number(const CollectionData *cd)
{
	return g_list_index(collection_list, cd);
}

CollectionData *collection_from_number(gint n)
{
	return static_cast<CollectionData *>(g_list_nth_data(collection_list, n));
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

const gchar *collection_get_info_text(CollectionData *cd, FileData *fd)
{
	if (!cd || !fd) return nullptr;

	CollectInfo *ci = collection_list_find_fd(cd->list, fd);
	if (!ci) return nullptr;

	return ci->infotext;
}

gboolean collection_set_info_text(CollectionData *cd, FileData *fd, const gchar *infotext)
{
	if (!cd || !fd) return FALSE;

	CollectInfo *ci = collection_list_find_fd(cd->list, fd);
	if (!ci) return FALSE;

	const gchar *new_infotext = (infotext && *infotext) ? infotext : nullptr;
	if (g_strcmp0(ci->infotext, new_infotext) == 0) return TRUE;

	g_free(ci->infotext);
	ci->infotext = g_strdup(new_infotext);
	cd->changed = TRUE;

	collection_changed(cd);

	return TRUE;
}

void collection_set_sort_method(CollectionData *cd, SortType method)
{
	if (!cd) return;

	if (cd->sort_method == method) return;

	cd->sort_method = method;
	cd->list = collection_list_sort(cd->list, cd->sort_method);
	if (cd->list) cd->changed = TRUE;

	collection_changed(cd);
}

void collection_randomize(CollectionData *cd)
{
	if (!cd) return;

	cd->list = collection_list_randomize(cd->list);
	cd->sort_method = SORT_NONE;
	if (cd->list) cd->changed = TRUE;

	collection_changed(cd);
}

static CollectInfo *collection_info_new_if_not_exists(CollectionData *cd, struct stat *st, FileData *fd, const gchar *infotext)
{
	if (!options->collections_duplicates &&
	    g_hash_table_contains(cd->existence, fd->path))
		{
		return nullptr;
		}

	CollectInfo *ci = collection_info_new(fd, st, infotext);
	if (ci) g_hash_table_add(cd->existence, fd->path);
	return ci;
}

// @TODO Drop must_exist and merge with collection_add()?
static gboolean collection_add_check(CollectionData *cd, FileData *fd, gboolean sorted, gboolean must_exist, const gchar *infotext)
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

		ci = collection_info_new_if_not_exists(cd, &st, fd, infotext);
		if (!ci) return FALSE;
		DEBUG_3("add to collection: %s", fd->path);

		cd->list = collection_list_add(cd->list, ci, sorted ? cd->sort_method : SORT_NONE);
		cd->changed = TRUE;

		}

	if (valid) collection_changed(cd);
	return valid;
}

gboolean collection_add(CollectionData *cd, FileData *fd, gboolean sorted, const gchar *infotext)
{
	return collection_add_check(cd, fd, sorted, TRUE, infotext);
}

gboolean collection_insert(CollectionData *cd, FileData *fd, CollectInfo *insert_ci, gboolean sorted)
{
	struct stat st;

	if (!insert_ci) return collection_add(cd, fd, sorted);

	if (stat_utf8(fd->path, &st) >= 0 && !S_ISDIR(st.st_mode))
		{
		CollectInfo *ci;

		ci = collection_info_new_if_not_exists(cd, &st, fd, nullptr);
		if (!ci) return FALSE;

		DEBUG_3("insert in collection: %s", fd->path);

		cd->list = collection_list_insert(cd->list, ci, insert_ci, sorted ? cd->sort_method : SORT_NONE);
		cd->changed = TRUE;

	collection_changed(cd);

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

	cd->list = collection_list_remove(cd->list, ci);
	cd->changed = TRUE;

	collection_changed(cd);

	return TRUE;
}

static gboolean collection_rename(CollectionData *cd, FileData *fd)
{
	CollectInfo *ci;
	ci = collection_list_find_fd(cd->list, fd);

	if (!ci) return FALSE;

	cd->changed = TRUE;

	collection_changed(cd);

	return TRUE;
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

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
