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

#ifndef CACHE_H
#define CACHE_H

#include <sys/types.h>

#include <glib.h>

struct ImageSimilarityData;

#define GQ_CACHE_THUMB		"thumbnails"
#define GQ_CACHE_METADATA    	"metadata"

#define GQ_CACHE_LOCAL_THUMB    ".thumbnails"
#define GQ_CACHE_LOCAL_METADATA ".metadata"

#define GQ_CACHE_EXT_THUMB      ".png"
#define GQ_CACHE_EXT_SIM        ".sim"
#define GQ_CACHE_EXT_METADATA   ".meta"
#define GQ_CACHE_EXT_XMP_METADATA   ".gq.xmp"


enum CacheType {
	CACHE_TYPE_THUMB,
	CACHE_TYPE_SIM,
	CACHE_TYPE_METADATA,
	CACHE_TYPE_XMP_METADATA
};

struct CacheData
{
	gchar *path;
	gint width;
	gint height;
	time_t date;
	guchar md5sum[16];
	ImageSimilarityData *sim;

	gboolean dimensions;
	gboolean have_date;
	gboolean have_md5sum;
	gboolean similarity;
};

gboolean cache_time_valid(const gchar *cache, const gchar *path);


CacheData *cache_sim_data_new();
void cache_sim_data_free(CacheData *cd);

gboolean cache_sim_data_save(CacheData *cd);
CacheData *cache_sim_data_load(const gchar *path);

void cache_sim_data_set_dimensions(CacheData *cd, gint w, gint h);
void cache_sim_data_set_md5sum(CacheData *cd, const guchar digest[16]);
void cache_sim_data_set_similarity(CacheData *cd, ImageSimilarityData *sd);

gchar *cache_create_location(CacheType cache_type, const gchar *source);
gchar *cache_get_location(CacheType cache_type, const gchar *source);
gchar *cache_find_location(CacheType type, const gchar *source);

const gchar *get_thumbnails_cache_dir();
const gchar *get_thumbnails_standard_cache_dir();
const gchar *get_metadata_cache_dir();

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
