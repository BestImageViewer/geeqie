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

#include <locale.h>

#include "main.h"
#include "metadata.h"

#include "cache.h"
#include "exif.h"
#include "filedata.h"
#include "misc.h"
#include "secure-save.h"
#include "ui-fileops.h"
#include "utilops.h"
#include "layout-util.h"
#include "rcfile.h"

typedef enum {
	MK_NONE,
	MK_KEYWORDS,
	MK_COMMENT
} MetadataKey;

/* If contents change, keep GuideOptionsMetadata.xml up to date */
/**
 *  @brief Tags that will be written to all files in a group - selected by: options->metadata.sync_grouped_files, Preferences/Metadata/Write The Same Description Tags To All Grouped Sidecars
 */
static const gchar *group_keys[] = {
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
	NULL};

static gboolean metadata_write_queue_idle_cb(gpointer data);
static gboolean metadata_legacy_write(FileData *fd);
static void metadata_legacy_delete(FileData *fd, const gchar *except);
static gboolean metadata_file_read(gchar *path, GList **keywords, gchar **comment);


/*
 *-------------------------------------------------------------------
 * long-term cache - keep keywords from whole dir in memory
 *-------------------------------------------------------------------
 */

/* fd->cached metadata list of lists
   each particular list contains key as a first entry, then the values
*/

static void metadata_cache_update(FileData *fd, const gchar *key, const GList *values)
{
	GList *work;

	work = fd->cached_metadata;
	while (work)
		{
		auto entry = static_cast<GList *>(work->data);
		auto entry_key = static_cast<gchar *>(entry->data);

		if (strcmp(entry_key, key) == 0)
			{
			/* key found - just replace values */
			GList *old_values = entry->next;
			entry->next = NULL;
			old_values->prev = NULL;
			string_list_free(old_values);
			work->data = g_list_append(entry, string_list_copy(values));
			DEBUG_1("updated %s %s\n", key, fd->path);
			return;
			}
		work = work->next;
		}

	/* key not found - prepend new entry */
	fd->cached_metadata = g_list_prepend(fd->cached_metadata,
				g_list_prepend(string_list_copy(values), g_strdup(key)));
	DEBUG_1("added %s %s\n", key, fd->path);

}

static const GList *metadata_cache_get(FileData *fd, const gchar *key)
{
	GList *work;

	work = fd->cached_metadata;
	while (work)
		{
		auto entry = static_cast<GList *>(work->data);
		auto entry_key = static_cast<gchar *>(entry->data);

		if (strcmp(entry_key, key) == 0)
			{
			/* key found */
			DEBUG_1("found %s %s\n", key, fd->path);
			return entry;
			}
		work = work->next;
		}
	return NULL;
	DEBUG_1("not found %s %s\n", key, fd->path);
}

static void metadata_cache_remove(FileData *fd, const gchar *key)
{
	GList *work;

	work = fd->cached_metadata;
	while (work)
		{
		auto entry = static_cast<GList *>(work->data);
		auto entry_key = static_cast<gchar *>(entry->data);

		if (strcmp(entry_key, key) == 0)
			{
			/* key found */
			string_list_free(entry);
			fd->cached_metadata = g_list_delete_link(fd->cached_metadata, work);
			DEBUG_1("removed %s %s\n", key, fd->path);
			return;
			}
		work = work->next;
		}
	DEBUG_1("not removed %s %s\n", key, fd->path);
}

void metadata_cache_free(FileData *fd)
{
	GList *work;
	if (fd->cached_metadata) DEBUG_1("freed %s\n", fd->path);

	work = fd->cached_metadata;
	while (work)
		{
		auto entry = static_cast<GList *>(work->data);
		string_list_free(entry);

		work = work->next;
		}
	g_list_free(fd->cached_metadata);
	fd->cached_metadata = NULL;
}






/*
 *-------------------------------------------------------------------
 * write queue
 *-------------------------------------------------------------------
 */

