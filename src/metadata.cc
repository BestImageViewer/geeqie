/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: John Ellis, Laurent Monin
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

#include "metadata.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <glib-object.h>
#include <grp.h>
#include <pwd.h>

#include <config.h>

#include "cache.h"
#include "exif.h"
#include "filedata.h"
#if HAVE_LUA
#  include "glua.h"
#endif
#include "intl.h"
#include "layout-util.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "rcfile.h"
#include "ui-fileops.h"
#include "utilops.h"

struct ExifData;

namespace
{

enum MetadataKey {
	MK_NONE,
	MK_KEYWORDS,
	MK_COMMENT
};

struct MetadataCacheEntry {
	gchar *key;
	GList *values;
};

/* If contents change, keep GuideOptionsMetadata.xml up to date */
/**
 *  @brief Tags that will be written to all files in a group - selected by: options->metadata.sync_grouped_files, Preferences/Metadata/Write The Same Description Tags To All Grouped Sidecars
 */
constexpr std::array<const gchar *, 22> group_keys{
	"Xmp.dc.title",
	"Xmp.photoshop.Urgency",
	"Xmp.photoshop.Category",
	"Xmp.photoshop.SupplementalCategory",
	"Xmp.dc.subject",
	"Xmp.iptc.Location",
	"Xmp.photoshop.Instruction",
	"Xmp.photoshop.DateCreated",
	"Xmp.dc.creator",
	"Xmp.photoshop.AuthorsPosition",
	"Xmp.photoshop.City",
	"Xmp.photoshop.State",
	"Xmp.iptc.CountryCode",
	"Xmp.photoshop.Country",
	"Xmp.photoshop.TransmissionReference",
	"Xmp.photoshop.Headline",
	"Xmp.photoshop.Credit",
	"Xmp.photoshop.Source",
	"Xmp.dc.rights",
	"Xmp.dc.description",
	"Xmp.photoshop.CaptionWriter",
	"Xmp.xmp.Rating",
};

GtkTreeStore *keyword_tree;

gint metadata_cache_entry_compare_key(const MetadataCacheEntry *entry, const gchar *key)
{
	return strcmp(entry->key, key);
}

void metadata_cache_entry_free(MetadataCacheEntry *entry)
{
	if (!entry) return;

	g_free(entry->key);
	g_list_free_full(entry->values, g_free);
	g_free(entry);
}

void string_list_free(gpointer data)
{
	g_list_free_full(static_cast<GList *>(data), g_free);
}

inline gboolean is_keywords_separator(gchar c)
{
	return c == ','
	    || c == ';'
	    || c == '\n'
	    || c == '\r'
	    || c == '\b';
}

/*
 *-------------------------------------------------------------------
 * file info
 * shares tag naming infrastructure with exif
 * we should probably not invest too much effort into this because
 * new exiv2 will support the same functionality
 * https://dev.exiv2.org/issues/505
 *-------------------------------------------------------------------
 */

gchar *mode_number(mode_t m)
{
	gint mb = 0;
	if (m & S_ISUID) mb |= 4;
	if (m & S_ISGID) mb |= 2;
	if (m & S_ISVTX) mb |= 1;

	gint mu = 0;
	if (m & S_IRUSR) mu |= 4;
	if (m & S_IWUSR) mu |= 2;
	if (m & S_IXUSR) mu |= 1;

	gint mg = 0;
	if (m & S_IRGRP) mg |= 4;
	if (m & S_IWGRP) mg |= 2;
	if (m & S_IXGRP) mg |= 1;

	gint mo = 0;
	if (m & S_IROTH) mo |= 4;
	if (m & S_IWOTH) mo |= 2;
	if (m & S_IXOTH) mo |= 1;

	gchar pbuf[10];
	pbuf[0] = (m & S_IRUSR) ? 'r' : '-';
	pbuf[1] = (m & S_IWUSR) ? 'w' : '-';
	pbuf[2] = (m & S_IXUSR) ? 'x' : '-';
	pbuf[3] = (m & S_IRGRP) ? 'r' : '-';
	pbuf[4] = (m & S_IWGRP) ? 'w' : '-';
	pbuf[5] = (m & S_IXGRP) ? 'x' : '-';
	pbuf[6] = (m & S_IROTH) ? 'r' : '-';
	pbuf[7] = (m & S_IWOTH) ? 'w' : '-';
	pbuf[8] = (m & S_IXOTH) ? 'x' : '-';
	pbuf[9] = '\0';

	return g_strdup_printf("%s (%d%d%d%d)", pbuf, mb, mu, mg, mo);
}

gchar *get_file_owner(const gchar *path_utf8)
{
	struct stat st;
	if (!stat_utf8(path_utf8, &st)) return nullptr;

	struct passwd *user = getpwuid(st.st_uid);

	return user ? g_strdup(user->pw_name) : g_strdup_printf("%u", st.st_uid);
}

gchar *get_file_group(const gchar *path_utf8)
{
	struct stat st;
	if (!stat_utf8(path_utf8, &st)) return nullptr;

	struct group *group = getgrgid(st.st_gid);

	return group ? g_strdup(group->gr_name) : g_strdup_printf("%u", st.st_gid);
}

gchar *metadata_file_info(const FileData *fd, const gchar *key)
{
	if (strcmp(key, "file.size") == 0)
		{
		return g_strdup_printf("%ld", static_cast<long>(fd->size));
		}
	if (strcmp(key, "file.date") == 0)
		{
		return g_strdup(text_from_time(fd->date));
		}
	if (strcmp(key, "file.mode") == 0)
		{
		return mode_number(fd->mode);
		}
	if (strcmp(key, "file.ctime") == 0)
		{
		return g_strdup(text_from_time(fd->cdate));
		}
	if (strcmp(key, "file.class") == 0)
		{
		return g_strdup(format_class_list[fd->format_class]);
		}
	if (strcmp(key, "file.owner") == 0)
		{
		return get_file_owner(fd->path);
		}
	if (strcmp(key, "file.group") == 0)
		{
		return get_file_group(fd->path);
		}
	if (strcmp(key, "file.link") == 0)
		{
		return get_symbolic_link(fd->path);
		}
	if (strcmp(key, "file.page_no") == 0)
		{
		return (fd->page_total > 1) ? g_strdup_printf("[%d/%d]", fd->page_num + 1, fd->page_total) : nullptr;
		}
	return g_strdup("");
}

#if HAVE_LUA
gchar *metadata_lua_info(FileData *fd, const gchar *key)
{
	const gchar *script_name_utf8 = key + 4;
	g_autofree gchar *script_name = path_from_utf8(script_name_utf8);

	g_autofree gchar *raw_data = lua_callvalue(fd, script_name, nullptr);
	g_autofree gchar *valid_data = g_utf8_make_valid(raw_data, -1);

	return g_utf8_substring(valid_data, 0, 150);
}
#endif

} // namespace

static gboolean metadata_write_queue_idle_cb(gpointer data);
static gboolean metadata_legacy_write(FileData *fd);
static void metadata_legacy_delete(FileData *fd, const gchar *except);
static gboolean metadata_file_read(gchar *path, GList **keywords, gchar **comment);


