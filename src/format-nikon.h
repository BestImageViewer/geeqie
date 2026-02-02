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

#ifndef FORMAT_NIKON_H
#define FORMAT_NIKON_H

#include <glib.h>

#include "exif-int.h"

gboolean format_nikon_raw(guchar *data, guint len,
		          guint *image_offset, guint *exif_offset);

#define FORMAT_RAW_NIKON { "nef", \
			   FORMAT_RAW_MATCH_TIFF_MAKE, 0, "NIKON CORPORATION", 17, \
			   FORMAT_RAW_EXIF_TIFF, nullptr, \
			   "Nikon raw", format_nikon_raw }

/* If your format is basically just TIFF with an embedded jpeg,
 * then avoid duplicating code and just stick it here and use the existing nikon parse.
 */
#define FORMAT_RAW_PENTAX { "pef", \
			    FORMAT_RAW_MATCH_TIFF_MAKE, 0, "PENTAX Corporation", 18, \
			    FORMAT_RAW_EXIF_TIFF, nullptr, \
			    "Pentax raw", format_nikon_raw }

#define FORMAT_RAW_SAMSUNG { "pef", \
			    FORMAT_RAW_MATCH_TIFF_MAKE, 0, "SAMSUNG TECHWIN", 15, \
			    FORMAT_RAW_EXIF_TIFF, nullptr, \
			    "Samsung raw", format_nikon_raw }

gboolean format_nikon_makernote(ExifData *exif, guchar *tiff, guint offset,
			        guint size, ExifByteOrder bo);

#define FORMAT_EXIF_NIKON { FORMAT_EXIF_MATCH_MAKERNOTE, "Nikon\x00", 6, "Nikon", format_nikon_makernote }, \
			  { FORMAT_EXIF_MATCH_MAKE,      "NIKON",     5, "Nikon", format_nikon_makernote }


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