static GList *metadata_write_queue = NULL;
static guint metadata_write_idle_id = 0; /* event source id */

static void metadata_write_queue_add(FileData *fd)
{
	if (!g_list_find(metadata_write_queue, fd))
		{
		metadata_write_queue = g_list_prepend(metadata_write_queue, fd);
		file_data_ref(fd);

		layout_util_status_update_write_all();
		}

	if (metadata_write_idle_id)
		{
		g_source_remove(metadata_write_idle_id);
		metadata_write_idle_id = 0;
		}

	if (options->metadata.confirm_after_timeout)
		{
		metadata_write_idle_id = g_timeout_add(options->metadata.confirm_timeout * 1000, metadata_write_queue_idle_cb, NULL);
		}
}


gboolean metadata_write_queue_remove(FileData *fd)
{
	g_hash_table_destroy(fd->modified_xmp);
	fd->modified_xmp = NULL;

	metadata_write_queue = g_list_remove(metadata_write_queue, fd);

	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_REREAD);

	file_data_unref(fd);

	layout_util_status_update_write_all();
	return TRUE;
}

//gboolean metadata_write_queue_remove_list(GList *list)
//{
	//GList *work;
	//gboolean ret = TRUE;

	//work = list;
	//while (work)
		//{
		//FileData *fd = static_cast<//FileData *>(work->data);
		//work = work->next;
		//ret = ret && metadata_write_queue_remove(fd);
		//}
	//return ret;
//}

void metadata_notify_cb(FileData *fd, NotifyType type, gpointer UNUSED(data))
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

gboolean metadata_write_queue_confirm(gboolean force_dialog, FileUtilDoneFunc done_func, gpointer done_data)
{
	GList *work;
	GList *to_approve = NULL;

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

	file_util_write_metadata(NULL, to_approve, NULL, force_dialog, done_func, done_data);

	return (metadata_write_queue != NULL);
}

static gboolean metadata_write_queue_idle_cb(gpointer UNUSED(data))
{
	metadata_write_queue_confirm(FALSE, NULL, NULL);
	metadata_write_idle_id = 0;
	return FALSE;
}

gboolean metadata_write_perform(FileData *fd)
{
	gboolean success;
	ExifData *exif;
	guint lf;

	g_assert(fd->change);

	lf = strlen(GQ_CACHE_EXT_METADATA);
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

gint metadata_queue_length(void)
{
	return g_list_length(metadata_write_queue);
}

static gboolean metadata_check_key(const gchar *keys[], const gchar *key)
{
	const gchar **k = keys;

	while (*k)
		{
		if (strcmp(key, *k) == 0) return TRUE;
		k++;
		}
	return FALSE;
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
		fd->modified_xmp = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)string_list_free);
		}
	g_hash_table_insert(fd->modified_xmp, g_strdup(key), string_list_copy((GList *)values));

	metadata_cache_remove(fd, key);

	if (fd->exif)
		{
		exif_update_metadata(fd->exif, key, values);
		}
	metadata_write_queue_add(fd);
	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_METADATA);

	if (options->metadata.sync_grouped_files && metadata_check_key(group_keys, key))
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
	GList *list = g_list_append(NULL, g_strdup(value));
	gboolean ret = metadata_write_list(fd, key, list);
	string_list_free(list);
	return ret;
}

gboolean metadata_write_int(FileData *fd, const gchar *key, guint64 value)
{
	gchar string[50];

	g_snprintf(string, sizeof(string), "%llu", (unsigned long long) value);
	return metadata_write_string(fd, key, string);
}

/*
 *-------------------------------------------------------------------
 * keyword / comment read/write
 *-------------------------------------------------------------------
 */

