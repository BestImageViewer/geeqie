/*
 * Copyright (C) 2003, 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Eric Swalens, Quy Tonthat
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
 *
 *  The tags were added with information from the FREE document:
 *     http://www.ba.wakwak.com/~tsuruzoh/Computer/Digicams/exif-e.html
 *
 *  For the official Exif Format, please refer to:
 *     http://www.exif.org
 *     http://www.exif.org/specifications.html (PDF spec sheets)
 *
 *  Notes:
 *     Additional tag formats should be added to the proper
 *     location in ExifKnownMarkersList[].
 *
 *     Human readable output (that needs additional processing of data to
 *     be useable) can be defined by adding a key to ExifFormattedList[],
 *     then handling that tag in the function exif_get_formatted_by_key().
 *     The human readable formatted keys must begin with the character 'f'.
 *
 *  Unsupported at this time:
 *     IFD1 (thumbnail)
 *     MakerNote
 *
 *  TODO:
 *     Convert data to useable form in the ??_as_text function for:
 *        ComponentsConfiguration
 *        UserComment (convert this to UTF-8?)
 *     Add support for marker tag 0x0000
 */

#include <config.h>

#ifndef HAVE_EXIV2

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <math.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "intl.h"

#include "main.h"
#include "exif-int.h"
#include "jpeg-parser.h"

#include "format-raw.h"
#include "ui-fileops.h"


/*
 *-----------------------------------------------------------------------------
 * Tag formats
 *-----------------------------------------------------------------------------
 */

ExifFormatAttrib ExifFormatList[] = {
	{ EXIF_FORMAT_UNKNOWN,		1, "unknown",	"unknown" },
	{ EXIF_FORMAT_BYTE_UNSIGNED,	1, "ubyte",	"unsigned byte" },
	{ EXIF_FORMAT_STRING,		1, "string",	"string" },
	{ EXIF_FORMAT_SHORT_UNSIGNED,	2, "ushort",	"unsigned short" },
	{ EXIF_FORMAT_LONG_UNSIGNED,	4, "ulong",	"unsigned long" },
	{ EXIF_FORMAT_RATIONAL_UNSIGNED,8, "urational",	"unsigned rational" },
	{ EXIF_FORMAT_BYTE,		1, "byte",	"byte" },
	{ EXIF_FORMAT_UNDEFINED,	1, "undefined",	"undefined" },
	{ EXIF_FORMAT_SHORT,		2, "sshort",	"signed short" },
	{ EXIF_FORMAT_LONG,		4, "slong",	"signed long" },
	{ EXIF_FORMAT_RATIONAL,		8, "srational",	"signed rational" },
	{ EXIF_FORMAT_FLOAT,		4, "float",	"float" },
	{ EXIF_FORMAT_DOUBLE,		8, "double",	"double" },
	{ -1, 0, NULL, NULL }
};

/* tags that are special, or need special treatment */
#define TAG_EXIFOFFSET          0x8769
#define TAG_EXIFMAKERNOTE	0x927c
#define TAG_GPSOFFSET		0x8825


/*
 *-----------------------------------------------------------------------------
 * Data
 *-----------------------------------------------------------------------------
 */
