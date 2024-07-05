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

// NOLINTBEGIN(readability-convert-member-functions-to-static)

#include "filedata.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <glib-object.h>
#include <grp.h>
#include <pwd.h>

#include <config.h>

#include "cache.h"
#include "debug.h"
#include "exif.h"
#include "filefilter.h"
#include "histogram.h"
#include "intl.h"
#include "main-defines.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "secure-save.h"
#include "trash.h"
#include "ui-fileops.h"

static gint sidecar_file_priority(const gchar *extension);
static void file_data_check_sidecars(const GList *basename_list);
static void file_data_disconnect_sidecar_file(FileData *target, FileData *sfd);


/*
 *-----------------------------------------------------------------------------
 * text conversion utils
 *-----------------------------------------------------------------------------
 */

gchar *FileData::text_from_size(gint64 size)
{
	gchar *a;
	gchar *b;
	gchar *s;
	gchar *d;
	gint l;
	gint n;
	gint i;

	/* what I would like to use is printf("%'d", size)
	 * BUT: not supported on every libc :(
	 */
	if (size > G_MAXINT)
		{
		/* the %lld conversion is not valid in all libcs, so use a simple work-around */
		a = g_strdup_printf("%d%09d", static_cast<guint>(size / 1000000000), static_cast<guint>(size % 1000000000));
		}
	else
		{
		a = g_strdup_printf("%d", static_cast<guint>(size));
		}
	l = strlen(a);
	n = (l - 1)/ 3;
	if (n < 1) return a;

	b = g_new(gchar, l + n + 1);

	s = a;
	d = b;
	i = l - n * 3;
	while (*s != '\0')
		{
		if (i < 1)
			{
			i = 3;
			*d = ',';
			d++;
			}

		*d = *s;
		s++;
		d++;
		i--;
		}
	*d = '\0';

	g_free(a);
	return b;
}

gchar *FileData::text_from_size_abrev(gint64 size)
{
	if (size < static_cast<gint64>(1024))
		{
		return g_strdup_printf(_("%d bytes"), static_cast<gint>(size));
		}
	if (size < static_cast<gint64>(1048576))
		{
		return g_strdup_printf(_("%.1f KiB"), static_cast<gdouble>(size) / 1024.0);
		}
	if (size < static_cast<gint64>(1073741824))
		{
		return g_strdup_printf(_("%.1f MiB"), static_cast<gdouble>(size) / 1048576.0);
		}

	/* to avoid overflowing the gdouble, do division in two steps */
	size /= 1048576;
	return g_strdup_printf(_("%.1f GiB"), static_cast<gdouble>(size) / 1024.0);
}

/* note: returned string is valid until next call to text_from_time() */
const gchar *FileData::text_from_time(time_t t)
{
	static gchar *ret = nullptr;
	gchar buf[128];
	gint buflen;
	struct tm *btime;
	GError *error = nullptr;

	btime = localtime(&t);

	/* the %x warning about 2 digit years is not an error */
	buflen = strftime(buf, sizeof(buf), "%x %X", btime);
	if (buflen < 1) return "";

	g_free(ret);
	ret = g_locale_to_utf8(buf, buflen, nullptr, nullptr, &error);
	if (error)
		{
		log_printf("Error converting locale strftime to UTF-8: %s\n", error->message);
		g_error_free(error);
		return "";
		}

	return ret;
}

/*
 *-----------------------------------------------------------------------------
 * changed files detection and notification
 *-----------------------------------------------------------------------------
 */

void FileData::file_data_increment_version(FileData *fd)
{
	fd->version++;
	fd->valid_marks = 0;
	if (fd->parent)
		{
		fd->parent->version++;
		fd->parent->valid_marks = 0;
		}
}

static gboolean file_data_check_changed_single_file(FileData *fd, struct stat *st)
{
	if (fd->size != st->st_size ||
	    fd->date != st->st_mtime)
		{
		fd->size = st->st_size;
		fd->date = st->st_mtime;
		fd->cdate = st->st_ctime;
		fd->mode = st->st_mode;
		if (fd->thumb_pixbuf) g_object_unref(fd->thumb_pixbuf);
		fd->thumb_pixbuf = nullptr;
		file_data_increment_version(fd);
		file_data_send_notification(fd, NOTIFY_REREAD);
		return TRUE;
		}
	return FALSE;
}

static gboolean file_data_check_changed_files_recursive(FileData *fd, struct stat *st)
{
	gboolean ret = FALSE;
	GList *work;

	ret = file_data_check_changed_single_file(fd, st);

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);
		struct stat st;
		work = work->next;

		if (!stat_utf8(sfd->path, &st))
			{
			fd->size = 0;
			fd->date = 0;
			file_data_ref(sfd);
			file_data_disconnect_sidecar_file(fd, sfd);
			ret = TRUE;
			file_data_increment_version(sfd);
			file_data_send_notification(sfd, NOTIFY_REREAD);
			file_data_unref(sfd);
			continue;
			}

		ret |= file_data_check_changed_files_recursive(sfd, &st);
		}
	return ret;
}


gboolean FileData::file_data_check_changed_files(FileData *fd)
{
	gboolean ret = FALSE;
	struct stat st;

	if (fd->parent) fd = fd->parent;

	if (!stat_utf8(fd->path, &st))
		{
		GList *sidecars;
		GList *work;
		FileData *sfd = nullptr;

		/* parent is missing, we have to rebuild whole group */
		ret = TRUE;
		fd->size = 0;
		fd->date = 0;

		/* file_data_disconnect_sidecar_file might delete the file,
		   we have to keep the reference to prevent this */
		sidecars = filelist_copy(fd->sidecar_files);
		::file_data_ref(fd);
		work = sidecars;
		while (work)
			{
			sfd = static_cast<FileData *>(work->data);
			work = work->next;

			file_data_disconnect_sidecar_file(fd, sfd);
			}
		file_data_check_sidecars(sidecars); /* this will group the sidecars back together */
		/* now we can release the sidecars */
		filelist_free(sidecars);
		file_data_increment_version(fd);
		file_data_send_notification(fd, NOTIFY_REREAD);
		::file_data_unref(fd);
		}
	else
		{
		ret |= file_data_check_changed_files_recursive(fd, &st);
		}

	return ret;
}

/*
 *-----------------------------------------------------------------------------
 * file name, extension, sorting, ...
 *-----------------------------------------------------------------------------
 */

static void file_data_set_collate_keys(FileData *fd)
{
	gchar *caseless_name;
	gchar *valid_name;

	valid_name = g_filename_display_name(fd->name);
	caseless_name = g_utf8_casefold(valid_name, -1);

	g_free(fd->collate_key_name);
	g_free(fd->collate_key_name_nocase);

 	fd->collate_key_name_natural = g_utf8_collate_key_for_filename(fd->name, -1);
 	fd->collate_key_name_nocase_natural = g_utf8_collate_key_for_filename(caseless_name, -1);
	fd->collate_key_name = g_utf8_collate_key(valid_name, -1);
	fd->collate_key_name_nocase = g_utf8_collate_key(caseless_name, -1);

	g_free(valid_name);
	g_free(caseless_name);
}

void FileData::set_path(const gchar *new_path)
{
	g_assert(new_path /* && *new_path*/); /* view_dir_tree uses FileData with zero length path */
	g_assert(context->file_data_pool);

	g_free(path);

	if (original_path)
		{
		g_hash_table_remove(context->file_data_pool, original_path);
		g_free(original_path);
		}

	g_assert(!g_hash_table_lookup(context->file_data_pool, new_path));

	original_path = g_strdup(new_path);
	g_hash_table_insert(context->file_data_pool, original_path, this);

	if (strcmp(new_path, G_DIR_SEPARATOR_S) == 0)
		{
		path = g_strdup(new_path);
		name = path;
		extension = name + 1;
		file_data_set_collate_keys(this);
		return;
		}

	path = g_strdup(new_path);
	name = filename_from_path(path);

	if (strcmp(name, "..") == 0)
		{
		gchar *dir = remove_level_from_path(new_path);
		g_free(path);
		path = remove_level_from_path(dir);
		g_free(dir);
		name = "..";
		extension = name + 2;
		file_data_set_collate_keys(this);
		return;
		}

	if (strcmp(name, ".") == 0)
		{
		g_free(path);
		path = remove_level_from_path(new_path);
		name = ".";
		extension = name + 1;
		file_data_set_collate_keys(this);
		return;
		}

	extension = registered_extension_from_path(path);
	if (extension == nullptr)
		{
		extension = name + strlen(name);
		}

	sidecar_priority = sidecar_file_priority(extension);
	file_data_set_collate_keys(this);
}

/*
 *-----------------------------------------------------------------------------
 * FileData context
 *-----------------------------------------------------------------------------
 */
std::mutex GlobalFileDataContext::s_instance_mutex;
std::unique_ptr<GlobalFileDataContext> GlobalFileDataContext::s_instance;

GlobalFileDataContext &GlobalFileDataContext::get_instance()
{
	std::lock_guard<std::mutex> instance_lock(GlobalFileDataContext::s_instance_mutex);
	if (GlobalFileDataContext::s_instance == nullptr)
		{
		GlobalFileDataContext::s_instance = std::make_unique<GlobalFileDataContext>();
		}

	return *GlobalFileDataContext::s_instance;
}

/*
 *-----------------------------------------------------------------------------
 * create or reuse Filedata
 *-----------------------------------------------------------------------------
 */
