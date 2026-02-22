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

#ifndef FILEFILTER_H
#define FILEFILTER_H

#include <glib.h>

#include "filedata.h"

enum FileFormatClass : gint {
	FORMAT_CLASS_UNKNOWN,
	FORMAT_CLASS_IMAGE,
	FORMAT_CLASS_RAWIMAGE,
	FORMAT_CLASS_META,
	FORMAT_CLASS_VIDEO,
	FORMAT_CLASS_COLLECTION,
	FORMAT_CLASS_DOCUMENT,
	FORMAT_CLASS_ARCHIVE,
	FILE_FORMAT_CLASSES
};

extern const gchar *format_class_list[];

enum FileFormatRating : gint {
    FORMAT_RATING_REJECTED = -1,
    FORMAT_RATING_UNRATED  = 0,
    FORMAT_RATING_1        = 1,
    FORMAT_RATING_2        = 2,
    FORMAT_RATING_3        = 3,
    FORMAT_RATING_4        = 4,
    FORMAT_RATING_5        = 5,
};
constexpr gint FORMAT_RATING_COUNT = 7;

extern const gchar *format_rating_list[];

struct FilterEntry {
	gchar *key;
	gchar *description;
	gchar *extensions;
	FileFormatClass file_class;
	gboolean enabled;
	gboolean writable;
	gboolean allow_sidecar;
};

/**
 * @headerfile filter_get_list
 * you can change, but not add or remove entries from the returned list
 */
GList *filter_get_list();
void filter_remove_entry(FilterEntry *fe);

void filter_add(const gchar *key, const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean writable, gboolean allow_sidecar, gboolean enabled);
void filter_add_unique(const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean writable, gboolean allow_sidecar, gboolean enabled);
void filter_add_defaults();
void filter_reset();
void filter_rebuild();
GList *filter_to_list(const gchar *extensions);

const gchar *registered_extension_from_path(const gchar *name);
gboolean filter_name_exists(const gchar *name);
gboolean filter_file_class(const gchar *name, FileFormatClass file_class);
gboolean filter_file_star(const gchar *name, FileFormatRating file_star);
FileFormatClass filter_file_get_class(const gchar *name);
FileFormatRating filter_file_get_rating(FileData *fd);
gboolean filter_name_is_writable(const gchar *name);
gboolean filter_name_allow_sidecar(const gchar *name);

void filter_write_list(GString *outstr, gint indent);
void filter_load_file_type(const gchar **attribute_names, const gchar **attribute_values);


void sidecar_ext_parse(const gchar *text);
gchar *sidecar_ext_to_string();
GList *sidecar_ext_get_list();

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
