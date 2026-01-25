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

#include "collect-io.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>

#include <config.h>

#if HAVE_MNTENT_H
#include <mntent.h>
#else
#include <sys/mount.h>
#endif

#include "collect.h"
#include "filedata.h"
#include "intl.h"
#include "layout-util.h"
#include "main-defines.h"
#include "options.h"
#include "thumb.h"
#include "ui-fileops.h"

#ifdef __NetBSD__
#define statfs statvfs
#endif

namespace
{

#if HAVE_MNTENT_H
using MFILE = FILE;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MFILE, endmntent)
#endif

constexpr guint GQ_COLLECTION_FAIL_MIN = 300;
constexpr guint GQ_COLLECTION_FAIL_PERCENT = 98;
constexpr size_t GQ_COLLECTION_READ_BUFSIZE = 4096;

#define GQ_COLLECTION_MARKER "#" GQ_APPNAME
const size_t GQ_COLLECTION_MARKER_LEN = strlen(GQ_COLLECTION_MARKER);

constexpr gchar gqview_collection_marker[] = "#GQview collection";
const size_t gqview_collection_marker_len = strlen(gqview_collection_marker);

constexpr gint COLLECT_MANAGER_ACTIONS_PER_IDLE = 1000;
constexpr guint COLLECT_MANAGER_FLUSH_DELAY = 10000;

struct CollectManagerEntry
{
	gchar *path;
	GList *add_list;
	GHashTable *oldpath_hash;
	GHashTable *newpath_hash;
	gboolean empty;
};

enum CollectManagerType {
	COLLECTION_MANAGER_UPDATE,
	COLLECTION_MANAGER_ADD,
	COLLECTION_MANAGER_REMOVE
};

struct CollectManagerAction
{
	gchar *oldpath;
	gchar *newpath;

	CollectManagerType type;

	gint ref;
};


GList *collection_manager_entry_list = nullptr;
GList *collection_manager_action_list = nullptr;
GList *collection_manager_action_tail = nullptr;

CollectManagerEntry *collect_manager_get_entry(const gchar *path)
{
	const auto collect_manager_entry_compare_path = [](gconstpointer data, gconstpointer user_data)
	{
		return strcmp(static_cast<const CollectManagerEntry *>(data)->path, static_cast<const gchar *>(user_data));
	};

	GList *work = g_list_find_custom(collection_manager_entry_list, path, collect_manager_entry_compare_path);

	return work ? static_cast<CollectManagerEntry *>(work->data) : nullptr;
}

gboolean scan_geometry(gchar *buffer, GdkRectangle &window)
{
	gint nx;
	gint ny;
	gint nw;
	gint nh;

	if (sscanf(buffer, "%d %d %d %d", &nx, &ny, &nw, &nh) != 4) return FALSE;

	window.x = nx;
	window.y = ny;
	window.width = nw;
	window.height = nh;

	return TRUE;
}

bool is_file_on_mounted_drive(const gchar *filename)
{
	const auto is_file_in_dir = [filename](const gchar *dirname)
	{
		return (g_strcmp0(dirname, G_DIR_SEPARATOR_S) != 0) &&
		       g_str_has_prefix(filename, dirname);
	};

#if HAVE_MNTENT_H
	g_autoptr(MFILE) mount_entries = setmntent("/proc/mounts", "r");
	if (mount_entries == nullptr)
		{
		/* It is assumed this will never fail */
		perror("setmntent");
		exit(EXIT_FAILURE);
		}

	struct mntent *mount_entry;
	while (nullptr != (mount_entry = getmntent(mount_entries)))
		{
		if (is_file_in_dir(mount_entry->mnt_dir))
			return true;
		}
#else
	struct statfs* mounts;
	int num_mounts = getmntinfo(&mounts, MNT_NOWAIT);

	if (num_mounts < 0)
		{
		/* It is assumed this will never fail */
		perror("getmntinfo");
		exit(EXIT_FAILURE);
		}

	for (int i = 0; i < num_mounts; i++)
		{
		if (is_file_in_dir(mounts[i].f_mntonname))
			return true;
		}
#endif
	return false;
}

} // namespace