/*
 *-------------------------------------------------------------------
 * long-term cache - keep keywords from whole dir in memory
 *-------------------------------------------------------------------
 */

/* fd->cached_metadata list of MetadataCacheEntry */

static void metadata_cache_update(FileData *fd, const gchar *key, const GList *values)
{
	GList *work;

	work = g_list_find_custom(fd->cached_metadata, key, reinterpret_cast<GCompareFunc>(metadata_cache_entry_compare_key));
	if (work)
		{
		/* key found - just replace values */
		auto *entry = static_cast<MetadataCacheEntry *>(work->data);

		g_list_free_full(entry->values, g_free);
		entry->values = string_list_copy(values);
		DEBUG_1("updated %s %s\n", key, fd->path);
		return;
		}

	/* key not found - prepend new entry */
	auto *entry = g_new0(MetadataCacheEntry, 1);
	entry->key = g_strdup(key);
	entry->values = string_list_copy(values);

	fd->cached_metadata = g_list_prepend(fd->cached_metadata, entry);
	DEBUG_1("added %s %s\n", key, fd->path);
}

static const GList *metadata_cache_get(FileData *fd, const gchar *key)
{
	GList *work;

	work = g_list_find_custom(fd->cached_metadata, key, reinterpret_cast<GCompareFunc>(metadata_cache_entry_compare_key));
	if (work)
		{
		/* key found */
		auto *entry = static_cast<MetadataCacheEntry *>(work->data);

		DEBUG_1("found %s %s\n", key, fd->path);
		return entry->values;
		}
	DEBUG_1("not found %s %s\n", key, fd->path);
	return nullptr;
}

static void metadata_cache_remove(FileData *fd, const gchar *key)
{
	GList *work;

	work = g_list_find_custom(fd->cached_metadata, key, reinterpret_cast<GCompareFunc>(metadata_cache_entry_compare_key));
	if (work)
		{
		/* key found */
		auto *entry = static_cast<MetadataCacheEntry *>(work->data);

		metadata_cache_entry_free(entry);
		fd->cached_metadata = g_list_delete_link(fd->cached_metadata, work);
		DEBUG_1("removed %s %s\n", key, fd->path);
		return;
		}
	DEBUG_1("not removed %s %s\n", key, fd->path);
}

void metadata_cache_free(FileData *fd)
{
	if (fd->cached_metadata) DEBUG_1("freed %s\n", fd->path);

	g_list_free_full(fd->cached_metadata, reinterpret_cast<GDestroyNotify>(metadata_cache_entry_free));
	fd->cached_metadata = nullptr;
}


/*
 *-------------------------------------------------------------------
 * write queue
 *-------------------------------------------------------------------
 */

static GList *metadata_write_queue = nullptr;

static void metadata_write_queue_add(FileData *fd)
{
	if (!g_list_find(metadata_write_queue, fd))
		{
		metadata_write_queue = g_list_prepend(metadata_write_queue, fd);
		file_data_ref(fd);

		layout_util_status_update_write_all();
		}

	static guint metadata_write_idle_id = 0; /* event source id */

	g_clear_handle_id(&metadata_write_idle_id, g_source_remove);

	if (options->metadata.confirm_after_timeout)
		{
		metadata_write_idle_id = g_timeout_add(options->metadata.confirm_timeout * 1000, metadata_write_queue_idle_cb, &metadata_write_idle_id);
		}
}


gboolean metadata_write_queue_remove(FileData *fd)
{
	g_hash_table_destroy(fd->modified_xmp);
	fd->modified_xmp = nullptr;

	metadata_write_queue = g_list_remove(metadata_write_queue, fd);

	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_REREAD);

	file_data_unref(fd);

	layout_util_status_update_write_all();
	return TRUE;
}

void metadata_notify_cb(FileData *fd, NotifyType type, gpointer)
{
	if (type & (NOTIFY_REREAD | NOTIFY_CHANGE))
		{
		metadata_cache_free(fd);

		if (g_list_find(metadata_write_queue, fd))
			{
			DEBUG_1("Notify metadata: %s %04x", fd->path, type);
			if (!isname(fd->path))
				{
				/* ignore deleted files */
				metadata_write_queue_remove(fd);
				}
			}
		}
}

gboolean metadata_write_queue_confirm(gboolean force_dialog, const FileUtilDoneFunc &done_func)
{
	GList *work;
	GList *to_approve = nullptr;

	work = metadata_write_queue;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (!isname(fd->path))
			{
			/* ignore deleted files */
			metadata_write_queue_remove(fd);
			continue;
			}

		if (fd->change) continue; /* another operation in progress, skip this file for now */

		to_approve = g_list_prepend(to_approve, file_data_ref(fd));
		}

	file_util_write_metadata(nullptr, to_approve, nullptr, force_dialog, done_func);

	return (metadata_write_queue != nullptr);
}

static gboolean metadata_write_queue_idle_cb(gpointer data)
{
	metadata_write_queue_confirm(FALSE, nullptr);

	auto *metadata_write_idle_id = static_cast<guint *>(data);
	*metadata_write_idle_id = 0;

	return G_SOURCE_REMOVE;
}

gboolean metadata_write_perform(FileData *fd)
{
	gboolean success;
	ExifData *exif;

	g_assert(fd->change);

	static const size_t lf = strlen(GQ_CACHE_EXT_METADATA);
	if (fd->change->dest &&
	    g_ascii_strncasecmp(fd->change->dest + strlen(fd->change->dest) - lf, GQ_CACHE_EXT_METADATA, lf) == 0)
		{
		success = metadata_legacy_write(fd);
		if (success) metadata_legacy_delete(fd, fd->change->dest);
		return success;
		}

	/* write via exiv2 */
	/*  we can either use cached metadata which have fd->modified_xmp already applied
	                             or read metadata from file and apply fd->modified_xmp
	    metadata are read also if the file was modified meanwhile */
	exif = exif_read_fd(fd);
	if (!exif) return FALSE;

	success = (fd->change->dest) ? exif_write_sidecar(exif, fd->change->dest) : exif_write(exif); /* write modified metadata */
	exif_free_fd(fd, exif);

	if (fd->change->dest)
		/* this will create a FileData for the sidecar and link it to the main file
		   (we can't wait until the sidecar is discovered by directory scanning because
		    exif_read_fd is called before that and it would read the main file only and
		    store the metadata in the cache)
		*/
		/**
		@FIXME this does not catch new sidecars created by independent external programs
		*/
		file_data_unref(file_data_new_group(fd->change->dest));

	if (success) metadata_legacy_delete(fd, fd->change->dest);
	return success;
}

gint metadata_queue_length()
{
	return g_list_length(metadata_write_queue);
}

gboolean metadata_write_revert(FileData *fd, const gchar *key)
{
	if (!fd->modified_xmp) return FALSE;

	g_hash_table_remove(fd->modified_xmp, key);

	if (g_hash_table_size(fd->modified_xmp) == 0)
		{
		metadata_write_queue_remove(fd);
		}
	else
		{
		/* reread the metadata to restore the original value */
		file_data_increment_version(fd);
		file_data_send_notification(fd, NOTIFY_REREAD);
		}
	return TRUE;
}