static gboolean metadata_file_write(gchar *path, const GList *keywords, const gchar *comment)
{
	SecureSaveInfo *ssi;

	ssi = secure_open(path);
	if (!ssi) return FALSE;

	secure_fprintf(ssi, "#%s comment (%s)\n\n", GQ_APPNAME, VERSION);

	secure_fprintf(ssi, "[keywords]\n");
	while (keywords && secsave_errno == SS_ERR_NONE)
		{
		auto word = static_cast<const gchar *>(keywords->data);
		keywords = keywords->next;

		secure_fprintf(ssi, "%s\n", word);
		}
	secure_fputc(ssi, '\n');

	secure_fprintf(ssi, "[comment]\n");
	secure_fprintf(ssi, "%s\n", (comment) ? comment : "");

	secure_fprintf(ssi, "#end\n");

	return (secure_close(ssi) == 0);
}

static gboolean metadata_legacy_write(FileData *fd)
{
	gboolean success = FALSE;
	gchar *metadata_pathl;
	gpointer keywords;
	gpointer comment_l;
	gboolean have_keywords;
	gboolean have_comment;
	const gchar *comment;
	GList *orig_keywords = NULL;
	gchar *orig_comment = NULL;

	g_assert(fd->change && fd->change->dest);

	DEBUG_1("Saving comment: %s", fd->change->dest);

	if (!fd->modified_xmp) return TRUE;

	metadata_pathl = path_from_utf8(fd->change->dest);

	have_keywords = g_hash_table_lookup_extended(fd->modified_xmp, KEYWORD_KEY, NULL, &keywords);
	have_comment = g_hash_table_lookup_extended(fd->modified_xmp, COMMENT_KEY, NULL, &comment_l);
	comment = static_cast<const gchar *>((have_comment && comment_l) ? ((GList *)comment_l)->data : NULL);

	if (!have_keywords || !have_comment) metadata_file_read(metadata_pathl, &orig_keywords, &orig_comment);

	success = metadata_file_write(metadata_pathl,
				      have_keywords ? (GList *)keywords : orig_keywords,
				      have_comment ? comment : orig_comment);

	g_free(metadata_pathl);
	g_free(orig_comment);
	string_list_free(orig_keywords);

	return success;
}

static gboolean metadata_file_read(gchar *path, GList **keywords, gchar **comment)
{
	FILE *f;
	gchar s_buf[1024];
	MetadataKey key = MK_NONE;
	GList *list = NULL;
	GString *comment_build = NULL;

	f = fopen(path, "r");
	if (!f) return FALSE;

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
				if (strlen(s_buf) > 0)
					{
					gchar *kw = utf8_validate_or_convert(s_buf);

					list = g_list_prepend(list, kw);
					}
				}
				break;
			case MK_COMMENT:
				if (!comment_build) comment_build = g_string_new("");
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
		string_list_free(list);
		}

	if (comment_build)
		{
		if (comment)
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
				gchar *text = g_strndup(ptr, len);

				*comment = utf8_validate_or_convert(text);
				g_free(text);
				}
			}
		g_string_free(comment_build, TRUE);
		}

	return TRUE;
}

static void metadata_legacy_delete(FileData *fd, const gchar *except)
{
	gchar *metadata_path;
	gchar *metadata_pathl;
	if (!fd) return;

	metadata_path = cache_find_location(CACHE_TYPE_METADATA, fd->path);
	if (metadata_path && (!except || strcmp(metadata_path, except) != 0))
		{
		metadata_pathl = path_from_utf8(metadata_path);
		unlink(metadata_pathl);
		g_free(metadata_pathl);
		g_free(metadata_path);
		}

#ifdef HAVE_EXIV2
	/* without exiv2: do not delete xmp metadata because we are not able to convert it,
	   just ignore it */
	metadata_path = cache_find_location(CACHE_TYPE_XMP_METADATA, fd->path);
	if (metadata_path && (!except || strcmp(metadata_path, except) != 0))
		{
		metadata_pathl = path_from_utf8(metadata_path);
		unlink(metadata_pathl);
		g_free(metadata_pathl);
		g_free(metadata_path);
		}
#endif
}

