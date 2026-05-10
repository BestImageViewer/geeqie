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

#include <config.h>

#include "main-defines.h"
#include "md5-util.h"
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
 * Date=[<value in time_t format, or -1 if no embedded date>] \n
 * MD5sum=[<32 character ascii text digest>] \n
 * SimilarityGrid[32 x 32]=<3072 bytes of data (1024 pixels in RGB format, 1 pixel is 24bits)>
 *
 * The first line (9 bytes) indicates it is a SIMcache format file. (new line char must exist) \n
 * Comment lines starting with a # are ignored up to a new line. \n
 * All data lines should end with a new line char. \n
 * Format is very strict, data must begin with the char immediately following '='. \n
 * Currently SimilarityGrid is always assumed to be 32 x 32 RGB. \n
 */

namespace
{

struct CachePathParts
{
	CachePathParts(CacheType cache_type)
	{
		if (cache_type == CACHE_TYPE_METADATA || cache_type == CACHE_TYPE_XMP_METADATA)
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
			case CACHE_TYPE_THUMB:
				ext = GQ_CACHE_EXT_THUMB;
				break;
			case CACHE_TYPE_SIM:
				ext = GQ_CACHE_EXT_SIM;
				break;
			case CACHE_TYPE_METADATA:
				ext = GQ_CACHE_EXT_METADATA;
				break;
			case CACHE_TYPE_XMP_METADATA:
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

CacheData *cache_sim_data_new()
{
	auto *cd = new CacheData();
	cd->date = -1;

	return cd;
}

CacheData *cache_sim_data_new(const gchar *path)
{
	std::unique_ptr<CacheData> cd(cache_sim_data_new());

	if (!cd->load(path)) return nullptr;

	return cd.release();
}

void cache_sim_data_free(CacheData *cd)
{
	delete cd;
}

/*
 *-------------------------------------------------------------------
 * sim cache write
 *-------------------------------------------------------------------
 */

bool CacheData::write_dimensions(GString *gstring) const
{
	if (!have_dimensions) return false;

	g_string_append_printf(gstring, "Dimensions=[%d x %d]\n", dimensions.width, dimensions.height);

	return true;
}

bool CacheData::write_date(GString *gstring) const
{
	if (!have_date) return false;

	g_string_append_printf(gstring, "Date=[%ld]\n", date);

	return true;
}

bool CacheData::write_md5sum(GString *gstring) const
{
	if (!have_md5sum) return false;

	g_autofree gchar *text = md5_digest_to_text(md5sum);

	g_string_append_printf(gstring, "MD5sum=[%s]\n", text);

	return true;
}

bool CacheData::write_similarity(GString *gstring) const
{
	if (!have_similarity || !sim || !sim->filled) return false;

	g_string_append(gstring, "SimilarityGrid[32 x 32]=");

	guint8 buf[3 * 32];
	for (guint y = 0; y < 32; y++)
		{
		guint s = y * 32;
		guint n = 0;

		for (guint x = 0; x < 32; x++)
			{
			buf[n++] = sim->avg_r[s + x];
			buf[n++] = sim->avg_g[s + x];
			buf[n++] = sim->avg_b[s + x];
			}

		g_string_append_len(gstring, (const gchar *)buf, sizeof(buf));
		}

	g_string_append(gstring, "\n");

	return true;
}

bool CacheData::save(const gchar *path) const
{
	if (!path) return false;

	g_autofree gchar *pathl = path_from_utf8(path);

	g_autoptr(GString) gstring = g_string_new("SIMcache\n#" PACKAGE " " VERSION "\n");

	write_dimensions(gstring);
	write_date(gstring);
	write_md5sum(gstring);
	write_similarity(gstring);

	secure_save(pathl, gstring->str, gstring->len);

	return true;
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

	if (s < 10 || strncmp("Dimensions", buffer, 10) != 0) return false;

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

	if (s < 4 || strncmp("Date", buffer, 4) != 0) return false;

	gchar buf[1024];
	if (!cache_sim_read_buf(f, s, buf, sizeof(buf))) return false;

	date = strtol(buf, nullptr, 10);
	have_date = TRUE;

	return true;
}

bool CacheData::read_md5sum(FILE *f, const gchar *buffer, gint s)
{
	if (!f || !buffer) return false;

	if (s < 8 || strncmp("MD5sum", buffer, 6) != 0) return false;

	gchar buf[64];
	if (!cache_sim_read_buf(f, s, buf, sizeof(buf))) return false;

	have_md5sum = md5_digest_from_text(buf, md5sum);

	return true;
}

bool CacheData::read_similarity(FILE *f, const gchar *buffer, gint s)
{
	if (!f || !buffer) return false;

	if (s < 11 || strncmp("Similarity", buffer, 10) != 0) return false;

	if (strncmp("Grid[32 x 32]", buffer + 10, 13) != 0) return false;

	if (fseek(f, - s, SEEK_CUR) != 0) return false;

	gchar b = 'X';
	while (b != '=')
		{
		if (fread(&b, sizeof(b), 1, f) != 1) return false;
		}

	guint8 pixel_buf[3];
	std::unique_ptr<ImageSimilarityData> sd;

	if (sim)
		{
		/* use current sim that may already contain data we will not touch here */
		sd.swap(sim);
		have_similarity = FALSE;
		}
	else
		{
		sd.reset(image_sim_new());
		}

	for (gint y = 0; y < 32; y++)
		{
		gint s = y * 32;
		for (gint x = 0; x < 32; x++)
			{
			if (fread(&pixel_buf, sizeof(pixel_buf), 1, f) != 1) return false;

			sd->avg_r[s + x] = pixel_buf[0];
			sd->avg_g[s + x] = pixel_buf[1];
			sd->avg_b[s + x] = pixel_buf[2];
			}
		}

	if (fread(&b, sizeof(b), 1, f) == 1)
		{
		if (b != '\n') fseek(f, -1, SEEK_CUR);
		}

	sd->filled = TRUE;

	sim.swap(sd);
	have_similarity = TRUE;

	return true;
}

bool CacheData::load(const gchar *path)
{
	if (!path) return false;

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
		    !read_similarity(f, buf, s))
			{
			if (!cache_sim_read_skipline(f, s)) break;

			success--;
			}
		else
			{
			success = CACHE_LOAD_LINE_NOISE;
			}
		}