FileData *FileData::file_data_new(const gchar *path_utf8, struct stat *st, gboolean disable_sidecars, FileDataContext *context)
{
	if (context == nullptr)
		{
		context = FileData::DefaultFileDataContext();
		}

	FileData *fd;
	struct passwd *user;
	struct group *group;

	DEBUG_2("file_data_new: '%s' %d", path_utf8, disable_sidecars);

	if (S_ISDIR(st->st_mode)) disable_sidecars = TRUE;

	fd = static_cast<FileData *>(g_hash_table_lookup(context->file_data_pool, path_utf8));
	if (fd)
		{
		::file_data_ref(fd);
		}
	else
		{
		fd = static_cast<FileData *>(g_hash_table_lookup(context->planned_change_map, path_utf8));
		if (fd)
			{
			DEBUG_1("planned change: using %s -> %s", path_utf8, fd->path);
			if (!isfile(fd->path))
				{
				::file_data_ref(fd);
				::file_data_apply_ci(fd);
				}
			else
				{
				fd = nullptr;
				}
			}
		}

	if (fd)
		{
		if (disable_sidecars) ::file_data_disable_grouping(fd, TRUE);

#ifdef DEBUG_FILEDATA
		gboolean changed =
#endif
		file_data_check_changed_single_file(fd, st);

		DEBUG_2("file_data_pool hit: '%s' %s", fd->path, changed ? "(changed)" : "");

		return fd;
		}

	fd = g_new0(FileData, 1);
#ifdef DEBUG_FILEDATA
	context->global_file_data_count++;
	DEBUG_2("file data count++: %d", context->global_file_data_count);
#endif

	fd->context = context;
	fd->size = st->st_size;
	fd->date = st->st_mtime;
	fd->cdate = st->st_ctime;
	fd->mode = st->st_mode;
	fd->ref = 1;
	fd->magick = FD_MAGICK;
	fd->exifdate = 0;
	fd->rating = STAR_RATING_NOT_READ;
	fd->format_class = filter_file_get_class(path_utf8);
	fd->page_num = 0;
	fd->page_total = 0;

	user = getpwuid(st->st_uid);
	if (!user)
		{
		fd->owner = g_strdup_printf("%u", st->st_uid);
		}
	else
		{
		fd->owner = g_strdup(user->pw_name);
		}

	group = getgrgid(st->st_gid);
	if (!group)
		{
		fd->group = g_strdup_printf("%u", st->st_gid);
		}
	else
		{
		fd->group = g_strdup(group->gr_name);
		}

	fd->sym_link = get_symbolic_link(path_utf8);

	if (disable_sidecars) fd->disable_grouping = TRUE;

	fd->set_path(path_utf8); /* set path, name, collate_key_*, original_path */

	return fd;
}

FileData *FileData::file_data_new_local(const gchar *path, struct stat *st, gboolean disable_sidecars, FileDataContext *context)
{
	gchar *path_utf8 = path_to_utf8(path);
	FileData *ret = file_data_new(path_utf8, st, disable_sidecars, context);

	g_free(path_utf8);
	return ret;
}

FileData *FileData::file_data_new_simple(const gchar *path_utf8, FileDataContext *context)
{
	struct stat st{};

	if (!stat_utf8(path_utf8, &st))
		{
		st.st_size = 0;
		st.st_mtime = 0;
		}

	if (context == nullptr)
		{
		context = FileData::DefaultFileDataContext();
		}

	auto *fd = static_cast<FileData *>(g_hash_table_lookup(context->file_data_pool, path_utf8));
	if (!fd)
		{
		fd = file_data_new(path_utf8, &st, TRUE, context);
		}
	else
		{
		::file_data_ref(fd);
		}

	return fd;
}

void FileData::read_exif_time_data(FileData *file)
{
	if (file->exifdate > 0)
		{
		DEBUG_1("%s read_exif_time_data: Already exists for %s", get_exec_time(), file->path);
		return;
		}

	if (!file->exif)
		{
		exif_read_fd(file);
		}

	if (file->exif)
		{
		gchar *tmp = exif_get_data_as_text(file->exif, "Exif.Photo.DateTimeOriginal");
		DEBUG_2("%s read_exif_time_data: reading %p %s", get_exec_time(), (void *)file, file->path);

		if (tmp)
			{
			struct tm time_str;
			uint year;
			uint month;
			uint day;
			uint hour;
			uint min;
			uint sec;

			sscanf(tmp, "%4u:%2u:%2u %2u:%2u:%2u", &year, &month, &day, &hour, &min, &sec);
			time_str.tm_year  = year - 1900;
			time_str.tm_mon   = month - 1;
			time_str.tm_mday  = day;
			time_str.tm_hour  = hour;
			time_str.tm_min   = min;
			time_str.tm_sec   = sec;
			time_str.tm_isdst = 0;

			file->exifdate = mktime(&time_str);
			g_free(tmp);
			}
		}
}

void FileData::read_exif_time_digitized_data(FileData *file)
{
	if (file->exifdate_digitized > 0)
		{
		DEBUG_1("%s read_exif_time_digitized_data: Already exists for %s", get_exec_time(), file->path);
		return;
		}

	if (!file->exif)
		{
		exif_read_fd(file);
		}

	if (file->exif)
		{
		gchar *tmp = exif_get_data_as_text(file->exif, "Exif.Photo.DateTimeDigitized");
		DEBUG_2("%s read_exif_time_digitized_data: reading %p %s", get_exec_time(), (void *)file, file->path);

		if (tmp)
			{
			struct tm time_str;
			uint year;
			uint month;
			uint day;
			uint hour;
			uint min;
			uint sec;

			sscanf(tmp, "%4u:%2u:%2u %2u:%2u:%2u", &year, &month, &day, &hour, &min, &sec);
			time_str.tm_year  = year - 1900;
			time_str.tm_mon   = month - 1;
			time_str.tm_mday  = day;
			time_str.tm_hour  = hour;
			time_str.tm_min   = min;
			time_str.tm_sec   = sec;
			time_str.tm_isdst = 0;

			file->exifdate_digitized = mktime(&time_str);
			g_free(tmp);
			}
		}
}

void FileData::read_rating_data(FileData *file)
{
	gchar *rating_str;

	rating_str = metadata_read_string(file, RATING_KEY, METADATA_PLAIN);
	if (rating_str)
		{
		file->rating = atoi(rating_str);
		g_free(rating_str);
		}
	else
		{
		file->rating = 0;
		}
}

FileData *FileData::file_data_new_no_grouping(const gchar *path_utf8, FileDataContext *context)
{
	struct stat st;

	if (!stat_utf8(path_utf8, &st))
		{
		st.st_size = 0;
		st.st_mtime = 0;
		}

	return file_data_new(path_utf8, &st, TRUE, context);
}

FileData *FileData::file_data_new_dir(const gchar *path_utf8, FileDataContext *context)
{
	struct stat st;

	if (!stat_utf8(path_utf8, &st))
		{
		st.st_size = 0;
		st.st_mtime = 0;
		}
	else
		/* dir or non-existing yet */
		g_assert(S_ISDIR(st.st_mode));

	return file_data_new(path_utf8, &st, TRUE, context);
}

/*
 *-----------------------------------------------------------------------------
 * reference counting
 *-----------------------------------------------------------------------------
 */

#ifdef DEBUG_FILEDATA
FileData *FileData::file_data_ref(const gchar *file, gint line)
#else
FileData *FileData::file_data_ref()
#endif
{
	FileData *fd = this;
        // TODO(xsdg): Do null checks at call-sites, if necessary.  The following is a no-op.
	if (fd == nullptr) return nullptr;
	if (fd->magick != FD_MAGICK)
#ifdef DEBUG_FILEDATA
		log_printf("Error: fd magick mismatch @ %s:%d  fd=%p", file, line, (void *)fd);
#else
		log_printf("Error: fd magick mismatch fd=%p", fd);
#endif
	g_assert(fd->magick == FD_MAGICK);
	fd->ref++;

#ifdef DEBUG_FILEDATA
	DEBUG_2("file_data_ref fd=%p (%d): '%s' @ %s:%d", (void *)fd, fd->ref, fd->path, file, line);
#else
	DEBUG_2("file_data_ref fd=%p (%d): '%s'", fd, fd->ref, fd->path);
#endif
	return fd;
}

/**
 * @brief Print ref. count and image name
 * @param
 *
 * Print image ref. count and full path name of all images in
 * the file_data_pool.
 *
 * Used only by debug_fd()
 */
void FileData::file_data_dump()
{
#ifdef DEBUG_FILEDATA
	FileDataContext *context = FileData::DefaultFileDataContext();
	GList *list = g_hash_table_get_values(context->file_data_pool);

	log_printf("%d", context->global_file_data_count);
	log_printf("%d", g_list_length(list));

	GList *work = list;
	while (work)
		{
		auto *fd = static_cast<FileData *>(work->data);
		log_printf("%-4d %s", fd->ref, fd->path);
		work = work->next;
		}

	g_list_free(list);

#endif
}

void FileData::file_data_free(FileData *fd)
{
	g_assert(fd->magick == FD_MAGICK);
	g_assert(fd->ref == 0);
	g_assert(!fd->locked);

#ifdef DEBUG_FILEDATA
	fd->context->global_file_data_count--;
	DEBUG_2("file data count--: %d", fd->context->global_file_data_count);
#endif

	metadata_cache_free(fd);
	g_hash_table_remove(fd->context->file_data_pool, fd->original_path);

	g_free(fd->path);
	g_free(fd->original_path);

	g_free(fd->collate_key_name_nocase);
	g_free(fd->collate_key_name);
	g_free(fd->collate_key_name_nocase_natural);
	g_free(fd->collate_key_name_natural);

	g_free(fd->extended_extension);
	if (fd->thumb_pixbuf) g_object_unref(fd->thumb_pixbuf);
	histmap_free(fd->histmap);
	g_free(fd->owner);
	g_free(fd->group);
	g_free(fd->sym_link);
	g_free(fd->format_name);
	g_assert(fd->sidecar_files == nullptr); /* sidecar files must be freed before calling this */

	::file_data_change_info_free(nullptr, fd);
	g_free(fd);
}

/**
 * @brief Checks if the FileData is referenced
 *
 * Checks the refcount and whether the FileData is locked.
 */
static gboolean file_data_check_has_ref(FileData *fd)
{
	return fd->ref > 0 || fd->locked;
}

/**
 * @brief Consider freeing a FileData.
 *
 * This function will free a FileData and its children provided that neither its parent nor it has
 * a positive refcount, and provided that neither is locked.
 */
