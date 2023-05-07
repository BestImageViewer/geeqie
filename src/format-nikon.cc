/*
 * Copyright (C) 2005 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Joseph Heled
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "intl.h"

#include "main.h"
#include "format-nikon.h"

#include "exif.h"


/*
 *-----------------------------------------------------------------------------
 * Raw NEF embedded jpeg extraction for Nikon
 *-----------------------------------------------------------------------------
 */

static guint nikon_tiff_table(guchar *data, const guint len, guint offset, ExifByteOrder bo,
			      gint level,
			      guint *image_offset, guint *jpeg_len);


static void nikon_tiff_entry(guchar *data, const guint len, guint offset, ExifByteOrder bo,
			     gint level,
			     guint *image_offset, guint *image_length, guint *jpeg_start, guint *jpeg_len)
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

	if (tag == 0x14a)
		{
		/* sub IFD table */
		guint i;

		for (i = 0; i < count; i++)
			{
			guint subset;

			subset = exif_byte_get_int32(data + segment + i * 4, bo);
			nikon_tiff_table(data, len, subset, bo, level + 1, image_offset, image_length);
			}

		}
	else if (tag == 0x201)
		{
		/* jpeg data start offset */
		*jpeg_start = exif_byte_get_int32(data + segment, bo);
		}
	else if (tag == 0x202)
		{
		/* jpeg data length */
		*jpeg_len = exif_byte_get_int32(data + segment, bo);
		}
}

static guint nikon_tiff_table(guchar *data, const guint len, guint offset, ExifByteOrder bo,
			      gint level,
			      guint *image_offset, guint *image_length)
{
	guint count;
	guint i;
	guint jpeg_start = 0;
	guint jpeg_len = 0;

	/* limit damage from infinite loops */
	if (level > EXIF_TIFF_MAX_LEVELS) return 0;

	if (len < offset + 2) return FALSE;

	count = exif_byte_get_int16(data + offset, bo);
	offset += 2;
	if (len < offset + count * EXIF_TIFD_SIZE + 4) return 0;

	for (i = 0; i < count; i++)
		{
		nikon_tiff_entry(data, len, offset + i * EXIF_TIFD_SIZE, bo, level,
				 image_offset, image_length, &jpeg_start, &jpeg_len);
		}

	if (jpeg_start > 0 &&
	    jpeg_len > *image_length)
		{
		*image_offset = jpeg_start;
		*image_length = jpeg_len;
		}

	return exif_byte_get_int32(data + offset + count * EXIF_TIFD_SIZE, bo);
}