static ExifTextList ExifCompressionList[] = {
	{ 1, "Uncompressed" },
	{ 2, "CCITT 1D" },
	{ 3, "T4/Group 3 Fax" },
	{ 4, "T6/Group 4 Fax" },
	{ 5, "LZW" },
	{ 6, "JPEG (old style)" },
	{ 7, "JPEG" },
	{ 8, "Adobe Deflate" },
	{ 9, "JBIG B&W" },
	{ 10, "JBIG Color" },
	{ 32766, "Next" },
	{ 32771, "CCIRLEW" },
	{ 32773, "PackBits" },
	{ 32809, "ThunderScan" },
	{ 32895, "IT8CTPAD" },
	{ 32896, "IT8LW" },
	{ 32897, "IT8MP" },
	{ 32898, "IT8BL" },
	{ 32908, "PixasFilm" },
	{ 32909, "PixasLog" },
	{ 32946, "Deflate" },
	{ 32947, "DCS" },
	{ 34661, "JBIG" },
	{ 34676, "SGILog" },
	{ 34677, "SGILog24" },
	{ 34712, "JPEF 2000" },
	{ 34713, "Nikon NEF Compressed" },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifOrientationList[] = {
	{ EXIF_ORIENTATION_UNKNOWN,	N_("unknown") },
	{ EXIF_ORIENTATION_TOP_LEFT,	N_("top left") },
	{ EXIF_ORIENTATION_TOP_RIGHT,	N_("top right") },
	{ EXIF_ORIENTATION_BOTTOM_RIGHT,N_("bottom right") },
	{ EXIF_ORIENTATION_BOTTOM_LEFT,	N_("bottom left") },
	{ EXIF_ORIENTATION_LEFT_TOP,	N_("left top") },
	{ EXIF_ORIENTATION_RIGHT_TOP,	N_("right top") },
	{ EXIF_ORIENTATION_RIGHT_BOTTOM,N_("right bottom") },
	{ EXIF_ORIENTATION_LEFT_BOTTOM,	N_("left bottom") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifUnitList[] = {
	{ EXIF_UNIT_UNKNOWN,	N_("unknown") },
	{ EXIF_UNIT_NOUNIT,	"" },
	{ EXIF_UNIT_INCH,	N_("inch") },
	{ EXIF_UNIT_CENTIMETER,	N_("centimeter") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifYCbCrPosList[] = {
	{ 1,	"center" },
	{ 2,	"datum" },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifMeteringModeList[] = {
	{ 0,	N_("unknown") },
	{ 1,	N_("average") },
	{ 2,	N_("center weighted") },
	{ 3,	N_("spot") },
	{ 4,	N_("multi-spot") },
	{ 5,	N_("multi-segment") },
	{ 6,	N_("partial") },
	{ 255,	N_("other") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifExposureProgramList[] = {
	{ 0,	N_("not defined") },
	{ 1,	N_("manual") },
	{ 2,	N_("normal") },
	{ 3,	N_("aperture") },
	{ 4,	N_("shutter") },
	{ 5,	N_("creative") },
	{ 6,	N_("action") },
	{ 7,	N_("portrait") },
	{ 8,	N_("landscape") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifLightSourceList[] = {
	{ 0,	N_("unknown") },
	{ 1,	N_("daylight") },
	{ 2,	N_("fluorescent") },
	{ 3,	N_("tungsten (incandescent)") },
	{ 4,	N_("flash") },
	{ 9,	N_("fine weather") },
	{ 10,	N_("cloudy weather") },
	{ 11,	N_("shade") },
	{ 12,	N_("daylight fluorescent") },
	{ 13,	N_("day white fluorescent") },
	{ 14,	N_("cool white fluorescent") },
	{ 15,	N_("white fluorescent") },
	{ 17,	N_("standard light A") },
	{ 18,	N_("standard light B") },
	{ 19,	N_("standard light C") },
	{ 20,	N_("D55") },
	{ 21,	N_("D65") },
	{ 22,	N_("D75") },
	{ 23,	N_("D50") },
	{ 24,	N_("ISO studio tungsten") },
	{ 255,	N_("other") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifFlashList[] = {
	{ 0,	N_("no") },
	{ 1,	N_("yes") },
	{ 5,	N_("yes, not detected by strobe") },
	{ 7,	N_("yes, detected by strobe") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifColorSpaceList[] = {
	{ 1,	N_("sRGB") },
	{ 65535,N_("uncalibrated") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifSensorList[] = {
	{ 1,	N_("not defined") },
	{ 2,	N_("1 chip color area") },
	{ 2,	N_("2 chip color area") },
	{ 4,	N_("3 chip color area") },
	{ 5,	N_("color sequential area") },
	{ 7,	N_("trilinear") },
	{ 8,	N_("color sequential linear") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifSourceList[] = {
	{ 3,	N_("digital still camera") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifSceneList[] = {
	{ 1,	N_("direct photo") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifCustRenderList[] = {
	{ 0,	N_("normal") },
	{ 1,	N_("custom") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifExposureModeList[] = {
	{ 0,	N_("auto") },
	{ 1,	N_("manual") },
	{ 2,	N_("auto bracket") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifWhiteBalanceList[] = {
	{ 0,	N_("auto") },
	{ 1,	N_("manual") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifSceneCaptureList[] = {
	{ 0,	N_("standard") },
	{ 1,	N_("landscape") },
	{ 2,	N_("portrait") },
	{ 3,	N_("night scene") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifGainControlList[] = {
	{ 0,	N_("none") },
	{ 1,	N_("low gain up") },
	{ 2,	N_("high gain up") },
	{ 3,	N_("low gain down") },
	{ 4,	N_("high gain down") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifContrastList[] = {
	{ 0,	N_("normal") },
	{ 1,	N_("soft") },
	{ 2,	N_("hard") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifSaturationList[] = {
	{ 0,	N_("normal") },
	{ 1,	N_("low") },
	{ 2,	N_("high") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifSharpnessList[] = {
	{ 0,	N_("normal") },
	{ 1,	N_("soft") },
	{ 2,	N_("hard") },
	EXIF_TEXT_LIST_END
};

static ExifTextList ExifSubjectRangeList[] = {
	{ 0,	N_("unknown") },
	{ 1,	N_("macro") },
	{ 2,	N_("close") },
	{ 3,	N_("distant") },
	EXIF_TEXT_LIST_END
};

/*
Tag names should match to exiv2 keys, https://www.exiv2.org/metadata.html
Tags that don't match are not supported by exiv2 and should not be used anywhere in the code
*/

ExifMarker ExifKnownMarkersList[] = {
{ 0x0100, EXIF_FORMAT_LONG_UNSIGNED, 1,		"Exif.Image.ImageWidth",	N_("Image Width"), NULL },
{ 0x0101, EXIF_FORMAT_LONG_UNSIGNED, 1,		"Exif.Image.ImageLength",	N_("Image Height"), NULL },
{ 0x0102, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Image.BitsPerSample",	N_("Bits per Sample/Pixel"), NULL },
{ 0x0103, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Image.Compression",	N_("Compression"), ExifCompressionList },
{ 0x010e, EXIF_FORMAT_STRING, -1,		"Exif.Image.ImageDescription",	N_("Image description"), NULL },
{ 0x010f, EXIF_FORMAT_STRING, -1,		"Exif.Image.Make",		N_("Camera make"), NULL },
{ 0x0110, EXIF_FORMAT_STRING, -1,		"Exif.Image.Model",		N_("Camera model"), NULL },
{ 0x0112, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Image.Orientation",	N_("Orientation"), ExifOrientationList },
{ 0x011a, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Image.XResolution",	N_("X resolution"), NULL },
{ 0x011b, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Image.YResolution",	N_("Y Resolution"), NULL },
{ 0x0128, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Image.ResolutionUnit",	N_("Resolution units"), ExifUnitList },
{ 0x0131, EXIF_FORMAT_STRING, -1, 		"Exif.Image.Software",		N_("Firmware"), NULL },
{ 0x0132, EXIF_FORMAT_STRING, 20,		"Exif.Image.DateTime",		N_("Date"), NULL },
{ 0x013e, EXIF_FORMAT_RATIONAL_UNSIGNED, 2,	"Exif.Image.WhitePoint",	N_("White point"), NULL },
{ 0x013f, EXIF_FORMAT_RATIONAL_UNSIGNED, 6,	"Exif.Image.PrimaryChromaticities",N_("Primary chromaticities"), NULL },
{ 0x0211, EXIF_FORMAT_RATIONAL_UNSIGNED, 3,	"Exif.Image.YCbCrCoefficients",	N_("YCbCy coefficients"), NULL },
{ 0x0213, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Image.YCbCrPositioning",	N_("YCbCr positioning"), ExifYCbCrPosList },
{ 0x0214, EXIF_FORMAT_RATIONAL_UNSIGNED, 6, 	"Exif.Image.ReferenceBlackWhite",N_("Black white reference"), NULL },
{ 0x8298, EXIF_FORMAT_STRING, -1,		"Exif.Image.Copyright",		N_("Copyright"), NULL },
{ 0x8769, EXIF_FORMAT_LONG_UNSIGNED, 1,		"Exif.Image.ExifTag",		N_("SubIFD Exif offset"), NULL },
	/* subIFD follows */
{ 0x829a, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.ExposureTime",	N_("Exposure time (seconds)"), NULL },
{ 0x829d, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.FNumber",		N_("FNumber"), NULL },
{ 0x8822, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.ExposureProgram",	N_("Exposure program"), ExifExposureProgramList },
{ 0x8824, EXIF_FORMAT_STRING, -1,		"Exif.Photo.SpectralSensitivity",N_("Spectral Sensitivity"), NULL },
{ 0x8827, EXIF_FORMAT_SHORT_UNSIGNED, -1,	"Exif.Photo.ISOSpeedRatings",	N_("ISO sensitivity"), NULL },
{ 0x8828, EXIF_FORMAT_UNDEFINED, -1,		"Exif.Photo.OECF",		N_("Optoelectric conversion factor"), NULL },
{ 0x9000, EXIF_FORMAT_UNDEFINED, 4,		"Exif.Photo.ExifVersion",	N_("Exif version"), NULL },
{ 0x9003, EXIF_FORMAT_STRING, 20,		"Exif.Photo.DateTimeOriginal",	N_("Date original"), NULL },
{ 0x9004, EXIF_FORMAT_STRING, 20,		"Exif.Photo.DateTimeDigitized",	N_("Date digitized"), NULL },
{ 0x9101, EXIF_FORMAT_UNDEFINED, -1,		"Exif.Photo.ComponentsConfiguration",N_("Pixel format"), NULL },
{ 0x9102, EXIF_FORMAT_RATIONAL_UNSIGNED,1,	"Exif.Photo.CompressedBitsPerPixel",N_("Compression ratio"), NULL },
{ 0x9201, EXIF_FORMAT_RATIONAL, 1,		"Exif.Photo.ShutterSpeedValue",	N_("Shutter speed"), NULL },
{ 0x9202, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.ApertureValue",	N_("Aperture"), NULL },
{ 0x9203, EXIF_FORMAT_RATIONAL, 1,		"Exif.Photo.BrightnessValue",	N_("Brightness"), NULL },
{ 0x9204, EXIF_FORMAT_RATIONAL, 1,		"Exif.Photo.ExposureBiasValue",	N_("Exposure bias"), NULL },
{ 0x9205, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.MaxApertureValue",	N_("Maximum aperture"), NULL },
{ 0x9206, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.SubjectDistance",	N_("Subject distance"), NULL },
{ 0x9207, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.MeteringMode",	N_("Metering mode"), ExifMeteringModeList },
{ 0x9208, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.LightSource",	N_("Light source"), ExifLightSourceList },
{ 0x9209, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.Flash",		N_("Flash"), ExifFlashList },
{ 0x920a, EXIF_FORMAT_RATIONAL_UNSIGNED, 1, 	"Exif.Photo.FocalLength",	N_("Focal length"), NULL },
{ 0x9214, EXIF_FORMAT_SHORT_UNSIGNED, -1,	"Exif.Photo.SubjectArea",	N_("Subject area"), NULL },
{ 0x927c, EXIF_FORMAT_UNDEFINED, -1,		"Exif.Photo.MakerNote",		N_("MakerNote"), NULL },
{ 0x9286, EXIF_FORMAT_UNDEFINED, -1, 		"Exif.Photo.UserComment",	N_("UserComment"), NULL },
{ 0x9290, EXIF_FORMAT_STRING, -1,		"Exif.Photo.SubSecTime",	N_("Subsecond time"), NULL },
{ 0x9291, EXIF_FORMAT_STRING, -1,		"Exif.Photo.SubSecTimeOriginal",N_("Subsecond time original"), NULL },
{ 0x9292, EXIF_FORMAT_STRING, -1,		"Exif.Photo.SubSecTimeDigitized",N_("Subsecond time digitized"), NULL },
{ 0xa000, EXIF_FORMAT_UNDEFINED, 4,		"Exif.Photo.FlashpixVersion",	N_("FlashPix version"), NULL },
{ 0xa001, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.ColorSpace",	N_("Colorspace"), ExifColorSpaceList },
	/* ExifImageWidth, ExifImageHeight can also be unsigned short */
{ 0xa002, EXIF_FORMAT_LONG_UNSIGNED, 1,		"Exif.Photo.PixelXDimension",	N_("Width"), NULL },
{ 0xa003, EXIF_FORMAT_LONG_UNSIGNED, 1,		"Exif.Photo.PixelYDimension",	N_("Height"), NULL },
{ 0xa004, EXIF_FORMAT_STRING, -1,		"Exif.Photo.RelatedSoundFile",	N_("Audio data"), NULL },
{ 0xa005, EXIF_FORMAT_LONG_UNSIGNED, 1,		"ExifInteroperabilityOffset",   N_("ExifR98 extension"), NULL },
{ 0xa20b, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.FlashEnergy",	N_("Flash strength"), NULL },
{ 0xa20c, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.SpatialFrequencyResponse",N_("Spatial frequency response"), NULL },
{ 0xa20e, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.FocalPlaneXResolution", N_("X Pixel density"), NULL },
{ 0xa20f, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.FocalPlaneYResolution", N_("Y Pixel density"), NULL },
{ 0xa210, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.FocalPlaneResolutionUnit", N_("Pixel density units"), ExifUnitList },
{ 0x0214, EXIF_FORMAT_SHORT_UNSIGNED, 2,	"Exif.Photo.SubjectLocation",	N_("Subject location"), NULL },
{ 0xa215, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.ExposureIndex",	N_("ISO sensitivity"), NULL },
{ 0xa217, EXIF_FORMAT_SHORT_UNSIGNED, -1,	"Exif.Photo.SensingMethod",	N_("Sensor type"), ExifSensorList },
{ 0xa300, EXIF_FORMAT_UNDEFINED, 1,		"Exif.Photo.FileSource",	N_("Source type"), ExifSourceList },
{ 0xa301, EXIF_FORMAT_UNDEFINED, 1,		"Exif.Photo.SceneType",		N_("Scene type"), ExifSceneList },
{ 0xa302, EXIF_FORMAT_UNDEFINED, -1,		"Exif.Image.CFAPattern",	N_("Color filter array pattern"), NULL },
	/* tags a4xx were added for Exif 2.2 (not just these - some above, as well) */
{ 0xa401, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.CustomRendered",	N_("Render process"), ExifCustRenderList },
{ 0xa402, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.ExposureMode",	N_("Exposure mode"), ExifExposureModeList },
{ 0xa403, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.WhiteBalance",	N_("White balance"), ExifWhiteBalanceList },
{ 0xa404, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.DigitalZoomRatio",	N_("Digital zoom ratio"), NULL },
{ 0xa405, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.FocalLengthIn35mmFilm",N_("Focal length (35mm)"), NULL },
{ 0xa406, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.SceneCaptureType",	N_("Scene capture type"), ExifSceneCaptureList },
{ 0xa407, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.GainControl",	N_("Gain control"), ExifGainControlList },
{ 0xa408, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.Contrast",		N_("Contrast"), ExifContrastList },
{ 0xa409, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.Saturation",	N_("Saturation"), ExifSaturationList },
{ 0xa40a, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.Sharpness",		N_("Sharpness"), ExifSharpnessList },
{ 0xa40b, EXIF_FORMAT_UNDEFINED, -1,		"Exif.Photo.DeviceSettingDescription",N_("Device setting"), NULL },
{ 0xa40c, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Exif.Photo.SubjectDistanceRange",N_("Subject range"), ExifSubjectRangeList },
{ 0xa420, EXIF_FORMAT_STRING, -1,		"Exif.Photo.ImageUniqueID",	N_("Image serial number"), NULL },
	/* place known, but undocumented or lesser used tags here */
{ 0x00fe, EXIF_FORMAT_LONG_UNSIGNED, 1,		"Exif.Image.NewSubfileType",	NULL, NULL },
{ 0x00ff, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"SubfileType",			NULL, NULL },
{ 0x012d, EXIF_FORMAT_SHORT_UNSIGNED, 3,	"Exif.Image.TransferFunction",	NULL, NULL },
{ 0x013b, EXIF_FORMAT_STRING, -1,		"Exif.Image.Artist",		"Artist", NULL },
{ 0x013d, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Predictor",		NULL, NULL },
{ 0x0142, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"TileWidth",		NULL, NULL },
{ 0x0143, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"TileLength",		NULL, NULL },
{ 0x0144, EXIF_FORMAT_LONG_UNSIGNED, -1,	"TileOffsets",		NULL, NULL },
{ 0x0145, EXIF_FORMAT_SHORT_UNSIGNED, -1,	"TileByteCounts",	NULL, NULL },
{ 0x014a, EXIF_FORMAT_LONG_UNSIGNED, -1,	"Exif.Image.SubIFDs",		NULL, NULL },
{ 0x015b, EXIF_FORMAT_UNDEFINED, -1,		"JPEGTables",		NULL, NULL },
{ 0x828d, EXIF_FORMAT_SHORT_UNSIGNED, 2,	"Exif.Image.CFARepeatPatternDim",	NULL, NULL },
{ 0x828e, EXIF_FORMAT_BYTE_UNSIGNED, -1,	"Exif.Image.CFAPattern",		NULL, NULL },
{ 0x828f, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Image.BatteryLevel",		NULL, NULL },
{ 0x83bb, EXIF_FORMAT_LONG_UNSIGNED, -1,	"IPTC/NAA",		NULL, NULL },
{ 0x8773, EXIF_FORMAT_UNDEFINED, -1,		"Exif.Image.InterColorProfile",		NULL, NULL },
{ 0x8825, EXIF_FORMAT_LONG_UNSIGNED, 1,		"GPSInfo",		"SubIFD GPS offset", NULL },
{ 0x8829, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"Interlace",		NULL, NULL },
{ 0x882a, EXIF_FORMAT_SHORT, 1,			"TimeZoneOffset",	NULL, NULL },
{ 0x882b, EXIF_FORMAT_SHORT_UNSIGNED, 1,	"SelfTimerMode",	NULL, NULL },
{ 0x920b, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.FlashEnergy",		NULL, NULL },
{ 0x920c, EXIF_FORMAT_UNDEFINED, -1,		"Exif.Photo.SpatialFrequencyResponse", NULL, NULL },
{ 0x920d, EXIF_FORMAT_UNDEFINED, -1,		"Noise",		NULL, NULL },
{ 0x9211, EXIF_FORMAT_LONG_UNSIGNED, 1,		"ImageNumber",		NULL, NULL },
{ 0x9212, EXIF_FORMAT_STRING, 1,		"SecurityClassification", NULL, NULL },
{ 0x9213, EXIF_FORMAT_STRING, -1,		"ImageHistory",		NULL, NULL },
{ 0x9215, EXIF_FORMAT_RATIONAL_UNSIGNED, 1,	"Exif.Photo.ExposureIndex",	NULL, NULL },
{ 0x9216, EXIF_FORMAT_BYTE_UNSIGNED, 4,		"TIFF/EPStandardID",	NULL, NULL },

EXIF_MARKER_LIST_END
};

ExifMarker ExifKnownGPSInfoMarkersList[] = {
        /* The following do not work at the moment as the tag value 0x0000 has a
         * special meaning. */
        /* { 0x0000, EXIF_FORMAT_BYTE, -1, "Exif.GPSInfo.GPSVersionID", NULL, NULL }, */
        { 0x0001, EXIF_FORMAT_STRING, 2, "Exif.GPSInfo.GPSLatitudeRef", NULL, NULL },
        { 0x0002, EXIF_FORMAT_RATIONAL_UNSIGNED, 3, "Exif.GPSInfo.GPSLatitude", NULL, NULL },
        { 0x0003, EXIF_FORMAT_STRING, 2, "Exif.GPSInfo.GPSLongitudeRef", NULL, NULL },
        { 0x0004, EXIF_FORMAT_RATIONAL_UNSIGNED, 3, "Exif.GPSInfo.GPSLongitude", NULL, NULL },
        { 0x0005, EXIF_FORMAT_BYTE_UNSIGNED, 1, "Exif.GPSInfo.GPSAltitudeRef", NULL, NULL },
        { 0x0006, EXIF_FORMAT_RATIONAL_UNSIGNED, 1, "Exif.GPSInfo.GPSAltitude", NULL, NULL },
        { 0x0007, EXIF_FORMAT_RATIONAL_UNSIGNED, 3, "Exif.GPSInfo.GPSTimeStamp", NULL, NULL },
        { 0x0008, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSSatellites", NULL, NULL },
        { 0x0009, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSStatus", NULL, NULL },
        { 0x000a, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSMeasureMode", NULL, NULL },
        { 0x000b, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSDOP", NULL, NULL },
        { 0x000c, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSSpeedRef", NULL, NULL },
        { 0x000d, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSSpeed", NULL, NULL },
        { 0x000e, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSTrackRef", NULL, NULL },
        { 0x000f, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSTrack", NULL, NULL },
        { 0x0010, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSImgDirectionRef", NULL, NULL },
        { 0x0011, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSImgDirection", NULL, NULL },
        { 0x0012, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSMapDatum", NULL, NULL },
        { 0x0013, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSDestLatitudeRef", NULL, NULL },
        { 0x0014, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSDestLatitude", NULL, NULL },
        { 0x0015, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSDestLongitudeRef", NULL, NULL },
        { 0x0016, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSDestLongitude", NULL, NULL },
        { 0x0017, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSDestBearingRef", NULL, NULL },
        { 0x0018, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSDestBearing", NULL, NULL },
        { 0x0019, EXIF_FORMAT_STRING, -1, "Exif.GPSInfo.GPSDestDistanceRef", NULL, NULL },
        { 0x001a, EXIF_FORMAT_RATIONAL_UNSIGNED, -1, "Exif.GPSInfo.GPSDestDistance", NULL, NULL },
        { 0x001b, EXIF_FORMAT_UNDEFINED, -1, "Exif.GPSInfo.GPSProcessingMethod", NULL, NULL },
        { 0x001c, EXIF_FORMAT_UNDEFINED, -1, "Exif.GPSInfo.GPSAreaInformation", NULL, NULL },
        { 0x001d, EXIF_FORMAT_RATIONAL_UNSIGNED, 3, "Exif.GPSInfo.GPSDateStamp", NULL, NULL },
        { 0x001e, EXIF_FORMAT_SHORT, -1, "Exif.GPSInfo.GPSDifferential", NULL, NULL },

        EXIF_MARKER_LIST_END
};

ExifMarker ExifUnknownMarkersList[] = {
{ 0x0000, EXIF_FORMAT_UNKNOWN, 0,		"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_BYTE_UNSIGNED, -1,	"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_STRING, -1,		"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_SHORT_UNSIGNED, -1,	"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_LONG_UNSIGNED, -1,	"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_RATIONAL_UNSIGNED, -1,	"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_BYTE, -1,			"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_UNDEFINED, -1,		"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_SHORT, -1,		"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_LONG, -1,			"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_RATIONAL, -1,		"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_FLOAT, -1,		"unknown",	NULL, NULL },
{ 0x0000, EXIF_FORMAT_DOUBLE, -1,		"unknown",	NULL, NULL },
};

static const ExifMarker *exif_marker_from_tag(guint16 tag, const ExifMarker *list);

/*
 *-----------------------------------------------------------------------------
 * ExifItem
 *-----------------------------------------------------------------------------
 */

ExifItem *exif_item_new(ExifFormatType format, guint tag,
			guint elements, const ExifMarker *marker)
{
	ExifItem *item;

	item = g_new0(ExifItem, 1);
	item->format = format;
	item->tag = tag;
	item->marker = marker;
	item->elements = elements;

	switch (format)
		{
		case EXIF_FORMAT_UNKNOWN:
			/* unknown, data is NULL */
			return item;
			break;
		case EXIF_FORMAT_BYTE_UNSIGNED:
			item->data_len = sizeof(gchar) * elements;
			break;
		case EXIF_FORMAT_STRING:
			item->data_len = sizeof(gchar) * elements;
			break;
		case EXIF_FORMAT_SHORT_UNSIGNED:
			item->data_len = sizeof(guint16) * elements;
			break;
		case EXIF_FORMAT_LONG_UNSIGNED:
			item->data_len = sizeof(guint32) * elements;
			break;
		case EXIF_FORMAT_RATIONAL_UNSIGNED:
			item->data_len = sizeof(ExifRational) * elements;
			break;
		case EXIF_FORMAT_BYTE:
			item->data_len = sizeof(gchar) * elements;
			break;
		case EXIF_FORMAT_UNDEFINED:
			item->data_len = sizeof(gchar) * elements;
			break;
		case EXIF_FORMAT_SHORT:
			item->data_len = sizeof(gint16) * elements;
			break;
		case EXIF_FORMAT_LONG:
			item->data_len = sizeof(gint32) * elements;
			break;
		case EXIF_FORMAT_RATIONAL:
			item->data_len = sizeof(ExifRational) * elements;
			break;
		case EXIF_FORMAT_FLOAT:
			item->data_len = sizeof(float) * elements;
			break;
		case EXIF_FORMAT_DOUBLE:
			item->data_len = sizeof(gdouble) * elements;
			break;
		}

	item->data = g_malloc0(item->data_len);

	return item;
}

static void exif_item_free(ExifItem *item)
{
	if (!item) return;

	g_free(item->data);
	g_free(item);
}

gchar *exif_item_get_tag_name(ExifItem *item)
{
	if (!item || !item->marker) return NULL;
	return g_strdup(item->marker->key);
}

guint exif_item_get_tag_id(ExifItem *item)
{
	if (!item) return 0;
	return item->tag;
}

guint exif_item_get_elements(ExifItem *item)
{
	if (!item) return 0;
	return item->elements;
}

gchar *exif_item_get_data(ExifItem *item, guint *data_len)
{
	if (data_len)
		*data_len = item->data_len;
	return g_memdup(item->data, item->data_len);
}

guint exif_item_get_format_id(ExifItem *item)
{
	if (!item) return EXIF_FORMAT_UNKNOWN;
	return item->format;
}


gchar *exif_item_get_description(ExifItem *item)
{
	if (!item || !item->marker) return NULL;
	return g_strdup(_(item->marker->description));
}

const gchar *exif_item_get_format_name(ExifItem *item, gboolean brief)
{
	if (!item || !item->marker) return NULL;
	return (brief) ? ExifFormatList[item->format].short_name : ExifFormatList[item->format].description;
}

static GString *string_append_raw_bytes(GString *string, gpointer data, gint ne)
{
	gint i;

	for (i = 0 ; i < ne; i++)
		{
		guchar c = ((gchar *)data)[i];
		if (c < 32 || c > 127) c = '.';
		g_string_append_printf(string, "%c", c);
		}
	string = g_string_append(string, " : ");
	for (i = 0 ; i < ne; i++)
		{
		const gchar *spacer;
		if (i > 0)
			{
			if (i%8 == 0)
				{
				spacer = " - ";
				}
			else
				{
				spacer = " ";
				}
			}
		else
			{
			spacer = "";
			}
		g_string_append_printf(string, "%s%02x", spacer, ((gchar *)data)[i]);
		}

	return string;
}


gchar *exif_text_list_find_value(ExifTextList *list, guint value)
{
	gchar *result = NULL;
	gint i;

	i = 0;
	while (!result && list[i].value >= 0)
		{
		if (value == (guint) list[i].value) result = g_strdup(_(list[i].description));
		i++;
		}
	if (!result) result = g_strdup_printf("%d (%s)", value, _("unknown"));

	return result;
}


/*
 *-------------------------------------------------------------------
 * byte order utils
 *-------------------------------------------------------------------
 */

/* note: the align_buf is used to avoid alignment issues (on sparc) */

guint16 exif_byte_get_int16(guchar *f, ExifByteOrder bo)
{
	guint16 align_buf;

	memcpy(&align_buf, f, sizeof(guint16));

	if (bo == EXIF_BYTE_ORDER_INTEL)
		return GUINT16_FROM_LE(align_buf);
	else
		return GUINT16_FROM_BE(align_buf);
}

guint32 exif_byte_get_int32(guchar *f, ExifByteOrder bo)
{
	guint32 align_buf;

	memcpy(&align_buf, f, sizeof(guint32));

	if (bo == EXIF_BYTE_ORDER_INTEL)
		return GUINT32_FROM_LE(align_buf);
	else
		return GUINT32_FROM_BE(align_buf);
}

void exif_byte_put_int16(guchar *f, guint16 n, ExifByteOrder bo)
{
	guint16 align_buf;

	if (bo == EXIF_BYTE_ORDER_INTEL)
		{
		align_buf = GUINT16_TO_LE(n);
		}
	else
		{
		align_buf = GUINT16_TO_BE(n);
		}

	memcpy(f, &align_buf, sizeof(guint16));
}

void exif_byte_put_int32(guchar *f, guint32 n, ExifByteOrder bo)
{
	guint32 align_buf;

	if (bo == EXIF_BYTE_ORDER_INTEL)
		{
		align_buf = GUINT32_TO_LE(n);
		}
	else
		{
		align_buf = GUINT32_TO_BE(n);
		}

	memcpy(f, &align_buf, sizeof(guint32));
}


/*
 *-------------------------------------------------------------------
 * IFD utils
 *-------------------------------------------------------------------
 */

static const ExifMarker *exif_marker_from_tag(guint16 tag, const ExifMarker *list)
{
	gint i = 0;

	if (!list) return NULL;

	while (list[i].tag != 0 && list[i].tag != tag)
		{
		i++;
		}

	return (list[i].tag == 0 ? NULL : &list[i]);
}

static void rational_from_data(ExifRational *r, gpointer src, ExifByteOrder bo)
{
	r->num = exif_byte_get_int32(src, bo);
	r->den = exif_byte_get_int32((guchar *)src + sizeof(guint32), bo);
}

/* src_format and item->format must be compatible
 * and not overrun src or item->data.
 */
void exif_item_copy_data(ExifItem *item, gpointer src, guint len,
			 ExifFormatType src_format, ExifByteOrder bo)
{
	gint bs;
	gint ne;
	gpointer dest;
	gint i;

	bs = ExifFormatList[item->format].size;
	ne = item->elements;
	dest = item->data;

	if (!dest ||
	    ExifFormatList[src_format].size * ne > len)
		{
		gchar *tag = exif_item_get_tag_name(item);
		log_printf("exif tag %s data size mismatch\n", tag);
		g_free(tag);
		return;
		}

	switch (item->format)
		{
		case EXIF_FORMAT_UNKNOWN:
			break;
		case EXIF_FORMAT_BYTE_UNSIGNED:
		case EXIF_FORMAT_BYTE:
		case EXIF_FORMAT_UNDEFINED:
			memcpy(dest, src, len);
			break;
		case EXIF_FORMAT_STRING:
			memcpy(dest, src, len);
			/* string is NULL terminated, make sure this is true */
			if (((gchar *)dest)[len - 1] != '\0') ((gchar *)dest)[len - 1] = '\0';
			break;
		case EXIF_FORMAT_SHORT_UNSIGNED:
		case EXIF_FORMAT_SHORT:
			for (i = 0; i < ne; i++)
				{
				((guint16 *)dest)[i] = exif_byte_get_int16((guchar *)src + i * bs, bo);
				}
			break;
		case EXIF_FORMAT_LONG_UNSIGNED:
		case EXIF_FORMAT_LONG:
			if (src_format == EXIF_FORMAT_SHORT_UNSIGNED ||
			    src_format == EXIF_FORMAT_SHORT)
				{
				/* a short fits into a long, so allow it */
				gint ss;

				ss = ExifFormatList[src_format].size;
				for (i = 0; i < ne; i++)
					{
					((gint32 *)dest)[i] =
						(gint32)exif_byte_get_int16((guchar *)src + i * ss, bo);
					}
				}
			else
				{
				for (i = 0; i < ne; i++)
					{
					((gint32 *)dest)[i] =
						exif_byte_get_int32((guchar *)src + i * bs, bo);
					}
				}
			break;
		case EXIF_FORMAT_RATIONAL_UNSIGNED:
		case EXIF_FORMAT_RATIONAL:
			for (i = 0; i < ne; i++)
				{
				rational_from_data(&((ExifRational *)dest)[i], (guchar *)src + i * bs, bo);
				}
			break;
		case EXIF_FORMAT_FLOAT:
			for (i = 0; i < ne; i++)
				{
				((float *)dest)[i] = exif_byte_get_int32((guchar *)src + i * bs, bo);
				}
			break;
		case EXIF_FORMAT_DOUBLE:
			for (i = 0; i < ne; i++)
				{
				ExifRational r;

				rational_from_data(&r, (guchar *)src + i * bs, bo);
				if (r.den) ((gdouble *)dest)[i] = (gdouble)r.num / r.den;
				}
			break;
		}
}

static gint exif_parse_IFD_entry(ExifData *exif, guchar *tiff, guint offset,
				 guint size, ExifByteOrder bo,
				 gint level,
				 const ExifMarker *list)
{
	guint tag;
	guint format;
	guint count;
	guint data_val;
	guint data_offset;
	guint data_length;
	const ExifMarker *marker;
	ExifItem *item;

	tag = exif_byte_get_int16(tiff + offset + EXIF_TIFD_OFFSET_TAG, bo);
	format = exif_byte_get_int16(tiff + offset + EXIF_TIFD_OFFSET_FORMAT, bo);
	count = exif_byte_get_int32(tiff + offset + EXIF_TIFD_OFFSET_COUNT, bo);
	data_val = exif_byte_get_int32(tiff + offset + EXIF_TIFD_OFFSET_DATA, bo);

	/* Check tag type. If it does not match, either the format is wrong,
	 * either it is a unknown tag; so it is not really an error.
	 */
	marker = exif_marker_from_tag(tag, list);
	if (!marker)
		{
		if (format >= EXIF_FORMAT_COUNT)
			{
			log_printf("warning: exif tag 0x%4x has invalid format %d\n", tag, format);
			return 0;
			}
		/* allow non recognized tags to be displayed */
		marker = &ExifUnknownMarkersList[format];
		}
	if (marker->format != format)
		{
		/* Some cameras got mixed up signed/unsigned_rational
		 * eg KODAK DC4800 on object_distance tag
		 *
		 * FIXME: what exactly is this test trying to do?
		 * ok, so this test is to allow the case of swapped signed/unsigned mismatch to leak through?
		 */
		if (!(marker->format == EXIF_FORMAT_RATIONAL_UNSIGNED && format == EXIF_FORMAT_RATIONAL) &&
		    !(marker->format == EXIF_FORMAT_RATIONAL && format == EXIF_FORMAT_RATIONAL_UNSIGNED) &&
			/* short fits into a long so allow this mismatch
			 * as well (some tags allowed to be unsigned short _or_ unsigned long)
			 */
		    !(marker->format == EXIF_FORMAT_LONG_UNSIGNED && format == EXIF_FORMAT_SHORT_UNSIGNED) )
			{
			if (format < EXIF_FORMAT_COUNT)
				{
				log_printf("warning: exif tag %s format mismatch, found %s exif spec requests %s\n",
					marker->key, ExifFormatList[format].short_name,
					ExifFormatList[marker->format].short_name);
				}
			else
				{
				log_printf("warning: exif tag %s format mismatch, found unknown id %d exif spec requests %d (%s)\n",
					marker->key, format, marker->format,
					ExifFormatList[marker->format].short_name);
				}
			return 0;
			}
		}

	/* Where is the data, is it available?
	 */
	if (marker->components > 0 && (guint) marker->components != count)
		{
		log_printf("warning: exif tag %s has %d elements, exif spec requests %d\n",
			marker->key, count, marker->components);
		}

	data_length = ExifFormatList[marker->format].size * count;
	if (data_length > 4)
		{
		data_offset = data_val;
		if (size < data_offset || size < data_offset + data_length)
			{
			log_printf("warning: exif tag %s data will overrun end of file, ignored.\n", marker->key);
			return -1;
			}
		}
	else
		{
		data_offset = offset + EXIF_TIFD_OFFSET_DATA;
		}

	item = exif_item_new(marker->format, tag, count, marker);
	exif_item_copy_data(item, tiff + data_offset, data_length, format, bo);
	exif->items = g_list_prepend(exif->items, item);

	if (list == ExifKnownMarkersList)
		{
		switch (item->tag)
			{
			case TAG_EXIFOFFSET:
				exif_parse_IFD_table(exif, tiff, data_val, size, bo, level + 1, list);
				break;
			case TAG_GPSOFFSET:
				exif_parse_IFD_table(exif, tiff, data_val, size, bo, level + 1, ExifKnownGPSInfoMarkersList);
				break;
			case TAG_EXIFMAKERNOTE:
				format_exif_makernote_parse(exif, tiff, data_val, size, bo);
				break;
			}
		}

	return 0;
}

gint exif_parse_IFD_table(ExifData *exif,
			  guchar *tiff, guint offset,
			  guint size, ExifByteOrder bo,
			  gint level,
			  const ExifMarker *list)
{
	guint count;
	guint i;

	/* limit damage from infinite loops */
	if (level > EXIF_TIFF_MAX_LEVELS) return -1;

	/* We should be able to read number of entries in IFD0) */
	if (size < offset + 2) return -1;

	count = exif_byte_get_int16(tiff + offset, bo);
	offset += 2;

	/* Entries and next IFD offset must be readable */
	if (size < offset + count * EXIF_TIFD_SIZE + 4) return -1;

	for (i = 0; i < count; i++)
		{
		exif_parse_IFD_entry(exif, tiff, offset + i * EXIF_TIFD_SIZE, size, bo, level, list);
		}

	return 0;
}

/*
 *-------------------------------------------------------------------
 * file formats
 *-------------------------------------------------------------------
 */

gint exif_tiff_directory_offset(guchar *data, const guint len,
				guint *offset, ExifByteOrder *bo)
{
	if (len < 8) return FALSE;

	if (memcmp(data, "II", 2) == 0)
		{
		*bo = EXIF_BYTE_ORDER_INTEL;
		}
	else if (memcmp(data, "MM", 2) == 0)
		{
		*bo = EXIF_BYTE_ORDER_MOTOROLA;
		}
	else
		{
		return FALSE;
		}

	if (exif_byte_get_int16(data + 2, *bo) != 0x002A)
		{
		return FALSE;
		}

	*offset = exif_byte_get_int32(data + 4, *bo);

	return (*offset < len);
}

gint exif_tiff_parse(ExifData *exif, guchar *tiff, guint size, ExifMarker *list)
{
	ExifByteOrder bo;
	guint offset;

	if (!exif_tiff_directory_offset(tiff, size, &offset, &bo)) return -1;

	return exif_parse_IFD_table(exif, tiff, offset, size, bo, 0, list);
}


/*
 *-------------------------------------------------------------------
 * jpeg marker utils
 *-------------------------------------------------------------------
 */

#define JPEG_MARKER		0xFF
#define JPEG_MARKER_SOI		0xD8
#define JPEG_MARKER_EOI		0xD9
#define JPEG_MARKER_APP1	0xE1
#define JPEG_MARKER_APP2	0xE2

/* jpeg container format:
     all data markers start with 0XFF
     2 byte long file start and end markers: 0xFFD8(SOI) and 0XFFD9(EOI)
     4 byte long data segment markers in format: 0xFFTTSSSSNNN...
       FF:   1 byte standard marker identifier
       TT:   1 byte data type
       SSSS: 2 bytes in Motorola byte alignment for length of the data.
	     This value includes these 2 bytes in the count, making actual
	     length of NN... == SSSS - 2.
       NNN.: the data in this segment
 */
static ExifMarker jpeg_color_marker = { 0x8773, EXIF_FORMAT_UNDEFINED, -1, "Exif.Image.InterColorProfile", NULL, NULL };

void exif_add_jpeg_color_profile(ExifData *exif, guchar *cp_data, guint cp_length)
{
	ExifItem *item = exif_item_new(jpeg_color_marker.format, jpeg_color_marker.tag, 1,
				     &jpeg_color_marker);
	g_free(item->data);
	item->data = cp_data;
	item->elements = cp_length;
	item->data_len = cp_length;
	exif->items = g_list_prepend(exif->items, item);

}

static gint exif_jpeg_parse(ExifData *exif,
			    guchar *data, guint size,
			    ExifMarker *list)
{
	guint seg_offset = 0;
	guint seg_length = 0;
	gint res = -1;

	if (size < 4 ||
	    memcmp(data, "\xFF\xD8", 2) != 0)
		{
		return -2;
		}

	if (jpeg_segment_find(data, size, JPEG_MARKER_APP1,
				   "Exif\x00\x00", 6,
				   &seg_offset, &seg_length))
		{
		res = exif_tiff_parse(exif, data + seg_offset + 6, seg_length - 6, list);
		}

	if (exif_jpeg_parse_color(exif, data, size))
		{
		res = 0;
		}

	return res;
}

guchar *exif_get_color_profile(ExifData *exif, guint *data_len)
{
	ExifItem *prof_item = exif_get_item(exif, "Exif.Image.InterColorProfile");
	if (prof_item && exif_item_get_format_id(prof_item) == EXIF_FORMAT_UNDEFINED)
		return (guchar *) exif_item_get_data(prof_item, data_len);
	return NULL;
}


gchar* exif_get_image_comment(FileData* UNUSED(fd))
{
	log_printf("%s", _("Can't get image comment: not compiled with Exiv2.\n"));
	return g_strdup("");
}

void exif_set_image_comment(FileData* UNUSED(fd), const gchar* UNUSED(comment))
{
	log_printf("%s", _("Can't set image comment: not compiled with Exiv2.\n"));
}


/*
 *-------------------------------------------------------------------
 * misc
 *-------------------------------------------------------------------
 */


ExifItem *exif_get_first_item(ExifData *exif)
{
	if (exif->items)
		{
		ExifItem *ret = (ExifItem *)exif->items->data;
		exif->current = exif->items->next;
		return ret;
		}
	exif->current = NULL;
	return NULL;
}

ExifItem *exif_get_next_item(ExifData *exif)
{
	if (exif->current)
		{
		ExifItem *ret = (ExifItem *)exif->current->data;
		exif->current = exif->current->next;
		return ret;
		}
	return NULL;
}

static gint map_file(const gchar *path, void **mapping, gint *size)
{
	gint fd;
	struct stat fs;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		{
		perror(path);
		return -1;
		}

	if (fstat(fd, &fs) == -1)
		{
		perror(path);
		close(fd);
		return -1;
		}

	*size = fs.st_size;

	*mapping = mmap(0, *size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (*mapping == MAP_FAILED)
		{
		perror(path);
		close(fd);
		return -1;
		}

	close(fd);
	return 0;
}

static gint unmap_file(gpointer mapping, gint size)
{
	if (munmap(mapping, size) == -1)
		{
		perror("munmap");
		return -1;
		}

	return 0;
}

ExifData *exif_get_original(ExifData *processed)
{
	return processed;
}

void exif_free(ExifData *exif)
{
	GList *work;

	if (!exif) return;

	work = exif->items;
	while (work)
		{
		ExifItem *item = work->data;
		work = work->next;
		exif_item_free(item);
		}

	g_list_free(exif->items);
	g_free(exif->path);
	g_free(exif);
}

ExifData *exif_read(gchar *path, gchar *UNUSED(sidecar_path), GHashTable *UNUSED(modified_xmp))
{
	ExifData *exif;
	gpointer f;
	gint size, res;
	gchar *pathl;

	if (!path) return NULL;

	pathl = path_from_utf8(path);
	if (map_file(pathl, &f, &size) == -1)
		{
		g_free(pathl);
		return NULL;
		}
	g_free(pathl);

	exif = g_new0(ExifData, 1);
	exif->path = g_strdup(path);

	res = exif_jpeg_parse(exif, (guchar *)f, size, ExifKnownMarkersList);
	if (res == -2)
		{
		res = exif_tiff_parse(exif, (guchar *)f, size, ExifKnownMarkersList);
		}

	if (res != 0)
		{
		FormatRawExifType exif_type;
		FormatRawExifParseFunc exif_parse_func;
		guint32 offset = 0;

		exif_type = format_raw_exif_offset(f, size, &offset, &exif_parse_func);
		switch (exif_type)
			{
			case FORMAT_RAW_EXIF_NONE:
			default:
				break;
			case FORMAT_RAW_EXIF_TIFF:
				res = exif_tiff_parse(exif, (guchar *)f + offset, size - offset,
						      ExifKnownMarkersList);
				break;
			case FORMAT_RAW_EXIF_JPEG:
				res = exif_jpeg_parse(exif, (guchar *)f + offset, size - offset,
						      ExifKnownMarkersList);
				break;
			case FORMAT_RAW_EXIF_IFD_II:
			case FORMAT_RAW_EXIF_IFD_MM:
				res = exif_parse_IFD_table(exif, (guchar *)f, offset, size - offset,
							   (exif_type == FORMAT_RAW_EXIF_IFD_II) ?
								EXIF_BYTE_ORDER_INTEL : EXIF_BYTE_ORDER_MOTOROLA,
							   0, ExifKnownMarkersList);
				break;
			case FORMAT_RAW_EXIF_PROPRIETARY:
				if (exif_parse_func)
					{
					res = exif_parse_func((guchar *)f + offset, size - offset, exif);
					}
				break;
			}
		}

	if (res != 0)
		{
		exif_free(exif);
		exif = NULL;
		}

	unmap_file(f, size);

	if (exif) exif->items = g_list_reverse(exif->items);

	return exif;
}

ExifItem *exif_get_item(ExifData *exif, const gchar *key)
{
	GList *work;

	if (!key) return NULL;

	work = exif->items;
	while (work)
		{
		ExifItem *item;

		item = work->data;
		work = work->next;
		if (item->marker->key && strcmp(key, item->marker->key) == 0) return item;
		}
	return NULL;
}

#define EXIF_DATA_AS_TEXT_MAX_COUNT 16


static gchar *exif_item_get_data_as_text_full(ExifItem *item, MetadataFormat format)
{
	const ExifMarker *marker;
	gpointer data;
	GString *string;
	gchar *text;
	gint ne;
	gint i;

	if (!item) return NULL;

	marker = item->marker;
	if (!marker) return NULL;

	data = item->data;
	ne = item->elements;
	if (ne > EXIF_DATA_AS_TEXT_MAX_COUNT) ne = EXIF_DATA_AS_TEXT_MAX_COUNT;
	string = g_string_new("");
	switch (item->format)
		{
		case EXIF_FORMAT_UNKNOWN:
			break;
		case EXIF_FORMAT_BYTE_UNSIGNED:
		case EXIF_FORMAT_BYTE:
		case EXIF_FORMAT_UNDEFINED:
			if (ne == 1 && marker->list && format == METADATA_FORMATTED)
				{
				gchar *result;
				guchar val;

				if (item->format == EXIF_FORMAT_BYTE_UNSIGNED ||
				    item->format == EXIF_FORMAT_UNDEFINED)
					{
					val = ((guchar *)data)[0];
					}
				else
					{
					val = (guchar)(((gchar *)data)[0]);
					}

				result = exif_text_list_find_value(marker->list, (guint)val);
				string = g_string_append(string, result);
				g_free(result);
				}
			else
				{
				string = string_append_raw_bytes(string, data, ne);
				}
			break;
		case EXIF_FORMAT_STRING:
			if (item->data) string = g_string_append(string, (gchar *)(item->data));
			break;
		case EXIF_FORMAT_SHORT_UNSIGNED:
			if (ne == 1 && marker->list && format == METADATA_FORMATTED)
				{
				gchar *result;

				result = exif_text_list_find_value(marker->list, ((guint16 *)data)[0]);
				string = g_string_append(string, result);
				g_free(result);
				}
			else for (i = 0; i < ne; i++)
				{
				g_string_append_printf(string, "%s%hd", (i > 0) ? ", " : "",
							((guint16 *)data)[i]);
				}
			break;
		case EXIF_FORMAT_LONG_UNSIGNED:
			for (i = 0; i < ne; i++)
				{
				g_string_append_printf(string, "%s%ld", (i > 0) ? ", " : "",
							(gulong)((guint32 *)data)[i]);
				}
			break;
		case EXIF_FORMAT_RATIONAL_UNSIGNED:
			for (i = 0; i < ne; i++)
				{
				ExifRational *r;

				r = &((ExifRational *)data)[i];
				g_string_append_printf(string, "%s%ld/%ld", (i > 0) ? ", " : "",
							(gulong)r->num, (gulong)r->den);
				}
			break;
		case EXIF_FORMAT_SHORT:
			for (i = 0; i < ne; i++)
				{
				g_string_append_printf(string, "%s%hd", (i > 0) ? ", " : "",
							((gint16 *)data)[i]);
				}
			break;
		case EXIF_FORMAT_LONG:
			for (i = 0; i < ne; i++)
				{
				g_string_append_printf(string, "%s%ld", (i > 0) ? ", " : "",
							(glong)((gint32 *)data)[i]);
				}
			break;
		case EXIF_FORMAT_RATIONAL:
			for (i = 0; i < ne; i++)
				{
				ExifRational *r;

				r = &((ExifRational *)data)[i];
				g_string_append_printf(string, "%s%ld/%ld", (i > 0) ? ", " : "",
							(glong)r->num, (glong)r->den);
				}
			break;
		case EXIF_FORMAT_FLOAT:
			for (i = 0; i < ne; i++)
				{
				g_string_append_printf(string, "%s%f", (i > 0) ? ", " : "",
							((float *)data)[i]);
				}
			break;
		case EXIF_FORMAT_DOUBLE:
			for (i = 0; i < ne; i++)
				{
				g_string_append_printf(string, "%s%f", (i > 0) ? ", " : "",
							((gdouble *)data)[i]);
				}
			break;
		}

	if (item->elements > EXIF_DATA_AS_TEXT_MAX_COUNT &&
	    item->format != EXIF_FORMAT_STRING)
		{
		g_string_append(string, " ...");
		}

	text = string->str;
	g_string_free(string, FALSE);

	return text;
}

gchar *exif_item_get_string(ExifItem *item, gint UNUSED(idx))
{
	return exif_item_get_data_as_text_full(item, METADATA_PLAIN);
}

gchar *exif_item_get_data_as_text(ExifItem *item)
{
	return exif_item_get_data_as_text_full(item, METADATA_FORMATTED);
}

gint exif_item_get_integer(ExifItem *item, gint *value)
{
	if (!item) return FALSE;
	if (!item->elements) return FALSE;

	switch (item->format)
		{
		case EXIF_FORMAT_SHORT:
			*value = (gint)(((gint16 *)(item->data))[0]);
			return TRUE;
			break;
		case EXIF_FORMAT_SHORT_UNSIGNED:
			*value = (gint)(((guint16 *)(item->data))[0]);
			return TRUE;
			break;
		case EXIF_FORMAT_LONG:
			*value = (gint)(((gint32 *)(item->data))[0]);
			return TRUE;
			break;
		case EXIF_FORMAT_LONG_UNSIGNED: /**< @FIXME overflow possible */
			*value = (gint)(((guint32 *)(item->data))[0]);
			return TRUE;
		default:
			/* all other type return FALSE */
			break;
		}
	return FALSE;
}


ExifRational *exif_item_get_rational(ExifItem *item, gint *sign, guint n)
{
	if (!item) return NULL;
	if (n >= item->elements) return NULL;

	if (item->format == EXIF_FORMAT_RATIONAL ||
	    item->format == EXIF_FORMAT_RATIONAL_UNSIGNED)
		{
		if (sign) *sign = (item->format == EXIF_FORMAT_RATIONAL);
		return &((ExifRational *)(item->data))[n];
		}

	return NULL;
}

gchar *exif_get_tag_description_by_key(const gchar *key)
{
	gint i;

	if (!key) return NULL;

	i = 0;
	while (ExifKnownMarkersList[i].tag > 0)
		{
		if (strcmp(key, ExifKnownMarkersList[i].key) == 0) return g_strdup(_(ExifKnownMarkersList[i].description));
		i++;
		}

	i = 0;
	while (ExifKnownGPSInfoMarkersList[i].tag > 0)
	{
	   if (strcmp(key, ExifKnownGPSInfoMarkersList[i].key) == 0) return g_strdup(_(ExifKnownGPSInfoMarkersList[i].description));
	   i++;
	}

	return NULL;
}

static void exif_write_item(FILE *f, ExifItem *item)
{
	gchar *text;

	text = exif_item_get_data_as_text(item);
	if (text)
		{
		gchar *tag = exif_item_get_tag_name(item);
		g_fprintf(f, "%4x %9s %30s %s\n", item->tag, ExifFormatList[item->format].short_name,
			tag, text);
		g_free(tag);
		}
	g_free(text);
}

void exif_write_data_list(ExifData *exif, FILE *f, gint human_readable_list)
{
	if (!f || !exif) return;

	g_fprintf(f, " tag   format                             key value\n");
	g_fprintf(f, "----------------------------------------------------\n");

	if (human_readable_list)
		{
		gint i;

		i = 0;
		while (ExifFormattedList[i].key)
			{
			gchar *text;

			text = exif_get_formatted_by_key(exif, ExifFormattedList[i].key, NULL);
			if (text)
				{
				g_fprintf(f, "     %9s %30s %s\n", "string", ExifFormattedList[i].key, text);
				}
			i++;
			}
		}
	else
		{
		GList *work;

		work = exif->items;
		while (work)
			{
			ExifItem *item;

			item = work->data;
			work = work->next;

			exif_write_item(f, item);
			}
		}
	g_fprintf(f, "----------------------------------------------------\n");
}

gboolean exif_write(ExifData *UNUSED(exif))
{
	log_printf("Not compiled with EXIF write support\n");
	return FALSE;
}

gboolean exif_write_sidecar(ExifData *UNUSED(exif), gchar *UNUSED(path))
{
	log_printf("Not compiled with EXIF write support\n");
	return FALSE;
}


gint exif_update_metadata(ExifData *UNUSED(exif), const gchar *UNUSED(key), const GList *UNUSED(values))
{
	return 0;
}

GList *exif_get_metadata(ExifData *exif, const gchar *key, MetadataFormat format)
{
	gchar *str;
	ExifItem *item;

	if (!key) return NULL;

	/* convert xmp key to exif key */
	if (strcmp(key, "Xmp.tiff.Orientation") == 0) key = "Exif.Image.Orientation";

	if (format == METADATA_FORMATTED)
		{
		gchar *text;
		gint key_valid;
		text = exif_get_formatted_by_key(exif, key, &key_valid);
		if (key_valid) return g_list_append(NULL, text);
		}

	item = exif_get_item(exif, key);
	if (!item) return NULL;

	str = exif_item_get_data_as_text_full(item, format);

	if (!str) return NULL;

	return g_list_append(NULL, str);
}

typedef struct _UnmapData UnmapData;
struct _UnmapData
{
	guchar *ptr;
	guchar *map_data;
	size_t map_len;
};

static GList *exif_unmap_list = 0;

guchar *exif_get_preview(ExifData *exif, guint *data_len, gint UNUSED(requested_width), gint UNUSED(requested_height))
{
	guint offset;
	const gchar* path;
	struct stat st;
	guchar *map_data;
	size_t map_len;
	int fd;

	if (!exif) return NULL;
	path = exif->path;

	fd = open(path, O_RDONLY);


	if (fd == -1)
		{
		return 0;
		}

	if (fstat(fd, &st) == -1)
		{
		close(fd);
		return 0;
		}
	map_len = st.st_size;
	map_data = (guchar *) mmap(0, map_len, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (map_data == MAP_FAILED)
		{
		return 0;
		}

	if (format_raw_img_exif_offsets(map_data, map_len, &offset, NULL) && offset)
		{
		UnmapData *ud;

		DEBUG_1("%s: offset %u", path, offset);

		*data_len = map_len - offset;
		ud = g_new(UnmapData, 1);
		ud->ptr = map_data + offset;
		ud->map_data = map_data;
		ud->map_len = map_len;

		exif_unmap_list = g_list_prepend(exif_unmap_list, ud);
		return ud->ptr;
		}

	munmap(map_data, map_len);
	return NULL;

}

void exif_free_preview(guchar *buf)
{
	GList *work = exif_unmap_list;

	while (work)
		{
		UnmapData *ud = (UnmapData *)work->data;
		if (ud->ptr == buf)
			{
			exif_unmap_list = g_list_remove_link(exif_unmap_list, work);
			g_free(ud);
			return;
			}
		work = work->next;
		}
	g_assert_not_reached();
}

void exif_init(void)
{
}
#else
typedef int dummy_variable;
#endif
/* not HAVE_EXIV2 */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