void FileData::file_data_consider_free(FileData *fd)
{
	GList *work;
	FileData *parent = fd->parent ? fd->parent : fd;

	g_assert(fd->magick == FD_MAGICK);
	if (file_data_check_has_ref(fd)) return;
	if (file_data_check_has_ref(parent)) return;

	work = parent->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);
		if (file_data_check_has_ref(sfd)) return;
		work = work->next;
		}

	/* Neither the parent nor the siblings are referenced, so we can free everything */
	DEBUG_2("file_data_consider_free: deleting '%s', parent '%s'",
		fd->path, fd->parent ? parent->path : "-");

	g_list_free_full(parent->sidecar_files, reinterpret_cast<GDestroyNotify>(FileData::file_data_free));
	parent->sidecar_files = nullptr;

	file_data_free(parent);
}

#ifdef DEBUG_FILEDATA
void FileData::file_data_unref(const gchar *file, gint line)
#else
void FileData::file_data_unref()
#endif
{
	FileData *fd = this;  // TODO(xsdg): clean this up across the board.
	if (fd == nullptr) return;
	if (fd->magick != FD_MAGICK)
#ifdef DEBUG_FILEDATA
		log_printf("Error: fd magick mismatch @ %s:%d  fd=%p", file, line, (void *)fd);
#else
		log_printf("Error: fd magick mismatch fd=%p", fd);
#endif
	g_assert(fd->magick == FD_MAGICK);

	fd->ref--;
#ifdef DEBUG_FILEDATA
	DEBUG_2("file_data_unref fd=%p (%d:%d): '%s' @ %s:%d", (void *)fd, fd->ref, fd->locked, fd->path,
		file, line);
#else
	DEBUG_2("file_data_unref fd=%p (%d:%d): '%s'", fd, fd->ref, fd->locked, fd->path);
#endif

	// Free FileData if it's no longer ref'd
	file_data_consider_free(fd);
}

/**
 * @brief Lock the FileData in memory.
 *
 * This allows the caller to prevent a FileData from being freed, even after its refcount is zero.
 * This is intended to be used in cases where a FileData _should_ stay in memory as an optimization,
 * even if the code would continue to function properly even if the FileData were freed.  Code that
 * _requires_ the FileData to remain in memory should continue to use file_data_(un)ref.
 * <p />
 * Note: This differs from file_data_ref in that the behavior is reentrant -- after N calls to
 * file_data_lock, a single call to file_data_unlock will unlock the FileData.
 */
void FileData::file_data_lock(FileData *fd)
{
	if (fd == nullptr) return;
	if (fd->magick != FD_MAGICK) log_printf("Error: fd magick mismatch fd=%p", (void *)fd);

	g_assert(fd->magick == FD_MAGICK);
	fd->locked = TRUE;

	DEBUG_2("file_data_ref fd=%p (%d): '%s'", (void *)fd, fd->ref, fd->path);
}

/**
 * @brief Reset the maintain-FileData-in-memory lock
 *
 * This again allows the FileData to be freed when its refcount drops to zero.  Automatically frees
 * the FileData if its refcount is already zero (which will happen if the lock is the only thing
 * keeping it from being freed.
 */
void FileData::file_data_unlock(FileData *fd)
{
	if (fd == nullptr) return;
	if (fd->magick != FD_MAGICK) log_printf("Error: fd magick mismatch fd=%p", (void *)fd);

	g_assert(fd->magick == FD_MAGICK);
	fd->locked = FALSE;

	// Free FileData if it's no longer ref'd
	file_data_consider_free(fd);
}

/**
 * @brief Lock all of the FileDatas in the provided list
 *
 * @see file_data_lock(#FileData)
 */
void FileData::file_data_lock_list(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		::file_data_lock(fd);
		}
}

/**
 * @brief Unlock all of the FileDatas in the provided list
 *
 * @see #file_data_unlock(#FileData)
 */
void FileData::file_data_unlock_list(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		::file_data_unlock(fd);
		}
}

/*
 *-----------------------------------------------------------------------------
 * sidecar file info struct
 *-----------------------------------------------------------------------------
 */

static gint file_data_sort_by_ext(gconstpointer a, gconstpointer b)
{
	auto fda = static_cast<const FileData *>(a);
	auto fdb = static_cast<const FileData *>(b);

	if (fda->sidecar_priority < fdb->sidecar_priority) return -1;
	if (fda->sidecar_priority > fdb->sidecar_priority) return 1;

	return strcmp(fdb->extension, fda->extension);
}


static gint sidecar_file_priority(const gchar *extension)
{
	gint i = 1;
	GList *work;

	if (extension == nullptr)
		return 0;

	work = sidecar_ext_get_list();

	while (work) {
		auto ext = static_cast<gchar *>(work->data);

		work = work->next;
		if (g_ascii_strcasecmp(extension, ext) == 0) return i;
		i++;
	}
	return 0;
}

static void file_data_check_sidecars(const GList *basename_list)
{
	/* basename_list contains the new group - first is the parent, then sorted sidecars */
	/* all files in the list have ref count > 0 */

	const GList *work;
	GList *s_work;
	GList *new_sidecars;
	FileData *parent_fd;

	if (!basename_list) return;


	DEBUG_2("basename start");
	work = basename_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		g_assert(fd->magick == FD_MAGICK);
		DEBUG_2("basename: %p %s", (void *)fd, fd->name);
		if (fd->parent)
			{
			g_assert(fd->parent->magick == FD_MAGICK);
			DEBUG_2("                  parent: %p", (void *)fd->parent);
			}
		s_work = fd->sidecar_files;
		while (s_work)
			{
			auto sfd = static_cast<FileData *>(s_work->data);
			s_work = s_work->next;
			g_assert(sfd->magick == FD_MAGICK);
			DEBUG_2("                  sidecar: %p %s", (void *)sfd, sfd->name);
			}

		g_assert(fd->parent == nullptr || fd->sidecar_files == nullptr);
		}

	parent_fd = static_cast<FileData *>(basename_list->data);

	/* check if the second and next entries of basename_list are already connected
	   as sidecars of the first entry (parent_fd) */
	work = basename_list->next;
	s_work = parent_fd->sidecar_files;

	while (work && s_work)
		{
		if (work->data != s_work->data) break;
		work = work->next;
		s_work = s_work->next;
		}

	if (!work && !s_work)
		{
		DEBUG_2("basename no change");
		return; /* no change in grouping */
		}

	/* we have to regroup it */

	/* first, disconnect everything and send notification*/

	work = basename_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		g_assert(fd->parent == nullptr || fd->sidecar_files == nullptr);

		if (fd->parent)
			{
			FileData *old_parent = fd->parent;
			g_assert(old_parent->parent == nullptr || old_parent->sidecar_files == nullptr);
			file_data_ref(old_parent);
			file_data_disconnect_sidecar_file(old_parent, fd);
			file_data_send_notification(old_parent, NOTIFY_REREAD);
			file_data_unref(old_parent);
			}

		while (fd->sidecar_files)
			{
			auto sfd = static_cast<FileData *>(fd->sidecar_files->data);
			g_assert(sfd->parent == nullptr || sfd->sidecar_files == nullptr);
			file_data_ref(sfd);
			file_data_disconnect_sidecar_file(fd, sfd);
			file_data_send_notification(sfd, NOTIFY_REREAD);
			file_data_unref(sfd);
			}
		file_data_send_notification(fd, NOTIFY_GROUPING);

		g_assert(fd->parent == nullptr && fd->sidecar_files == nullptr);
		}

	/* now we can form the new group */
	work = basename_list->next;
	new_sidecars = nullptr;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);
		g_assert(sfd->magick == FD_MAGICK);
		g_assert(sfd->parent == nullptr && sfd->sidecar_files == nullptr);
		sfd->parent = parent_fd;
		new_sidecars = g_list_prepend(new_sidecars, sfd);
		work = work->next;
		}
	g_assert(parent_fd->sidecar_files == nullptr);
	parent_fd->sidecar_files = g_list_reverse(new_sidecars);
	DEBUG_1("basename group changed for %s", parent_fd->path);
}


static void file_data_disconnect_sidecar_file(FileData *target, FileData *sfd)
{
	g_assert(target->magick == FD_MAGICK);
	g_assert(sfd->magick == FD_MAGICK);
	g_assert(g_list_find(target->sidecar_files, sfd));

	file_data_ref(target);
	file_data_ref(sfd);

	g_assert(sfd->parent == target);

	file_data_increment_version(sfd); /* increments both sfd and target */

	target->sidecar_files = g_list_remove(target->sidecar_files, sfd);
	sfd->parent = nullptr;
	g_free(sfd->extended_extension);
	sfd->extended_extension = nullptr;

	file_data_unref(target);
	file_data_unref(sfd);
}

/* disables / enables grouping for particular file, sends UPDATE notification */
void FileData::file_data_disable_grouping(FileData *fd, gboolean disable)
{
	if (!fd->disable_grouping == !disable) return;

	fd->disable_grouping = !!disable;

	if (disable)
		{
		if (fd->parent)
			{
			FileData *parent = ::file_data_ref(fd->parent);
			file_data_disconnect_sidecar_file(parent, fd);
			file_data_send_notification(parent, NOTIFY_GROUPING);
			::file_data_unref(parent);
			}
		else if (fd->sidecar_files)
			{
			GList *sidecar_files = filelist_copy(fd->sidecar_files);
			GList *work = sidecar_files;
			while (work)
				{
				auto sfd = static_cast<FileData *>(work->data);
				work = work->next;
				file_data_disconnect_sidecar_file(fd, sfd);
				file_data_send_notification(sfd, NOTIFY_GROUPING);
				}
			file_data_check_sidecars(sidecar_files); /* this will group the sidecars back together */
			filelist_free(sidecar_files);
			}
		else
			{
			file_data_increment_version(fd); /* the functions called in the cases above increments the version too */
			}
		}
	else
		{
		file_data_increment_version(fd);
		/* file_data_check_sidecars call is not necessary - the file will be re-grouped on next dir read */
		}
	file_data_send_notification(fd, NOTIFY_GROUPING);
}

void FileData::file_data_disable_grouping_list(GList *fd_list, gboolean disable)
{
	GList *work;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		::file_data_disable_grouping(fd, disable);
		work = work->next;
		}
}


/*
 *-----------------------------------------------------------------------------
 * basename hash - grouping of sidecars in filelist
 *-----------------------------------------------------------------------------
 */


