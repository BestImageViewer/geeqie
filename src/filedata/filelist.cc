/*
 * Copyright (C) 2024 The Geeqie Team
 *
 * Author: Omari Stephens
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
 *
 *
 * FileList functionality for FileData.
 *
 */


#include "filedata.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#include <glib.h>

#include "cache.h"
#include "debug.h"
#include "filefilter.h"
#include "main.h"
#include "options.h"
#include "thumb-standard.h"
#include "typedefs.h"
#include "ui-fileops.h"


// Globals.
SortType FileData::FileList::sort_method = SORT_NONE;
gboolean FileData::FileList::sort_ascend = TRUE;
gboolean FileData::FileList::sort_case = TRUE;


/*
 *-----------------------------------------------------------------------------
 * handling sidecars in filelist
 *-----------------------------------------------------------------------------
 */

GList *FileData::FileList::filter_out_sidecars(GList *flist)
{
	GList *work = flist;
	GList *flist_filtered = nullptr;

	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		work = work->next;
		if (fd->parent) /* remove fd's that are children */
			::file_data_unref(fd);
		else
			flist_filtered = g_list_prepend(flist_filtered, fd);
		}
	g_list_free(flist);

	return flist_filtered;
}

/*
 *-----------------------------------------------------------------------------
 * the main filelist function
 *-----------------------------------------------------------------------------
 */
