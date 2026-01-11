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

#ifndef __EXIF_INT_H
#define __EXIF_INT_H

#include <cstdio>

#include "exif.h"

/*
 *-----------------------------------------------------------------------------
 * Tag formats
 *-----------------------------------------------------------------------------
 */

enum ExifByteOrder {
	EXIF_BYTE_ORDER_INTEL,
	EXIF_BYTE_ORDER_MOTOROLA
};

struct ExifFormatAttrib
{
	ExifFormatType type;
	guint size;
	const gchar *short_name;
	const gchar *description;
};

extern ExifFormatAttrib ExifFormatList[]; /**< the list of known tag data formats */


/*
 *-----------------------------------------------------------------------------
 * Data storage
 *-----------------------------------------------------------------------------
 */

struct ExifData
{
	gchar *path;
	GList *items;	/**< list of (ExifItem *) */
	GList *current; /**< for exif_get_next_item */
};

struct ExifTextList
{
	gint value;
	const gchar *description;
};

struct ExifMarker
{
	guint		tag;
	ExifFormatType	format;
	gint		components;
	const gchar		*key;
	const gchar		*description;
	ExifTextList	*list;
};

struct ExifItem
{
	ExifFormatType format;
	guint tag;
	const ExifMarker *marker;
	guint elements;
	gpointer data;
	guint data_len;
};

#define EXIF_MARKER_LIST_END { 0x0000, EXIF_FORMAT_UNKNOWN, 0, NULL, NULL, NULL }

#define EXIF_TEXT_LIST_END { -1, NULL }


/*
 *-----------------------------------------------------------------------------
 * functions
 *-----------------------------------------------------------------------------
 */

void exif_write_data_list(ExifData *exif, FILE *f, bool human_readable_list);



/* These funcs for use by makernote/tiff parsers only */

#define EXIF_TIFF_MAX_LEVELS 4

#define EXIF_TIFD_OFFSET_TAG 0
#define EXIF_TIFD_OFFSET_FORMAT 2
#define EXIF_TIFD_OFFSET_COUNT 4
#define EXIF_TIFD_OFFSET_DATA 8
#define EXIF_TIFD_SIZE 12


guint16 exif_byte_get_int16(guchar *f, ExifByteOrder bo);
guint32 exif_byte_get_int32(guchar *f, ExifByteOrder bo);
void exif_byte_put_int16(guchar *f, guint16 n, ExifByteOrder bo);
void exif_byte_put_int32(guchar *f, guint32 n, ExifByteOrder bo);

ExifItem *exif_item_new(ExifFormatType format, guint tag,
			guint elements, const ExifMarker *marker);
void exif_item_copy_data(ExifItem *item, gpointer src, guint len,
			 ExifFormatType src_format, ExifByteOrder bo);

gint exif_parse_IFD_table(ExifData *exif,
			  guchar *tiff, guint offset,
			  guint size, ExifByteOrder bo,
			  gint level,
			  const ExifMarker *list);

gint exif_tiff_directory_offset(guchar *data, guint len,
				guint *offset, ExifByteOrder *bo);
gint exif_tiff_parse(ExifData *exif, guchar *tiff, guint size, ExifMarker *list);

gchar *exif_text_list_find_value(ExifTextList *list, guint value);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