GHashTable *FileData::file_data_basename_hash_new()
{
	return g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
}

GList *FileData::file_data_basename_hash_insert(GHashTable *basename_hash, FileData *fd)
{
	GList *list;
	gchar *basename = g_strndup(fd->path, fd->extension - fd->path);

	list = static_cast<GList *>(g_hash_table_lookup(basename_hash, basename));

	if (!list)
		{
		DEBUG_1("TG: basename_hash not found for %s",fd->path);
		const gchar *parent_extension = registered_extension_from_path(basename);

		if (parent_extension)
			{
			DEBUG_1("TG: parent extension %s",parent_extension);
			gchar *parent_basename = g_strndup(basename, parent_extension - basename);
			DEBUG_1("TG: parent basename %s",parent_basename);
			auto parent_fd = static_cast<FileData *>(g_hash_table_lookup(fd->context->file_data_pool, basename));
			if (parent_fd)
				{
				DEBUG_1("TG: parent fd found");
				list = static_cast<GList *>(g_hash_table_lookup(basename_hash, parent_basename));
				if (!g_list_find(list, parent_fd))
					{
					DEBUG_1("TG: parent fd doesn't fit");
					g_free(parent_basename);
					list = nullptr;
					}
				else
					{
					g_free(basename);
					basename = parent_basename;
					fd->extended_extension = g_strconcat(parent_extension, fd->extension, NULL);
					}
				}
			}
		}

	if (!g_list_find(list, fd))
		{
		list = g_list_insert_sorted(list, ::file_data_ref(fd), file_data_sort_by_ext);
		g_hash_table_insert(basename_hash, basename, list);
		}
	else
		{
		g_free(basename);
		}
	return list;
}

void FileData::file_data_basename_hash_insert_cb(gpointer fd, gpointer basename_hash)
{
	file_data_basename_hash_insert(static_cast<GHashTable *>(basename_hash), static_cast<FileData *>(fd));
}

void FileData::file_data_basename_hash_remove_list(gpointer, gpointer value, gpointer)
{
	filelist_free(static_cast<GList *>(value));
}

void FileData::file_data_basename_hash_free(GHashTable *basename_hash)
{
	g_hash_table_foreach(basename_hash, file_data_basename_hash_remove_list, nullptr);
	g_hash_table_destroy(basename_hash);
}

void FileData::file_data_basename_hash_to_sidecars(gpointer, gpointer value, gpointer)
{
	auto basename_list = static_cast<GList *>(value);
	file_data_check_sidecars(basename_list);
}


FileData *FileData::file_data_new_group(const gchar *path_utf8, FileDataContext *context)
{
	if (context == nullptr)
		{
		context = FileData::DefaultFileDataContext();
		}

	struct stat st{};
	if (!stat_utf8(path_utf8, &st))
		{
		st.st_size = 0;
		st.st_mtime = 0;
		}

	if (S_ISDIR(st.st_mode))
		return file_data_new(path_utf8, &st, TRUE, context);

	gchar *dir = remove_level_from_path(path_utf8);

        GList *files;
	FileList::read_list_real(dir, &files, nullptr, TRUE);

	auto *fd = static_cast<FileData *>(g_hash_table_lookup(context->file_data_pool, path_utf8));
	if (!fd)
		{
		fd = file_data_new(path_utf8, &st, TRUE, context);
		}
	else
		{
		::file_data_ref(fd);
		}

	filelist_free(files);
	g_free(dir);
	return fd;
}

/*
 *-----------------------------------------------------------------------------
 * file modification support
 *-----------------------------------------------------------------------------
 */


void FileData::file_data_change_info_free(FileDataChangeInfo *fdci, FileData *fd)
{
	if (!fdci && fd) fdci = fd->change;

	if (!fdci) return;

	g_free(fdci->source);
	g_free(fdci->dest);

	g_free(fdci);

	if (fd) fd->change = nullptr;
}

static gboolean file_data_can_write_directly(FileData *fd)
{
	return filter_name_is_writable(fd->extension);
}

static gboolean file_data_can_write_sidecar(FileData *fd)
{
	return filter_name_allow_sidecar(fd->extension) && !filter_name_is_writable(fd->extension);
}

gchar *FileData::file_data_get_sidecar_path(FileData *fd, gboolean existing_only)
{
	gchar *sidecar_path = nullptr;
	GList *work;

	if (!file_data_can_write_sidecar(fd)) return nullptr;

	work = fd->parent ? fd->parent->sidecar_files : fd->sidecar_files;
	gchar *extended_extension = g_strconcat(fd->parent ? fd->parent->extension : fd->extension, ".xmp", NULL);
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);
		work = work->next;
		if (g_ascii_strcasecmp(sfd->extension, ".xmp") == 0 || g_ascii_strcasecmp(sfd->extension, extended_extension) == 0)
			{
			sidecar_path = g_strdup(sfd->path);
			break;
			}
		}
	g_free(extended_extension);

	if (!existing_only && !sidecar_path)
		{
		if (options->metadata.sidecar_extended_name)
			sidecar_path = g_strconcat(fd->path, ".xmp", NULL);
		else
			{
			gchar *base = g_strndup(fd->path, fd->extension - fd->path);
			sidecar_path = g_strconcat(base, ".xmp", NULL);
			g_free(base);
			}
		}

	return sidecar_path;
}

/*
 * marks and orientation
 */

static FileData::GetMarkFunc file_data_get_mark_func[FILEDATA_MARKS_SIZE];
static FileData::SetMarkFunc file_data_set_mark_func[FILEDATA_MARKS_SIZE];
static gpointer file_data_mark_func_data[FILEDATA_MARKS_SIZE];
static GDestroyNotify file_data_destroy_mark_func[FILEDATA_MARKS_SIZE];

gboolean FileData::file_data_get_mark(FileData *fd, gint n)
{
	gboolean valid = (fd->valid_marks & (1 << n));

	if (file_data_get_mark_func[n] && !valid)
		{
		guint old = fd->marks;
		gboolean value = (file_data_get_mark_func[n])(fd, n, file_data_mark_func_data[n]);

		if (!value != !(fd->marks & (1 << n)))
			{
			fd->marks = fd->marks ^ (1 << n);
			}

		fd->valid_marks |= (1 << n);
		if (old && !fd->marks) /* keep files with non-zero marks in memory */
			{
			::file_data_unref(fd);
			}
		else if (!old && fd->marks)
			{
			::file_data_ref(fd);
			}
		}

	return !!(fd->marks & (1 << n));
}

guint FileData::file_data_get_marks(FileData *fd)
{
	gint i;
	for (i = 0; i < FILEDATA_MARKS_SIZE; i++) file_data_get_mark(fd, i);
	return fd->marks;
}

void FileData::file_data_set_mark(FileData *fd, gint n, gboolean value)
{
	guint old;
	if (!value == !file_data_get_mark(fd, n)) return;

	if (file_data_set_mark_func[n])
		{
		(file_data_set_mark_func[n])(fd, n, value, file_data_mark_func_data[n]);
		}

	old = fd->marks;

	fd->marks = fd->marks ^ (1 << n);

	if (old && !fd->marks) /* keep files with non-zero marks in memory */
		{
		::file_data_unref(fd);
		}
	else if (!old && fd->marks)
		{
		::file_data_ref(fd);
		}

	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_MARKS);
}

gboolean FileData::file_data_filter_marks(FileData *fd, guint filter)
{
	gint i;
	for (i = 0; i < FILEDATA_MARKS_SIZE; i++) if (filter & (1 << i)) file_data_get_mark(fd, i);
	return ((fd->marks & filter) == filter);
}

GList *FileData::file_data_filter_marks_list(GList *list, guint filter)
{
	GList *work;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *link = work;
		work = work->next;

		if (!::file_data_filter_marks(fd, filter))
			{
			list = g_list_remove_link(list, link);
			::file_data_unref(fd);
			g_list_free(link);
			}
		}

	return list;
}

gboolean FileData::file_data_mark_to_selection(FileData *fd, gint mark, MarkToSelectionMode mode, gboolean selected)
{
	gint n = mark - 1;
	gboolean mark_val = file_data_get_mark(fd, n);

	switch (mode)
		{
		case MTS_MODE_MINUS: return !mark_val && selected;
		case MTS_MODE_SET: return mark_val;
		case MTS_MODE_OR: return mark_val || selected;
		case MTS_MODE_AND: return mark_val && selected;
		}

	return selected; // arbitrary value, we shouldn't get here
}

void FileData::file_data_selection_to_mark(FileData *fd, gint mark, SelectionToMarkMode mode)
{
	gint n = mark - 1;

	switch (mode)
		{
		case STM_MODE_RESET: file_data_set_mark(fd, n, FALSE); break;
		case STM_MODE_SET: file_data_set_mark(fd, n, TRUE); break;
		case STM_MODE_TOGGLE: file_data_set_mark(fd, n, !file_data_get_mark(fd, n)); break;
		}
}

gboolean FileData::file_data_filter_file_filter(FileData *fd, GRegex *filter)
{
	return g_regex_match(filter, fd->name, static_cast<GRegexMatchFlags>(0), nullptr);
}

GList *FileData::file_data_filter_file_filter_list(GList *list, GRegex *filter)
{
	GList *work;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *link = work;
		work = work->next;

		if (!::file_data_filter_file_filter(fd, filter))
			{
			list = g_list_remove_link(list, link);
			::file_data_unref(fd);
			g_list_free(link);
			}
		}

	return list;
}

static gboolean file_data_filter_class(FileData *fd, guint filter)
{
	gint i;

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		if (filter & (1 << i))
			{
			if (static_cast<FileFormatClass>(i) == filter_file_get_class(fd->path))
				{
				return TRUE;
				}
			}
		}

	return FALSE;
}

GList *FileData::file_data_filter_class_list(GList *list, guint filter)
{
	GList *work;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *link = work;
		work = work->next;

		if (!file_data_filter_class(fd, filter))
			{
			list = g_list_remove_link(list, link);
			::file_data_unref(fd);
			g_list_free(link);
			}
		}

	return list;
}

static void file_data_notify_mark_func(gpointer, gpointer value, gpointer)
{
	auto fd = static_cast<FileData *>(value);
	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_MARKS);
}

