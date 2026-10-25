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

#include "cache.h"

#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include <config.h>
#include <glib/gstdio.h>

#include "main-defines.h"
#include "md5-util.h"
#include "misc.h"
#include "options.h"
#include "similar.h"
#include "thumb-standard.h"
#include "ui-fileops.h"


/**
 * @file
 *-------------------------------------------------------------------
 * Cache data file format:
 *-------------------------------------------------------------------
 *
 * SIMcache \n
 * #comment \n
 * Dimensions=[<width> x <height>] \n
 * DateTimeOriginal=[<value in time_t format, or -1 if no original date>] \n
 * MD5sum=[<32 character ascii text digest>] \n
 * SimilarityGrid[32 x 32]=<3072 bytes of data (1024 pixels in RGB format, 1 pixel is 24bits)>
 * PerceptualHashV1=[<eight 16-digit hexadecimal hashes separated by spaces>] \n
 *
 * The first line (9 bytes) indicates it is a SIMcache format file. (new line char must exist) \n
 * Comment lines starting with a # are ignored up to a new line. \n
 * All data lines should end with a new line char. \n
 * Format is very strict, data must begin with the char immediately following '='. \n
 * Currently SimilarityGrid is always assumed to be 32 x 32 RGB. \n
 * Legacy Date fields from Exif.Image.DateTime are ignored and omitted when saving. \n
 */

namespace
{

struct CachePathParts
{
	CachePathParts(CacheType cache_type)
	{
		if (cache_type == CacheType::METADATA || cache_type == CacheType::XMP_METADATA)
			{
			rc = get_metadata_cache_dir();
			local = GQ_CACHE_LOCAL_METADATA;
			use_local_dir = options->metadata.enable_metadata_dirs;
			}
		else
			{
			rc = get_thumbnails_cache_dir();
			local = GQ_CACHE_LOCAL_THUMB;
			use_local_dir = options->thumbnails.cache_into_dirs;
			}

		switch (cache_type)
			{
			case CacheType::THUMB:
				ext = GQ_CACHE_EXT_THUMB;
				break;
			case CacheType::SIM:
				ext = GQ_CACHE_EXT_SIM;
				break;
			case CacheType::METADATA:
				ext = GQ_CACHE_EXT_METADATA;
				break;
			case CacheType::XMP_METADATA:
				ext = GQ_CACHE_EXT_XMP_METADATA;
				break;
			}
	}

	gchar *build_path_local(const gchar *source) const
	{
		g_autofree gchar *base = remove_level_from_path(source);
		g_autofree gchar *name = g_strconcat(filename_from_path(source), ext, nullptr);

		return g_build_filename(base, local, name, nullptr);
	}

	gchar *build_path_rc(const gchar *source) const
	{
		g_autofree gchar *name = g_strconcat(source, ext, nullptr);

		return g_build_filename(rc, name, nullptr);
	}