static gboolean metadata_legacy_read(FileData *fd, GList **keywords, gchar **comment)
{
	gchar *metadata_path;
	gchar *metadata_pathl;
	gboolean success = FALSE;

	if (!fd) return FALSE;

	metadata_path = cache_find_location(CACHE_TYPE_METADATA, fd->path);
	if (!metadata_path) return FALSE;

	metadata_pathl = path_from_utf8(metadata_path);

	success = metadata_file_read(metadata_pathl, keywords, comment);

	g_free(metadata_pathl);
	g_free(metadata_path);

	return success;
}

static GList *remove_duplicate_strings_from_list(GList *list)
{
	GList *work = list;
	GHashTable *hashtable = g_hash_table_new(g_str_hash, g_str_equal);
	GList *newlist = NULL;

	while (work)
		{
		auto key = static_cast<gchar *>(work->data);

		if (g_hash_table_lookup(hashtable, key) == NULL)
			{
			g_hash_table_insert(hashtable, (gpointer) key, GINT_TO_POINTER(1));
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
	GList *list = NULL;
	const GList *cache_entry;
	if (!fd) return NULL;

	/* unwritten data override everything */
	if (fd->modified_xmp && format == METADATA_PLAIN)
		{
	        list = static_cast<GList *>(g_hash_table_lookup(fd->modified_xmp, key));
		if (list) return string_list_copy(list);
		}


	if (format == METADATA_PLAIN && strcmp(key, KEYWORD_KEY) == 0
	    && (cache_entry = metadata_cache_get(fd, key)))
		{
		return string_list_copy(cache_entry->next);
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
		if (metadata_legacy_read(fd, &list, NULL))
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
		gchar *comment = NULL;
	        if (metadata_legacy_read(fd, NULL, &comment)) return g_list_append(NULL, comment);
	        }
	else if (strncmp(key, "file.", 5) == 0)
		{
	        return g_list_append(NULL, metadata_file_info(fd, key, format));
		}
#ifdef HAVE_LUA
	else if (strncmp(key, "lua.", 4) == 0)
		{
		return g_list_append(NULL, metadata_lua_info(fd, key, format));
		}
#endif

	exif = exif_read_fd(fd); /* this is cached, thus inexpensive */
	if (!exif) return NULL;
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
		string_list->data = NULL;
		string_list_free(string_list);
		return str;
		}
	return NULL;
}

guint64 metadata_read_int(FileData *fd, const gchar *key, guint64 fallback)
{
	guint64 ret;
	gchar *endptr;
	gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!string) return fallback;

	ret = g_ascii_strtoull(string, &endptr, 10);
	if (string == endptr) ret = fallback;
	g_free(string);
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
	gdouble deg, min, sec;
	gboolean ok = FALSE;
	gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
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

	g_free(string);
	return coord;
}

gdouble metadata_read_GPS_direction(FileData *fd, const gchar *key, gdouble fallback)
{
	gchar *endptr;
	gdouble deg;
	gboolean ok = FALSE;
	gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
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

	g_free(string);

	return deg;
}

gboolean metadata_append_string(FileData *fd, const gchar *key, const char *value)
{
	gchar *str = metadata_read_string(fd, key, METADATA_PLAIN);

	if (!str)
		{
		return metadata_write_string(fd, key, value);
		}
	else
		{
		gchar *new_string = g_strconcat(str, value, NULL);
		gboolean ret = metadata_write_string(fd, key, new_string);
		g_free(str);
		g_free(new_string);
		return ret;
		}
}

