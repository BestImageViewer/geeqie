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

#include "main.h"
#include "cache.h"

#include "md5-util.h"
#include "secure-save.h"
#include "thumb-standard.h"
#include "ui-fileops.h"

#include <utime.h>


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


/*
 *-------------------------------------------------------------------
 * sim cache data
 *-------------------------------------------------------------------
 */

CacheData *cache_sim_data_new()
{
	CacheData *cd;

	cd = g_new0(CacheData, 1);
	cd->date = -1;

	return cd;
}

void cache_sim_data_free(CacheData *cd)
{
	if (!cd) return;

	g_free(cd->path);
	image_sim_free(cd->sim);
	g_free(cd);
}

/*
 *-------------------------------------------------------------------
 * sim cache write
 *-------------------------------------------------------------------
 */

static gboolean cache_sim_write_dimensions(SecureSaveInfo *ssi, CacheData *cd)
{
	if (!cd || !cd->dimensions) return FALSE;

	secure_fprintf(ssi, "Dimensions=[%d x %d]\n", cd->width, cd->height);

	return TRUE;
}

static gboolean cache_sim_write_date(SecureSaveInfo *ssi, CacheData *cd)
{
	if (!cd || !cd->have_date) return FALSE;

	secure_fprintf(ssi, "Date=[%ld]\n", cd->date);

	return TRUE;
}

static gboolean cache_sim_write_md5sum(SecureSaveInfo *ssi, CacheData *cd)
{
	gchar *text;

	if (!cd || !cd->have_md5sum) return FALSE;

	text = md5_digest_to_text(cd->md5sum);
	secure_fprintf(ssi, "MD5sum=[%s]\n", text);
	g_free(text);

	return TRUE;
}

static gboolean cache_sim_write_similarity(SecureSaveInfo *ssi, CacheData *cd)
{
	guint x, y;
	guint8 buf[3 * 32];

	if (!cd || !cd->similarity || !cd->sim || !cd->sim->filled) return FALSE;

	secure_fprintf(ssi, "SimilarityGrid[32 x 32]=");
	for (y = 0; y < 32; y++)
		{
		guint s = y * 32;
		guint8 *avg_r = &cd->sim->avg_r[s];
		guint8 *avg_g = &cd->sim->avg_g[s];
		guint8 *avg_b = &cd->sim->avg_b[s];
		guint n = 0;

		for (x = 0; x < 32; x++)
			{
			buf[n++] = avg_r[x];
			buf[n++] = avg_g[x];
			buf[n++] = avg_b[x];
			}

		secure_fwrite(buf, sizeof(buf), 1, ssi);
		}

	secure_fputc(ssi, '\n');

	return TRUE;
}

gboolean cache_sim_data_save(CacheData *cd)
{
	SecureSaveInfo *ssi;
	gchar *pathl;

	if (!cd || !cd->path) return FALSE;

	pathl = path_from_utf8(cd->path);
	ssi = secure_open(pathl);
	g_free(pathl);

	if (!ssi)
		{
		log_printf("Unable to save sim cache data: %s\n", cd->path);
		return FALSE;
		}

	secure_fprintf(ssi, "SIMcache\n#%s %s\n", PACKAGE, VERSION);
	cache_sim_write_dimensions(ssi, cd);
	cache_sim_write_date(ssi, cd);
	cache_sim_write_md5sum(ssi, cd);
	cache_sim_write_similarity(ssi, cd);

	if (secure_close(ssi))
		{
		log_printf(_("error saving sim cache data: %s\nerror: %s\n"), cd->path,
			    secsave_strerror(secsave_errno));
		return FALSE;
		}

	return TRUE;
}

/*
 *-------------------------------------------------------------------
 * sim cache read
 *-------------------------------------------------------------------
 */

static gboolean cache_sim_read_skipline(FILE *f, gint s)
{
	if (!f) return FALSE;

	if (fseek(f, 0 - s, SEEK_CUR) == 0)
		{
		gchar b;
		while (fread(&b, sizeof(b), 1, f) == 1)
			{
			if (b == '\n') return TRUE;
			}
		return TRUE;
		}

	return FALSE;
}