	const gchar *rc = nullptr;
	const gchar *local = nullptr;
	const gchar *ext = nullptr;
	gboolean use_local_dir = FALSE;
};

bool path_is_in_thumbnail_cache(const gchar *path)
{
	for (const gchar *root : {get_thumbnails_cache_dir(), get_default_thumbnails_cache_dir(), get_thumbnails_standard_cache_dir()})
		{
		g_autofree gchar *canonical_root = g_canonicalize_filename(root, nullptr);
		g_autofree gchar *root_path = path_from_utf8(canonical_root);
		g_autofree gchar *resolved_root = realpath(root_path, nullptr);
		g_autofree gchar *resolved_root_utf8 = resolved_root ? path_to_utf8(resolved_root) : nullptr;
		for (const gchar *cache_root : {canonical_root, resolved_root_utf8})
			{
			if (!cache_root) continue;
			const gsize length = strlen(cache_root);
			if (g_str_has_prefix(path, cache_root) && (path[length] == G_DIR_SEPARATOR || path[length] == '\0')) return true;
			}
		}

	g_auto(GStrv) components = g_strsplit(path, G_DIR_SEPARATOR_S, -1);
	for (gchar **component = components; *component; ++component)
		{
		if (g_strcmp0(*component, GQ_CACHE_LOCAL_THUMB) == 0 ||
		    g_strcmp0(*component, THUMB_FOLDER_LOCAL) == 0) return true;
		}
	return false;
}

constexpr gint CACHE_LOAD_LINE_NOISE = 8;

gchar *cache_get_location(CacheType type, const gchar *source, gint include_name, mode_t *mode)
{
	if (!source) return nullptr;

	g_autofree gchar *base = remove_level_from_path(source);

	const CachePathParts cache{type};

	g_autofree gchar *name = nullptr;
	if (include_name)
		{
		name = g_strconcat(filename_from_path(source), cache.ext, NULL);
		}

	gchar *path = nullptr;

	if (cache.use_local_dir && access_file(base, W_OK))
		{
		path = g_build_filename(base, cache.local, name, NULL);
		if (mode) *mode = 0775;
		}

	if (!path)
		{
		path = g_build_filename(cache.rc, base, name, NULL);
		if (mode) *mode = 0755;
		}

	return path;
}

} // namespace

/*
 *-------------------------------------------------------------------
 * sim cache data
 *-------------------------------------------------------------------
 */

CacheData::CacheData(const gchar *path)
	: CacheData()
{
	if (path) load(path);
}

/*
 *-------------------------------------------------------------------
 * sim cache write
 *-------------------------------------------------------------------
 */

bool CacheData::write_dimensions(GString *gstring) const
{
	if (!dimensions) return false;

	g_string_append_printf(gstring, "Dimensions=[%d x %d]\n", dimensions->width, dimensions->height); // NOTRANSLATE: Cache-file format token.

	return true;
}

bool CacheData::write_date(GString *gstring) const
{
	if (!date) return false;

	g_string_append_printf(gstring, "DateTimeOriginal=[%lld]\n", static_cast<long long int>(*date)); // NOTRANSLATE: Cache-file format token.

	return true;
}

bool CacheData::write_md5sum(GString *gstring) const
{
	if (!md5sum) return false;

	g_string_append_printf(gstring, "MD5sum=[%s]\n", md5_digest_to_text(*md5sum).c_str());

	return true;
}

bool CacheData::write_similarity(GString *gstring) const
{
	if (!image_sim_filled(similarity.get())) return false;

	g_string_append(gstring, "SimilarityGrid[32 x 32]="); // NOTRANSLATE: Cache-file format token.

	similarity->to_string(gstring);

	g_string_append(gstring, "\n");

	return true;
}

bool CacheData::write_perceptual_hashes(GString *gstring) const
{
	if (!image_sim_filled(similarity.get())) return false;

	auto hashes = similarity->perceptual_hashes;
	if (!hashes && options->alternate_similarity_algorithm.enabled)
		{
		ImageSimilarityData sim = *similarity;
		sim.ensure_perceptual_hashes();
		hashes = sim.perceptual_hashes;
		}
	if (!hashes) return false;

	g_string_append(gstring, "PerceptualHashV1=["); // NOTRANSLATE: Cache-file format token.
	for (guint i = 0; i < hashes->size(); i++)
		{
		g_string_append_printf(gstring, "%s%016" G_GINT64_MODIFIER "x", i == 0 ? "" : " ", (*hashes)[i]);
		}
	g_string_append(gstring, "]\n");
	return true;
}

void CacheData::save(const gchar *source) const
{
	g_autofree gchar *base = cache_create_location(CacheType::SIM, source);
	if (!base) return;

	g_autofree gchar *path = cache_get_location(CacheType::SIM, source);
	if (!path) return;

	g_autofree gchar *pathl = path_from_utf8(path);

	g_autoptr(GString) gstring = g_string_new("SIMcache\n#" PACKAGE " " VERSION "\n");

	write_dimensions(gstring);
	write_date(gstring);
	write_md5sum(gstring);
	write_similarity(gstring);
	write_perceptual_hashes(gstring);

	secure_save(pathl, gstring->str, gstring->len);

	filetime_set(path, filetime(source));
}

