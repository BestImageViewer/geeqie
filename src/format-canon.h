/*
 * Copyright (C) 2005 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Daniel M. German <dmgerman@uvic.ca>
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

#ifndef FORMAT_CANON_H
#define FORMAT_CANON_H

#include <glib.h>

#include "exif-int.h"


gboolean format_canon_raw_crw(guchar *data, guint len,
			      guint *image_offset, guint *exif_offset);

gboolean format_canon_raw_cr2(guchar *data, guint len,
			      guint *image_offset, guint *exif_offset);

#define FORMAT_RAW_CANON { "crw", \
			   FORMAT_RAW_MATCH_MAGIC,     6, "HEAPCCDR", 8, \
			   FORMAT_RAW_EXIF_NONE, nullptr, \
			   "Canon crw", format_canon_raw_crw }, \
			 { "cr2", \
			   FORMAT_RAW_MATCH_TIFF_MAKE, 0, "Canon", 5, \
			   FORMAT_RAW_EXIF_TIFF, nullptr, \
			   "Canon cr2", format_canon_raw_cr2 }


gboolean format_canon_makernote(ExifData *exif, guchar *tiff, guint offset,
			        guint size, ExifByteOrder bo);

#define FORMAT_EXIF_CANON { FORMAT_EXIF_MATCH_MAKE, "Canon", 5, "Canon", format_canon_makernote }


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
