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

#include <config.h>

#ifndef HAVE_EXIV2

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <glib.h>

#include "intl.h"

#include "main.h"
#include "format-olympus.h"
#include "format-raw.h"

#include "exif.h"


/*
 *-----------------------------------------------------------------------------
 * Raw ORF embedded jpeg extraction for Olympus
 *-----------------------------------------------------------------------------
 */

static guint olympus_tiff_table(guchar *data, guint len, guint offset, ExifByteOrder bo,
				gint level,
				guint *image_offset, guint *exif_offset);


static void olympus_tiff_entry(guchar *data, const guint len, guint offset, ExifByteOrder bo,
			       gint,
			       guint *image_offset, guint *exif_offset)
{
	guint tag;
	guint type;
	guint count;
	guint segment;
	guint seg_len;

	tag = exif_byte_get_int16(data + offset + EXIF_TIFD_OFFSET_TAG, bo);
	type = exif_byte_get_int16(data + offset + EXIF_TIFD_OFFSET_FORMAT, bo);
	count = exif_byte_get_int32(data + offset + EXIF_TIFD_OFFSET_COUNT, bo);

	/* so far, we only care about tags with type long */
	if (type != EXIF_FORMAT_LONG_UNSIGNED && type != EXIF_FORMAT_LONG) return;

	seg_len = ExifFormatList[type].size * count;
	if (seg_len > 4)
		{
		segment = exif_byte_get_int32(data + offset + EXIF_TIFD_OFFSET_DATA, bo);
		if (segment + seg_len > len) return;
		}
	else
		{
		segment = offset + EXIF_TIFD_OFFSET_DATA;
		}

	if (tag == 0x201)
		{
		/* start of embedded jpeg, not all olympus cameras embed a jpeg */
		*image_offset = exif_byte_get_int32(data + segment, bo);
		}

	if (tag == 0x8769)
		{
		/* This is the Exif info */
		*exif_offset = exif_byte_get_int32(data + segment, bo);
		}
}

static guint olympus_tiff_table(guchar *data, const guint len, guint offset, ExifByteOrder bo,
				gint level,
				guint *image_offset, guint *exif_offset)
{
	guint count;
	guint i;

	if (level > EXIF_TIFF_MAX_LEVELS) return 0;

	if (len < offset + 2) return FALSE;

	count = exif_byte_get_int16(data + offset, bo);
	offset += 2;
	if (len < offset + count * EXIF_TIFD_SIZE + 4) return 0;

	for (i = 0; i < count; i++)
		{
		olympus_tiff_entry(data, len, offset + i * EXIF_TIFD_SIZE, bo, level,
				   image_offset, exif_offset);
		}

	return exif_byte_get_int32(data + offset + count * EXIF_TIFD_SIZE, bo);
}