gboolean metadata_write_GPS_coord(FileData *fd, const gchar *key, gdouble value)
{
	gint deg;
	gdouble min;
	gdouble param;
	char *coordinate;
	const char *ref;
	gboolean ok = TRUE;
	char *old_locale, *saved_locale;

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
		old_locale = setlocale(LC_ALL, NULL);
		saved_locale = strdup(old_locale);
		if (saved_locale == NULL)
			{
			return FALSE;
			}
		setlocale(LC_ALL, "C");

		coordinate = g_strdup_printf("%i,%lf,%s", deg, min, ref);
		metadata_write_string(fd, key, coordinate );

		setlocale(LC_ALL, saved_locale);
		free(saved_locale);
		g_free(coordinate);
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
	else
		{
		gboolean ret;
		list = g_list_concat(list, string_list_copy(values));
		list = remove_duplicate_strings_from_list(list);

		ret = metadata_write_list(fd, key, list);
		string_list_free(list);
		return ret;
		}
}

/**
 * @see find_string_in_list
 */
gchar *find_string_in_list_utf8nocase(GList *list, const gchar *string)
{
	gchar *string_casefold = g_utf8_casefold(string, -1);

	while (list)
		{
		auto haystack = static_cast<gchar *>(list->data);

		if (haystack)
			{
			gboolean equal;
			gchar *haystack_casefold = g_utf8_casefold(haystack, -1);

			equal = (strcmp(haystack_casefold, string_casefold) == 0);
			g_free(haystack_casefold);

			if (equal)
				{
				g_free(string_casefold);
				return haystack;
				}
			}

		list = list->next;
		}

	g_free(string_casefold);
	return NULL;
}

/**
 * @see find_string_in_list
 */
gchar *find_string_in_list_utf8case(GList *list, const gchar *string)
{
	while (list)
		{
		auto haystack = static_cast<gchar *>(list->data);

		if (haystack && strcmp(haystack, string) == 0)
			return haystack;

		list = list->next;
		} // while (list)

	return NULL;
} // gchar *find_string_in_list_utf...

/**
 * @brief Find a existent string in a list.
 *
 * This is a switch between find_string_in_list_utf8case and
 * find_string_in_list_utf8nocase to search with or without case for the
 * existence of a string.
 *
 * @param list The list to search in
 * @param string The string to search for
 * @return The string or NULL
 *
 * @see find_string_in_list_utf8case
 * @see find_string_in_list_utf8nocase
 */
gchar *find_string_in_list(GList *list, const gchar *string)
{
	if (options->metadata.keywords_case_sensitive)
		return find_string_in_list_utf8case(list, string);
	else
		return find_string_in_list_utf8nocase(list, string);
}

#define KEYWORDS_SEPARATOR(c) ((c) == ',' || (c) == ';' || (c) == '\n' || (c) == '\r' || (c) == '\b')

GList *string_to_keywords_list(const gchar *text)
{
	GList *list = NULL;
	const gchar *ptr = text;

	while (*ptr != '\0')
		{
		const gchar *begin;
		gint l = 0;

		while (KEYWORDS_SEPARATOR(*ptr)) ptr++;
		begin = ptr;
		while (*ptr != '\0' && !KEYWORDS_SEPARATOR(*ptr))
			{
			ptr++;
			l++;
			}

		/* trim starting and ending whitespaces */
		while (l > 0 && g_ascii_isspace(*begin)) begin++, l--;
		while (l > 0 && g_ascii_isspace(begin[l-1])) l--;

		if (l > 0)
			{
			gchar *keyword = g_strndup(begin, l);

			/* only add if not already in the list */
			if (!find_string_in_list(list, keyword))
				list = g_list_append(list, keyword);
			else
				g_free(keyword);
			}
		}

	return list;
}

/*
 * keywords to marks
 */


gboolean meta_data_get_keyword_mark(FileData *fd, gint UNUSED(n), gpointer data)
{
	/** @FIXME do not use global keyword_tree */
	auto path = static_cast<GList *>(data);
	GList *keywords;
	gboolean found = FALSE;
	keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);
	if (keywords)
		{
		GtkTreeIter iter;
		if (keyword_tree_get_iter(GTK_TREE_MODEL(keyword_tree), &iter, path) &&
		    keyword_tree_is_set(GTK_TREE_MODEL(keyword_tree), &iter, keywords))
			found = TRUE;

		}
	return found;
}