static gboolean cache_sim_read_comment(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 1 || buf[0] != '#') return FALSE;

	return cache_sim_read_skipline(f, s - 1);
}

static gboolean cache_sim_read_dimensions(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 10 || strncmp("Dimensions", buf, 10) != 0) return FALSE;

	if (fseek(f, - s, SEEK_CUR) == 0)
		{
		gchar b;
		gchar buf[1024];
		gsize p = 0;
		gint w, h;

		b = 'X';
		while (b != '[')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			}
		while (b != ']' && p < sizeof(buf) - 1)
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			buf[p] = b;
			p++;
			}

		while (b != '\n')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) break;
			}

		buf[p] = '\0';
		if (sscanf(buf, "%d x %d", &w, &h) != 2) return FALSE;

		cd->width = w;
		cd->height = h;
		cd->dimensions = TRUE;

		return TRUE;
		}

	return FALSE;
}

static gboolean cache_sim_read_date(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 4 || strncmp("Date", buf, 4) != 0) return FALSE;

	if (fseek(f, - s, SEEK_CUR) == 0)
		{
		gchar b;
		gchar buf[1024];
		gsize p = 0;

		b = 'X';
		while (b != '[')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			}
		while (b != ']' && p < sizeof(buf) - 1)
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			buf[p] = b;
			p++;
			}

		while (b != '\n')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) break;
			}

		buf[p] = '\0';
		cd->date = strtol(buf, nullptr, 10);

		cd->have_date = TRUE;

		return TRUE;
		}

	return FALSE;
}

static gboolean cache_sim_read_md5sum(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 8 || strncmp("MD5sum", buf, 6) != 0) return FALSE;

	if (fseek(f, - s, SEEK_CUR) == 0)
		{
		gchar b;
		gchar buf[64];
		gsize p = 0;

		b = 'X';
		while (b != '[')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			}
		while (b != ']' && p < sizeof(buf) - 1)
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			buf[p] = b;
			p++;
			}
		while (b != '\n')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) break;
			}

		buf[p] = '\0';
		cd->have_md5sum = md5_digest_from_text(buf, cd->md5sum);

		return TRUE;
		}

	return FALSE;
}

static gboolean cache_sim_read_similarity(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 11 || strncmp("Similarity", buf, 10) != 0) return FALSE;

	if (strncmp("Grid[32 x 32]", buf + 10, 13) != 0) return FALSE;

	if (fseek(f, - s, SEEK_CUR) == 0)
		{
		gchar b;
		guint8 pixel_buf[3];
		ImageSimilarityData *sd;
		gint x, y;

		b = 'X';
		while (b != '=')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			}

		if (cd->sim)
			{
			/* use current sim that may already contain data we will not touch here */
			sd = cd->sim;
			cd->sim = nullptr;
			cd->similarity = FALSE;
			}
		else
			{
			sd = image_sim_new();
			}

		for (y = 0; y < 32; y++)
			{
			gint s = y * 32;
			for (x = 0; x < 32; x++)
				{
				if (fread(&pixel_buf, sizeof(pixel_buf), 1, f) != 1)
					{
					image_sim_free(sd);
					return FALSE;
					}
				sd->avg_r[s + x] = pixel_buf[0];
				sd->avg_g[s + x] = pixel_buf[1];
				sd->avg_b[s + x] = pixel_buf[2];
				}
			}

		if (fread(&b, sizeof(b), 1, f) == 1)
			{
			if (b != '\n') fseek(f, -1, SEEK_CUR);
			}

		cd->sim = sd;
		cd->sim->filled = TRUE;
		cd->similarity = TRUE;

		return TRUE;
		}

	return FALSE;
}

#define CACHE_LOAD_LINE_NOISE 8