gboolean FileData::FileList::is_hidden_file(const gchar *name)
{
	if (name[0] != '.') return FALSE;
	if (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')) return FALSE;
	return TRUE;
}

gboolean FileData::FileList::read_list_real(const gchar *dir_path, GList **files, GList **dirs, gboolean follow_symlinks)
{
	DIR *dp;
	struct dirent *dir;
	gchar *pathl;
	GList *dlist = nullptr;
	GList *flist = nullptr;
	GList *xmp_files = nullptr;
	gint (*stat_func)(const gchar *path, struct stat *buf);
	GHashTable *basename_hash = nullptr;

	g_assert(files || dirs);

	if (files) *files = nullptr;
	if (dirs) *dirs = nullptr;

	pathl = path_from_utf8(dir_path);
	if (!pathl) return FALSE;

	dp = opendir(pathl);
	if (dp == nullptr)
		{
		g_free(pathl);
		return FALSE;
		}

	if (files) basename_hash = file_data_basename_hash_new();

	if (follow_symlinks)
		stat_func = stat;
	else
		stat_func = lstat;

	while ((dir = readdir(dp)) != nullptr)
		{
		struct stat ent_sbuf;
		const gchar *name = dir->d_name;
		gchar *filepath;

		if (!options->file_filter.show_hidden_files && is_hidden_file(name))
			continue;

		filepath = g_build_filename(pathl, name, NULL);
		if (stat_func(filepath, &ent_sbuf) >= 0)
			{
			if (S_ISDIR(ent_sbuf.st_mode))
				{
				/* we ignore the .thumbnails dir for cleanliness */
				if (dirs &&
				    (name[0] != '.' || (name[1] != '\0' && (name[1] != '.' || name[2] != '\0'))) &&
				    strcmp(name, GQ_CACHE_LOCAL_THUMB) != 0 &&
				    strcmp(name, GQ_CACHE_LOCAL_METADATA) != 0 &&
				    strcmp(name, THUMB_FOLDER_LOCAL) != 0)
					{
					dlist = g_list_prepend(dlist, file_data_new_local(filepath, &ent_sbuf, TRUE));
					}
				}
			else
				{
				if (files && filter_name_exists(name))
					{
					FileData *fd = file_data_new_local(filepath, &ent_sbuf, FALSE);
					flist = g_list_prepend(flist, fd);
					if (fd->sidecar_priority && !fd->disable_grouping)
						{
						if (strcmp(fd->extension, ".xmp") != 0)
							file_data_basename_hash_insert(basename_hash, fd);
						else
							xmp_files = g_list_append(xmp_files, fd);
						}
					}
				}
			}
		else
			{
			if (errno == EOVERFLOW)
				{
				log_printf("stat(): EOVERFLOW, skip '%s'", filepath);
				}
			}
		g_free(filepath);
		}

	closedir(dp);

	g_free(pathl);

	if (xmp_files)
		{
		g_list_foreach(xmp_files,file_data_basename_hash_insert_cb,basename_hash);
		g_list_free(xmp_files);
		}

	if (dirs) *dirs = dlist;

	if (files)
		{
		g_hash_table_foreach(basename_hash, file_data_basename_hash_to_sidecars, nullptr);

		*files = filter_out_sidecars(flist);
		}
	if (basename_hash) file_data_basename_hash_free(basename_hash);

	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * filelist sorting
 *-----------------------------------------------------------------------------
 */


gint FileData::FileList::sort_compare_filedata(const FileData *fa, const FileData *fb)
{
	gint ret;
	if (!sort_ascend)
		{
		std::swap(fa, fb);
		}

	switch (sort_method)
		{
		case SORT_NAME:
			break;
		case SORT_SIZE:
			if (fa->size < fb->size) return -1;
			if (fa->size > fb->size) return 1;
			/* fall back to name */
			break;
		case SORT_TIME:
			if (fa->date < fb->date) return -1;
			if (fa->date > fb->date) return 1;
			/* fall back to name */
			break;
		case SORT_CTIME:
			if (fa->cdate < fb->cdate) return -1;
			if (fa->cdate > fb->cdate) return 1;
			/* fall back to name */
			break;
		case SORT_EXIFTIME:
			if (fa->exifdate < fb->exifdate) return -1;
			if (fa->exifdate > fb->exifdate) return 1;
			/* fall back to name */
			break;
		case SORT_EXIFTIMEDIGITIZED:
			if (fa->exifdate_digitized < fb->exifdate_digitized) return -1;
			if (fa->exifdate_digitized > fb->exifdate_digitized) return 1;
			/* fall back to name */
			break;
		case SORT_RATING:
			if (fa->rating < fb->rating) return -1;
			if (fa->rating > fb->rating) return 1;
			/* fall back to name */
			break;
		case SORT_CLASS:
			if (fa->format_class < fb->format_class) return -1;
			if (fa->format_class > fb->format_class) return 1;
			/* fall back to name */
			break;
		case SORT_NUMBER:
			ret = strcmp(fa->collate_key_name_natural, fb->collate_key_name_natural);
			if (ret != 0) return ret;
			/* fall back to name */
			break;
		default:
			break;
		}

	if (sort_case)
		ret = strcmp(fa->collate_key_name, fb->collate_key_name);
	else
		ret = strcmp(fa->collate_key_name_nocase, fb->collate_key_name_nocase);

	if (ret != 0) return ret;

	/* do not return 0 unless the files are really the same
	   file_data_pool ensures that original_path is unique
	*/
	return strcmp(fa->original_path, fb->original_path);
}

gint FileData::FileList::sort_compare_filedata_full(const FileData *fa, const FileData *fb, SortType method, gboolean ascend)
{
	sort_method = method;
	sort_ascend = ascend;
	return sort_compare_filedata(fa, fb);
}

gint FileData::FileList::sort_file_cb(gconstpointer a, gconstpointer b)
{
	return FileData::FileList::sort_compare_filedata(static_cast<const FileData *>(a), static_cast<const FileData *>(b));
}

GList *FileData::FileList::sort_full(GList *list, SortType method, gboolean ascend, gboolean case_sensitive, GCompareFunc cb)
{
	sort_method = method;
	sort_ascend = ascend;
	sort_case = case_sensitive;
	return g_list_sort(list, cb);
}

GList *FileData::FileList::sort(GList *list, SortType method, gboolean ascend, gboolean case_sensitive)
{
	return sort_full(list, method, ascend, case_sensitive, sort_file_cb);
}

gboolean FileData::FileList::read_list(FileData *dir_fd, GList **files, GList **dirs)
{
	return read_list_real(dir_fd->path, files, dirs, TRUE);
}

gboolean FileData::FileList::read_list_lstat(FileData *dir_fd, GList **files, GList **dirs)
{
	return read_list_real(dir_fd->path, files, dirs, FALSE);
}

void FileData::FileList::free_list(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		::file_data_unref((FileData *)work->data);
		work = work->next;
		}

	g_list_free(list);
}


GList *FileData::FileList::copy(GList *list)
{
	GList *new_list = nullptr;

	for (GList *work = list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		new_list = g_list_prepend(new_list, ::file_data_ref(fd));
		}

	return g_list_reverse(new_list);
}

GList *FileData::FileList::from_path_list(GList *list)
{
	GList *new_list = nullptr;
	GList *work;

	work = list;
	while (work)
		{
		gchar *path;

		path = static_cast<gchar *>(work->data);
		work = work->next;

		new_list = g_list_prepend(new_list, file_data_new_group(path));
		}

	return g_list_reverse(new_list);
}

GList *FileData::FileList::to_path_list(GList *list)
{
	GList *new_list = nullptr;
	GList *work;

	work = list;
	while (work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		new_list = g_list_prepend(new_list, g_strdup(fd->path));
		}

	return g_list_reverse(new_list);
}

GList *FileData::FileList::filter(GList *list, gboolean is_dir_list)
{
	GList *work;

	if (!is_dir_list && options->file_filter.disable && options->file_filter.show_hidden_files) return list;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		const gchar *name = fd->name;
		GList *link = work;
		work = work->next;

		if ((!options->file_filter.show_hidden_files && is_hidden_file(name)) ||
		    (!is_dir_list && !filter_name_exists(name)) ||
		    (is_dir_list && name[0] == '.' && (strcmp(name, GQ_CACHE_LOCAL_THUMB) == 0 ||
						       strcmp(name, GQ_CACHE_LOCAL_METADATA) == 0)) )
			{
			list = g_list_remove_link(list, link);
			::file_data_unref(fd);
			g_list_free(link);
			}
		}

	return list;
}

