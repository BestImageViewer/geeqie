/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Eric Swalens, Quy Tonthat, John Ellis
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

#ifndef __EXIF_H
#define __EXIF_H

#include <optional>

#include <glib.h>

enum ColorManProfileType : int;
enum MetadataFormat : gint;

struct ColorManMemData;
struct ExifData;
struct ExifItem;
class FileData;


/*
 *-----------------------------------------------------------------------------
 * Data storage
 *-----------------------------------------------------------------------------
 */

struct ExifRational
{
	guint32 num;
	guint32 den;
};


/* enums useful for image manipulation */

enum ExifOrientationType {
	EXIF_ORIENTATION_UNKNOWN	= 0,
	EXIF_ORIENTATION_TOP_LEFT	= 1,
	EXIF_ORIENTATION_TOP_RIGHT	= 2,
	EXIF_ORIENTATION_BOTTOM_RIGHT	= 3,
	EXIF_ORIENTATION_BOTTOM_LEFT	= 4,
	EXIF_ORIENTATION_LEFT_TOP	= 5,
	EXIF_ORIENTATION_RIGHT_TOP	= 6,
	EXIF_ORIENTATION_RIGHT_BOTTOM	= 7,
	EXIF_ORIENTATION_LEFT_BOTTOM	= 8
};

/*
 *-----------------------------------------------------------------------------
 * functions
 *-----------------------------------------------------------------------------
 */

void exif_init();

ExifData *exif_read(gchar *path, gchar *sidecar_path, GHashTable *modified_xmp);

ExifData *exif_read_fd(FileData *fd);
void exif_free_fd(FileData *fd, ExifData *exif);

ExifData *exif_get_original(ExifData *exif);


gboolean exif_write(ExifData *exif);
gboolean exif_write_sidecar(ExifData *exif, gchar *path);

void exif_free(ExifData *exif);

gchar *exif_get_data_as_text(ExifData *exif, const gchar *key);
gint exif_get_integer(ExifData *exif, const gchar *key, gint *value);
ExifRational *exif_get_rational(ExifData *exif, const gchar *key, gint *sign);

ExifItem *exif_get_item(ExifData *exif, const gchar *key);
ExifItem *exif_get_first_item(ExifData *exif);
ExifItem *exif_get_next_item(ExifData *exif);


gchar *exif_item_get_tag_name(ExifItem *item);
guint exif_item_get_tag_id(ExifItem *item);
guint exif_item_get_elements(ExifItem *item);
gchar *exif_item_get_data(ExifItem *item, guint *data_len);
gchar *exif_item_get_description(ExifItem *item);
const gchar *exif_item_get_format_name(ExifItem *item, gboolean brief);
gchar *exif_item_get_data_as_text(ExifItem *item, ExifData *exif);
gint exif_item_get_integer(ExifItem *item, gint *value);
ExifRational *exif_item_get_rational(ExifItem *item, gint *sign, guint n);

gchar *exif_item_get_string(ExifItem *item, gint idx);

gchar *exif_get_description_by_key(const gchar *key);
gchar *exif_get_tag_description_by_key(const gchar *key);

GHashTable *exif_get_formatted(ExifData *exif);
std::optional<gchar *> exif_get_formatted_by_key(ExifData *exif, const gchar *key);

gint exif_update_metadata(ExifData *exif, const gchar *key, const GList *values);
GList *exif_get_metadata(ExifData *exif, const gchar *key, MetadataFormat format);

guchar *exif_get_color_profile(ExifData *exif, guint *data_len);
ColorManMemData exif_get_color_profile(FileData *fd, ColorManProfileType &color_profile_from_image);

/* jpeg embedded icc support */

void exif_add_jpeg_color_profile(ExifData *exif, guchar *cp_data, guint cp_length);


gboolean exif_jpeg_parse_color(ExifData *exif, guchar *data, guint size);

/* support for so called "jpeg comment" */
gchar* exif_get_image_comment(FileData* fd);
void exif_set_image_comment(FileData* fd, const gchar* comment);

/*raw support */
guchar *exif_get_preview(ExifData *exif, guint *data_len, gint requested_width, gint requested_height);
void exif_free_preview(const guchar *buf);

gchar *metadata_file_info(FileData *fd, const gchar *key, MetadataFormat format);
gchar *metadata_lua_info(FileData *fd, const gchar *key, MetadataFormat format);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