gboolean meta_data_set_keyword_mark(FileData *fd, gint UNUSED(n), gboolean value, gpointer data)
{
	auto path = static_cast<GList *>(data);
	GList *keywords = NULL;
	GtkTreeIter iter;

	if (!keyword_tree_get_iter(GTK_TREE_MODEL(keyword_tree), &iter, path)) return FALSE;

	keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);

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

	string_list_free(keywords);
	return TRUE;
}



void meta_data_connect_mark_with_keyword(GtkTreeModel *keyword_tree, GtkTreeIter *kw_iter, gint mark)
{

	FileDataGetMarkFunc get_mark_func;
	FileDataSetMarkFunc set_mark_func;
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
			     keyword_compare(keyword_tree, &old_kw_iter, kw_iter) == 0)) /* or given keyword */
				{
				file_data_register_mark_func(i, NULL, NULL, NULL, NULL);
				gtk_tree_store_set(GTK_TREE_STORE(keyword_tree), &old_kw_iter, KEYWORD_COLUMN_MARK, "", -1);
				}
			}
		}


	if (mark >= 0 && mark < FILEDATA_MARKS_SIZE)
		{
		GList *path;
		gchar *mark_str;
		path = keyword_tree_get_path(keyword_tree, kw_iter);
		file_data_register_mark_func(mark, meta_data_get_keyword_mark, meta_data_set_keyword_mark, path, (GDestroyNotify)string_list_free);

		mark_str = g_strdup_printf("%d", (mark < 9 ? mark : -1) + 1);
		gtk_tree_store_set(GTK_TREE_STORE(keyword_tree), kw_iter, KEYWORD_COLUMN_MARK, mark_str, -1);
		g_free(mark_str);
		}
}


/*
 *-------------------------------------------------------------------
 * keyword tree
 *-------------------------------------------------------------------
 */



GtkTreeStore *keyword_tree;

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
	gchar *casefold = g_utf8_casefold(name, -1);
	gtk_tree_store_set(keyword_tree, iter, KEYWORD_COLUMN_MARK, "",
						KEYWORD_COLUMN_NAME, name,
						KEYWORD_COLUMN_CASEFOLD, casefold,
						KEYWORD_COLUMN_IS_KEYWORD, is_keyword, -1);
	g_free(casefold);
}

gboolean keyword_compare(GtkTreeModel *keyword_tree, GtkTreeIter *a, GtkTreeIter *b)
{
	GtkTreePath *pa = gtk_tree_model_get_path(keyword_tree, a);
	GtkTreePath *pb = gtk_tree_model_get_path(keyword_tree, b);
	gint ret = gtk_tree_path_compare(pa, pb);
	gtk_tree_path_free(pa);
	gtk_tree_path_free(pb);
	return ret;
}

gboolean keyword_same_parent(GtkTreeModel *keyword_tree, GtkTreeIter *a, GtkTreeIter *b)
{
	GtkTreeIter parent_a;
	GtkTreeIter parent_b;

	gboolean valid_pa = gtk_tree_model_iter_parent(keyword_tree, &parent_a, a);
	gboolean valid_pb = gtk_tree_model_iter_parent(keyword_tree, &parent_b, b);

	if (valid_pa && valid_pb)
		{
		return keyword_compare(keyword_tree, &parent_a, &parent_b) == 0;
		}
	else
		{
		return (!valid_pa && !valid_pb); /* both are toplevel */
		}
}