/*
 *-------------------------------------------------------------------
 * sim cache read
 *-------------------------------------------------------------------
 */

static gboolean cache_sim_read_skipline(FILE *f, gint s)
{
	if (!f) return FALSE;

	if (fseek(f, 0 - s, SEEK_CUR) != 0) return FALSE;

	gchar b;
	while (fread(&b, sizeof(b), 1, f) == 1)
		{
		if (b == '\n') break;
		}

	return TRUE;
}

static gboolean cache_sim_read_comment(FILE *f, const gchar *buf, gint s)
{
	if (!f || !buf) return FALSE;

	if (s < 1 || buf[0] != '#') return FALSE;

	return cache_sim_read_skipline(f, s - 1);
}

static bool cache_sim_read_buf(FILE *f, gint s, gchar *buf, gsize buf_size)
{
	if (fseek(f, - s, SEEK_CUR) != 0) return false;

	gchar b = 'X';
	while (b != '[')
		{
		if (fread(&b, sizeof(b), 1, f) != 1) return false;
		}

	gsize p = 0;
	while (b != ']' && p < buf_size - 1)
		{
		if (fread(&b, sizeof(b), 1, f) != 1) return false;
		buf[p] = b;
		p++;
		}

	while (b != '\n')
		{
		if (fread(&b, sizeof(b), 1, f) != 1) break;
		}

	buf[p] = '\0';

	return true;
}

bool CacheData::read_dimensions(FILE *f, const gchar *buffer, gint s)
{
	if (!f || !buffer) return false;

	if (s < 10 || strncmp("Dimensions", buffer, 10) != 0) return false; // NOTRANSLATE: Cache-file format token.

	gchar buf[1024];
	if (!cache_sim_read_buf(f, s, buf, sizeof(buf))) return false;

	GqSize dimensions;
	if (sscanf(buf, "%d x %d", &dimensions.width, &dimensions.height) != 2) return false;

	set_dimensions(dimensions);

	return true;
}

bool CacheData::read_date(FILE *f, const gchar *buffer, gint s)
{
	if (!f || !buffer) return false;

	if (s < 18 || strncmp("DateTimeOriginal=[", buffer, 18) != 0) return false; // NOTRANSLATE: Cache-file format token.

	gchar buf[1024];
	if (!cache_sim_read_buf(f, s, buf, sizeof(buf))) return false;

	date = strtol(buf, nullptr, 10);

	return true;
}

bool CacheData::read_md5sum(FILE *f, const gchar *buffer, gint s)
{
	if (!f || !buffer) return false;

	if (s < 8 || strncmp("MD5sum", buffer, 6) != 0) return false;

	gchar buf[64];
	if (!cache_sim_read_buf(f, s, buf, sizeof(buf))) return false;

	if (Md5Digest digest; md5_digest_from_text(buf, digest)) set_md5sum(digest);

	return true;
}

bool CacheData::read_similarity(FILE *f, const gchar *buffer, gint s)
{
	if (!f || !buffer) return false;

	if (s < 11 || strncmp("Similarity", buffer, 10) != 0) return false; // NOTRANSLATE: Cache-file format token.

	if (strncmp("Grid[32 x 32]", buffer + 10, 13) != 0) return false; // NOTRANSLATE: Cache-file format token.

	if (fseek(f, - s, SEEK_CUR) != 0) return false;

	gchar b = 'X';
	while (b != '=')
		{
		if (fread(&b, sizeof(b), 1, f) != 1) return false;
		}

	std::unique_ptr<ImageSimilarityData> sd = nullptr;

	if (similarity)
		{
		/* use current sim that may already contain data we will not touch here */
		sd.swap(similarity);
		}
	else
		{
		sd = std::make_unique<ImageSimilarityData>();
		}

	if (!sd->fill_data(f)) return false;

	if (fread(&b, sizeof(b), 1, f) == 1)
		{
		if (b != '\n') fseek(f, -1, SEEK_CUR);
		}

	similarity.swap(sd);

	return true;
}