gboolean metadata_write_list(FileData *fd, const gchar *key, const GList *values)
{
	if (!fd->modified_xmp)
		{
		fd->modified_xmp = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, string_list_free);
		}
	g_hash_table_insert(fd->modified_xmp, g_strdup(key), string_list_copy(const_cast<GList *>(values)));

	metadata_cache_remove(fd, key);

	if (fd->exif)
		{
		exif_update_metadata(fd->exif, key, values);
		}
	metadata_write_queue_add(fd);
	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_METADATA);

	auto metadata_check_key = [key](const gchar *k) { return strcmp(key, k) == 0; };
	if (options->metadata.sync_grouped_files && std::any_of(group_keys.cbegin(), group_keys.cend(), metadata_check_key))
		{
		GList *work = fd->sidecar_files;

		while (work)
			{
			auto sfd = static_cast<FileData *>(work->data);
			work = work->next;

			if (sfd->format_class == FORMAT_CLASS_META) continue;

			metadata_write_list(sfd, key, values);
			}
		}


	return TRUE;
}

gboolean metadata_write_string(FileData *fd, const gchar *key, const char *value)
{
	GList *list = g_list_append(nullptr, g_strdup(value));
	gboolean ret = metadata_write_list(fd, key, list);
	g_list_free_full(list, g_free);
	return ret;
}

gboolean metadata_write_int(FileData *fd, const gchar *key, guint64 value)
{
	return metadata_write_string(fd, key, std::to_string(static_cast<unsigned long long>(value)).c_str());
}

/*
 *-------------------------------------------------------------------
 * keyword / comment read/write
 *-------------------------------------------------------------------
 */

static gboolean metadata_file_write(gchar *path, const GList *keywords, const gchar *comment)
{
	g_autoptr(GString) gstring = g_string_new("#" GQ_APPNAME " comment (" VERSION ")\n\n[keywords]\n");

	for (; keywords; keywords = keywords->next)
		{
		auto word = static_cast<const gchar *>(keywords->data);
		g_string_append(gstring, word);
		}

	g_string_append_printf(gstring, "\n[comment]\n%s\n#end\n", comment ? comment : "");

	return secure_save(path, gstring->str, -1);
}

static gboolean metadata_legacy_write(FileData *fd)
{
	gboolean success = FALSE;
	gpointer keywords;
	gpointer comment_l;
	gboolean have_keywords;
	gboolean have_comment;
	const gchar *comment;
	GList *orig_keywords = nullptr;

	g_assert(fd->change && fd->change->dest);

	DEBUG_1("Saving comment: %s", fd->change->dest);

	if (!fd->modified_xmp) return TRUE;

	g_autofree gchar *metadata_pathl = path_from_utf8(fd->change->dest);

	have_keywords = g_hash_table_lookup_extended(fd->modified_xmp, KEYWORD_KEY, nullptr, &keywords);
	have_comment = g_hash_table_lookup_extended(fd->modified_xmp, COMMENT_KEY, nullptr, &comment_l);
	comment = static_cast<const gchar *>((have_comment && comment_l) ? (static_cast<GList *>(comment_l))->data : nullptr);

	g_autofree gchar *orig_comment = nullptr;
	if (!have_keywords || !have_comment) metadata_file_read(metadata_pathl, &orig_keywords, &orig_comment);

	success = metadata_file_write(metadata_pathl,
				      have_keywords ? static_cast<GList *>(keywords) : orig_keywords,
				      have_comment ? comment : orig_comment);

	g_list_free_full(orig_keywords, g_free);

	return success;
}

static gboolean metadata_file_read(gchar *path, GList **keywords, gchar **comment)
{
	FILE *f;
	gchar s_buf[1024];
	MetadataKey key = MK_NONE;
	GList *list = nullptr;

	f = fopen(path, "r");
	if (!f) return FALSE;

	g_autoptr(GString) comment_build = g_string_new(nullptr);

	while (fgets(s_buf, sizeof(s_buf), f))
		{
		gchar *ptr = s_buf;

		if (*ptr == '#') continue;
		if (*ptr == '[' && key != MK_COMMENT)
			{
			gchar *keystr = ++ptr;

			key = MK_NONE;
			while (*ptr != ']' && *ptr != '\n' && *ptr != '\0') ptr++;

			if (*ptr == ']')
				{
				*ptr = '\0';
				if (g_ascii_strcasecmp(keystr, "keywords") == 0)
					key = MK_KEYWORDS;
				else if (g_ascii_strcasecmp(keystr, "comment") == 0)
					key = MK_COMMENT;
				}
			continue;
			}

		switch (key)
			{
			case MK_NONE:
				break;
			case MK_KEYWORDS:
				{
				while (*ptr != '\n' && *ptr != '\0') ptr++;
				*ptr = '\0';
				if (s_buf[0] != '\0')
					{
					gchar *kw = utf8_validate_or_convert(s_buf);

					list = g_list_prepend(list, kw);
					}
				}
				break;
			case MK_COMMENT:
				g_string_append(comment_build, s_buf);
				break;
			}
		}

	fclose(f);

	if (keywords)
		{
		*keywords = g_list_reverse(list);
		}
	else
		{
		g_list_free_full(list, g_free);
		}

	if (comment && comment_build->len > 0)
		{
		gint len;
		gchar *ptr = comment_build->str;

		/* strip leading and trailing newlines */
		while (*ptr == '\n') ptr++;
		len = strlen(ptr);
		while (len > 0 && ptr[len - 1] == '\n') len--;
		if (ptr[len] == '\n') len++; /* keep the last one */
		if (len > 0)
			{
			g_autofree gchar *text = g_strndup(ptr, len);

			*comment = utf8_validate_or_convert(text);
			}
		}

	return TRUE;
}

static void metadata_legacy_delete(FileData *fd, const gchar *except)
{
	if (!fd) return;

	g_autofree gchar *metadata_path = cache_find_location(CACHE_TYPE_METADATA, fd->path);
	if (metadata_path && (!except || strcmp(metadata_path, except) != 0))
		{
		unlink_file(metadata_path);
		}

#if HAVE_EXIV2
	/* without exiv2: do not delete xmp metadata because we are not able to convert it,
	   just ignore it */
	g_autofree gchar *xmp_metadata_path = cache_find_location(CACHE_TYPE_XMP_METADATA, fd->path);
	if (xmp_metadata_path && (!except || strcmp(xmp_metadata_path, except) != 0))
		{
		unlink_file(xmp_metadata_path);
		}
#endif
}

static gboolean metadata_legacy_read(FileData *fd, GList **keywords, gchar **comment)
{
	if (!fd) return FALSE;

	g_autofree gchar *metadata_path = cache_find_location(CACHE_TYPE_METADATA, fd->path);
	if (!metadata_path) return FALSE;

	g_autofree gchar *metadata_pathl = path_from_utf8(metadata_path);

	return metadata_file_read(metadata_pathl, keywords, comment);
}

static GList *remove_duplicate_strings_from_list(GList *list)
{
	GList *work = list;
	GHashTable *hashtable = g_hash_table_new(g_str_hash, g_str_equal);
	GList *newlist = nullptr;

	while (work)
		{
		auto key = static_cast<gchar *>(work->data);

		if (g_hash_table_lookup(hashtable, key) == nullptr)
			{
			g_hash_table_insert(hashtable, key, GINT_TO_POINTER(1));
			newlist = g_list_prepend(newlist, key);
			}
		work = work->next;
		}

	g_hash_table_destroy(hashtable);
	g_list_free(list);

	return g_list_reverse(newlist);
}

