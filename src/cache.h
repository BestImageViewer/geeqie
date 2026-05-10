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

#include <cstdio>
#include <memory>

#include <glib.h>

#include "geometry.h"
#include "md5-util.h"

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
	bool save() const;
	bool load(const gchar *path);

	void set_dimensions(GqSize dimensions);
	void set_md5sum(const Md5Digest &digest);
	void set_similarity(ImageSimilarityData *sd);

	gchar *path;
	GqSize dimensions;
	time_t date;
	Md5Digest md5sum;
	std::unique_ptr<ImageSimilarityData> sim;

	gboolean have_dimensions;
	gboolean have_date;
	gboolean have_md5sum;
	gboolean have_similarity;

private:
	bool write_dimensions(GString *gstring) const;
	bool write_date(GString *gstring) const;
	bool write_md5sum(GString *gstring) const;
	bool write_similarity(GString *gstring) const;

	bool read_dimensions(FILE *f, const gchar *buffer, gint s);
	bool read_date(FILE *f, const gchar *buffer, gint s);
	bool read_md5sum(FILE *f, const gchar *buffer, gint s);
	bool read_similarity(FILE *f, const gchar *buffer, gint s);
};

gboolean cache_time_valid(const gchar *cache, const gchar *path);

CacheData *cache_sim_data_new();
CacheData *cache_sim_data_new(const gchar *path);
void cache_sim_data_free(CacheData *cd);

gchar *cache_create_location(CacheType cache_type, const gchar *source);
gchar *cache_get_location(CacheType cache_type, const gchar *source);
gchar *cache_find_location(CacheType type, const gchar *source);

const gchar *get_thumbnails_cache_dir();
const gchar *get_thumbnails_standard_cache_dir();
const gchar *get_metadata_cache_dir();

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