static void collection_load_thumb_step(CollectionData *cd);
static gboolean collection_save_private(CollectionData *cd, const gchar *path);

static void collect_manager_entry_reset(CollectManagerEntry *entry);
static gint collect_manager_process_action(CollectManagerEntry *entry, gchar **path_ptr);

static gboolean collection_load_private(CollectionData *cd, const gchar *path, CollectionLoadFlags flags)
{
	gchar s_buf[GQ_COLLECTION_READ_BUFSIZE];
	FILE *f;
	gboolean limit_failures = TRUE;
	gboolean success = TRUE;
	gboolean has_official_header = FALSE;
	gboolean has_geometry_header = FALSE;
	gboolean has_gqview_header   = FALSE;
	gboolean need_header	 = TRUE;
	guint total = 0;
	guint fail = 0;
	gboolean changed = FALSE;
	CollectManagerEntry *entry = nullptr;
	guint flush = !!(flags & COLLECTION_LOAD_FLUSH);
	guint append = !!(flags & COLLECTION_LOAD_APPEND);
	guint only_geometry = !!(flags & COLLECTION_LOAD_GEOMETRY);
	g_autofree gchar *infotext = nullptr;

	if (!only_geometry)
		{
		collection_load_stop(cd);

		if (flush)
			collect_manager_flush();
		else
			entry = collect_manager_get_entry(path);

		if (!append)
			{
			g_list_free_full(cd->list, reinterpret_cast<GDestroyNotify>(collection_info_free));
			cd->list = nullptr;
			}
		}

	if (!path && !cd->path) return FALSE;

	if (!path) path = cd->path;

	g_autofree gchar *pathl = path_from_utf8(path);

	DEBUG_1("collection load: append=%d flush=%d only_geometry=%d path=%s", append, flush, only_geometry, pathl);

	/* load it */
	f = fopen(pathl, "r");
	if (!f)
		{
		log_printf("Failed to open collection file: \"%s\"\n", path);
		return FALSE;
		}

	g_autoptr(GString) extended_filename_buffer = nullptr;
	while (fgets(s_buf, sizeof(s_buf), f))
		{
		gchar *buf;
		gchar *p = s_buf;

		if (!extended_filename_buffer)
			{
			/* Skip whitespaces and empty lines */
			while (*p && g_ascii_isspace(*p)) p++;
			if (*p == '\n' || *p == '\r') continue;

			/* Parse comments */
			if (*p == '#')
				{
				if (strncmp(p, "#i ", 3 ) == 0)
					{
					g_free(infotext);
					infotext = g_strdup(p+3);
					gchar *q = strpbrk(infotext, "\r\n");
					if (q) *q = 0;
					}
				if (!need_header) continue;

				if (g_ascii_strncasecmp(p, GQ_COLLECTION_MARKER, GQ_COLLECTION_MARKER_LEN) == 0)
					{
					/* Looks like an official collection, allow unchecked input.
					 * All this does is allow adding files that may not exist,
					 * which is needed for the collection manager to work.
					 * Also unofficial files abort after too many invalid entries.
					 */
					has_official_header = TRUE;
					limit_failures = FALSE;
					}
				else if (strncmp(p, "#geometry:", 10 ) == 0 && scan_geometry(p + 10, cd->window))
					{
					has_geometry_header = TRUE;
					cd->window_read = TRUE;
					if (only_geometry) break;
					}
				else if (g_ascii_strncasecmp(p, gqview_collection_marker, gqview_collection_marker_len) == 0)
					{
					/* As 2008/04/15 there is no difference between our collection file format
					 * and GQview 2.1.5 collection file format so ignore failures as well. */
					has_gqview_header = TRUE;
					limit_failures = FALSE;
					}
				need_header = (!has_official_header && !has_gqview_header) || !has_geometry_header;
				continue;
				}

			if (only_geometry) continue;
			}

		/* Read filenames */
		/** @todo This is not safe! */
		/* Updated: anything within double quotes is considered a filename */
		if (!extended_filename_buffer)
			{
			/* not yet reading filename */
			while (*p && *p != '"') p++;
			if (*p) p++;
			/* start of filename read */
			buf = p;
			while (*p && *p != '"') p++;
			if (p[0] != '"')
				{
				/* first part of extended filename */
				extended_filename_buffer = g_string_new(buf);
				continue;
				}
			}
		else
			{
			buf = p;
			while (*p && *p != '"') p++;
			if (p[0] != '"')
				{
				/* end of extended filename still not found */
				g_string_append(extended_filename_buffer, buf);
				continue;
				}

			/* end of extended filename found */
			g_string_append_len(extended_filename_buffer, buf, p - buf);
			}

		g_autofree gchar *filename = nullptr;
		if (extended_filename_buffer)
			{
			filename = g_string_free(extended_filename_buffer, FALSE);
			extended_filename_buffer = nullptr;
			}
		else
			{
			*p = 0;
			filename = g_strdup(buf);
			}

		if (!*filename) continue;

		total++;

		if (!flush)
			changed |= collect_manager_process_action(entry, &filename);

		if (filename[0] == G_DIR_SEPARATOR && collection_add(cd, file_data_new_simple(filename), FALSE, infotext))
			{
			g_clear_pointer(&infotext, g_free);
			continue;
			}

		log_printf("Warning: Collection: %s Invalid file: %s", cd->name, filename);
		DEBUG_1("collection invalid file: %s", filename);

		/* If the file path has the prefix home, tmp or usr it was on the local file system and has
		 * been deleted. Ignore it. */
		if (!g_str_has_prefix(filename, "/home") && !g_str_has_prefix(filename, "/tmp") && !g_str_has_prefix(filename, "/usr"))
			{
			/* The file was on a mountable drive and either has been deleted or the drive is not
			 * mounted.
			 */
			if (!is_file_on_mounted_drive(filename))
				{
				/* The is on a mountable drive which is not mounted.
				 * This event can happen when the user opens a Collection, or when a
				 * file_data_register_notify_func runs. Whenever a file move or rename happens, the
				 * notify function runs, presumably to check if the file is in a Collection and so
				 * modify it.
				 * Therefore it is better to use a notification rather than a warning message that
				 * requires the user to acknowledge it.
				 */
				log_printf("%s is a file on an unmounted filesystem: %s", filename, cd->path);
				g_autofree gchar *text = g_strdup_printf(_("This Collection cannot be opened because it contains a link to a file on a drive which is not yet mounted.\n\nCollection: %s\nFile: %s\n"), cd->path, filename);

				g_autoptr(GNotification) notification = g_notification_new("Geeqie");
				auto *app = g_application_get_default();

				g_notification_set_title(notification, _("Collections"));
				g_notification_set_body(notification, _(text));
				g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
				g_notification_set_default_action(notification, "app.null");

				g_application_send_notification(G_APPLICATION(app), "collection-unmounted-drive", notification);

				success = FALSE;
				break;
				}

			log_printf("%s was a file on a mounted filesystem but has been deleted: %s", filename, cd->name);
			}
		else
			{
			log_printf("%s was a file on local filesystem but has been deleted: %s", filename, cd->name);
			}

		fail++;
		if (limit_failures && fail > GQ_COLLECTION_FAIL_MIN && fail * 100 / total > GQ_COLLECTION_FAIL_PERCENT)
			{
			log_printf("%u invalid filenames in unofficial collection file, closing: %s\n", fail, path);
			success = FALSE;
			break;
			}
		}

	DEBUG_1("collection files: total = %u fail = %u official=%d gqview=%d geometry=%d", total, fail, has_official_header, has_gqview_header, has_geometry_header);

	fclose(f);
	if (only_geometry) return has_geometry_header;

	if (!flush)
		{
		gchar *buf = nullptr;
		while (collect_manager_process_action(entry, &buf))
			{
			collection_add(cd, file_data_new_group(buf), FALSE);
			changed = TRUE;
			g_free(buf);
			buf = nullptr;
			}
		}

	cd->list = collection_list_sort(cd->list, cd->sort_method);

	if (!flush && changed && success)
		collection_save_private(cd, path);

	if (!flush)
		collect_manager_entry_reset(entry);

	if (!append) cd->changed = FALSE;

	return success;
}