bool CacheData::read_perceptual_hashes(FILE *f, const gchar *buffer, gint s)
{
	constexpr gchar prefix[] = "PerceptualHashV1=["; // NOTRANSLATE: Cache-file format token.
	if (!f || !buffer || s < static_cast<gint>(sizeof(prefix) - 1) ||
	    strncmp(prefix, buffer, sizeof(prefix) - 1) != 0) return false;

	// Read only this text line so a malformed optional field cannot consume the grid.
	if (fseek(f, -s, SEEK_CUR) != 0) return false;
	gchar buf[256];
	if (!fgets(buf, sizeof(buf), f)) return true;
	if (!strchr(buf, '\n'))
		{
		gint c;
		while ((c = fgetc(f)) != EOF && c != '\n')
			{
			}
		}

	ImageSimilarityData::PerceptualHashes hashes{};
	const gchar *p = buf + sizeof(prefix) - 1;
	for (guint i = 0; i < hashes.size(); i++)
		{
		// Ignore invalid optional hashes after consuming their line.
		// Require exactly eight fixed-width values, with no partial hashes accepted.
		for (guint digit = 0; digit < 16; digit++)
			{
			if (!g_ascii_isxdigit(*p)) return true;
			hashes[i] = (hashes[i] << 4) | g_ascii_xdigit_value(*p++);
			}
		if (hashes[i] & 1) return true; // The unused DC bit must be clear.
		if (*p++ != (i + 1 == hashes.size() ? ']' : ' ')) return true;
		}
	if (*p != '\0' && (*p != '\n' || p[1] != '\0')) return true;

	if (!similarity) similarity = std::make_unique<ImageSimilarityData>();
	similarity->perceptual_hashes = hashes;
	return true;
}

bool CacheData::load(const gchar *source)
{
	g_autofree gchar *path = cache_find_location(CacheType::SIM, source);
	if (!path) return false;

	if (filetime(path) != filetime(source)) return false;

	g_autofree gchar *pathl = path_from_utf8(path);
	g_autoptr(FILE) f = fopen(pathl, "r");
	if (!f) return false;

	gchar buf[32];
	if (fread(&buf, sizeof(gchar), 9, f) != 9 ||
	    strncmp(buf, "SIMcache", 8) != 0)
		{
		DEBUG_1("%s is not a cache file", path);
		return false;
		}

	gint success = CACHE_LOAD_LINE_NOISE;
	while (success > 0)
		{
		const gint s = fread(&buf, sizeof(gchar), sizeof(buf), f);
		if (s < 1) break;

		if (!cache_sim_read_comment(f, buf, s) &&
		    !read_dimensions(f, buf, s) &&
		    !read_date(f, buf, s) &&
		    !read_md5sum(f, buf, s) &&
		    !read_similarity(f, buf, s) &&
		    !read_perceptual_hashes(f, buf, s))
			{
			if (!cache_sim_read_skipline(f, s)) break;

			success--;
			}
		else
			{
			success = CACHE_LOAD_LINE_NOISE;
			}
		}

	if (!image_sim_filled(similarity.get())) similarity.reset();

	return dimensions
	    || date
	    || md5sum
	    || similarity;
}

/*
 *-------------------------------------------------------------------
 * sim cache setting
 *-------------------------------------------------------------------
 */

void CacheData::set_dimensions(GqSize dimensions)
{
	this->dimensions = dimensions;
}

void CacheData::set_md5sum(const Md5Digest &digest)
{
	md5sum = digest;
}

void CacheData::set_similarity(const ImageSimilarityData &sd)
{
	if (!sd.filled) return;

	similarity = std::make_unique<ImageSimilarityData>(sd);
}

