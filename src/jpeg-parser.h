/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#ifndef JPEG_PARSER_H
#define JPEG_PARSER_H

#include <string_view>
#include <vector>

#include <glib.h>

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

gboolean is_jpeg_container(const guchar *data, guint size);

struct JpegSegment
{
	guint offset = 0;
	guint length = 0;
};

bool jpeg_segment_find(const guchar *data, guint size,
                       guchar app_marker, std::string_view magic,
                       JpegSegment &seg);


struct MPOEntry {
	guint type_code;
	gboolean representative;
	gboolean dependent_child;
	gboolean dependent_parent;
	guint offset;
	guint length;
	guint dep1;
	guint dep2;

	guint MPFVersion;
	guint MPIndividualNum;
	guint PanOrientation;
	// double PanOverlap_H;
	// double PanOverlap_V;
	// guint BaseViewpointNum;
	// double ConvergenceAngle;
	// double BaselineLength;
	// double VerticalDivergence;
	// double AxisDistance_X;
	// double AxisDistance_Y;
	// double AxisDistance_Z;
	// double YawAngle;
	// double PitchAngle;
	// double RollAngle;
};


struct MPOData {
	guint mpo_offset;

	guint version;
	guint num_images;
	std::vector<MPOEntry> images;
};

MPOData jpeg_get_mpo_data(const guchar *data, guint size);

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