gboolean FileData::file_data_register_mark_func(gint n, GetMarkFunc get_mark_func, SetMarkFunc set_mark_func, gpointer data, GDestroyNotify notify)
{
	if (n < 0 || n >= FILEDATA_MARKS_SIZE) return FALSE;

	if (file_data_destroy_mark_func[n]) (file_data_destroy_mark_func[n])(file_data_mark_func_data[n]);

	file_data_get_mark_func[n] = get_mark_func;
	file_data_set_mark_func[n] = set_mark_func;
	file_data_mark_func_data[n] = data;
	file_data_destroy_mark_func[n] = notify;

	FileDataContext *context = FileData::DefaultFileDataContext();
	if (get_mark_func)
		{
		/* this effectively changes all known files */
		g_hash_table_foreach(context->file_data_pool, file_data_notify_mark_func, nullptr);
		}

	return TRUE;
}

void FileData::file_data_get_registered_mark_func(gint n, GetMarkFunc *get_mark_func, SetMarkFunc *set_mark_func, gpointer *data)
{
	if (get_mark_func) *get_mark_func = file_data_get_mark_func[n];
	if (set_mark_func) *set_mark_func = file_data_set_mark_func[n];
	if (data) *data = file_data_mark_func_data[n];
}


/*
 * file_data    - operates on the given fd
 * file_data_sc - operates on the given fd + sidecars - all fds linked via fd->sidecar_files or fd->parent
 */


/* return list of sidecar file extensions in a string */
gchar *FileData::file_data_sc_list_to_string(FileData *fd)
{
	GList *work;
	GString *result = g_string_new("");

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		result = g_string_append(result, "+ ");
		result = g_string_append(result, sfd->extension);
		work = work->next;
		if (work) result = g_string_append_c(result, ' ');
		}

	return g_string_free(result, FALSE);
}



/*
 * add FileDataChangeInfo (see typedefs.h) for the given operation
 * uses file_data_add_change_info
 *
 * fails if the fd->change already exists - change operations can't run in parallel
 * fd->change_info works as a lock
 *
 * dest can be NULL - in this case the current name is used for now, it will
 * be changed later
 */

/*
   FileDataChangeInfo types:
   COPY
   MOVE   - path is changed, name may be changed too
   RENAME - path remains unchanged, name is changed
            extension should remain (FIXME should we allow editing extension? it will make problems with grouping)
	    sidecar names are changed too, extensions are not changed
   DELETE
   UPDATE - file size, date or grouping has been changed
*/

gboolean FileData::file_data_add_ci(FileData *fd, FileDataChangeType type, const gchar *src, const gchar *dest)
{
	FileDataChangeInfo *fdci;

	if (fd->change) return FALSE;

	fdci = g_new0(FileDataChangeInfo, 1);

	fdci->type = type;

	if (src)
		fdci->source = g_strdup(src);
	else
		fdci->source = g_strdup(fd->path);

	if (dest)
		fdci->dest = g_strdup(dest);

	fd->change = fdci;

	return TRUE;
}

void FileData::planned_change_remove()
{
        // Avoids potentially having the class destructed out from under us.
        FileDataRef this_ref(*this);

	if (g_hash_table_size(context->planned_change_map) != 0 &&
	    (change->type == FILEDATA_CHANGE_MOVE || change->type == FILEDATA_CHANGE_RENAME))
		{
		if (g_hash_table_lookup(context->planned_change_map, change->dest) == this)
			{
			DEBUG_1("planned change: removing %s -> %s", change->dest, path);
			g_hash_table_remove(context->planned_change_map, change->dest);
			::file_data_unref(this);
			if (g_hash_table_size(context->planned_change_map) == 0)
				{
				DEBUG_1("planned change: empty");
				}
			}
		}
}


void FileData::file_data_free_ci(FileData *fd)
{
	FileDataChangeInfo *fdci = fd->change;

	if (!fdci) return;

	fd->planned_change_remove();

	if (fdci->regroup_when_finished) file_data_disable_grouping(fd, FALSE);

	g_free(fdci->source);
	g_free(fdci->dest);

	g_free(fdci);

	fd->change = nullptr;
}

void FileData::file_data_set_regroup_when_finished(FileData *fd, gboolean enable)
{
	FileDataChangeInfo *fdci = fd->change;
	if (!fdci) return;
	fdci->regroup_when_finished = enable;
}

static gboolean file_data_sc_add_ci(FileData *fd, FileDataChangeType type)
{
	GList *work;

	if (fd->parent) fd = fd->parent;

	if (fd->change) return FALSE;

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		if (sfd->change) return FALSE;
		work = work->next;
		}

	file_data_add_ci(fd, type, nullptr, nullptr);

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		file_data_add_ci(sfd, type, nullptr, nullptr);
		work = work->next;
		}

	return TRUE;
}

static gboolean file_data_sc_check_ci(FileData *fd, FileDataChangeType type)
{
	GList *work;

	if (fd->parent) fd = fd->parent;

	if (!fd->change || fd->change->type != type) return FALSE;

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		if (!sfd->change || sfd->change->type != type) return FALSE;
		work = work->next;
		}

	return TRUE;
}


gboolean FileData::file_data_sc_add_ci_copy(FileData *fd, const gchar *dest_path)
{
	if (!file_data_sc_add_ci(fd, FILEDATA_CHANGE_COPY)) return FALSE;
	file_data_sc_update_ci_copy(fd, dest_path);
	return TRUE;
}

gboolean FileData::file_data_sc_add_ci_move(FileData *fd, const gchar *dest_path)
{
	if (!file_data_sc_add_ci(fd, FILEDATA_CHANGE_MOVE)) return FALSE;
	file_data_sc_update_ci_move(fd, dest_path);
	return TRUE;
}

gboolean FileData::file_data_sc_add_ci_rename(FileData *fd, const gchar *dest_path)
{
	if (!file_data_sc_add_ci(fd, FILEDATA_CHANGE_RENAME)) return FALSE;
	file_data_sc_update_ci_rename(fd, dest_path);
	return TRUE;
}

gboolean FileData::file_data_sc_add_ci_delete(FileData *fd)
{
	return file_data_sc_add_ci(fd, FILEDATA_CHANGE_DELETE);
}

gboolean FileData::file_data_sc_add_ci_unspecified(FileData *fd, const gchar *dest_path)
{
	if (!file_data_sc_add_ci(fd, FILEDATA_CHANGE_UNSPECIFIED)) return FALSE;
	file_data_sc_update_ci_unspecified(fd, dest_path);
	return TRUE;
}

gboolean FileData::file_data_add_ci_write_metadata(FileData *fd)
{
	return file_data_add_ci(fd, FILEDATA_CHANGE_WRITE_METADATA, nullptr, nullptr);
}

void FileData::file_data_sc_free_ci(FileData *fd)
{
	GList *work;

	if (fd->parent) fd = fd->parent;

	file_data_free_ci(fd);

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		file_data_free_ci(sfd);
		work = work->next;
		}
}

gboolean FileData::file_data_sc_add_ci_delete_list(GList *fd_list)
{
	GList *work;
	gboolean ret = TRUE;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (!::file_data_sc_add_ci_delete(fd)) ret = FALSE;
		work = work->next;
		}

	return ret;
}

static void file_data_sc_revert_ci_list(GList *fd_list)
{
	GList *work;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		file_data_sc_free_ci(fd);
		work = work->prev;
		}
}

static gboolean file_data_sc_add_ci_list_call_func(GList *fd_list, const gchar *dest, gboolean (*func)(FileData *, const gchar *))
{
	GList *work;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (!func(fd, dest))
			{
			file_data_sc_revert_ci_list(work->prev);
			return FALSE;
			}
		work = work->next;
		}

	return TRUE;
}

gboolean FileData::file_data_sc_add_ci_copy_list(GList *fd_list, const gchar *dest)
{
	return file_data_sc_add_ci_list_call_func(fd_list, dest, ::file_data_sc_add_ci_copy);
}

gboolean FileData::file_data_sc_add_ci_move_list(GList *fd_list, const gchar *dest)
{
	return file_data_sc_add_ci_list_call_func(fd_list, dest, ::file_data_sc_add_ci_move);
}

gboolean FileData::file_data_sc_add_ci_rename_list(GList *fd_list, const gchar *dest)
{
	return file_data_sc_add_ci_list_call_func(fd_list, dest, ::file_data_sc_add_ci_rename);
}

gboolean FileData::file_data_sc_add_ci_unspecified_list(GList *fd_list, const gchar *dest)
{
	return file_data_sc_add_ci_list_call_func(fd_list, dest, ::file_data_sc_add_ci_unspecified);
}

gboolean FileData::file_data_add_ci_write_metadata_list(GList *fd_list)
{
	GList *work;
	gboolean ret = TRUE;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (!::file_data_add_ci_write_metadata(fd)) ret = FALSE;
		work = work->next;
		}

	return ret;
}

void FileData::file_data_free_ci_list(GList *fd_list)
{
	GList *work;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		::file_data_free_ci(fd);
		work = work->next;
		}
}

void FileData::file_data_sc_free_ci_list(GList *fd_list)
{
	GList *work;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		::file_data_sc_free_ci(fd);
		work = work->next;
		}
}

/*
 * update existing fd->change, it will be used from dialog callbacks for interactive editing
 * fails if fd->change does not exist or the change type does not match
 */

void FileData::update_planned_change_hash(const gchar *old_path, gchar *new_path)
{
        // Avoids potentially having the class destructed out from under us.
        FileDataRef this_ref(*this);

	FileDataChangeType type = change->type;

	if (type == FILEDATA_CHANGE_MOVE || type == FILEDATA_CHANGE_RENAME)
		{
		FileData *ofd;

		if (old_path && g_hash_table_lookup(context->planned_change_map, old_path) == this)
			{
			DEBUG_1("planned change: removing %s -> %s", old_path, path);
			g_hash_table_remove(context->planned_change_map, old_path);
			::file_data_unref(this);
			}

		ofd = static_cast<FileData *>(g_hash_table_lookup(context->planned_change_map, new_path));
		if (ofd != this)
			{
			if (ofd)
				{
				DEBUG_1("planned change: replacing %s -> %s", new_path, ofd->path);
				g_hash_table_remove(context->planned_change_map, new_path);
				::file_data_unref(ofd);
				}

			DEBUG_1("planned change: inserting %s -> %s", new_path, path);
			::file_data_ref(this);
			g_hash_table_insert(context->planned_change_map, new_path, this);
			}
		}
}