GList *metadata_read_list(FileData *fd, const gchar *key, MetadataFormat format)
{
	ExifData *exif;
	GList *list = nullptr;
	const GList *cache_values;
	if (!fd) return nullptr;

	/* unwritten data override everything */
	if (fd->modified_xmp && format == METADATA_PLAIN)
		{
		list = static_cast<GList *>(g_hash_table_lookup(fd->modified_xmp, key));
		if (list) return string_list_copy(list);
		}


	if (format == METADATA_PLAIN && strcmp(key, KEYWORD_KEY) == 0
	    && (cache_values = metadata_cache_get(fd, key)))
		{
		return string_list_copy(cache_values);
		}

	/*
	    Legacy metadata file is the primary source if it exists.
	    Merging the lists does not make much sense, because the existence of
	    legacy metadata file indicates that the other metadata sources are not
	    writable and thus it would not be possible to delete the keywords
	    that comes from the image file.
	*/
	if (strcmp(key, KEYWORD_KEY) == 0)
		{
		if (metadata_legacy_read(fd, &list, nullptr))
			{
			if (format == METADATA_PLAIN)
				{
				metadata_cache_update(fd, key, list);
				}
			return list;
			}
		}
	else if (strcmp(key, COMMENT_KEY) == 0)
		{
		gchar *comment = nullptr;
		if (metadata_legacy_read(fd, nullptr, &comment)) return g_list_append(nullptr, comment);
		}
	else if (strncmp(key, "file.", 5) == 0)
		{
		return g_list_append(nullptr, metadata_file_info(fd, key));
		}
#if HAVE_LUA
	else if (strncmp(key, "lua.", 4) == 0)
		{
		return g_list_append(nullptr, metadata_lua_info(fd, key));
		}
#endif

	exif = exif_read_fd(fd); /* this is cached, thus inexpensive */
	if (!exif) return nullptr;
	list = exif_get_metadata(exif, key, format);
	exif_free_fd(fd, exif);

	if (format == METADATA_PLAIN && strcmp(key, KEYWORD_KEY) == 0)
		{
		metadata_cache_update(fd, key, list);
		}

	return list;
}

gchar *metadata_read_string(FileData *fd, const gchar *key, MetadataFormat format)
{
	GList *string_list = metadata_read_list(fd, key, format);
	if (string_list)
		{
		auto str = static_cast<gchar *>(string_list->data);
		string_list->data = nullptr;
		g_list_free_full(string_list, g_free);
		return str;
		}
	return nullptr;
}

guint64 metadata_read_int(FileData *fd, const gchar *key, guint64 fallback)
{
	guint64 ret;
	gchar *endptr;
	g_autofree gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!string) return fallback;

	ret = g_ascii_strtoull(string, &endptr, 10);
	if (string == endptr) ret = fallback;
	return ret;
}

gchar *metadata_read_rating_stars(FileData *fd)
{
	gchar *ret;
	gint n = metadata_read_int(fd, RATING_KEY, METADATA_PLAIN);

	ret = convert_rating_to_stars(n);

	return ret;
}

gdouble metadata_read_GPS_coord(FileData *fd, const gchar *key, gdouble fallback)
{
	gdouble coord;
	gchar *endptr;
	gdouble deg;
	gdouble min;
	gdouble sec;
	gboolean ok = FALSE;
	g_autofree gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!string) return fallback;

	deg = g_ascii_strtod(string, &endptr);
	if (*endptr == ',')
		{
		min = g_ascii_strtod(endptr + 1, &endptr);
		if (*endptr == ',')
			sec = g_ascii_strtod(endptr + 1, &endptr);
		else
			sec = 0.0;


		if (*endptr == 'S' || *endptr == 'W' || *endptr == 'N' || *endptr == 'E')
			{
			coord = deg + min /60.0 + sec / 3600.0;
			ok = TRUE;
			if (*endptr == 'S' || *endptr == 'W') coord = -coord;
			}
		}

	if (!ok)
		{
		coord = fallback;
		log_printf("unable to parse GPS coordinate '%s'\n", string);
		}

	return coord;
}

gdouble metadata_read_GPS_direction(FileData *fd, const gchar *key, gdouble fallback)
{
	gchar *endptr;
	gdouble deg;
	gboolean ok = FALSE;
	g_autofree gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!string) return fallback;

	DEBUG_3("GPS_direction: %s\n", string);
	deg = g_ascii_strtod(string, &endptr);

	/* Expected text string is of the format e.g.:
	 * 18000/100
	 */
	if (*endptr == '/')
		{
		deg = deg/100;
		ok = TRUE;
		}

	if (!ok)
		{
		deg = fallback;
		log_printf("unable to parse GPS direction '%s: %f'\n", string, deg);
		}

	return deg;
}

gboolean metadata_append_string(FileData *fd, const gchar *key, const char *value)
{
	g_autofree gchar *str = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!str)
		{
		return metadata_write_string(fd, key, value);
		}

	g_autofree gchar *new_string = g_strconcat(str, value, NULL);
	return metadata_write_string(fd, key, new_string);
}

gboolean metadata_write_GPS_coord(FileData *fd, const gchar *key, gdouble value)
{
	gint deg;
	gdouble min;
	gdouble param;
	const char *ref;
	gboolean ok = TRUE;
	char *old_locale;
	char *saved_locale;

	param = value;
	if (param < 0)
		param = -param;
	deg = param;
	min = (param * 60) - (deg * 60);
	if (g_strcmp0(key, "Xmp.exif.GPSLongitude") == 0)
		if (value < 0)
			ref = "W";
		else
			ref = "E";
	else if (g_strcmp0(key, "Xmp.exif.GPSLatitude") == 0)
		if (value < 0)
			ref = "S";
		else
			ref = "N";
	else
		{
		log_printf("unknown GPS parameter key '%s'\n", key);
		ok = FALSE;
		}

	if (ok)
		{
		/* Avoid locale problems with commas and decimal points in numbers */
		old_locale = setlocale(LC_ALL, nullptr);
		saved_locale = strdup(old_locale);
		if (saved_locale == nullptr)
			{
			return FALSE;
			}
		setlocale(LC_ALL, "C");

		g_autofree gchar *coordinate = g_strdup_printf("%i,%f,%s", deg, min, ref);
		metadata_write_string(fd, key, coordinate );

		setlocale(LC_ALL, saved_locale);
		free(saved_locale);
		}

	return ok;
}

gboolean metadata_append_list(FileData *fd, const gchar *key, const GList *values)
{
	GList *list = metadata_read_list(fd, key, METADATA_PLAIN);

	if (!list)
		{
		return metadata_write_list(fd, key, values);
		}

	gboolean ret;
	list = g_list_concat(list, string_list_copy(values));
	list = remove_duplicate_strings_from_list(list);

	ret = metadata_write_list(fd, key, list);
	g_list_free_full(list, g_free);
	return ret;
}