CacheData *cache_sim_data_load(const gchar *path)
{
	FILE *f;
	CacheData *cd = nullptr;
	gchar buf[32];
	gint success = CACHE_LOAD_LINE_NOISE;
	gchar *pathl;

	if (!path) return nullptr;

	pathl = path_from_utf8(path);
	f = fopen(pathl, "r");
	g_free(pathl);

	if (!f) return nullptr;

	cd = cache_sim_data_new();
	cd->path = g_strdup(path);

	if (fread(&buf, sizeof(gchar), 9, f) != 9 ||
	    strncmp(buf, "SIMcache", 8) != 0)
		{
		DEBUG_1("%s is not a cache file", cd->path);
		success = 0;
		}

	while (success > 0)
		{
		gint s;
		s = fread(&buf, sizeof(gchar), sizeof(buf), f);

		if (s < 1)
			{
			success = 0;
			}
		else
			{
			if (!cache_sim_read_comment(f, buf, s, cd) &&
			    !cache_sim_read_dimensions(f, buf, s, cd) &&
			    !cache_sim_read_date(f, buf, s, cd) &&
			    !cache_sim_read_md5sum(f, buf, s, cd) &&
			    !cache_sim_read_similarity(f, buf, s, cd))
				{
				if (!cache_sim_read_skipline(f, s))
					{
					success = 0;
					}
				else
					{
					success--;
					}
				}
			else
				{
				success = CACHE_LOAD_LINE_NOISE;
				}
			}
		}

	fclose(f);

	if (!cd->dimensions &&
	    !cd->have_date &&
	    !cd->have_md5sum &&
	    !cd->similarity)
		{
		cache_sim_data_free(cd);
		cd = nullptr;
		}

	return cd;
}

/*
 *-------------------------------------------------------------------
 * sim cache setting
 *-------------------------------------------------------------------
 */