gboolean format_nikon_raw(guchar *data, const guint len,
		          guint *image_offset, guint *UNUSED(exif_offset))
{
	guint i_off = 0;
	guint i_len = 0;
	ExifByteOrder bo;
	guint offset;
	gint level;

	if (!exif_tiff_directory_offset(data, len, &offset, &bo)) return FALSE;

	level = 0;
	while (offset && level < EXIF_TIFF_MAX_LEVELS)
		{
		offset = nikon_tiff_table(data, len, offset, bo, 0, &i_off, &i_len);
		level++;
		}

	if (i_off != 0)
		{
		if (image_offset) *image_offset = i_off;
		return TRUE;
		}

	return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 * EXIF Makernote for Nikon
 *-----------------------------------------------------------------------------
 */

static ExifTextList NikonTagQuality[]= {
	{ 1,	"VGA basic" },
	{ 2,	"VGA normal" },
	{ 3,	"VGA fine" },
	{ 4,	"SXGA basic" },
	{ 5,	"SXGA normal" },
	{ 6,	"SXGA fine" },
	{ 7,	"XGA basic (?)" },
	{ 8,	"XGA normal (?)" },
	{ 9,	"XGA fine (?)" },
	{ 10,	"UXGA basic" },
	{ 11,	"UXGA normal" },
	{ 12,	"UXGA fine" },
	EXIF_TEXT_LIST_END
};

static ExifTextList NikonTagColorMode[]= {
	{ 1,	"color" },
	{ 2,	"monochrome" },
	EXIF_TEXT_LIST_END
};

static ExifTextList NikonTagImgAdjust[]= {
	{ 0,	"normal" },
	{ 1,	"bright+" },
	{ 2,	"bright-" },
	{ 3,	"contrast+" },
	{ 4,	"contrast-" },
	EXIF_TEXT_LIST_END
};

static ExifTextList NikonTagISOSensitivity[]= {
	{ 0,	"80" },
	{ 2,	"160" },
	{ 4,	"320" },
	{ 5,	"100" },
	EXIF_TEXT_LIST_END
};

static ExifTextList NikonTagWhiteBalance[]= {
	{ 0,	"auto" },
	{ 1,	"preset" },
	{ 2,	"daylight" },
	{ 3,	"incandescent" },
	{ 4,	"fluorescence" },
	{ 5,	"cloudy" },
	{ 6,	"speedlight" },
	EXIF_TEXT_LIST_END
};

static ExifTextList NikonTagConverter[]= {
	{ 0,	"none" },
	{ 1,	"Fisheye" },
	EXIF_TEXT_LIST_END
};

static ExifMarker NikonExifMarkersList1[] = {
{ 0x0002, EXIF_FORMAT_STRING, 6,		"Nikon.unknown",	NULL,		NULL },
{ 0x0003, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.Quality",	"Quality",	NikonTagQuality },
{ 0x0004, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.ColorMode",	"Color mode",	NikonTagColorMode },
{ 0x0005, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.ImageAdjustment",
								"Image adjustment",	NikonTagImgAdjust },
{ 0x0006, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.ISOSensitivity",
								"ISO sensitivity",	NikonTagISOSensitivity },
{ 0x0007, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.WhiteBalance",	"White balance",NikonTagWhiteBalance },
{ 0x0008, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Nikon.Focus",		"Focus",	NULL },
{ 0x000a, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Nikon.DigitalZoom",	"Digital zoom",	NULL },
{ 0x000b, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.Converter",	"Converter",	NikonTagConverter },
EXIF_MARKER_LIST_END
};

static ExifTextList NikonTag2FlashComp[]= {
	{ 0x06,	"+1.0 EV" },
	{ 0x04,	"+0.7 EV" },
	{ 0x03,	"+0.5 EV" },
	{ 0x02,	"+0.3 EV" },
	{ 0x00,	"0.0 EV" },
	{ 0xfe,	"-0.3 EV" },
	{ 0xfd,	"-0.5 EV" },
	{ 0xfc,	"-0.7 EV" },
	{ 0xfa,	"-1.0 EV" },
	{ 0xf8,	"-1.3 EV" },
	{ 0xf7,	"-1.5 EV" },
	{ 0xf6,	"-1.7 EV" },
	{ 0xf4,	"-2.0 EV" },
	{ 0xf2,	"-2.3 EV" },
	{ 0xf1,	"-2.5 EV" },
	{ 0xf0,	"-2.7 EV" },
	{ 0xee,	"-3.0 EV" },
	EXIF_TEXT_LIST_END
};

static ExifTextList NikonTag2LensType[]= {
	{ 0,	"AF non D" },
	{ 1,	"manual" },
	{ 2,	"AF-D or AF-s" },
	{ 6,	"AF-D G" },
	{ 10,	"AF-D VR" },
	EXIF_TEXT_LIST_END
};

static ExifTextList NikonTag2FlashUsed[]= {
	{ 0,	"no" },
	{ 4,	"unit unknown" },
	{ 7,	"external" },
	{ 9,	"yes" },
	EXIF_TEXT_LIST_END
};


static ExifMarker NikonExifMarkersList2[] = {
{ 0x0002, EXIF_FORMAT_SHORT_UNSIGNED, 2,	"Nikon.ISOSpeed",	"ISO speed",	NULL },
{ 0x0003, EXIF_FORMAT_STRING, -1,		"Nikon.ColorMode",	"Color mode",	NULL },
{ 0x0004, EXIF_FORMAT_STRING, -1,		"Nikon.Quality",	"Quality",	NULL },
{ 0x0005, EXIF_FORMAT_STRING, -1,		"Nikon.WhiteBalance",	"White balance",NULL },
{ 0x0006, EXIF_FORMAT_STRING, -1,		"Nikon.Sharpening",	"Sharpening",	NULL },
{ 0x0007, EXIF_FORMAT_STRING, -1,		"Nikon.FocusMode",	"Focus mode",	NULL },
{ 0x0008, EXIF_FORMAT_STRING, -1,		"Nikon.FlashSetting",	"Flash setting",NULL },
{ 0x0009, EXIF_FORMAT_STRING, -1,		"Nikon.AutoFlashMode","Auto flash mode",NULL },
{ 0x000b, EXIF_FORMAT_SHORT, 1,			"Nikon.WhiteBalanceBias",
							"White balance bias value",	NULL },
/* { 0x000c, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.WhiteBalanceRB",
						"White balance red/blue coefficients",	NULL }, */
/* { 0x000f, EXIF_FORMAT_STRING, -1,		"Nikon.ISOSelect",	"ISO selection",NULL }, */
{ 0x0012, EXIF_FORMAT_UNDEFINED, 4,		"Nikon.FlashCompensation",
								"Flash compensation",	NikonTag2FlashComp },
{ 0x0013, EXIF_FORMAT_SHORT_UNSIGNED, 2,	"Nikon.ISOSpeedRequest",
								"ISO speed requested",	NULL },
{ 0x0016, EXIF_FORMAT_SHORT_UNSIGNED, 4,	"Nikon.CornerCoord",
								"Corner coordinates",	NULL },
{ 0x0018, EXIF_FORMAT_UNDEFINED, 4,		"Nikon.FlashBracketCompensation",
							"Flash bracket compensation",	NikonTag2FlashComp },
{ 0x0019, EXIF_FORMAT_RATIONAL, 1,		"Nikon.AEBracketCompensation",
							"AE bracket compensation",	NULL },
{ 0x0080, EXIF_FORMAT_STRING, -1,		"Nikon.ImageAdjustment",
								"Image adjustment",	NULL },
{ 0x0081, EXIF_FORMAT_STRING, -1,		"Nikon.Contrast",	"Contrast",	NULL },
{ 0x0082, EXIF_FORMAT_STRING, -1,		"Nikon.AuxLens", "Aux lens adapter",	NULL },
{ 0x0083, EXIF_FORMAT_BYTE_UNSIGNED, -1,	"Nikon.LensType",	"Lens type",	NikonTag2LensType },
{ 0x0084, EXIF_FORMAT_RATIONAL_UNSIGNED, -1,	"Nikon.LensFocalLength",
							"Lens min/max focal length and aperture", NULL },
{ 0x0085, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.ManualFocusDistance",
							"Manual focus distance", 	NULL },
{ 0x0086, EXIF_FORMAT_RATIONAL, 1,		"Nikon.DigitalZoomFactor",
							"Digital zoom factor",		NULL },
{ 0x0087, EXIF_FORMAT_BYTE_UNSIGNED, 1,		"Nikon.FlashUsed",	"Flash used",	NikonTag2FlashUsed },
{ 0x0088, EXIF_FORMAT_UNDEFINED, 4,		"Nikon.AutoFocusArea","Auto focus area",NULL },
/* { 0x0089, EXIF_FORMAT_SHORT_UNSIGNED, -1,	"Nikon.Bracket/ShootingMode", NULL,	NULL }, */
{ 0x008d, EXIF_FORMAT_STRING, -1,		"Nikon.ColorMode",	"Color mode",	NULL },
{ 0x008f, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.SceneMode",	"Scene mode",	NULL },
{ 0x0090, EXIF_FORMAT_STRING, -1,		"Nikon.LightingType",	"Lighting type",NULL },
{ 0x0092, EXIF_FORMAT_SHORT, 1,			"Nikon.HueAdjust",	"Hue adjustment",NULL },
/* { 0x0094, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Nikon.Saturation",	"Saturation",	NikonTag2Saturation }, */
{ 0x0095, EXIF_FORMAT_STRING, -1,		"Nikon.NoiseReduction", "Noise reduction", NULL },
{ 0x00a7, EXIF_FORMAT_LONG_UNSIGNED, 1,		"Nikon.ShutterCount", "Shutter release count", NULL },
{ 0x00a9, EXIF_FORMAT_STRING, -1,		"Nikon.ImageOptimization", "Image optimization", NULL },
{ 0x00aa, EXIF_FORMAT_STRING, -1,		"Nikon.Saturation", "Saturation",	NULL },
{ 0x00ab, EXIF_FORMAT_STRING, -1,		"Nikon.DigitalVariProg", "Digital Vari-program", NULL },
EXIF_MARKER_LIST_END
};

static ExifTextList NikonAFPoint[]= {
	{ 0,	"center" },
	{ 1,	"top" },
	{ 2,	"bottom" },
	{ 3,	"left" },
	{ 4,	"right" },
	EXIF_TEXT_LIST_END
};


gboolean format_nikon_makernote(ExifData *exif, guchar *tiff, guint offset,
			        guint size, ExifByteOrder bo)
{
	guchar *data;
	ExifItem *item;

	if (offset + 8 + 4 >= size) return FALSE;

	data = tiff + offset;

	/* Nikon tag format 1 */
	if (memcmp(data, "Nikon\x00\x01\x00", 8) == 0)
		{
		if (exif_parse_IFD_table(exif, tiff, offset + 8, size,
					 bo, 0, NikonExifMarkersList1) != 0)
			{
			return FALSE;
			}
		return TRUE;
		}

	/* Nikon tag format 2 uses Embedded tiff header */
	if (memcmp(data, "Nikon\x00\x02\x00\x00\x00", 10) == 0 ||
	    memcmp(data, "Nikon\x00\x02\x10\x00\x00", 10) == 0)
		{
		guint tiff_header;

		tiff_header = offset + 10;
		if (exif_tiff_parse(exif, tiff + tiff_header, size - tiff_header,
		    NikonExifMarkersList2) != 0)
			{
			return FALSE;
			}
		}
	/* Nikon tag format 3 uses format 2 tags without "Nikon" and tiff header */
	else if (exif_parse_IFD_table(exif, tiff, offset, size,
				      bo, 0, NikonExifMarkersList2) != 0)
		{
		return FALSE;
		}

	item = exif_get_item(exif, "Nikon.AutoFocusArea");
	if (item && item->data_len == 4 * sizeof(guchar))
		{
		static ExifMarker marker = { 0x0088, EXIF_FORMAT_STRING, -1,
					     "Nikon.AutoFocusPoint", "Auto focus point", NULL };
		guchar *array = (guchar*)item->data;
		gchar *text;
		gint l;

		text = exif_text_list_find_value(NikonAFPoint, (gint)array[1]);
		l = strlen(text) + 1;

		item = exif_item_new(marker.format, marker.tag, l, &marker);
		memcpy(item->data, text, l);

		g_free(text);

		exif->items = g_list_prepend(exif->items, item);
		}

	item = exif_get_item(exif, "Nikon.ISOSpeed");
	if (item && item->data_len == 2 * 2)
		{
		static ExifMarker marker = { 0x0002, EXIF_FORMAT_SHORT_UNSIGNED, 1,
					     "ISOSpeedRatings", "ISO speed", NULL };
		ExifItem *shadow;

		shadow = exif_item_new(marker.format, marker.tag, 1, &marker);
		memcpy(shadow->data, (char *)item->data + 2, 2);

		exif->items = g_list_prepend(exif->items, shadow);
		}

	item = exif_get_item(exif, "Nikon.WhiteBalance");
	if (item && item->format == EXIF_FORMAT_STRING)
		{
		static ExifMarker marker = { 0x0005, EXIF_FORMAT_STRING, -1,
					     "LightSource", "Light source", NULL };
		ExifItem *shadow;

		shadow = exif_item_new(marker.format, marker.tag, item->data_len, &marker);
		memcpy(shadow->data, item->data, item->data_len);

		exif->items = g_list_prepend(exif->items, shadow);
		}

	return TRUE;
}

#else
typedef int dummy_variable;
#endif
/* not HAVE_EXIV2 */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