/**
 * @brief Find a existent string in a list.
 *
 * Search with or without case for the existence of a string.
 *
 * @param list The list to search in
 * @param string The string to search for
 * @return The GList or NULL
 */
static GList *find_string_in_list(GList *list, const gchar *string)
{
	if (!string) return nullptr;

	if (options->metadata.keywords_case_sensitive)
		return g_list_find_custom(list, string, reinterpret_cast<GCompareFunc>(g_strcmp0));

	g_autofree gchar *string_casefold = g_utf8_casefold(string, -1);
	static const auto string_compare_utf8nocase = [](gconstpointer data, gconstpointer user_data)
	{
		if (!data) return 1;

		g_autofree gchar *haystack = g_utf8_casefold(static_cast<const gchar *>(data), -1);
		return strcmp(haystack, static_cast<const gchar *>(user_data));
	};

	return g_list_find_custom(list, string_casefold, string_compare_utf8nocase);
}

GList *string_to_keywords_list(const gchar *text)
{
	GList *list = nullptr;
	const gchar *ptr = text;

	while (*ptr != '\0')
		{
		const gchar *begin;
		gint l = 0;

		while (is_keywords_separator(*ptr)) ptr++;
		begin = ptr;
		while (*ptr != '\0' && !is_keywords_separator(*ptr))
			{
			ptr++;
			l++;
			}

		/* trim starting and ending whitespaces */
		while (l > 0 && g_ascii_isspace(*begin)) begin++, l--;
		while (l > 0 && g_ascii_isspace(begin[l-1])) l--;

		if (l > 0)
			{
			g_autofree gchar *keyword = g_strndup(begin, l);

			/* only add if not already in the list */
			if (!find_string_in_list(list, keyword))
				{
				list = g_list_append(list, g_steal_pointer(&keyword));
				}
			}
		}

	return list;
}

/*
 * keywords to marks
 */


gboolean meta_data_get_keyword_mark(FileData *fd, gint, gpointer data)
{
	/** @FIXME do not use global keyword_tree */
	GList *keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);
	if (!keywords) return FALSE;

	auto path = static_cast<GList *>(data);
	GtkTreeIter iter;
	gboolean found = (keyword_tree_get_iter(GTK_TREE_MODEL(keyword_tree), &iter, path) &&
	                  keyword_tree_is_set(GTK_TREE_MODEL(keyword_tree), &iter, keywords));

	g_list_free_full(keywords, g_free);
	return found;
}

gboolean meta_data_set_keyword_mark(FileData *fd, gint, gboolean value, gpointer data)
{
	auto path = static_cast<GList *>(data);
	GtkTreeIter iter;

	if (!keyword_tree_get_iter(GTK_TREE_MODEL(keyword_tree), &iter, path)) return FALSE;

	GList *keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);

	if (!!keyword_tree_is_set(GTK_TREE_MODEL(keyword_tree), &iter, keywords) != !!value)
		{
		if (value)
			{
			keyword_tree_set(GTK_TREE_MODEL(keyword_tree), &iter, &keywords);
			}
		else
			{
			keyword_tree_reset(GTK_TREE_MODEL(keyword_tree), &iter, &keywords);
			}
		metadata_write_list(fd, KEYWORD_KEY, keywords);
		}

	g_list_free_full(keywords, g_free);
	return TRUE;
}



void meta_data_connect_mark_with_keyword(GtkTreeModel *keyword_tree, GtkTreeIter *kw_iter, gint mark)
{

	FileData::GetMarkFunc get_mark_func;
	FileData::SetMarkFunc set_mark_func;
	gpointer mark_func_data;

	gint i;

	for (i = 0; i < FILEDATA_MARKS_SIZE; i++)
		{
		file_data_get_registered_mark_func(i, &get_mark_func, &set_mark_func, &mark_func_data);
		if (get_mark_func == meta_data_get_keyword_mark)
			{
			GtkTreeIter old_kw_iter;
			auto old_path = static_cast<GList *>(mark_func_data);

			if (keyword_tree_get_iter(keyword_tree, &old_kw_iter, old_path) &&
			    (i == mark || /* release any previous connection of given mark */
			     keyword_equal(keyword_tree, &old_kw_iter, kw_iter))) /* or given keyword */
				{
				file_data_register_mark_func(i, nullptr, nullptr, nullptr, nullptr);
				gtk_tree_store_set(GTK_TREE_STORE(keyword_tree), &old_kw_iter, KEYWORD_COLUMN_MARK, "", -1);
				}
			}
		}


	if (mark >= 0 && mark < FILEDATA_MARKS_SIZE)
		{
		GList *path;
		path = keyword_tree_get_path(keyword_tree, kw_iter);
		file_data_register_mark_func(mark, meta_data_get_keyword_mark, meta_data_set_keyword_mark, path, string_list_free);

		gtk_tree_store_set(GTK_TREE_STORE(keyword_tree), kw_iter,
		                   KEYWORD_COLUMN_MARK, std::to_string((mark < 9) ? (mark + 1) : 0).c_str(),
		                   -1);
		}
}


/*
 *-------------------------------------------------------------------
 * keyword tree
 *-------------------------------------------------------------------
 */

gchar *keyword_get_name(GtkTreeModel *keyword_tree, GtkTreeIter *iter)
{
	gchar *name;
	gtk_tree_model_get(keyword_tree, iter, KEYWORD_COLUMN_NAME, &name, -1);
	return name;
}

gchar *keyword_get_mark(GtkTreeModel *keyword_tree, GtkTreeIter *iter)
{
	gchar *mark_str;

	gtk_tree_model_get(keyword_tree, iter, KEYWORD_COLUMN_MARK, &mark_str, -1);
	return mark_str;
}

gchar *keyword_get_casefold(GtkTreeModel *keyword_tree, GtkTreeIter *iter)
{
	gchar *casefold;
	gtk_tree_model_get(keyword_tree, iter, KEYWORD_COLUMN_CASEFOLD, &casefold, -1);
	return casefold;
}

gboolean keyword_get_is_keyword(GtkTreeModel *keyword_tree, GtkTreeIter *iter)
{
	gboolean is_keyword;
	gtk_tree_model_get(keyword_tree, iter, KEYWORD_COLUMN_IS_KEYWORD, &is_keyword, -1);
	return is_keyword;
}

void keyword_set(GtkTreeStore *keyword_tree, GtkTreeIter *iter, const gchar *name, gboolean is_keyword)
{
	g_autofree gchar *casefold = g_utf8_casefold(name, -1);
	gtk_tree_store_set(keyword_tree, iter, KEYWORD_COLUMN_MARK, "",
						KEYWORD_COLUMN_NAME, name,
						KEYWORD_COLUMN_CASEFOLD, casefold,
						KEYWORD_COLUMN_IS_KEYWORD, is_keyword, -1);
}

gboolean keyword_equal(GtkTreeModel *keyword_tree, GtkTreeIter *a, GtkTreeIter *b)
{
	g_autoptr(GtkTreePath) pa = gtk_tree_model_get_path(keyword_tree, a);
	g_autoptr(GtkTreePath) pb = gtk_tree_model_get_path(keyword_tree, b);

	return gtk_tree_path_compare(pa, pb) == 0;
}