gboolean collection_load(CollectionData *cd, const gchar *path, CollectionLoadFlags flags)
{
	if (collection_load_private(cd, path, static_cast<CollectionLoadFlags>(flags | COLLECTION_LOAD_FLUSH)))
		{
		layout_recent_add_path(cd->path);
		return TRUE;
		}

	return FALSE;
}

static void collection_load_thumb_do(CollectionData *cd)
{
	GdkPixbuf *pixbuf;

	if (!cd->thumb_loader || !g_list_find(cd->list, cd->thumb_info)) return;

	pixbuf = thumb_loader_get_pixbuf(cd->thumb_loader);
	collection_info_set_thumb(cd->thumb_info, pixbuf);
	g_object_unref(pixbuf);

	if (cd->info_updated_func) cd->info_updated_func(cd, cd->thumb_info);
}

static void collection_load_thumb_error_cb(ThumbLoader *, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	collection_load_thumb_do(cd);
	collection_load_thumb_step(cd);
}

static void collection_load_thumb_done_cb(ThumbLoader *, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	collection_load_thumb_do(cd);
	collection_load_thumb_step(cd);
}

static void collection_load_thumb_step(CollectionData *cd)
{
	GList *work;
	CollectInfo *ci;

	if (!cd->list)
		{
		collection_load_stop(cd);
		return;
		}

	work = cd->list;
	ci = static_cast<CollectInfo *>(work->data);
	work = work->next;
	/* find first unloaded thumb */
	while (work && ci->pixbuf)
		{
		ci = static_cast<CollectInfo *>(work->data);
		work = work->next;
		}

	if (!ci || ci->pixbuf)
		{
		/* done */
		collection_load_stop(cd);

		/* send a NULL CollectInfo to notify end */
		if (cd->info_updated_func) cd->info_updated_func(cd, nullptr);

		return;
		}

	/* setup loader and call it */
	cd->thumb_info = ci;
	thumb_loader_free(cd->thumb_loader);
	cd->thumb_loader = thumb_loader_new(options->thumbnails.max_width, options->thumbnails.max_height);
	thumb_loader_set_callbacks(cd->thumb_loader,
				   collection_load_thumb_done_cb,
				   collection_load_thumb_error_cb,
				   nullptr,
				   cd);

	/* start it */
	if (!thumb_loader_start(cd->thumb_loader, ci->fd))
		{
		/* error, handle it, do next */
		DEBUG_1("error loading thumb for %s", ci->fd->path);
		collection_load_thumb_do(cd);
		collection_load_thumb_step(cd);
		}
}