void cache_sim_data_set_dimensions(CacheData *cd, gint w, gint h)
{
	if (!cd) return;

	cd->width = w;
	cd->height = h;
	cd->dimensions = TRUE;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
void cache_sim_data_set_date_unused(CacheData *cd, time_t date)
{
	if (!cd) return;

	cd->date = date;
	cd->have_date = TRUE;
}
#pragma GCC diagnostic pop

void cache_sim_data_set_md5sum(CacheData *cd, guchar digest[16])
{
	gint i;

	if (!cd) return;

	for (i = 0; i < 16; i++)
		{
		cd->md5sum[i] = digest[i];
		}
	cd->have_md5sum = TRUE;
}

void cache_sim_data_set_similarity(CacheData *cd, ImageSimilarityData *sd)
{
	if (!cd || !sd || !sd->filled) return;

	if (!cd->sim) cd->sim = image_sim_new();

	memcpy(cd->sim->avg_r, sd->avg_r, 1024);
	memcpy(cd->sim->avg_g, sd->avg_g, 1024);
	memcpy(cd->sim->avg_b, sd->avg_b, 1024);
	cd->sim->filled = TRUE;

	cd->similarity = TRUE;
}

gboolean cache_sim_data_filled(ImageSimilarityData *sd)
{
	if (!sd) return FALSE;
	return sd->filled;
}

/*
 *-------------------------------------------------------------------
 * cache path location utils
 *-------------------------------------------------------------------
 */

struct CachePathParts
{
	CachePathParts(CacheType type)
	{
		switch (type)
			{
			case CACHE_TYPE_THUMB:
				rc = get_thumbnails_cache_dir();
				local = GQ_CACHE_LOCAL_THUMB;
				ext = GQ_CACHE_EXT_THUMB;
				break;
			case CACHE_TYPE_SIM:
				rc = get_thumbnails_cache_dir();
				local = GQ_CACHE_LOCAL_THUMB;
				ext = GQ_CACHE_EXT_SIM;
				break;
			case CACHE_TYPE_METADATA:
				rc = get_metadata_cache_dir();
				local = GQ_CACHE_LOCAL_METADATA;
				ext = GQ_CACHE_EXT_METADATA;
				break;
			case CACHE_TYPE_XMP_METADATA:
				rc = get_metadata_cache_dir();
				local = GQ_CACHE_LOCAL_METADATA;
				ext = GQ_CACHE_EXT_XMP_METADATA;
				break;
			}
	}

	gchar *build_path_local(const gchar *source) const
	{
		gchar *base = remove_level_from_path(source);
		gchar *name = g_strconcat(filename_from_path(source), ext, nullptr);
		gchar *path = g_build_filename(base, local, name, nullptr);
		g_free(name);
		g_free(base);

		return path;
	}

	gchar *build_path_rc(const gchar *source) const
	{
		gchar *name = g_strconcat(source, ext, nullptr);
		gchar *path = g_build_filename(rc, name, nullptr);
		g_free(name);

		return path;
	}

	const gchar *rc = nullptr;
	const gchar *local = nullptr;
	const gchar *ext = nullptr;
};

gchar *cache_get_location(CacheType type, const gchar *source, gint include_name, mode_t *mode)
{
	gchar *path = nullptr;
	gchar *base;
	gchar *name = nullptr;

	if (!source) return nullptr;

	const CachePathParts cache{type};

	base = remove_level_from_path(source);
	if (include_name)
		{
		name = g_strconcat(filename_from_path(source), cache.ext, NULL);
		}

	if (((type != CACHE_TYPE_METADATA && type != CACHE_TYPE_XMP_METADATA && options->thumbnails.cache_into_dirs) ||
	     ((type == CACHE_TYPE_METADATA || type == CACHE_TYPE_XMP_METADATA) && options->metadata.enable_metadata_dirs)) &&
	    access_file(base, W_OK))
		{
		path = g_build_filename(base, cache.local, name, NULL);
		if (mode) *mode = 0775;
		}

	if (!path)
		{
		path = g_build_filename(cache.rc, base, name, NULL);
		if (mode) *mode = 0755;
		}

	g_free(base);
	if (name) g_free(name);

	return path;
}

gchar *cache_find_location(CacheType type, const gchar *source)
{
	gchar *path;
	gboolean prefer_local;

	if (!source) return nullptr;

	const CachePathParts cache{type};

	if (type == CACHE_TYPE_METADATA || type == CACHE_TYPE_XMP_METADATA)
		{
		prefer_local = options->metadata.enable_metadata_dirs;
		}
	else
		{
		prefer_local = options->thumbnails.cache_into_dirs;
		}

	if (prefer_local)
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
		if (!prefer_local)
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
	gchar *cachel;
	gchar *pathl;
	gboolean ret = FALSE;

	if (!cache || !path) return FALSE;

	cachel = path_from_utf8(cache);
	pathl = path_from_utf8(path);

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

	g_free(pathl);
	g_free(cachel);

	return ret;
}

const gchar *get_thumbnails_cache_dir()
{
	static gchar *thumbnails_cache_dir = nullptr;

	if (thumbnails_cache_dir) return thumbnails_cache_dir;

	if (USE_XDG)
		{
		thumbnails_cache_dir = g_build_filename(xdg_cache_home_get(),
								GQ_APPNAME_LC, GQ_CACHE_THUMB, NULL);
		}
	else
		{
		thumbnails_cache_dir = g_build_filename(get_rc_dir(), GQ_CACHE_THUMB, NULL);
		}

	return thumbnails_cache_dir;
}

const gchar *get_thumbnails_standard_cache_dir()
{
	static gchar *thumbnails_standard_cache_dir = nullptr;

	if (thumbnails_standard_cache_dir) return thumbnails_standard_cache_dir;

	thumbnails_standard_cache_dir = g_build_filename(xdg_cache_home_get(),
										THUMB_FOLDER_GLOBAL, NULL);

	return thumbnails_standard_cache_dir;
}

const gchar *get_metadata_cache_dir()
{
	static gchar *metadata_cache_dir = nullptr;

	if (metadata_cache_dir) return metadata_cache_dir;

	if (USE_XDG)
		{
		/* Metadata go to $XDG_DATA_HOME.
		 * "Keywords and comments, among other things, are irreplaceable and cannot be auto-generated,
		 * so I don't think they'd be appropriate for the cache directory." -- Omari Stephens on geeqie-devel ml
		 */
		metadata_cache_dir = g_build_filename(xdg_data_home_get(), GQ_APPNAME_LC, GQ_CACHE_METADATA, NULL);
		}
	else
		{
		metadata_cache_dir = g_build_filename(get_rc_dir(), GQ_CACHE_METADATA, NULL);
		}

	return metadata_cache_dir;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