gboolean keyword_same_parent(GtkTreeModel *keyword_tree, GtkTreeIter *a, GtkTreeIter *b)
{
	GtkTreeIter parent_a;
	GtkTreeIter parent_b;

	gboolean valid_pa = gtk_tree_model_iter_parent(keyword_tree, &parent_a, a);
	gboolean valid_pb = gtk_tree_model_iter_parent(keyword_tree, &parent_b, b);

	if (valid_pa && valid_pb)
		{
		return keyword_equal(keyword_tree, &parent_a, &parent_b);
		}

	return (!valid_pa && !valid_pb); /* both are toplevel */
}

gboolean keyword_exists(GtkTreeModel *keyword_tree, GtkTreeIter *parent_ptr, GtkTreeIter *sibling, const gchar *name, gboolean exclude_sibling, GtkTreeIter *result)
{
	GtkTreeIter parent;
	GtkTreeIter iter;
	gboolean toplevel = FALSE;

	if (parent_ptr)
		{
		parent = *parent_ptr;
		}
	else if (sibling)
		{
		toplevel = !gtk_tree_model_iter_parent(keyword_tree, &parent, sibling);
		}
	else
		{
		toplevel = TRUE;
		}

	if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(keyword_tree), &iter, toplevel ? nullptr : &parent)) return FALSE;

	g_autofree gchar *casefold = g_utf8_casefold(name, -1);
	gboolean ret = FALSE;

	do
		{
		if (exclude_sibling && sibling && keyword_equal(keyword_tree, &iter, sibling)) continue;

		if (options->metadata.keywords_case_sensitive)
			{
			g_autofree gchar *iter_name = keyword_get_name(keyword_tree, &iter);
			ret = strcmp(name, iter_name) == 0;
			}
		else
			{
			g_autofree gchar *iter_casefold = keyword_get_casefold(keyword_tree, &iter);
			ret = strcmp(casefold, iter_casefold) == 0;
			}

		if (ret) break;
		}
	while (gtk_tree_model_iter_next(keyword_tree, &iter));

	if (ret && result) *result = iter;

	return ret;
}


void keyword_copy(GtkTreeStore *keyword_tree, GtkTreeIter *to, GtkTreeIter *from)
{
	g_autofree gchar *mark = nullptr;
	g_autofree gchar *name = nullptr;
	g_autofree gchar *casefold = nullptr;
	gboolean is_keyword;

	/* do not copy KEYWORD_COLUMN_HIDE_IN, it fully shows the new subtree */
	gtk_tree_model_get(GTK_TREE_MODEL(keyword_tree), from, KEYWORD_COLUMN_MARK, &mark,
						KEYWORD_COLUMN_NAME, &name,
						KEYWORD_COLUMN_CASEFOLD, &casefold,
						KEYWORD_COLUMN_IS_KEYWORD, &is_keyword, -1);

	gtk_tree_store_set(keyword_tree, to, KEYWORD_COLUMN_MARK, mark,
						KEYWORD_COLUMN_NAME, name,
						KEYWORD_COLUMN_CASEFOLD, casefold,
						KEYWORD_COLUMN_IS_KEYWORD, is_keyword, -1);
}

void keyword_copy_recursive(GtkTreeStore *keyword_tree, GtkTreeIter *to, GtkTreeIter *from)
{
	GtkTreeIter from_child;

	keyword_copy(keyword_tree, to, from);

	if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(keyword_tree), &from_child, from)) return;

	while (TRUE)
		{
		GtkTreeIter to_child;
		gtk_tree_store_append(keyword_tree, &to_child, to);
		keyword_copy_recursive(keyword_tree, &to_child, &from_child);
		if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(keyword_tree), &from_child)) return;
		}
}

void keyword_move_recursive(GtkTreeStore *keyword_tree, GtkTreeIter *to, GtkTreeIter *from)
{
	keyword_copy_recursive(keyword_tree, to, from);
	keyword_delete(keyword_tree, from);
}

GList *keyword_tree_get_path(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr)
{
	GList *path = nullptr;
	GtkTreeIter iter = *iter_ptr;

	while (TRUE)
		{
		GtkTreeIter parent;
		path = g_list_prepend(path, keyword_get_name(keyword_tree, &iter));
		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) break;
		iter = parent;
		}
	return path;
}

gboolean keyword_tree_get_iter(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr, GList *path)
{
	GtkTreeIter iter;

	if (!gtk_tree_model_get_iter_first(keyword_tree, &iter)) return FALSE;

	while (TRUE)
		{
		GtkTreeIter children;
		while (TRUE)
			{
			g_autofree gchar *name = keyword_get_name(keyword_tree, &iter);
			if (strcmp(name, static_cast<const gchar *>(path->data)) == 0) break;
			if (!gtk_tree_model_iter_next(keyword_tree, &iter)) return FALSE;
			}
		path = path->next;
		if (!path)
			{
			*iter_ptr = iter;
			return TRUE;
			}

	    	if (!gtk_tree_model_iter_children(keyword_tree, &children, &iter)) return FALSE;
	    	iter = children;
		}
}


static gboolean keyword_tree_is_set_casefold(GtkTreeModel *keyword_tree, GtkTreeIter iter, GList *casefold_list)
{
	if (!casefold_list) return FALSE;

	if (!keyword_get_is_keyword(keyword_tree, &iter))
		{
		/* for the purpose of expanding and hiding, a helper is set if it has any children set */
		GtkTreeIter child;
		if (!gtk_tree_model_iter_children(keyword_tree, &child, &iter))
			return FALSE; /* this should happen only on empty helpers */

		while (TRUE)
			{
			if (keyword_tree_is_set_casefold(keyword_tree, child, casefold_list)) return TRUE;
			if (!gtk_tree_model_iter_next(keyword_tree, &child)) return FALSE;
			}
		}

	while (TRUE)
		{
		GtkTreeIter parent;

		if (keyword_get_is_keyword(keyword_tree, &iter))
			{
			g_autofree gchar *iter_casefold = keyword_get_casefold(keyword_tree, &iter);
			GList *found = g_list_find_custom(casefold_list, iter_casefold, reinterpret_cast<GCompareFunc>(strcmp));
			if (!found) return FALSE;
			}

		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return TRUE;
		iter = parent;
		}
}

static gboolean keyword_tree_is_set_casefull(GtkTreeModel *keyword_tree, GtkTreeIter iter, GList *kw_list)
{
	if (!kw_list) return FALSE;

	if (!keyword_get_is_keyword(keyword_tree, &iter))
		{
		/* for the purpose of expanding and hiding, a helper is set if it has any children set */
		GtkTreeIter child;
		if (!gtk_tree_model_iter_children(keyword_tree, &child, &iter))
			return FALSE; /* this should happen only on empty helpers */

		while (TRUE)
			{
			if (keyword_tree_is_set_casefull(keyword_tree, child, kw_list)) return TRUE;
			if (!gtk_tree_model_iter_next(keyword_tree, &child)) return FALSE;
			}
		}

	while (TRUE)
		{
		GtkTreeIter parent;

		if (keyword_get_is_keyword(keyword_tree, &iter))
			{
			g_autofree gchar *iter_name = keyword_get_name(keyword_tree, &iter);
			GList *found = g_list_find_custom(kw_list, iter_name, reinterpret_cast<GCompareFunc>(strcmp));
			if (!found) return FALSE;
			}

		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return TRUE;
		iter = parent;
		}
}