void collection_load_thumb_idle(CollectionData *cd)
{
	if (!cd->thumb_loader) collection_load_thumb_step(cd);
}

gboolean collection_load_begin(CollectionData *cd, const gchar *path, CollectionLoadFlags flags)
{
	if (!collection_load(cd, path, flags)) return FALSE;

	collection_load_thumb_idle(cd);

	return TRUE;
}

void collection_load_stop(CollectionData *cd)
{
	if (!cd->thumb_loader) return;

	thumb_loader_free(cd->thumb_loader);
	cd->thumb_loader = nullptr;
}

static gboolean collection_save_private(CollectionData *cd, const gchar *path)
{
	if (!path && !cd->path) return FALSE;

	if (!path)
		{
		path = cd->path;
		}

	g_autofree gchar *pathl = path_from_utf8(path);
	g_autoptr(GString) gstring = g_string_new(GQ_COLLECTION_MARKER " collection\n#created with " GQ_APPNAME " version " VERSION "\n");

	collection_update_geometry(cd);
	if (cd->window_read)
		{
		g_string_append_printf(gstring, "#geometry: %d %d %d %d\n", cd->window.x, cd->window.y, cd->window.width, cd->window.height);
		}

	for (GList *work = cd->list; work; work = work->next)
		{
		auto ci = static_cast<CollectInfo *>(work->data);
		if (ci->infotext && *ci->infotext)
			g_string_append_printf(gstring, "#i %s\n", ci->infotext);

		g_string_append_printf(gstring, "\"%s\"\n", ci->fd->path);
		}

	g_string_append(gstring, "#end\n");

	secure_save(pathl, gstring->str, -1);

	if (!cd->path || strcmp(path, cd->path) != 0)
		{
		g_autofree gchar *buf = cd->path;
		cd->path = g_strdup(path);
		path = cd->path;

		g_free(cd->name);
		cd->name = g_strdup(filename_from_path(cd->path));

		collection_path_changed(cd);
		}

	cd->changed = FALSE;

	return TRUE;
}