void FileData::file_data_update_ci_dest(FileData *fd, const gchar *dest_path)
{
	gchar *old_path = fd->change->dest;

	fd->change->dest = g_strdup(dest_path);
	fd->update_planned_change_hash(old_path, fd->change->dest);
	g_free(old_path);
}

void FileData::file_data_update_ci_dest_preserve_ext(FileData *fd, const gchar *dest_path)
{
	const gchar *extension = registered_extension_from_path(fd->change->source);
	gchar *base = remove_extension_from_path(dest_path);
	gchar *old_path = fd->change->dest;

	fd->change->dest = g_strconcat(base, fd->extended_extension ? fd->extended_extension : extension, NULL);
	fd->update_planned_change_hash(old_path, fd->change->dest);

	g_free(old_path);
	g_free(base);
}

void FileData::file_data_sc_update_ci(FileData *fd, const gchar *dest_path)
{
	GList *work;
	gchar *dest_path_full = nullptr;

	if (fd->parent) fd = fd->parent;

	if (!dest_path)
		{
		dest_path = fd->path;
		}
	else if (!strchr(dest_path, G_DIR_SEPARATOR)) /* we got only filename, not a full path */
		{
		gchar *dir = remove_level_from_path(fd->path);

		dest_path_full = g_build_filename(dir, dest_path, NULL);
		g_free(dir);
		dest_path = dest_path_full;
		}
	else if (fd->change->type != FILEDATA_CHANGE_RENAME && isdir(dest_path)) /* rename should not move files between directories */
		{
		dest_path_full = g_build_filename(dest_path, fd->name, NULL);
		dest_path = dest_path_full;
		}

	file_data_update_ci_dest(fd, dest_path);

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		file_data_update_ci_dest_preserve_ext(sfd, dest_path);
		work = work->next;
		}

	g_free(dest_path_full);
}

gboolean FileData::file_data_sc_check_update_ci(FileData *fd, const gchar *dest_path, FileDataChangeType type)
{
	if (!file_data_sc_check_ci(fd, type)) return FALSE;
	file_data_sc_update_ci(fd, dest_path);
	return TRUE;
}

gboolean FileData::file_data_sc_update_ci_copy(FileData *fd, const gchar *dest_path)
{
	return file_data_sc_check_update_ci(fd, dest_path, FILEDATA_CHANGE_COPY);
}

gboolean FileData::file_data_sc_update_ci_move(FileData *fd, const gchar *dest_path)
{
	return file_data_sc_check_update_ci(fd, dest_path, FILEDATA_CHANGE_MOVE);
}

gboolean FileData::file_data_sc_update_ci_rename(FileData *fd, const gchar *dest_path)
{
	return file_data_sc_check_update_ci(fd, dest_path, FILEDATA_CHANGE_RENAME);
}

gboolean FileData::file_data_sc_update_ci_unspecified(FileData *fd, const gchar *dest_path)
{
	return file_data_sc_check_update_ci(fd, dest_path, FILEDATA_CHANGE_UNSPECIFIED);
}

static gboolean file_data_sc_update_ci_list_call_func(GList *fd_list,
						      const gchar *dest,
						      gboolean (*func)(FileData *, const gchar *))
{
	GList *work;
	gboolean ret = TRUE;

	work = fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);

		if (!func(fd, dest)) ret = FALSE;
		work = work->next;
		}

	return ret;
}

gboolean FileData::file_data_sc_update_ci_move_list(GList *fd_list, const gchar *dest)
{
	return file_data_sc_update_ci_list_call_func(fd_list, dest, ::file_data_sc_update_ci_move);
}

gboolean FileData::file_data_sc_update_ci_copy_list(GList *fd_list, const gchar *dest)
{
	return file_data_sc_update_ci_list_call_func(fd_list, dest, ::file_data_sc_update_ci_copy);
}

gboolean FileData::file_data_sc_update_ci_unspecified_list(GList *fd_list, const gchar *dest)
{
	return file_data_sc_update_ci_list_call_func(fd_list, dest, ::file_data_sc_update_ci_unspecified);
}


/*
 * verify source and dest paths - dest image exists, etc.
 * it should detect all possible problems with the planned operation
 */

gint FileData::file_data_verify_ci(FileData *fd, GList *list)
{
	gint ret = CHANGE_OK;
	gchar *dir;
	GList *work = nullptr;
	FileData *fd1 = nullptr;

	if (!fd->change)
		{
		DEBUG_1("Change checked: no change info: %s", fd->path);
		return ret;
		}

	if (!isname(fd->path))
		{
		/* this probably should not happen */
		ret |= CHANGE_NO_SRC;
		DEBUG_1("Change checked: file does not exist: %s", fd->path);
		return ret;
		}

	dir = remove_level_from_path(fd->path);

	if (fd->change->type != FILEDATA_CHANGE_DELETE &&
	    fd->change->type != FILEDATA_CHANGE_MOVE && /* the unsaved metadata should survive move and rename operations */
	    fd->change->type != FILEDATA_CHANGE_RENAME &&
	    fd->change->type != FILEDATA_CHANGE_WRITE_METADATA &&
	    fd->modified_xmp)
		{
		ret |= CHANGE_WARN_UNSAVED_META;
		DEBUG_1("Change checked: unsaved metadata: %s", fd->path);
		}

	if (fd->change->type != FILEDATA_CHANGE_DELETE &&
	    fd->change->type != FILEDATA_CHANGE_WRITE_METADATA &&
	    !access_file(fd->path, R_OK))
		{
		ret |= CHANGE_NO_READ_PERM;
		DEBUG_1("Change checked: no read permission: %s", fd->path);
		}
	else if ((fd->change->type == FILEDATA_CHANGE_DELETE || fd->change->type == FILEDATA_CHANGE_MOVE) &&
	    	 !access_file(dir, W_OK))
		{
		ret |= CHANGE_NO_WRITE_PERM_DIR;
		DEBUG_1("Change checked: source dir is readonly: %s", fd->path);
		}
	else if (fd->change->type != FILEDATA_CHANGE_COPY &&
		 fd->change->type != FILEDATA_CHANGE_UNSPECIFIED &&
		 fd->change->type != FILEDATA_CHANGE_WRITE_METADATA &&
		 !access_file(fd->path, W_OK))
		{
		ret |= CHANGE_WARN_NO_WRITE_PERM;
		DEBUG_1("Change checked: no write permission: %s", fd->path);
		}
	/* WRITE_METADATA is special because it can be configured to silently write to ~/.geeqie/...
	   - that means that there are no hard errors and warnings can be disabled
	   - the destination is determined during the check
	*/
	else if (fd->change->type == FILEDATA_CHANGE_WRITE_METADATA)
		{
		/* determine destination file */
		gboolean have_dest = FALSE;
		gchar *dest_dir = nullptr;

		if (options->metadata.save_in_image_file)
			{
			if (file_data_can_write_directly(fd))
				{
				/* we can write the file directly */
				if (access_file(fd->path, W_OK))
					{
					have_dest = TRUE;
					}
				else
					{
					if (options->metadata.warn_on_write_problems)
						{
						ret |= CHANGE_WARN_NO_WRITE_PERM;
						DEBUG_1("Change checked: file is not writable: %s", fd->path);
						}
					}
				}
			else if (file_data_can_write_sidecar(fd))
				{
				/* we can write sidecar */
				gchar *sidecar = file_data_get_sidecar_path(fd, FALSE);
				if (access_file(sidecar, W_OK) || (!isname(sidecar) && access_file(dir, W_OK)))
					{
					file_data_update_ci_dest(fd, sidecar);
					have_dest = TRUE;
					}
				else
					{
					if (options->metadata.warn_on_write_problems)
						{
						ret |= CHANGE_WARN_NO_WRITE_PERM;
						DEBUG_1("Change checked: file is not writable: %s", sidecar);
						}
					}
				g_free(sidecar);
				}
			}

		if (!have_dest)
			{
			/* write private metadata file under ~/.geeqie */

			/* If an existing metadata file exists, we will try writing to
			 * it's location regardless of the user's preference.
			 */
			gchar *metadata_path = nullptr;
#if HAVE_EXIV2
			/* but ignore XMP if we are not able to write it */
			metadata_path = cache_find_location(CACHE_TYPE_XMP_METADATA, fd->path);
#endif
			if (!metadata_path) metadata_path = cache_find_location(CACHE_TYPE_METADATA, fd->path);

			if (metadata_path && !access_file(metadata_path, W_OK))
				{
				g_free(metadata_path);
				metadata_path = nullptr;
				}

			if (!metadata_path)
				{
				mode_t mode = 0755;

				dest_dir = cache_get_location(CACHE_TYPE_METADATA, fd->path, FALSE, &mode);
				if (recursive_mkdir_if_not_exists(dest_dir, mode))
					{
					gchar *filename = g_strconcat(fd->name, options->metadata.save_legacy_format ? GQ_CACHE_EXT_METADATA : GQ_CACHE_EXT_XMP_METADATA, NULL);

					metadata_path = g_build_filename(dest_dir, filename, NULL);
					g_free(filename);
					}
				}
			if (access_file(metadata_path, W_OK) || (!isname(metadata_path) && access_file(dest_dir, W_OK)))
				{
				file_data_update_ci_dest(fd, metadata_path);
				}
			else
				{
				ret |= CHANGE_NO_WRITE_PERM_DEST;
				DEBUG_1("Change checked: file is not writable: %s", metadata_path);
				}
			g_free(metadata_path);
			}
		g_free(dest_dir);
		}

	if (fd->change->dest && fd->change->type != FILEDATA_CHANGE_WRITE_METADATA)
		{
		gboolean same;
		gchar *dest_dir;

		same = (strcmp(fd->path, fd->change->dest) == 0);

		if (!same)
			{
			const gchar *dest_ext = registered_extension_from_path(fd->change->dest);
			if (!dest_ext) dest_ext = "";
			if (!options->file_filter.disable_file_extension_checks)
				{
				if (g_ascii_strcasecmp(fd->extension, dest_ext) != 0)
					{
					ret |= CHANGE_WARN_CHANGED_EXT;
					DEBUG_1("Change checked: source and destination have different extensions: %s -> %s", fd->path, fd->change->dest);
					}
				}
			}
		else
			{
			if (fd->change->type != FILEDATA_CHANGE_UNSPECIFIED) /** @FIXME this is now needed for running editors */
		   		{
				ret |= CHANGE_WARN_SAME;
				DEBUG_1("Change checked: source and destination are the same: %s -> %s", fd->path, fd->change->dest);
				}
			}

		dest_dir = remove_level_from_path(fd->change->dest);

		if (!isdir(dest_dir))
			{
			ret |= CHANGE_NO_DEST_DIR;
			DEBUG_1("Change checked: destination dir does not exist: %s -> %s", fd->path, fd->change->dest);
			}
		else if (!access_file(dest_dir, W_OK))
			{
			ret |= CHANGE_WARN_NO_WRITE_PERM_DEST_DIR;
			DEBUG_1("Change checked: destination dir is readonly: %s -> %s", fd->path, fd->change->dest);
			}
		else if (!same)
			{
			if (isfile(fd->change->dest))
				{
				if (!access_file(fd->change->dest, W_OK))
					{
					ret |= CHANGE_NO_WRITE_PERM_DEST;
					DEBUG_1("Change checked: destination file exists and is readonly: %s -> %s", fd->path, fd->change->dest);
					}
				else
					{
					ret |= CHANGE_WARN_DEST_EXISTS;
					DEBUG_1("Change checked: destination exists: %s -> %s", fd->path, fd->change->dest);
					}
				}
			else if (isdir(fd->change->dest))
				{
				ret |= CHANGE_DEST_EXISTS;
				DEBUG_1("Change checked: destination exists: %s -> %s", fd->path, fd->change->dest);
				}
			}

		g_free(dest_dir);
		}

	/* During a rename operation, check if another planned destination file has
	 * the same filename
	 */
 	if(fd->change->type == FILEDATA_CHANGE_RENAME ||
				fd->change->type == FILEDATA_CHANGE_COPY ||
				fd->change->type == FILEDATA_CHANGE_MOVE)
		{
		work = list;
		while (work)
			{
			fd1 = static_cast<FileData *>(work->data);
			work = work->next;
			if (fd1 != nullptr && fd != fd1 )
				{
				if (!strcmp(fd->change->dest, fd1->change->dest))
					{
					ret |= CHANGE_DUPLICATE_DEST;
					}
				}
			}
		}

	fd->change->error = ret;
	if (ret == 0) DEBUG_1("Change checked: OK: %s", fd->path);

	g_free(dir);
	return ret;
}