gboolean keyword_tree_is_set(GtkTreeModel *keyword_tree, GtkTreeIter *iter, GList *kw_list)
{
	gboolean ret;
	GList *casefold_list = nullptr;
	GList *work;

	if (options->metadata.keywords_case_sensitive)
		{
		ret = keyword_tree_is_set_casefull(keyword_tree, *iter, kw_list);
		}
	else
		{
		work = kw_list;
		while (work)
			{
			auto kw = static_cast<const gchar *>(work->data);
			work = work->next;

			casefold_list = g_list_prepend(casefold_list, g_utf8_casefold(kw, -1));
			}

		ret = keyword_tree_is_set_casefold(keyword_tree, *iter, casefold_list);

		g_list_free_full(casefold_list, g_free);
		}

	return ret;
}

void keyword_tree_set(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr, GList **kw_list)
{
	GtkTreeIter iter = *iter_ptr;
	while (TRUE)
		{
		GtkTreeIter parent;

		if (keyword_get_is_keyword(keyword_tree, &iter))
			{
			g_autofree gchar *name = keyword_get_name(keyword_tree, &iter);
			if (!find_string_in_list(*kw_list, name))
				{
				*kw_list = g_list_append(*kw_list, g_steal_pointer(&name));
				}
			}

		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return;
		iter = parent;
		}
}

GList *keyword_tree_get(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr)
{
	GtkTreeIter iter = *iter_ptr;
	GList *kw_list = nullptr;

	while (TRUE)
		{
		GtkTreeIter parent;

		if (keyword_get_is_keyword(keyword_tree, &iter))
			{
			gchar *name = keyword_get_name(keyword_tree, &iter);
			kw_list = g_list_append(kw_list, name);
			}

		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return kw_list;
		iter = parent;
		}
} // GList *keyword_tree_get(GtkTre...

static void keyword_tree_reset1(GtkTreeModel *keyword_tree, GtkTreeIter *iter, GList **kw_list)
{
	if (!keyword_get_is_keyword(keyword_tree, iter)) return;

	g_autofree gchar *name = keyword_get_name(keyword_tree, iter);

	GList *found = find_string_in_list(*kw_list, name);
	if (found)
		{
		g_free(found->data);
		*kw_list = g_list_delete_link(*kw_list, found);
		}
}

static void keyword_tree_reset_recursive(GtkTreeModel *keyword_tree, GtkTreeIter *iter, GList **kw_list)
{
	GtkTreeIter child;
	keyword_tree_reset1(keyword_tree, iter, kw_list);

	if (!gtk_tree_model_iter_children(keyword_tree, &child, iter)) return;

	while (TRUE)
		{
		keyword_tree_reset_recursive(keyword_tree, &child, kw_list);
		if (!gtk_tree_model_iter_next(keyword_tree, &child)) return;
		}
}

static gboolean keyword_tree_check_empty_children(GtkTreeModel *keyword_tree, GtkTreeIter *parent, GList *kw_list)
{
	GtkTreeIter iter;

	if (!gtk_tree_model_iter_children(keyword_tree, &iter, parent))
		return TRUE; /* this should happen only on empty helpers */

	while (TRUE)
		{
		if (keyword_tree_is_set(keyword_tree, &iter, kw_list)) return FALSE;
		if (!gtk_tree_model_iter_next(keyword_tree, &iter)) return TRUE;
		}
}

void keyword_tree_reset(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr, GList **kw_list)
{
	GtkTreeIter iter = *iter_ptr;
	GtkTreeIter parent;
	keyword_tree_reset_recursive(keyword_tree, &iter, kw_list);

	if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return;
	iter = parent;

	while (keyword_tree_check_empty_children(keyword_tree, &iter, *kw_list))
		{
		GtkTreeIter parent;
		keyword_tree_reset1(keyword_tree, &iter, kw_list);
		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return;
		iter = parent;
		}
}

void keyword_delete(GtkTreeStore *keyword_tree, GtkTreeIter *iter_ptr)
{
	GList *list;
	GtkTreeIter child;
	while (gtk_tree_model_iter_children(GTK_TREE_MODEL(keyword_tree), &child, iter_ptr))
		{
		keyword_delete(keyword_tree, &child);
		}

	meta_data_connect_mark_with_keyword(GTK_TREE_MODEL(keyword_tree), iter_ptr, -1);

	gtk_tree_model_get(GTK_TREE_MODEL(keyword_tree), iter_ptr, KEYWORD_COLUMN_HIDE_IN, &list, -1);
	g_list_free(list);

	gtk_tree_store_remove(keyword_tree, iter_ptr);
}


void keyword_hide_in(GtkTreeStore *keyword_tree, GtkTreeIter *iter, gpointer id)
{
	GList *list;
	gtk_tree_model_get(GTK_TREE_MODEL(keyword_tree), iter, KEYWORD_COLUMN_HIDE_IN, &list, -1);
	if (!g_list_find(list, id))
		{
		list = g_list_prepend(list, id);
		gtk_tree_store_set(keyword_tree, iter, KEYWORD_COLUMN_HIDE_IN, list, -1);
		}
}

void keyword_show_in(GtkTreeStore *keyword_tree, GtkTreeIter *iter, gpointer id)
{
	GList *list;
	gtk_tree_model_get(GTK_TREE_MODEL(keyword_tree), iter, KEYWORD_COLUMN_HIDE_IN, &list, -1);
	list = g_list_remove(list, id);
	gtk_tree_store_set(keyword_tree, iter, KEYWORD_COLUMN_HIDE_IN, list, -1);
}

gboolean keyword_is_hidden_in(GtkTreeModel *keyword_tree, GtkTreeIter *iter, gpointer id)
{
	GList *list;
	gtk_tree_model_get(keyword_tree, iter, KEYWORD_COLUMN_HIDE_IN, &list, -1);
	return !!g_list_find(list, id);
}

static gboolean keyword_show_all_in_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
	keyword_show_in(GTK_TREE_STORE(model), iter, data);
	return FALSE;
}

void keyword_show_all_in(GtkTreeStore *keyword_tree, gpointer id)
{
	gtk_tree_model_foreach(GTK_TREE_MODEL(keyword_tree), keyword_show_all_in_cb, id);
}

static gboolean keyword_revert_hidden_in_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
	if (keyword_is_hidden_in(GTK_TREE_MODEL(keyword_tree), iter, data))
		{
		keyword_show_in(GTK_TREE_STORE(model), iter, data);
		}
	return FALSE;
}

void keyword_revert_hidden_in(GtkTreeStore *keyword_tree, gpointer id)
{
	gtk_tree_model_foreach(GTK_TREE_MODEL(keyword_tree), keyword_revert_hidden_in_cb, id);
}