gboolean collection_save(CollectionData *cd, const gchar *path)
{
	if (collection_save_private(cd, path))
		{
		layout_recent_add_path(cd->path);
		return TRUE;
		}

	return FALSE;
}

gboolean collection_load_only_geometry(CollectionData *cd, const gchar *path)
{
	return collection_load(cd, path, COLLECTION_LOAD_GEOMETRY);
}


/*
 *-------------------------------------------------------------------
 * collection manager
 *-------------------------------------------------------------------
 */

static CollectManagerAction *collect_manager_action_new(const gchar *oldpath, const gchar *newpath,
							CollectManagerType type)
{
	CollectManagerAction *action;

	action = g_new0(CollectManagerAction, 1);
	action->ref = 1;

	action->oldpath = g_strdup(oldpath);
	action->newpath = g_strdup(newpath);

	action->type = type;

	return action;
}

static void collect_manager_action_ref(CollectManagerAction *action)
{
	action->ref++;
}

static void collect_manager_action_unref(CollectManagerAction *action)
{
	action->ref--;

	if (action->ref > 0) return;

	g_free(action->oldpath);
	g_free(action->newpath);
	g_free(action);
}

static void collect_manager_entry_free_data(CollectManagerEntry *entry)
{
	g_list_free_full(entry->add_list, reinterpret_cast<GDestroyNotify>(collect_manager_action_unref));
	if (g_hash_table_size(entry->oldpath_hash) > 0)
		g_hash_table_destroy(entry->oldpath_hash);
	else
		g_hash_table_unref(entry->oldpath_hash);
	if (g_hash_table_size(entry->newpath_hash) > 0)
		g_hash_table_destroy(entry->newpath_hash);
	else
		g_hash_table_unref(entry->newpath_hash);
}

static void collect_manager_entry_init_data(CollectManagerEntry *entry)
{
	entry->add_list = nullptr;
	entry->oldpath_hash = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, reinterpret_cast<GDestroyNotify>(collect_manager_action_unref));
	entry->newpath_hash = g_hash_table_new(g_str_hash, g_str_equal);
	entry->empty = TRUE;

}

static CollectManagerEntry *collect_manager_entry_new(const gchar *path)
{
	CollectManagerEntry *entry;

	entry = g_new0(CollectManagerEntry, 1);
	entry->path = g_strdup(path);
	collect_manager_entry_init_data(entry);

	collection_manager_entry_list = g_list_append(collection_manager_entry_list, entry);

	return entry;
}


static void collect_manager_entry_free(CollectManagerEntry *entry)
{
	collection_manager_entry_list = g_list_remove(collection_manager_entry_list, entry);

	collect_manager_entry_free_data(entry);

	g_free(entry->path);
	g_free(entry);
}

static void collect_manager_entry_reset(CollectManagerEntry *entry)
{
	collect_manager_entry_free_data(entry);
	collect_manager_entry_init_data(entry);
}