gint FileData::file_data_sc_verify_ci(FileData *fd, GList *list)
{
	GList *work;
	gint ret;

	ret = file_data_verify_ci(fd, list);

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		ret |= file_data_verify_ci(sfd, list);
		work = work->next;
		}

	return ret;
}

gchar *FileData::file_data_get_error_string(gint error)
{
	GString *result = g_string_new("");

	if (error & CHANGE_NO_SRC)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("file or directory does not exist"));
		}

	if (error & CHANGE_DEST_EXISTS)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("destination already exists"));
		}

	if (error & CHANGE_NO_WRITE_PERM_DEST)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("destination can't be overwritten"));
		}

	if (error & CHANGE_WARN_NO_WRITE_PERM_DEST_DIR)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("destination directory is not writable"));
		}

	if (error & CHANGE_NO_DEST_DIR)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("destination directory does not exist"));
		}

	if (error & CHANGE_NO_WRITE_PERM_DIR)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("source directory is not writable"));
		}

	if (error & CHANGE_NO_READ_PERM)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("no read permission"));
		}

	if (error & CHANGE_WARN_NO_WRITE_PERM)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("file is readonly"));
		}

	if (error & CHANGE_WARN_DEST_EXISTS)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("destination already exists and will be overwritten"));
		}

	if (error & CHANGE_WARN_SAME)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("source and destination are the same"));
		}

	if (error & CHANGE_WARN_CHANGED_EXT)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("source and destination have different extension"));
		}

	if (error & CHANGE_WARN_UNSAVED_META)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("there are unsaved metadata changes for the file"));
		}

	if (error & CHANGE_DUPLICATE_DEST)
		{
		if (result->len > 0) g_string_append(result, ", ");
		g_string_append(result, _("another destination file has the same filename"));
		}

	return g_string_free(result, FALSE);
}

gint FileData::file_data_verify_ci_list(GList *list, gchar **desc, gboolean with_sidecars)
{
	GList *work;
	gint all_errors = 0;
	gint common_errors = ~0;
	gint num;
	gint *errors;
	gint i;

	if (!list) return 0;

	num = g_list_length(list);
	errors = g_new(int, num);
	work = list;
	i = 0;
	while (work)
		{
		FileData *fd;
		gint error;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		error = with_sidecars ? ::file_data_sc_verify_ci(fd, list) : ::file_data_verify_ci(fd, list);
		all_errors |= error;
		common_errors &= error;

		errors[i] = error;

		i++;
		}

	if (desc && all_errors)
		{
		GList *work;
		GString *result = g_string_new("");

		if (common_errors)
			{
			gchar *str = file_data_get_error_string(common_errors);
			g_string_append(result, str);
			g_string_append(result, "\n");
			g_free(str);
			}

		work = list;
		i = 0;
		while (work)
			{
			FileData *fd;
			gint error;

			fd = static_cast<FileData *>(work->data);
			work = work->next;

			error = errors[i] & ~common_errors;

			if (error)
				{
				gchar *str = file_data_get_error_string(error);
				g_string_append_printf(result, "%s: %s\n", fd->name, str);
				g_free(str);
				}
			i++;
			}
		*desc = g_string_free(result, FALSE);
		}

	g_free(errors);
	return all_errors;
}


/*
 * perform the change described by FileFataChangeInfo
 * it is used for internal operations,
 * this function actually operates with files on the filesystem
 * it should implement safe delete
 */

static gboolean file_data_perform_move(FileData *fd)
{
	g_assert(!strcmp(fd->change->source, fd->path));
	return move_file(fd->change->source, fd->change->dest);
}

static gboolean file_data_perform_copy(FileData *fd)
{
	g_assert(!strcmp(fd->change->source, fd->path));
	return copy_file(fd->change->source, fd->change->dest);
}

static gboolean file_data_perform_delete(FileData *fd)
{
	if (isdir(fd->path) && !islink(fd->path))
		return rmdir_utf8(fd->path);

	if (options->file_ops.safe_delete_enable)
		return file_util_safe_unlink(fd->path);

	return unlink_file(fd->path);
}

gboolean FileData::file_data_perform_ci(FileData *fd)
{
	/** @FIXME When a directory that is a symbolic link is deleted,
	 * at this point fd->change is null because no FileDataChangeInfo
	 * has been set up. Therefore there is a seg. fault.
	 * This code simply aborts the delete.
	 */
	if (!fd->change)
		{
		return FALSE;
		}

	FileDataChangeType type = fd->change->type;

	switch (type)
		{
		case FILEDATA_CHANGE_MOVE:
			return file_data_perform_move(fd);
		case FILEDATA_CHANGE_COPY:
			return file_data_perform_copy(fd);
		case FILEDATA_CHANGE_RENAME:
			return file_data_perform_move(fd); /* the same as move */
		case FILEDATA_CHANGE_DELETE:
			return file_data_perform_delete(fd);
		case FILEDATA_CHANGE_WRITE_METADATA:
			return metadata_write_perform(fd);
		case FILEDATA_CHANGE_UNSPECIFIED:
			/* nothing to do here */
			break;
		}
	return TRUE;
}



gboolean FileData::file_data_sc_perform_ci(FileData *fd)
{
	GList *work;
	gboolean ret = TRUE;
	FileDataChangeType type = fd->change->type;

	if (!file_data_sc_check_ci(fd, type)) return FALSE;

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		if (!file_data_perform_ci(sfd)) ret = FALSE;
		work = work->next;
		}

	if (!file_data_perform_ci(fd)) ret = FALSE;

	return ret;
}

/*
 * updates FileData structure according to FileDataChangeInfo
 */

gboolean FileData::file_data_apply_ci(FileData *fd)
{
	FileDataChangeType type = fd->change->type;

	/** @FIXME delete ?*/
	if (type == FILEDATA_CHANGE_MOVE || type == FILEDATA_CHANGE_RENAME)
		{
		DEBUG_1("planned change: applying %s -> %s", fd->change->dest, fd->path);
		fd->planned_change_remove();

		if (g_hash_table_lookup(fd->context->file_data_pool, fd->change->dest))
			{
			/* this change overwrites another file which is already known to other modules
			   renaming fd would create duplicate FileData structure
			   the best thing we can do is nothing
			*/
			/**  @FIXME maybe we could copy stuff like marks
			*/
			DEBUG_1("can't rename fd, target exists %s -> %s", fd->change->dest, fd->path);
			}
		else
			{
			fd->set_path(fd->change->dest);
			}
		}
	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_CHANGE);

	return TRUE;
}

gboolean FileData::file_data_sc_apply_ci(FileData *fd)
{
	GList *work;
	FileDataChangeType type = fd->change->type;

	if (!file_data_sc_check_ci(fd, type)) return FALSE;

	work = fd->sidecar_files;
	while (work)
		{
		auto sfd = static_cast<FileData *>(work->data);

		file_data_apply_ci(sfd);
		work = work->next;
		}

	file_data_apply_ci(fd);

	return TRUE;
}

static gboolean file_data_list_contains_whole_group(GList *list, FileData *fd)
{
	GList *work;
	if (fd->parent) fd = fd->parent;
	if (!g_list_find(list, fd)) return FALSE;

	work = fd->sidecar_files;
	while (work)
		{
		if (!g_list_find(list, work->data)) return FALSE;
		work = work->next;
		}
	return TRUE;
}

