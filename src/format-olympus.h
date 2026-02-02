/*
 * Copyright (C) 2005 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
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

#ifndef FORMAT_OLYMPUS_H
#define FORMAT_OLYMPUS_H

#include <glib.h>

#include "exif-int.h"


gboolean format_olympus_raw(guchar *data, guint len,
			    guint *image_offset, guint *exif_offset);


#define FORMAT_RAW_OLYMPUS { "orf", \
			     FORMAT_RAW_MATCH_MAGIC, 0, "IIR", 3, \
			     FORMAT_RAW_EXIF_IFD_II, nullptr, \
			     "Olympus raw", format_olympus_raw }


gboolean format_olympus_makernote(ExifData *exif, guchar *tiff, guint offset,
			          guint size, ExifByteOrder bo);

#define FORMAT_EXIF_OLYMPUS { FORMAT_EXIF_MATCH_MAKERNOTE, "OLYMP\x00\x01", 7, \
			      "Olympus", format_olympus_makernote }, \
			    { FORMAT_EXIF_MATCH_MAKERNOTE, "OLYMP\x00\x02", 7, \
			      "Olympus", format_olympus_makernote }


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