static void collect_manager_entry_add_action(CollectManagerEntry *entry, CollectManagerAction *action)
{

	CollectManagerAction *orig_action;

	entry->empty = FALSE;

	if (action->oldpath == nullptr)
		{
		/* add file */
		if (action->newpath == nullptr)
			{
			return;
			}

		orig_action = static_cast<CollectManagerAction *>(g_hash_table_lookup(entry->newpath_hash, action->newpath));
		if (orig_action)
			{
			/* target already exists */
			log_printf("collection manager failed to add another action for target %s in collection %s\n",
				action->newpath, entry->path);
			return;
			}
		entry->add_list = g_list_append(entry->add_list, action);
		g_hash_table_insert(entry->newpath_hash, action->newpath, action);
		collect_manager_action_ref(action);
		return;
		}

	orig_action = static_cast<CollectManagerAction *>(g_hash_table_lookup(entry->newpath_hash, action->oldpath));
	if (orig_action)
		{
		/* new action with the same file */
		CollectManagerAction *new_action = collect_manager_action_new(orig_action->oldpath, action->newpath, action->type);

		if (new_action->oldpath)
			{
			g_hash_table_steal(entry->oldpath_hash, orig_action->oldpath);
			g_hash_table_insert(entry->oldpath_hash, new_action->oldpath, new_action);
			}
		else
			{
			GList *work = g_list_find(entry->add_list, orig_action);
			work->data = new_action;
			}

		g_hash_table_steal(entry->newpath_hash, orig_action->newpath);
		if (new_action->newpath)
			{
			g_hash_table_insert(entry->newpath_hash, new_action->newpath, new_action);
			}
		collect_manager_action_unref(orig_action);
		return;
		}


	orig_action = static_cast<CollectManagerAction *>(g_hash_table_lookup(entry->oldpath_hash, action->oldpath));
	if (orig_action)
		{
		/* another action for the same source, ignore */
		log_printf("collection manager failed to add another action for source %s in collection %s\n",
			action->oldpath, entry->path);
		return;
		}

	g_hash_table_insert(entry->oldpath_hash, action->oldpath, action);
	if (action->newpath)
		{
		g_hash_table_insert(entry->newpath_hash, action->newpath, action);
		}
	collect_manager_action_ref(action);
}

static gboolean collect_manager_process_action(CollectManagerEntry *entry, gchar **path_ptr)
{
	gchar *path = *path_ptr;
	CollectManagerAction *action;

	if (path == nullptr)
		{
		/* get new files */
		if (entry->add_list)
			{
			action = static_cast<CollectManagerAction *>(entry->add_list->data);
			g_assert(action->oldpath == nullptr);
			entry->add_list = g_list_remove(entry->add_list, action);
			path = g_strdup(action->newpath);
			g_hash_table_remove(entry->newpath_hash, path);
			collect_manager_action_unref(action);
			}
		*path_ptr = path;
		return (path != nullptr);
		}

	action = static_cast<CollectManagerAction *>(g_hash_table_lookup(entry->oldpath_hash, path));

	if (action)
		{
		strcpy(*path_ptr, action->newpath);
		return TRUE;
		}

	return FALSE; /* no change */
}

static void collect_manager_refresh()
{
	GList *work;
	FileData *dir_fd;

	dir_fd = file_data_new_dir(get_collections_dir());
	g_autoptr(FileDataList) list = nullptr;
	filelist_read(dir_fd, &list, nullptr);
	file_data_unref(dir_fd);

	work = collection_manager_entry_list;
	while (work && list)
		{
		CollectManagerEntry *entry;
		GList *list_step;

		entry = static_cast<CollectManagerEntry *>(work->data);
		work = work->next;

		list_step = list;
		while (list_step && entry)
			{
			FileData *fd;

			fd = static_cast<FileData *>(list_step->data);
			list_step = list_step->next;

			if (strcmp(fd->path, entry->path) == 0)
				{
				list = g_list_remove(list, fd);
				file_data_unref(fd);

				entry = nullptr;
				}
			else
				{
				collect_manager_entry_free(entry);

				entry = nullptr;
				}
			}
		}

	work = list;
	while (work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		collect_manager_entry_new(fd->path);
		}
}