GList *FileData::file_data_process_groups_in_selection(GList *list, gboolean ungroup, GList **ungrouped_list)
{
	GList *out = nullptr;
	GList *work = list;

	/* change partial groups to independent files */
	if (ungroup)
		{
		while (work)
			{
			auto fd = static_cast<FileData *>(work->data);
			work = work->next;

			if (!file_data_list_contains_whole_group(list, fd))
				{
				::file_data_disable_grouping(fd, TRUE);
				if (ungrouped_list)
					{
					*ungrouped_list = g_list_prepend(*ungrouped_list, ::file_data_ref(fd));
					}
				}
			}
		}

	/* remove sidecars from the list,
	   they can be still accessed via main_fd->sidecar_files */
	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (!fd->parent ||
		    (!ungroup && !file_data_list_contains_whole_group(list, fd)))
			{
			out = g_list_prepend(out, ::file_data_ref(fd));
			}
		}

	filelist_free(list);
	out = g_list_reverse(out);

	return out;
}





/*
 * notify other modules about the change described by FileDataChangeInfo
 */

/* might use file_maint_ functions for now, later it should be changed to a system of callbacks */
/** @FIXME do we need the ignore_list? It looks like a workaround for ineffective
   implementation in view-file-list.cc */


struct NotifyIdleData {
	FileData *fd;
	NotifyType type;
};


struct NotifyData {
	FileData::NotifyFunc func;
	gpointer data;
	NotifyPriority priority;
};

static GList *notify_func_list = nullptr;

static gint file_data_notify_sort(gconstpointer a, gconstpointer b)
{
	auto nda = static_cast<const NotifyData *>(a);
	auto ndb = static_cast<const NotifyData *>(b);

	if (nda->priority < ndb->priority) return -1;
	if (nda->priority > ndb->priority) return 1;
	return 0;
}

gboolean FileData::file_data_register_notify_func(NotifyFunc func, gpointer data, NotifyPriority priority)
{
	NotifyData *nd;
	GList *work = notify_func_list;

	while (work)
		{
		auto nd = static_cast<NotifyData *>(work->data);

		if (nd->func == func && nd->data == data)
			{
			g_warning("Notify func already registered");
			return FALSE;
			}
		work = work->next;
		}

	nd = g_new(NotifyData, 1);
	nd->func = func;
	nd->data = data;
	nd->priority = priority;

	notify_func_list = g_list_insert_sorted(notify_func_list, nd, file_data_notify_sort);
	DEBUG_2("Notify func registered: %p", (void *)nd);

	return TRUE;
}

gboolean FileData::file_data_unregister_notify_func(NotifyFunc func, gpointer data)
{
	GList *work = notify_func_list;

	while (work)
		{
		auto nd = static_cast<NotifyData *>(work->data);

		if (nd->func == func && nd->data == data)
			{
			notify_func_list = g_list_delete_link(notify_func_list, work);
			DEBUG_2("Notify func unregistered: %p", (void *)nd);
			g_free(nd);
			return TRUE;
			}
		work = work->next;
		}

	g_warning("Notify func not found");
	return FALSE;
}

void FileData::file_data_send_notification(FileData *fd, NotifyType type)
{
	GList *work = notify_func_list;

	while (work)
		{
		auto nd = static_cast<NotifyData *>(work->data);

		nd->func(fd, type, nd->data);
		work = work->next;
		}
}

static GHashTable *file_data_monitor_pool = nullptr;
static guint realtime_monitor_id = 0; /* event source id */

static void realtime_monitor_check_cb(gpointer key, gpointer, gpointer)
{
	auto fd = static_cast<FileData *>(key);

	file_data_check_changed_files(fd);

	DEBUG_1("monitor %s", fd->path);
}

static gboolean realtime_monitor_cb(gpointer)
{
	if (!options->update_on_time_change) return TRUE;
	g_hash_table_foreach(file_data_monitor_pool, realtime_monitor_check_cb, nullptr);
	return TRUE;
}

gboolean FileData::file_data_register_real_time_monitor(FileData *fd)
{
	gint count;

	::file_data_ref(fd);

	if (!file_data_monitor_pool)
		file_data_monitor_pool = g_hash_table_new(g_direct_hash, g_direct_equal);

	count = GPOINTER_TO_INT(g_hash_table_lookup(file_data_monitor_pool, fd));

	DEBUG_1("Register realtime %d %s", count, fd->path);

	count++;
	g_hash_table_insert(file_data_monitor_pool, fd, GINT_TO_POINTER(count));

	if (!realtime_monitor_id)
		{
		realtime_monitor_id = g_timeout_add(5000, realtime_monitor_cb, nullptr);
		}

	return TRUE;
}

gboolean FileData::file_data_unregister_real_time_monitor(FileData *fd)
{
	gint count;

	g_assert(file_data_monitor_pool);

	count = GPOINTER_TO_INT(g_hash_table_lookup(file_data_monitor_pool, fd));

	DEBUG_1("Unregister realtime %d %s", count, fd->path);

	g_assert(count > 0);

	count--;

	if (count == 0)
		g_hash_table_remove(file_data_monitor_pool, fd);
	else
		g_hash_table_insert(file_data_monitor_pool, fd, GINT_TO_POINTER(count));

	::file_data_unref(fd);

	if (g_hash_table_size(file_data_monitor_pool) == 0)
		{
		g_source_remove(realtime_monitor_id);
		realtime_monitor_id = 0;
		return FALSE;
		}

	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * Saving marks list, clearing marks
 * Uses file_data_pool
 *-----------------------------------------------------------------------------
 */

static void marks_get_files(gpointer key, gpointer value, gpointer userdata)
{
	auto file_name = static_cast<gchar *>(key);
	auto result = static_cast<GString *>(userdata);
	FileData *fd;

	if (isfile(file_name))
		{
		fd = static_cast<FileData *>(value);
		if (fd && fd->marks > 0)
			{
			g_string_append_printf(result, "%s,%i\n", fd->path, fd->marks);
			}
		}
}

gboolean FileData::marks_list_load(const gchar *path)
{
	FILE *f;
	gchar s_buf[1024];
	gchar *pathl;
	gchar *file_path;
	gchar *marks_value;

	pathl = path_from_utf8(path);
	f = fopen(pathl, "r");
	g_free(pathl);
	if (!f) return FALSE;

	/* first line must start with Marks comment */
	if (!fgets(s_buf, sizeof(s_buf), f) ||
					strncmp(s_buf, "#Marks", 6) != 0)
		{
		fclose(f);
		return FALSE;
		}

	while (fgets(s_buf, sizeof(s_buf), f))
		{
		if (s_buf[0]=='#') continue;
			file_path = strtok(s_buf, ",");
			marks_value = strtok(nullptr, ",");
			if (isfile(file_path))
				{
				FileData *fd = file_data_new_no_grouping(file_path);
				::file_data_ref(fd);
				gint n = 0;
				while (n <= 9)
					{
					gint mark_no = 1 << n;
					if (atoi(marks_value) & mark_no)
						{
						::file_data_set_mark(fd, n , 1);
						}
					n++;
					}
				}
		}

	fclose(f);
	return TRUE;
}

gboolean FileData::marks_list_save(gchar *path, gboolean save)
{
	SecureSaveInfo *ssi;
	gchar *pathl;

	pathl = path_from_utf8(path);
	ssi = secure_open(pathl);
	g_free(pathl);
	if (!ssi)
		{
		log_printf(_("Error: Unable to write marks lists to: %s\n"), path);
		return FALSE;
		}

	secure_fprintf(ssi, "#Marks lists\n");

	GString *marks = g_string_new("");
	if (save)
		{
		FileDataContext *context = FileData::DefaultFileDataContext();
		g_hash_table_foreach(context->file_data_pool, marks_get_files, marks);
		}
	secure_fprintf(ssi, "%s", marks->str);
	g_string_free(marks, TRUE);

	secure_fprintf(ssi, "#end\n");
	return (secure_close(ssi) == 0);
}

static void marks_clear(gpointer key, gpointer value, gpointer)
{
	auto file_name = static_cast<gchar *>(key);
	gint mark_no;
	gint n;
	FileData *fd;

	if (isfile(file_name))
		{
		fd = static_cast<FileData *>(value);
		if (fd && fd->marks > 0)
			{
			n = 0;
			while (n <= 9)
				{
				mark_no = 1 << n;
				if (fd->marks & mark_no)
					{
					file_data_set_mark(fd, n , 0);
					}
				n++;
				}
			}
		}
}

void FileData::marks_clear_all()
{
	FileDataContext *context = FileData::DefaultFileDataContext();
	g_hash_table_foreach(context->file_data_pool, marks_clear, nullptr);
}

void FileData::file_data_set_page_num(FileData *fd, gint page_num)
{
	if (fd->page_total > 1 && page_num < 0)
		{
		fd->page_num = fd->page_total - 1;
		}
	else if (fd->page_total > 1 && page_num <= fd->page_total)
		{
		fd->page_num = page_num - 1;
		}
	else
		{
		fd->page_num = 0;
		}
	file_data_send_notification(fd, NOTIFY_REREAD);
}

void FileData::file_data_inc_page_num(FileData *fd)
{
	if (fd->page_total > 0 && fd->page_num < fd->page_total - 1)
		{
		fd->page_num = fd->page_num + 1;
		}
	else if (fd->page_total == 0)
		{
		fd->page_num = fd->page_num + 1;
		}
	file_data_send_notification(fd, NOTIFY_REREAD);
}

void FileData::file_data_dec_page_num(FileData *fd)
{
	if (fd->page_num > 0)
		{
		fd->page_num = fd->page_num - 1;
		}
	file_data_send_notification(fd, NOTIFY_REREAD);
}

void FileData::file_data_set_page_total(FileData *fd, gint page_total)
{
	fd->page_total = page_total;
}

FileDataRef::FileDataRef(FileData &fd) : fd_(fd)
{
        fd_.file_data_ref();
}

FileDataRef::~FileDataRef()
{
        fd_.file_data_unref();
}

// NOLINTEND(readability-convert-member-functions-to-static)

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