gboolean keyword_exists(GtkTreeModel *keyword_tree, GtkTreeIter *parent_ptr, GtkTreeIter *sibling, const gchar *name, gboolean exclude_sibling, GtkTreeIter *result)
{
	GtkTreeIter parent;
	GtkTreeIter iter;
	gboolean toplevel = FALSE;
	gboolean ret;
	gchar *casefold;

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

	if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(keyword_tree), &iter, toplevel ? NULL : &parent)) return FALSE;

	casefold = g_utf8_casefold(name, -1);
	ret = FALSE;

	while (TRUE)
		{
		if (!(exclude_sibling && sibling && keyword_compare(keyword_tree, &iter, sibling) == 0))
			{
			if (options->metadata.keywords_case_sensitive)
				{
				gchar *iter_name = keyword_get_name(keyword_tree, &iter);
				ret = strcmp(name, iter_name) == 0;
				g_free(iter_name);
				}
			else
				{
				gchar *iter_casefold = keyword_get_casefold(keyword_tree, &iter);
				ret = strcmp(casefold, iter_casefold) == 0;
				g_free(iter_casefold);
				} // if (options->metadata.tags_cas...
			}
		if (ret)
			{
			if (result) *result = iter;
			break;
			}
		if (!gtk_tree_model_iter_next(keyword_tree, &iter)) break;
		}
	g_free(casefold);
	return ret;
}


void keyword_copy(GtkTreeStore *keyword_tree, GtkTreeIter *to, GtkTreeIter *from)
{

	gchar *mark, *name, *casefold;
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
	g_free(mark);
	g_free(name);
	g_free(casefold);
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
	GList *path = NULL;
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
			gchar *name = keyword_get_name(keyword_tree, &iter);
			if (strcmp(name, static_cast<const gchar *>(path->data)) == 0) break;
			g_free(name);
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
			GList *work = casefold_list;
			gboolean found = FALSE;
			gchar *iter_casefold = keyword_get_casefold(keyword_tree, &iter);
			while (work)
				{
				auto casefold = static_cast<const gchar *>(work->data);
				work = work->next;

				if (strcmp(iter_casefold, casefold) == 0)
					{
					found = TRUE;
					break;
					}
				}
			g_free(iter_casefold);
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
			GList *work = kw_list;
			gboolean found = FALSE;
			gchar *iter_name = keyword_get_name(keyword_tree, &iter);
			while (work)
				{
				auto name = static_cast<const gchar *>(work->data);
				work = work->next;

				if (strcmp(iter_name, name) == 0)
					{
					found = TRUE;
					break;
					}
				}
			g_free(iter_name);
			if (!found) return FALSE;
			}

		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return TRUE;
		iter = parent;
		}
}

gboolean keyword_tree_is_set(GtkTreeModel *keyword_tree, GtkTreeIter *iter, GList *kw_list)
{
	gboolean ret;
	GList *casefold_list = NULL;
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

		string_list_free(casefold_list);
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
			gchar *name = keyword_get_name(keyword_tree, &iter);
			if (!find_string_in_list(*kw_list, name))
				{
				*kw_list = g_list_append(*kw_list, name);
				}
			else
				{
				g_free(name);
				}
			}

		if (!gtk_tree_model_iter_parent(keyword_tree, &parent, &iter)) return;
		iter = parent;
		}
}