static void collect_manager_process_actions(gint max)
{
	if (collection_manager_action_list) DEBUG_1("collection manager processing actions");

	while (collection_manager_action_list != nullptr && max > 0)
		{
		CollectManagerAction *action;
		GList *work;

		action = static_cast<CollectManagerAction *>(collection_manager_action_list->data);
		work = collection_manager_entry_list;
		while (work)
			{
			CollectManagerEntry *entry;

			entry = static_cast<CollectManagerEntry *>(work->data);
			work = work->next;

			if (action->type == COLLECTION_MANAGER_UPDATE)
				{
				collect_manager_entry_add_action(entry, action);
				}
			else if (action->oldpath && action->newpath &&
				 strcmp(action->newpath, entry->path) == 0)
				{
				/* convert action to standard add format */
				g_free(action->newpath);
				if (action->type == COLLECTION_MANAGER_ADD)
					{
					action->newpath = action->oldpath;
					action->oldpath = nullptr;
					}
				else if (action->type == COLLECTION_MANAGER_REMOVE)
					{
					action->newpath = nullptr;
					}
				collect_manager_entry_add_action(entry, action);
				}

			max--;
			}

		if (action->type != COLLECTION_MANAGER_UPDATE &&
		    action->oldpath && action->newpath)
			{
			log_printf("collection manager failed to %s %s for collection %s\n",
				(action->type == COLLECTION_MANAGER_ADD) ? "add" : "remove",
				action->oldpath, action->newpath);
			}

		if (collection_manager_action_tail == collection_manager_action_list)
			{
			collection_manager_action_tail = nullptr;
			}
		collection_manager_action_list = g_list_remove(collection_manager_action_list, action);
		collect_manager_action_unref(action);
		}
}

static gboolean collect_manager_process_entry(CollectManagerEntry *entry)
{
	CollectionData *cd;

	if (entry->empty) return FALSE;

	cd = collection_new(entry->path);
	(void) collection_load_private(cd, entry->path, COLLECTION_LOAD_NONE);

	collection_unref(cd);

	return TRUE;
}

static gboolean collect_manager_process_entry_list()
{
	GList *work;

	work = collection_manager_entry_list;
	while (work)
		{
		CollectManagerEntry *entry;

		entry = static_cast<CollectManagerEntry *>(work->data);
		work = work->next;
		if (collect_manager_process_entry(entry)) return TRUE;
		}

	return FALSE;
}



static gboolean collect_manager_process_cb(gpointer)
{
	if (collection_manager_action_list) collect_manager_refresh();
	collect_manager_process_actions(COLLECT_MANAGER_ACTIONS_PER_IDLE);
	if (collection_manager_action_list) return G_SOURCE_CONTINUE;

	if (collect_manager_process_entry_list()) return G_SOURCE_CONTINUE;

	DEBUG_1("collection manager is up to date");
	return G_SOURCE_REMOVE;
}

static gboolean collect_manager_timer_cb(gpointer user_data)
{
	DEBUG_1("collection manager timer expired");

	g_idle_add_full(G_PRIORITY_LOW, collect_manager_process_cb, nullptr, nullptr);

	auto *collection_manager_timer_id = static_cast<guint *>(user_data);
	*collection_manager_timer_id = 0;

	return G_SOURCE_REMOVE;
}

static void collect_manager_timer_push(gint stop)
{
	static guint collection_manager_timer_id = 0; /* event source id */

	if (collection_manager_timer_id)
		{
		if (!stop) return;

		g_clear_handle_id(&collection_manager_timer_id, g_source_remove);
		}

	if (!stop)
		{
		collection_manager_timer_id = g_timeout_add(COLLECT_MANAGER_FLUSH_DELAY,
		                                            collect_manager_timer_cb, &collection_manager_timer_id);
		DEBUG_1("collection manager timer started");
		}
}

static void collect_manager_add_action(CollectManagerAction *action)
{
	if (!action) return;

	/* we keep track of the list's tail to keep this a n(1) operation */

	if (collection_manager_action_tail)
		{
		collection_manager_action_tail = g_list_append(collection_manager_action_tail, action);
		collection_manager_action_tail = collection_manager_action_tail->next;
		}
	else
		{
		collection_manager_action_list = g_list_append(collection_manager_action_list, action);
		collection_manager_action_tail = collection_manager_action_list;
		}

	collect_manager_timer_push(FALSE);
}

/**
 * @brief These are used to update collections contained in user's collection
 * folder when moving or renaming files.
 * also handles:
 *   deletes file when newpath == NULL
 *   adds file when oldpath == NULL
 */
