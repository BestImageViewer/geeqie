/*
 * Copyright (C) 2005 Lars Ellenberg
 * Copyright (C) 2005 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Lars Ellenberg
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

#include <config.h>

#ifndef HAVE_EXIV2

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <glib.h>

#include "intl.h"

#include "main.h"
#include "format-fuji.h"
#include "format-raw.h"

#include "exif.h"


/*
 *-----------------------------------------------------------------------------
 * Raw (RAF) embedded jpeg extraction for Fujifilm
 *-----------------------------------------------------------------------------
 */


gboolean format_fuji_raw(guchar *data, const guint len,
		         guint *image_offset, guint *exif_offset)
{
	guint io;
	guint eo;

	if (len < 128 ||
	    memcmp(data, "FUJIFILM", 8) != 0)
		{
		return FALSE;
		}

	/* offset to jpeg is embedded at bytes 84-87 */
	io = exif_byte_get_int32(data + 84, EXIF_BYTE_ORDER_MOTOROLA);
	if (io + 4 > len) return FALSE;

	/* verify jpeg marker */
	if (memcmp(data + io, "\xff\xd8\xff\xe1", 4) != 0)
		{
		return FALSE;
		}

	/* Exif is stored in the jpeg, so use the same offset */
	eo=io;

	if (image_offset) *image_offset = io;
	if (exif_offset) *exif_offset = eo;

	return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 * EXIF Makernote for Fujifilm
 *-----------------------------------------------------------------------------
 */

static ExifTextList FujiTagSharpness[] = {
	{ 1,	"soft" },
	{ 2,	"soft" },
	{ 3,	"normal" },
	{ 4,	"hard" },
	{ 5,	"hard" },
	EXIF_TEXT_LIST_END
};

static ExifTextList FujiTagWhiteBalance[]= {
	{ 0,	"auto" },
	{ 256,	"daylight" },
	{ 512,	"cloudy" },
	{ 768,	"daylight color-fluorescence" },
	{ 769,	"daywhite color-fluorescence" },
	{ 770,	"white-fluorescence" },
	{ 1024,	"incandescent" },
	{ 3840,	"custom" },
	EXIF_TEXT_LIST_END
};

static ExifTextList FujiTagColorTone[]= {
	{ 0,	"normal" },
	{ 256,	"high" },
	{ 512,	"low" },
	EXIF_TEXT_LIST_END
};

static ExifTextList FujiTagFlashMode[]= {
	{ 0,	"auto" },
	{ 1,	"on" },
	{ 2,	"off" },
	{ 3,	"red-eye reduction" },
	EXIF_TEXT_LIST_END
};

static ExifTextList FujiTagOffOn[]= {
	{ 0,	"off" },
	{ 1,	"on" },
	EXIF_TEXT_LIST_END
};

static ExifTextList FujiTagFocusMode[]= {
	{ 0,	"auto" },
	{ 1,	"manual" },
	EXIF_TEXT_LIST_END
};

static ExifTextList FujiTagPictureMode[]= {
	{ 0,	"auto" },
	{ 1,	"portrait" },
	{ 2,	"landscape" },
	{ 4,	"sports" },
	{ 5,	"night" },
	{ 6,	"program AE" },
	{ 256,	"aperture priority AE" },
	{ 512,	"shutter priority AE" },
	{ 768,	"manual" },
	EXIF_TEXT_LIST_END
};

static ExifTextList FujiTagNoYes[]= {
	{ 0,	"no" },
	{ 1,	"yes" },
	EXIF_TEXT_LIST_END
};


static ExifMarker FujiExifMarkersList[] = {
{ 0x1000,	EXIF_FORMAT_STRING, 8,		"Fuji.Quality",		"Quality",	nullptr },
{ 0x1001,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.Sharpness",	"Sharpness",	FujiTagSharpness },
{ 0x1002,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.WhiteBalance",	"White balance",FujiTagWhiteBalance },
{ 0x1003,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.Color",		"Color",	FujiTagColorTone },
{ 0x1004,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.Tone",		"Tone",		FujiTagColorTone },
{ 0x1010,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.FlashMode",	"Flash mode",	FujiTagFlashMode },
{ 0x1011,	EXIF_FORMAT_RATIONAL, 1,	"Fuji.FlashStrength",	"Flash strength", nullptr },
{ 0x1020,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.Macro",		"Macro",	FujiTagOffOn },
{ 0x1021,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.FocusMode",	"Focus mode",	FujiTagFocusMode },
{ 0x1030,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.SlowSync",	"Slow synchro",	FujiTagOffOn },
{ 0x1031,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.PictureMode",	"Picture mode",	FujiTagPictureMode },
{ 0x1100,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.ContTake/Bracket",
							"Continuous / Auto bracket",	FujiTagOffOn },
{ 0x1300,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.BlurWarning",	"Blue warning",	FujiTagNoYes },
{ 0x1301,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.FocusWarning",	"Focus warning",FujiTagNoYes },
{ 0x1302,	EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Fuji.AEWarning",	"AE warning",	FujiTagNoYes },
EXIF_MARKER_LIST_END
};



gboolean format_fuji_makernote(ExifData *exif, guchar *tiff, guint offset,
			       guint size, ExifByteOrder UNUSED(bo))
{
	guchar *data;
	guint ifdstart;

	if (offset + 8 + 4 >= size) return FALSE;

	data = tiff + offset;

	/* Fuji tag format starts with "FUJIFILM",
	 * followed by 4 bytes indicating offset to IFD directory using Fuji tags,
	 * byte order is always little endian (II).
	 */
	if (memcmp(data, "FUJIFILM", 8) != 0) return FALSE;

	ifdstart = exif_byte_get_int32(data + 8, EXIF_BYTE_ORDER_INTEL);
	if (offset + ifdstart >= size) return FALSE;

	if (exif_parse_IFD_table(exif, tiff + offset, ifdstart, size - offset,
				 EXIF_BYTE_ORDER_INTEL, 0, FujiExifMarkersList) != 0)
		{
		return FALSE;
		}

	return TRUE;
}
#else
using dummy_variable = int;
#endif
/* not HAVE_EXIV2 */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