/**
 * @brief Identify source paths inside global or local thumbnail caches.
 * Checks directory boundaries and resolves existing symbolic links.
 */
bool cache_source_is_thumbnail(const gchar *source)
{
	if (!source) return false;
	g_autofree gchar *canonical = g_canonicalize_filename(source, nullptr);
	if (path_is_in_thumbnail_cache(canonical)) return true;

	g_autofree gchar *source_path = path_from_utf8(source);
	g_autofree gchar *resolved = realpath(source_path, nullptr);
	if (!resolved) return false;
	g_autofree gchar *resolved_utf8 = path_to_utf8(resolved);
	return path_is_in_thumbnail_cache(resolved_utf8);
}

/*
 *-------------------------------------------------------------------
 * cache path location utils
 *-------------------------------------------------------------------
 */

gchar *cache_create_location(CacheType cache_type, const gchar *source)
{
	if ((cache_type == CacheType::THUMB || cache_type == CacheType::SIM) && cache_source_is_thumbnail(source)) return nullptr;

	mode_t mode = 0755;
	g_autofree gchar *path = cache_get_location(cache_type, source, FALSE, &mode);
	if (!path) return nullptr;
	const gchar *root = get_thumbnails_cache_dir();
	if ((cache_type == CacheType::THUMB || cache_type == CacheType::SIM) &&
	    g_str_has_prefix(path, root) && !cache_prepare_thumbnail_directory(root, true))
		{
		log_printf("Refusing to use an unrecognized thumbnail cache directory: %s\n", root); // NOTRANSLATE: Diagnostic message.
		return nullptr;
		}

	if (!recursive_mkdir_if_not_exists(path, mode))
		{
		log_printf("Failed to create cache dir %s\n", path); // NOTRANSLATE: Diagnostic message.
		return nullptr;
		}

	return g_steal_pointer(&path);
}

gchar *cache_get_location(CacheType cache_type, const gchar *source)
{
	return cache_get_location(cache_type, source, TRUE, nullptr);
}

gchar *cache_find_location(CacheType type, const gchar *source)
{
	gchar *path;

	if (!source) return nullptr;

	const CachePathParts cache{type};

	if (cache.use_local_dir)
		{
		path = cache.build_path_local(source);
		}
	else
		{
		path = cache.build_path_rc(source);
		}

	if (!isfile(path))
		{
		g_free(path);

		/* try the opposite method if not found */
		if (!cache.use_local_dir)
			{
			path = cache.build_path_local(source);
			}
		else
			{
			path = cache.build_path_rc(source);
			}

		if (!isfile(path))
			{
			g_free(path);
			path = nullptr;
			}
		}

	return path;
}

void cache_remove_thumbnails(const gchar *source)
{
	if (!source) return;
	const CachePathParts cache{CacheType::THUMB};
	g_autofree gchar *local = cache.build_path_local(source);
	g_autofree gchar *shared = cache.build_path_rc(source);
	for (const gchar *path : {local, shared})
		{
		if (isfile(path) && !unlink_file(path))
			{
			log_printf("Failed to remove thumbnail cache file: %s\n", path); // NOTRANSLATE: Diagnostic message.
			}
		}
	thumb_std_maint_removed(source);
}

gboolean cache_time_valid(const gchar *cache, const gchar *path)
{
	struct stat cache_st;
	struct stat path_st;
	gboolean ret = FALSE;

	if (!cache || !path) return FALSE;

	g_autofree gchar *cachel = path_from_utf8(cache);
	g_autofree gchar *pathl = path_from_utf8(path);

	if (stat(cachel, &cache_st) == 0 &&
	    stat(pathl, &path_st) == 0)
		{
		if (cache_st.st_mtime == path_st.st_mtime)
			{
			ret = TRUE;
			}
		else if (cache_st.st_mtime > path_st.st_mtime)
			{
			struct utimbuf ut;

			ut.actime = ut.modtime = cache_st.st_mtime;
			if (utime(cachel, &ut) < 0 &&
			    errno == EPERM)
				{
				DEBUG_1("cache permission workaround: %s", cachel);
				ret = TRUE;
				}
			}
		}

	return ret;
}