GList *keyword_tree_get(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr)
{
	GtkTreeIter iter = *iter_ptr;
	GList *kw_list = NULL;

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
	gchar *found;
	gchar *name;
	if (!keyword_get_is_keyword(keyword_tree, iter)) return;

	name = keyword_get_name(keyword_tree, iter);
	found = find_string_in_list(*kw_list, name);

	if (found)
		{
		*kw_list = g_list_remove(*kw_list, found);
		g_free(found);
		}
	g_free(name);
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

static gboolean keyword_show_all_in_cb(GtkTreeModel *model, GtkTreePath *UNUSED(path), GtkTreeIter *iter, gpointer data)
{
	keyword_show_in(GTK_TREE_STORE(model), iter, data);
	return FALSE;
}

void keyword_show_all_in(GtkTreeStore *keyword_tree, gpointer id)
{
	gtk_tree_model_foreach(GTK_TREE_MODEL(keyword_tree), keyword_show_all_in_cb, id);
}

static gboolean keyword_revert_hidden_in_cb(GtkTreeModel *model, GtkTreePath *UNUSED(path), GtkTreeIter *iter, gpointer data)
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

static gboolean keyword_show_set_in_cb(GtkTreeModel *model, GtkTreePath *UNUSED(path), GtkTreeIter *iter_ptr, gpointer data)
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


void keyword_tree_new(void)
{
	if (keyword_tree) return;

	keyword_tree = gtk_tree_store_new(KEYWORD_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER);
}

static GtkTreeIter keyword_tree_default_append(GtkTreeStore *keyword_tree, GtkTreeIter *parent, const gchar *name, gboolean is_keyword)
{
	GtkTreeIter iter;
	gtk_tree_store_append(keyword_tree, &iter, parent);
	keyword_set(keyword_tree, &iter, name, is_keyword);
	return iter;
}

void keyword_tree_new_default(void)
{
	GtkTreeIter i1, i2;

	if (!keyword_tree) keyword_tree_new();

	i1 = keyword_tree_default_append(keyword_tree, NULL, _("People"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Family"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Free time"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Children"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Sport"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Culture"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Festival"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, NULL, _("Nature"), TRUE);
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
	i1 = keyword_tree_default_append(keyword_tree, NULL, _("Art"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Statue"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Painting"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Historic"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Modern"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, NULL, _("City"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Park"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Street"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Square"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, NULL, _("Architecture"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Buildings"), FALSE);
			keyword_tree_default_append(keyword_tree, &i2, _("House"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Cathedral"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Palace"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Castle"), TRUE);
			keyword_tree_default_append(keyword_tree, &i2, _("Bridge"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Interior"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Historic"), TRUE);
		i2 = keyword_tree_default_append(keyword_tree, &i1, _("Modern"), TRUE);
	i1 = keyword_tree_default_append(keyword_tree, NULL, _("Places"), FALSE);
	i1 = keyword_tree_default_append(keyword_tree, NULL, _("Conditions"), FALSE);
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
	i1 = keyword_tree_default_append(keyword_tree, NULL, _("Photo"), FALSE);
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
		gchar *name;
		gchar *mark_str;

		WRITE_NL(); WRITE_STRING("<keyword ");
		name = keyword_get_name(keyword_tree, &iter);
		write_char_option(outstr, indent, "name", name);
		g_free(name);
		write_bool_option(outstr, indent, "kw", keyword_get_is_keyword(keyword_tree, &iter));
		mark_str = keyword_get_mark(keyword_tree, &iter);
		if (mark_str && mark_str[0])
			{
			write_char_option(outstr, indent, "mark", mark_str);
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

 void keyword_tree_node_disconnect_marks(GtkTreeModel *keyword_tree, GtkTreeIter *iter_ptr)
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
	gchar *name = NULL;
	gboolean is_kw = TRUE;
	gchar *mark_str = NULL;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("name", name)) continue;
		if (READ_BOOL_FULL("kw", is_kw)) continue;
		if (READ_CHAR_FULL("mark", mark_str)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}
	if (name && name[0])
		{
		GtkTreeIter iter;
		/* re-use existing keyword if any */
		if (!keyword_exists(GTK_TREE_MODEL(keyword_tree), parent, NULL, name, FALSE, &iter))
			{
			gtk_tree_store_append(keyword_tree, &iter, parent);
			}
		keyword_set(keyword_tree, &iter, name, is_kw);

		if (mark_str)
			{
			gint i = (gint)atoi(mark_str);
			if (i == 0) i = 10;

			meta_data_connect_mark_with_keyword(GTK_TREE_MODEL(keyword_tree),
											&iter, i - 1);
			}

		g_free(name);
		return gtk_tree_iter_copy(&iter);
		}
	g_free(name);
	return NULL;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