void collect_manager_moved(FileData *fd)
{
	CollectManagerAction *action;
	const gchar *oldpath = fd->change->source;
	const gchar *newpath = fd->change->dest;

	action = collect_manager_action_new(oldpath, newpath, COLLECTION_MANAGER_UPDATE);
	collect_manager_add_action(action);
}

/**
 * @brief Add from a specific collection
 */
void collect_manager_add(FileData *fd, const gchar *collection)
{
	CollectManagerAction *action;
	CollectWindow *cw;

	if (!fd || !collection) return;

	cw = collection_window_find_by_path(collection);
	if (cw)
		{
		if (collection_list_find_fd(cw->cd->list, fd) == nullptr)
			{
			collection_add(cw->cd, fd, FALSE);
			}
		return;
		}

	action = collect_manager_action_new(fd->path, collection, COLLECTION_MANAGER_ADD);
	collect_manager_add_action(action);
}

/**
 * @brief Removing from a specific collection
 */
void collect_manager_remove(FileData *fd, const gchar *collection)
{
	CollectManagerAction *action;
	CollectWindow *cw;

	if (!fd || !collection) return;

	cw = collection_window_find_by_path(collection);
	if (cw)
		{
		while (collection_remove(cw->cd, fd));
		return;
		}

	action = collect_manager_action_new(fd->path, collection, COLLECTION_MANAGER_REMOVE);
	collect_manager_add_action(action);
}

/**
 * @brief Commit pending operations to disk
 */
void collect_manager_flush()
{
	collect_manager_timer_push(TRUE);

	DEBUG_1("collection manager flushing");
	while (collect_manager_process_cb(nullptr));
}

void collect_manager_notify_cb(FileData *fd, NotifyType type, gpointer)
{
	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify collect_manager: %s %04x", fd->path, type);
	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
			collect_manager_moved(fd);
			break;
		case FILEDATA_CHANGE_COPY:
			break;
		case FILEDATA_CHANGE_RENAME:
			collect_manager_moved(fd);
			break;
		case FILEDATA_CHANGE_DELETE:
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}
}

/**
 * @brief Creates sorted list of collections
 * @param[out] names_exc sorted list of collections names excluding extension
 * @param[out] names_inc sorted list of collections names including extension
 * @param[out] paths sorted list of collection paths
 *
 * Lists of type gchar.
 * Used lists must be freed with data.
 */
void collect_manager_list(GList **names_exc, GList **names_inc, GList **paths)
{
	FileData *dir_fd;

	if (names_exc == nullptr && names_inc == nullptr && paths == nullptr)
		{
		return;
		}

	dir_fd = file_data_new_dir((get_collections_dir()));

	g_autoptr(FileDataList) list = nullptr;
	filelist_read(dir_fd, &list, nullptr);

	for (GList *work = list; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		const gchar *filename = filename_from_path(fd->path);

		if (file_extension_match(filename, GQ_COLLECTION_EXT))
			{
			if (names_exc != nullptr)
				{
				*names_exc = g_list_insert_sorted(*names_exc, remove_extension_from_path(filename),
				                                  reinterpret_cast<GCompareFunc>(g_strcmp0));
				}
			if (names_inc != nullptr)
				{
				*names_inc = g_list_insert_sorted(*names_inc, g_strdup(filename),
				                                  reinterpret_cast<GCompareFunc>(g_strcmp0));
				}
			if (paths != nullptr)
				{
				*paths = g_list_insert_sorted(*paths, g_strdup(fd->path),
				                              reinterpret_cast<GCompareFunc>(g_strcmp0));
				}
			}
		}
}

gchar *collection_manager_path_by_index(gint index)
{
	if (index < 0) return nullptr;

	GList *collection_list = nullptr;
	collect_manager_list(&collection_list, nullptr, nullptr);

	auto *collection_name = static_cast<gchar *>(g_list_nth_data(collection_list, index));
	gchar *path = collection_path(collection_name);
	g_list_free_full(collection_list, g_free);

	return path;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