const gchar *get_default_thumbnails_cache_dir()
{
#if USE_XDG
	static gchar *thumbnails_cache_dir = g_build_filename(xdg_cache_home_get(), GQ_APPNAME_LC, GQ_CACHE_THUMB, NULL);
#else
	static gchar *thumbnails_cache_dir = g_build_filename(get_rc_dir(), GQ_CACHE_THUMB, NULL);
#endif

	return thumbnails_cache_dir;
}

const gchar *get_thumbnails_cache_dir()
{
	if (options && options->thumbnails.cache_dir && *options->thumbnails.cache_dir)
		{
		g_autofree gchar *expanded = expand_tilde(options->thumbnails.cache_dir);
		g_autofree gchar *canonical = g_canonicalize_filename(expanded, nullptr);
		// Cache maintenance may keep a borrowed path while Preferences changes it.
		g_autofree gchar *root = g_build_filename(canonical, "geeqie-cache", nullptr);
		return g_intern_string(root);
		}
	return get_default_thumbnails_cache_dir();
}

namespace {

constexpr const gchar *thumbnail_cache_marker = ".geeqie-cache";
constexpr const gchar *thumbnail_cache_marker_contents = "GEEQIE_CACHE_V1\n";

} // namespace

bool cache_is_thumbnail_directory_marker(const gchar *root, const gchar *path)
{
	g_autofree gchar *marker = g_build_filename(root, thumbnail_cache_marker, nullptr);
	return g_strcmp0(marker, path) == 0;
}

/** @brief Verify ownership of a custom cache directory, optionally creating it. */
bool cache_prepare_thumbnail_directory(const gchar *root, bool create)
{
	if (g_strcmp0(root, get_default_thumbnails_cache_dir()) == 0) return true;
	if (g_file_test(root, G_FILE_TEST_IS_SYMLINK)) return false;
	g_autofree gchar *marker = g_build_filename(root, thumbnail_cache_marker, nullptr);
	g_autofree gchar *contents = nullptr;
	g_autofree gchar *marker_path = path_from_utf8(marker);
	if (g_file_test(marker_path, G_FILE_TEST_IS_SYMLINK)) return false;
	if (g_file_get_contents(marker_path, &contents, nullptr, nullptr))
		{
		return g_strcmp0(contents, thumbnail_cache_marker_contents) == 0;
		}
	if (!create) return false;

	g_autofree gchar *root_path = path_from_utf8(root);
	if (g_mkdir_with_parents(root_path, 0755) != 0) return false;
	g_autoptr(GDir) directory = g_dir_open(root_path, 0, nullptr);
	if (!directory || g_dir_read_name(directory)) return false;
	return g_file_set_contents(marker_path, thumbnail_cache_marker_contents, -1, nullptr);
}

const gchar *get_thumbnails_standard_cache_dir()
{
	static gchar *thumbnails_standard_cache_dir = g_build_filename(xdg_cache_home_get(),
	                                                               THUMB_FOLDER_GLOBAL, NULL);

	return thumbnails_standard_cache_dir;
}

const gchar *get_metadata_cache_dir()
{
#if USE_XDG
	/* Metadata go to $XDG_DATA_HOME.
	 * "Keywords and comments, among other things, are irreplaceable and cannot be auto-generated,
	 * so I don't think they'd be appropriate for the cache directory." -- Omari Stephens on geeqie-devel ml
	 */
	static gchar *metadata_cache_dir = g_build_filename(xdg_data_home_get(), GQ_APPNAME_LC, GQ_CACHE_METADATA, NULL);
#else
	static gchar *metadata_cache_dir = g_build_filename(get_rc_dir(), GQ_CACHE_METADATA, NULL);
#endif

	return metadata_cache_dir;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