gboolean format_olympus_raw(guchar *data, const guint len,
			    guint *image_offset, guint *exif_offset)
{
	guint i_off = 0;
	guint e_off = 0;
	guint offset;
	gint level;

	if (len < 8) return FALSE;

	/* these are in tiff file format with a different magick header */
	if (memcmp(data, "IIR", 3) != 0) return FALSE;

	offset = exif_byte_get_int32(data + 4, EXIF_BYTE_ORDER_INTEL);

	level = 0;
	while (offset && level < EXIF_TIFF_MAX_LEVELS)
		{
		offset = olympus_tiff_table(data, len, offset, EXIF_BYTE_ORDER_INTEL, 0, &i_off, &e_off);
		level++;
		}

	if (i_off != 0 || e_off != 0)
		{
		if (image_offset) *image_offset = i_off;
		if (exif_offset) *exif_offset = e_off;
		return TRUE;
		}

	return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 * EXIF Makernote for Olympus
 *-----------------------------------------------------------------------------
 */

static ExifTextList KonMinTagColorMode[]= {
	{ 0,	"natural" },
	{ 1,	"black and white" },
	{ 2,	"vivid" },
	{ 3,	"solarization" },
	{ 4,	"Adobe RGB" },
	EXIF_TEXT_LIST_END
};

static ExifTextList KonMinTagQuality[]= {
	{ 0,	"raw" },
	{ 1,	"super fine" },
	{ 2,	"find" },
	{ 3,	"standard" },
	{ 4,	"extra fine" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusTagJpegQuality[]= {
	{ 1,	"standard" },
	{ 2,	"high" },
	{ 3,	"super high" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusTagMacro[]= {
	{ 0,	"off" },
	{ 1,	"on" },
	{ 2,	"view" },
	{ 3,	"manual" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusTagFlashMode[]= {
	{ 0,	"auto" },
	{ 1,	"red-eye reduction" },
	{ 2,	"fill" },
	{ 3,	"off" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusTagFocusMode[]= {
	{ 0,	"auto" },
	{ 1,	"manual" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusTagSharpness[]= {
	{ 0,	"normal" },
	{ 1,	"hard" },
	{ 2,	"soft" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusTagContrast[]= {
	{ 0,	"hard" },
	{ 1,	"normal" },
	{ 2,	"soft" },
	EXIF_TEXT_LIST_END
};


static ExifMarker OlympusExifMarkersList[] = {
{ 0x0001, EXIF_FORMAT_LONG_UNSIGNED, -1, "Konica/MinoltaSettings", "Konica / Minolta settings", nullptr },
{ 0x0003, EXIF_FORMAT_LONG_UNSIGNED, -1, "Konica/MinoltaSettings", "Konica / Minolta settings", nullptr },
{ 0x0040, EXIF_FORMAT_LONG_UNSIGNED, -1, "CompressedImageSize",	"Compressed image size", nullptr },
{ 0x0081, EXIF_FORMAT_LONG_UNSIGNED, 1,  "ThumbnailOffset",	"Thumbnail offset",	nullptr },
{ 0x0088, EXIF_FORMAT_LONG_UNSIGNED, 1,  "ThumbnailOffset",	"Thumbnail offset",	nullptr },
{ 0x0089, EXIF_FORMAT_LONG_UNSIGNED, 1,  "ThumbnailLength",	"Thumbnail length",	nullptr },
{ 0x0101, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Konica/Minolta.ColorMode", "Color mode",	KonMinTagColorMode },
{ 0x0102, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Konica/Minolta.Quality", "Quality",		KonMinTagQuality },
{ 0x0103, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Konica/Minolta.Quality", "Quality",		KonMinTagQuality },
{ 0x0200, EXIF_FORMAT_LONG_UNSIGNED, 3,  "Olympus.SpecialMode",	"Special mode",		nullptr },
{ 0x0201, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.JpegQuality",	"Jpeg quality",		OlympusTagJpegQuality },
{ 0x0202, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.Macro",	"Macro",		OlympusTagMacro },
{ 0x0204, EXIF_FORMAT_RATIONAL_UNSIGNED, 1, "Olympus.DigitalZoom", "Digital zoom",	nullptr },
{ 0x0207, EXIF_FORMAT_STRING, -1,	 "Olympus.Firmware",	"Firmware version",	nullptr },
{ 0x0208, EXIF_FORMAT_STRING, -1,	 "Olympus.PictureInfo",	"Picture info",		nullptr },
{ 0x0209, EXIF_FORMAT_UNDEFINED, -1,	 "Olympus.CameraID",	"Camera ID",		nullptr },
{ 0x020b, EXIF_FORMAT_LONG_UNSIGNED, 1,	 "Epson.ImageWidth",	"Image width",		nullptr },
{ 0x020c, EXIF_FORMAT_LONG_UNSIGNED, 1,  "Epson.ImageHeight",	"Image height",		nullptr },
{ 0x020d, EXIF_FORMAT_STRING, -1,	 "Epson.Manufacturer",	"Manufacturer",		nullptr },
{ 0x0e00, EXIF_FORMAT_BYTE, -1,		 "Olympus.PrintImageMatching", "Print image matching", nullptr },
{ 0x1004, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.FlashMode",	"Flash mode",		OlympusTagFlashMode },
{ 0x1006, EXIF_FORMAT_RATIONAL, 1,	 "Olympus.Bracket",	"Bracket",		nullptr },
{ 0x100b, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.FocusMode",	"Focus mode",		OlympusTagFocusMode },
{ 0x100c, EXIF_FORMAT_RATIONAL_UNSIGNED, 1, "Olympus.FocusDistance", "Focus distance",	nullptr },
{ 0x100d, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.Zoom",	"Zoom",			nullptr },
{ 0x1006, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.MacroFocus",	"Macro focus",		nullptr },
{ 0x100f, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.Sharpness",	"Sharpness",		OlympusTagSharpness },
{ 0x1011, EXIF_FORMAT_SHORT_UNSIGNED, 9, "Olympus.ColorMatrix",	"Color matrix",		nullptr },
{ 0x1012, EXIF_FORMAT_SHORT_UNSIGNED, 4, "Olympus.BlackLevel",	"Black level",		nullptr },
{ 0x1015, EXIF_FORMAT_SHORT_UNSIGNED, 2, "Olympus.WhiteBalance", "White balance",	nullptr },
{ 0x1017, EXIF_FORMAT_SHORT_UNSIGNED, 2, "Olympus.RedBias",	"Red bias",		nullptr },
{ 0x1018, EXIF_FORMAT_SHORT_UNSIGNED, 2, "Olympus.BlueBias",	"Blue bias",		nullptr },
{ 0x101a, EXIF_FORMAT_STRING, -1,	 "Olympus.SerialNumber", "Serial number",	nullptr },
{ 0x1023, EXIF_FORMAT_RATIONAL, 1,	 "Olympus.FlashBias",	"Flash bias",		nullptr },
{ 0x1029, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.Contrast",	"Contrast",		OlympusTagContrast },
{ 0x102a, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.SharpnessFactor", "Sharpness factor",	nullptr },
{ 0x102b, EXIF_FORMAT_SHORT_UNSIGNED, 6, "Olympus.ColorControl", "Color control",	nullptr },
{ 0x102c, EXIF_FORMAT_SHORT_UNSIGNED, 2, "Olympus.ValidBits",	"Valid bits",		nullptr },
{ 0x102d, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.CoringFilter", "Coring filter",	nullptr },
{ 0x102e, EXIF_FORMAT_LONG_UNSIGNED, 1,  "Olympus.FinalWidth",	"Final width",		nullptr },
{ 0x102f, EXIF_FORMAT_LONG_UNSIGNED, 1,  "Olympus.FinalHeight",	"Final height",		nullptr },
{ 0x1034, EXIF_FORMAT_SHORT_UNSIGNED, 1, "Olympus.CompressionRatio", "Compression ratio", nullptr },
EXIF_MARKER_LIST_END
};

static ExifTextList OlympusShootingMode[]= {
	{ 0,	"normal" },
	{ 1,	"unknown" },
	{ 2,	"fast" },
	{ 3,	"panorama" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusPanoramaDirection[]= {
	{ 1,	"left to right" },
	{ 2,	"right to left" },
	{ 3,	"bottom to top" },
	{ 4,	"top to bottom" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusWB[]= {
	{ 1,	"auto" },
	{ 2,	"manual" },
	{ 3,	"one-touch" },
	EXIF_TEXT_LIST_END
};

static ExifTextList OlympusWBColorTemp[]= {
	{ 2,	"3000" },
	{ 3,	"3700" },
	{ 4,	"4000" },
	{ 5,	"4500" },
	{ 6,	"5500" },
	{ 7,	"6500" },
	{ 8,	"7500" },
	EXIF_TEXT_LIST_END
};

gboolean format_olympus_makernote(ExifData *exif, guchar *tiff, guint offset,
			          guint size, ExifByteOrder bo)
{
	guchar *data;
	ExifItem *item;

	if (offset + 8 + 4 >= size) return FALSE;

	data = tiff + offset;

	/* Olympus tag format starts with "OLYMP\x00\x01" or "OLYMP\x00\x02",
	 * plus an unknown byte,
	 * followed by IFD data using Olympus tags.
	 */
	if (memcmp(data, "OLYMP\x00\x01", 7) != 0 &&
	    memcmp(data, "OLYMP\x00\x02", 7) != 0) return FALSE;

	if (exif_parse_IFD_table(exif, tiff, offset + 8, size,
				 bo, 0, OlympusExifMarkersList) != 0)
		{
		return FALSE;
		}

	item = exif_get_item(exif, "Olympus.SpecialMode");
	if (item && item->data_len == 3 * sizeof(guint32))
		{
		static ExifMarker marker = { 0x0200, EXIF_FORMAT_STRING, -1,
					     "Olympus.ShootingMode", "Shooting mode", nullptr };
		auto array = static_cast<guint32 *>(item->data);
		gchar *mode;
		gchar *pdir = nullptr;
		gchar *text;
		gint l;

		mode = exif_text_list_find_value(OlympusShootingMode, array[0]);
		if (array[0] == 3)
			{
			pdir = exif_text_list_find_value(OlympusPanoramaDirection, array[2]);
			}

		text = g_strdup_printf("%s%s%s, seq %d", mode,
				       (pdir) ? " " : "", (pdir) ? pdir : "",
				       array[1] + 1);
		l = strlen(text) + 1;
		item = exif_item_new(marker.format, marker.tag, l, &marker);
		memcpy(item->data, text, l);

		g_free(text);
		g_free(pdir);
		g_free(mode);

		exif->items = g_list_prepend(exif->items, item);
		}

	item = exif_get_item(exif, "Olympus.WhiteBalance");
	if (item && item->data_len == 2 * sizeof(guint16))
		{
		static ExifMarker marker = { 0x1015, EXIF_FORMAT_STRING, -1,
					     "Olympus.WhiteBalance", "White balance", nullptr };
		auto array = static_cast<guint16 *>(item->data);
		gchar *mode;
		gchar *color = nullptr;
		gchar *text;
		gint l;

		mode = exif_text_list_find_value(OlympusWB, array[0]);
		if (array[0] == 2)
			{
			color = exif_text_list_find_value(OlympusWBColorTemp, array[1]);
			}

		text = g_strdup_printf("%s%s%s", mode,
				       (color) ? " " : "", (color) ? color : "");
		l = strlen(text) + 1;
		item = exif_item_new(marker.format, marker.tag, l, &marker);
		memcpy(item->data, text, l);

		g_free(text);
		g_free(color);
		g_free(mode);

		exif->items = g_list_prepend(exif->items, item);
		}

	return TRUE;
}

#else
using dummy_variable = int;
#endif
/* not HAVE_EXIV2 */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