static void keyword_hide_unset_in_recursive(GtkTreeStore *keyword_tree, GtkTreeIter *iter_ptr, gpointer id, GList *keywords)
{
	GtkTreeIter iter = *iter_ptr;
	while (TRUE)
		{
		if (!keyword_tree_is_set(GTK_TREE_MODEL(keyword_tree), &iter, keywords))
			{
			keyword_hide_in(keyword_tree, &iter, id);
			/* no need to check children of hidden node */
			}
		else
			{
			GtkTreeIter child;
			if (gtk_tree_model_iter_children(GTK_TREE_MODEL(keyword_tree), &child, &iter))
				{
				keyword_hide_unset_in_recursive(keyword_tree, &child, id, keywords);
				}
			}
		if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(keyword_tree), &iter)) return;
		}
}

void keyword_hide_unset_in(GtkTreeStore *keyword_tree, gpointer id, GList *keywords)
{
	GtkTreeIter iter;
	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(keyword_tree), &iter)) return;
	keyword_hide_unset_in_recursive(keyword_tree, &iter, id, keywords);
}

static gboolean keyword_show_set_in_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter_ptr, gpointer data)
{
	GtkTreeIter iter = *iter_ptr;
	auto keywords = static_cast<GList *>(data);
	gpointer id = keywords->data;
	keywords = keywords->next; /* hack */
	if (keyword_tree_is_set(model, &iter, keywords))
		{
		while (TRUE)
			{
			GtkTreeIter parent;
			keyword_show_in(GTK_TREE_STORE(model), &iter, id);
			if (!gtk_tree_model_iter_parent(GTK_TREE_MODEL(keyword_tree), &parent, &iter)) break;
			iter = parent;
			}
		}
	return FALSE;
}

void keyword_show_set_in(GtkTreeStore *keyword_tree, gpointer id, GList *keywords)
{
	/* hack: pass id to keyword_hide_unset_in_cb in the list */
	keywords = g_list_prepend(keywords, id);
	gtk_tree_model_foreach(GTK_TREE_MODEL(keyword_tree), keyword_show_set_in_cb, keywords);
	keywords = g_list_delete_link(keywords, keywords);
}


GtkTreeStore *keyword_tree_get_or_new()
{
	if (!keyword_tree)
		keyword_tree = gtk_tree_store_new(KEYWORD_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER);

	return keyword_tree;
}

static GtkTreeIter keyword_tree_default_append(GtkTreeStore *keyword_tree, GtkTreeIter *parent, const gchar *name, gboolean is_keyword)
{
	GtkTreeIter iter;
	gtk_tree_store_append(keyword_tree, &iter, parent);
	keyword_set(keyword_tree, &iter, name, is_keyword);
	return iter;
}

void keyword_tree_set_default(GtkTreeStore *keyword_tree)
{
	if (!keyword_tree) return;

	GtkTreeIter i1;
	GtkTreeIter i2;

	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("People"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Family"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Free time"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Children"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Sport"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Culture"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Festival"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("Nature"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Animal"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Bird"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Insect"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Pets"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Wildlife"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Zoo"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Plant"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Tree"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Flower"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Water"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("River"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Lake"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Sea"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Landscape"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("Art"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Statue"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Painting"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Historic"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Modern"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("City"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Park"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Street"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Square"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("Architecture"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Buildings"), FALSE);
			keyword_tree_default_append(keyword_tree, &i2, _("House"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Cathedral"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Palace"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Castle"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Bridge"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Interior"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Historic"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Modern"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("Places"), FALSE);
	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("Conditions"), FALSE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Night"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Lights"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Reflections"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Sun"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Weather"), FALSE);
			keyword_tree_default_append(keyword_tree, &i2, _("Fog"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Rain"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Clouds"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Snow"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Sunny weather"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, nullptr, _("Photo"), FALSE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Edited"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Detail"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Macro"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Portrait"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Black and White"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Perspective"), TRUE);
}


static void keyword_tree_node_write_config(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr, GString *outstr, gint indent)
{
	GtkTreeIter iter = *iter_ptr;
	while (TRUE)
		{
		GtkTreeIter children;

		WRITE_NL(); WRITE_STRING("<keyword ");
		g_autofree gchar *name = keyword_get_name(keyword_tree, &iter);
		write_char_option(outstr, "name", name);
		write_bool_option(outstr, "kw", keyword_get_is_keyword(keyword_tree, &iter));
		g_autofree gchar *mark_str = keyword_get_mark(keyword_tree, &iter);
		if (mark_str && mark_str[0])
			{
			write_char_option(outstr, "mark", mark_str);
			}

		if (gtk_tree_model_iter_children(keyword_tree, &children, &iter))
			{
			WRITE_STRING(">");
			indent++;
			keyword_tree_node_write_config(keyword_tree, &children, outstr, indent);
			indent--;
			WRITE_NL(); WRITE_STRING("</keyword>");
			}
		else
			{
			WRITE_STRING("/>");
			}
		if (!gtk_tree_model_iter_next(keyword_tree, &iter)) return;
		}
}

void keyword_tree_write_config(GString *outstr, gint indent)
{
	GtkTreeIter iter;
	WRITE_NL(); WRITE_STRING("<keyword_tree>");
	indent++;

	if (keyword_tree && gtk_tree_model_get_iter_first(GTK_TREE_MODEL(keyword_tree), &iter))
		{
		keyword_tree_node_write_config(GTK_TREE_MODEL(keyword_tree), &iter, outstr, indent);
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</keyword_tree>");
}

static void keyword_tree_node_disconnect_marks(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr)
{
	GtkTreeIter iter = *iter_ptr;

	while (TRUE)
		{
		GtkTreeIter children;

		meta_data_connect_mark_with_keyword((keyword_tree), &iter, -1);

		if (gtk_tree_model_iter_children((keyword_tree), &children, &iter))
			{
			keyword_tree_node_disconnect_marks((keyword_tree), &children);
			}

		if (!gtk_tree_model_iter_next((keyword_tree), &iter)) return;
		}
}

void keyword_tree_disconnect_marks()
{
	GtkTreeIter iter;

	if (keyword_tree && gtk_tree_model_get_iter_first(GTK_TREE_MODEL(keyword_tree), &iter))
		{
		keyword_tree_node_disconnect_marks(GTK_TREE_MODEL(keyword_tree), &iter);
		}
}

GtkTreeIter *keyword_add_from_config(GtkTreeStore *keyword_tree, GtkTreeIter *parent, const gchar **attribute_names, const gchar **attribute_values)
{
	g_autofree gchar *name = nullptr;
	gboolean is_kw = TRUE;
	g_autofree gchar *mark_str = nullptr;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("name", name)) continue;
		if (READ_BOOL_FULL("kw", is_kw)) continue;
		if (READ_CHAR_FULL("mark", mark_str)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	if (name && name[0])
		{
		GtkTreeIter iter;
		/* re-use existing keyword if any */
		if (!keyword_exists(GTK_TREE_MODEL(keyword_tree), parent, nullptr, name, FALSE, &iter))
			{
			gtk_tree_store_append(keyword_tree, &iter, parent);
			}
		keyword_set(keyword_tree, &iter, name, is_kw);

		if (mark_str)
			{
			gint i = static_cast<gint>(atoi(mark_str));
			if (i == 0) i = 10;

			meta_data_connect_mark_with_keyword(GTK_TREE_MODEL(keyword_tree),
											&iter, i - 1);
			}

		return gtk_tree_iter_copy(&iter);
		}

	return nullptr;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