	return have_dimensions
	    || have_date
	    || have_md5sum
	    || have_similarity;
}

/*
 *-------------------------------------------------------------------
 * sim cache setting
 *-------------------------------------------------------------------
 */

void CacheData::set_dimensions(GqSize dimensions)
{
	this->dimensions = dimensions;
	have_dimensions = TRUE;
}

void CacheData::set_md5sum(const Md5Digest &digest)
{
	md5sum = digest;
	have_md5sum = TRUE;
}

void CacheData::set_similarity(const ImageSimilarityData &sd)
{
	if (!sd.filled) return;

	if (!sim) sim.reset(image_sim_new());

	*sim = sd;
	have_similarity = TRUE;
}

/*
 *-------------------------------------------------------------------
 * cache path location utils
 *-------------------------------------------------------------------
 */

gchar *cache_create_location(CacheType cache_type, const gchar *source)
{
	mode_t mode = 0755;
	g_autofree gchar *path = cache_get_location(cache_type, source, FALSE, &mode);

	if (!recursive_mkdir_if_not_exists(path, mode))
		{
		log_printf("Failed to create cache dir %s\n", path);
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

const gchar *get_thumbnails_cache_dir()
{
#if USE_XDG
	static gchar *thumbnails_cache_dir = g_build_filename(xdg_cache_home_get(), GQ_APPNAME_LC, GQ_CACHE_THUMB, NULL);
#else
	static gchar *thumbnails_cache_dir = g_build_filename(get_rc_dir(), GQ_CACHE_THUMB, NULL);
#endif

	return thumbnails_cache_dir;
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