/*
 *-----------------------------------------------------------------------------
 * filelist recursive
 *-----------------------------------------------------------------------------
 */

gint FileData::FileList::sort_path_cb(gconstpointer a, gconstpointer b)
{
	return CASE_SORT(((FileData *)a)->path, ((FileData *)b)->path);
}

GList *FileData::FileList::sort_path(GList *list)
{
	return g_list_sort(list, sort_path_cb);
}

void FileData::FileList::recursive_append(GList **list, GList *dirs)
{
	GList *work;

	work = dirs;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *f;
		GList *d;

		if (read_list(fd, &f, &d))
			{
			f = filter(f, FALSE);
			f = sort_path(f);
			*list = g_list_concat(*list, f);

			d = filter(d, TRUE);
			d = sort_path(d);
			recursive_append(list, d);
			free_list(d);
			}

		work = work->next;
		}
}

void FileData::FileList::recursive_append_full(GList **list, GList *dirs, SortType method, gboolean ascend, gboolean case_sensitive)
{
	GList *work;

	work = dirs;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *f;
		GList *d;

		if (read_list(fd, &f, &d))
			{
			f = filter(f, FALSE);
			f = sort_full(f, method, ascend, case_sensitive, sort_file_cb);
			*list = g_list_concat(*list, f);

			d = filter(d, TRUE);
			d = sort_path(d);
			recursive_append_full(list, d, method, ascend, case_sensitive);
			free_list(d);
			}

		work = work->next;
		}
}

GList *FileData::FileList::recursive(FileData *dir_fd)
{
	GList *list;
	GList *d;

	if (!read_list(dir_fd, &list, &d)) return nullptr;
	list = filter(list, FALSE);
	list = sort_path(list);

	d = filter(d, TRUE);
	d = sort_path(d);
	recursive_append(&list, d);
	free_list(d);

	return list;
}

GList *FileData::FileList::recursive_full(FileData *dir_fd, SortType method, gboolean ascend, gboolean case_sensitive)
{
	GList *list;
	GList *d;

	if (!read_list(dir_fd, &list, &d)) return nullptr;
	list = filter(list, FALSE);
	list = sort_full(list, method, ascend, case_sensitive, sort_file_cb);

	d = filter(d, TRUE);
	d = sort_path(d);
	recursive_append_full(&list, d, method, ascend, case_sensitive);
	free_list(d);

	return list;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
