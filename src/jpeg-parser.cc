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

#include "main.h"
#include "jpeg-parser.h"

gboolean jpeg_segment_find(const guchar *data, guint size,
			    guchar app_marker, const gchar *magic, guint magic_len,
			    guint *seg_offset, guint *seg_length)
{
	guchar marker = 0;
	guint offset = 0;
	guint length = 0;

	while (marker != JPEG_MARKER_EOI)
		{
		offset += length;
		length = 2;

		if (offset + 2 >= size ||
		    data[offset] != JPEG_MARKER) return FALSE;

		marker = data[offset + 1];
		if (marker != JPEG_MARKER_SOI &&
		    marker != JPEG_MARKER_EOI)
			{
			if (offset + 4 >= size) return FALSE;
			length += (static_cast<guint>(data[offset + 2]) << 8) + data[offset + 3];

			if (marker == app_marker &&
			    offset + length < size &&
			    length >= 4 + magic_len &&
			    memcmp(data + offset + 4, magic, magic_len) == 0)
				{
				*seg_offset = offset + 4;
				*seg_length = length - 4;
				return TRUE;
				}
			}
		}
	return FALSE;
}


typedef enum {
	TIFF_BYTE_ORDER_INTEL,
	TIFF_BYTE_ORDER_MOTOROLA
} TiffByteOrder;

#define TIFF_TIFD_OFFSET_TAG 0
#define TIFF_TIFD_OFFSET_FORMAT 2
#define TIFF_TIFD_OFFSET_COUNT 4
#define TIFF_TIFD_OFFSET_DATA 8
#define TIFF_TIFD_SIZE 12



guint16 tiff_byte_get_int16(const guchar *f, TiffByteOrder bo)
{
	guint16 align_buf;

	memcpy(&align_buf, f, sizeof(guint16));

	if (bo == TIFF_BYTE_ORDER_INTEL)
		return GUINT16_FROM_LE(align_buf);
	else
		return GUINT16_FROM_BE(align_buf);
}

guint32 tiff_byte_get_int32(const guchar *f, TiffByteOrder bo)
{
	guint32 align_buf;

	memcpy(&align_buf, f, sizeof(guint32));

	if (bo == TIFF_BYTE_ORDER_INTEL)
		return GUINT32_FROM_LE(align_buf);
	else
		return GUINT32_FROM_BE(align_buf);
}

//void tiff_byte_put_int16(guchar *f, guint16 n, TiffByteOrder bo)
//{
	//guint16 align_buf;

	//if (bo == TIFF_BYTE_ORDER_INTEL)
		//{
		//align_buf = GUINT16_TO_LE(n);
		//}
	//else
		//{
		//align_buf = GUINT16_TO_BE(n);
		//}

	//memcpy(f, &align_buf, sizeof(guint16));
//}

//void tiff_byte_put_int32(guchar *f, guint32 n, TiffByteOrder bo)
//{
	//guint32 align_buf;

	//if (bo == TIFF_BYTE_ORDER_INTEL)
		//{
		//align_buf = GUINT32_TO_LE(n);
		//}
	//else
		//{
		//align_buf = GUINT32_TO_BE(n);
		//}

	//memcpy(f, &align_buf, sizeof(guint32));
//}

gint tiff_directory_offset(const guchar *data, const guint len,
				guint *offset, TiffByteOrder *bo)
{
	if (len < 8) return FALSE;

	if (memcmp(data, "II", 2) == 0)
		{
		*bo = TIFF_BYTE_ORDER_INTEL;
		}
	else if (memcmp(data, "MM", 2) == 0)
		{
		*bo = TIFF_BYTE_ORDER_MOTOROLA;
		}
	else
		{
		return FALSE;
		}

	if (tiff_byte_get_int16(data + 2, *bo) != 0x002A)
		{
		return FALSE;
		}

	*offset = tiff_byte_get_int32(data + 4, *bo);

	return (*offset < len);
}

typedef gint (* FuncParseIFDEntry)(const guchar *tiff, guint offset,
				 guint size, TiffByteOrder bo,
				 gpointer data);


gint tiff_parse_IFD_table(const guchar *tiff, guint offset,
			  guint size, TiffByteOrder bo,
			  guint *next_offset,
			  FuncParseIFDEntry parse_entry, gpointer data)
{
	guint count;
	guint i;
	guint next;


	/* We should be able to read number of entries in IFD0) */
	if (size < offset + 2) return -1;

	count = tiff_byte_get_int16(tiff + offset, bo);
	offset += 2;
	/* Entries and next IFD offset must be readable */
	if (size < offset + count * TIFF_TIFD_SIZE + 4) return -1;

	for (i = 0; i < count; i++)
		{
		parse_entry(tiff, offset + i * TIFF_TIFD_SIZE, size, bo, data);
		}

	next = tiff_byte_get_int32(tiff + offset + count * TIFF_TIFD_SIZE, bo);
	if (next_offset) *next_offset = next;

	return 0;
}

static gint mpo_parse_Index_IFD_entry(const guchar *tiff, guint offset,
				 guint size, TiffByteOrder bo,
				 gpointer data)
{
	guint tag;
	guint format;
	guint count;
	guint data_val;
	guint data_offset;
	guint data_length;

	auto mpo = static_cast<MPOData *>(data);

	tag = tiff_byte_get_int16(tiff + offset + TIFF_TIFD_OFFSET_TAG, bo);
	format = tiff_byte_get_int16(tiff + offset + TIFF_TIFD_OFFSET_FORMAT, bo);
	count = tiff_byte_get_int32(tiff + offset + TIFF_TIFD_OFFSET_COUNT, bo);
	data_val = tiff_byte_get_int32(tiff + offset + TIFF_TIFD_OFFSET_DATA, bo);
	DEBUG_1("   tag %x format %x count %x data_val %x", tag, format, count, data_val);

	if (tag == 0xb000)
		{
		mpo->version = data_val;
		DEBUG_1("    mpo version %x", mpo->version);
		}
	else if (tag == 0xb001)
		{
		mpo->num_images = data_val;
		DEBUG_1("    num images %x", mpo->num_images);
		}
	else if (tag == 0xb002)
		{
		guint i;
		data_offset = data_val;
		data_length = count;
		if (size < data_offset || size < data_offset + data_length)
			{
			return -1;
			}
		if (count != mpo->num_images * 16)
			{
			return -1;
			}

		mpo->images = g_new0(MPOEntry, mpo->num_images);

		for (i = 0; i < mpo->num_images; i++) {
			guint image_attr = tiff_byte_get_int32(tiff + data_offset + i * 16, bo);
			mpo->images[i].type_code = image_attr & 0xffffff;
			mpo->images[i].representative = !!(image_attr & 0x20000000);
			mpo->images[i].dependent_child = !!(image_attr & 0x40000000);
			mpo->images[i].dependent_parent = !!(image_attr & 0x80000000);
			mpo->images[i].length = tiff_byte_get_int32(tiff + data_offset + i * 16 + 4, bo);
			mpo->images[i].offset = tiff_byte_get_int32(tiff + data_offset + i * 16 + 8, bo);
			mpo->images[i].dep1 = tiff_byte_get_int16(tiff + data_offset + i * 16 + 12, bo);
			mpo->images[i].dep2 = tiff_byte_get_int16(tiff + data_offset + i * 16 + 14, bo);

			if (i == 0)
				{
				mpo->images[i].offset = 0;
				}
			else
				{
				mpo->images[i].offset += mpo->mpo_offset;
				}

			DEBUG_1("   image %x %x %x", image_attr, mpo->images[i].length, mpo->images[i].offset);
			}
		}

	return 0;
}

static gint mpo_parse_Attributes_IFD_entry(const guchar *tiff, guint offset,
				 guint UNUSED(size), TiffByteOrder bo,
				 gpointer data)
{
	guint tag;
	guint format;
	guint count;
	guint data_val;

	auto mpe = static_cast<MPOEntry *>(data);

	tag = tiff_byte_get_int16(tiff + offset + TIFF_TIFD_OFFSET_TAG, bo);
	format = tiff_byte_get_int16(tiff + offset + TIFF_TIFD_OFFSET_FORMAT, bo);
	count = tiff_byte_get_int32(tiff + offset + TIFF_TIFD_OFFSET_COUNT, bo);
	data_val = tiff_byte_get_int32(tiff + offset + TIFF_TIFD_OFFSET_DATA, bo);
	DEBUG_1("   tag %x format %x count %x data_val %x", tag, format, count, data_val);

	switch (tag)
		{
		case 0xb000:
			mpe->MPFVersion = data_val;
			DEBUG_1("    mpo version %x", data_val);
			break;
		case 0xb101:
			mpe->MPIndividualNum = data_val;
			DEBUG_1("    Individual Image Number %x", mpe->MPIndividualNum);
			break;
		case 0xb201:
			mpe->PanOrientation = data_val;
			break;
/**

@FIXME
Panorama Scanning Orientation PanOrientation 45569 B201 LONG 1 \n
Panorama Horizontal Overlap PanOverlap_H 45570 B202 RATIONAL 1 \n
Panorama Vertical Overlap PanOverlap_V 45571 B203 RATIONAL 1 \n
Base Viewpoint Number BaseViewpointNum 45572 B204 LONG 1 \n
Convergence Angle ConvergenceAngle 45573 B205 SRATIONAL 1 \n
Baseline Length BaselineLength 45574 B206 RATIONAL 1 \n
Divergence Angle VerticalDivergence 45575 B207 SRATIONAL 1 \n
Horizontal Axis Distance AxisDistance_X 45576 B208 SRATIONAL 1 \n
Vertical Axis Distance AxisDistance_Y 45577 B209 SRATIONAL 1 \n
Collimation Axis Distance AxisDistance_Z 45578 B20A SRATIONAL 1 \n
Yaw Angle YawAngle 45579 B20B SRATIONAL 1 \n
Pitch Angle PitchAngle 45580 B20C SRATIONAL 1 \n
Roll Angle RollAngle 45581 B20D
  */
		default:
			break;
		}

	return 0;
}

MPOData *jpeg_get_mpo_data(const guchar *data, guint size)
{
	guint seg_offset;
	guint seg_size;
	if (jpeg_segment_find(data, size, JPEG_MARKER_APP2, "MPF\x00", 4, &seg_offset, &seg_size) && seg_size >16)
		{
		guint offset;
		guint next_offset;
		TiffByteOrder bo;
		MPOData *mpo;
		guint i;

		DEBUG_1("mpo signature found at %x", seg_offset);
		seg_offset += 4;
		seg_size -= 4;

		if (!tiff_directory_offset(data + seg_offset, seg_size, &offset, &bo)) return NULL;

		mpo = g_new0(MPOData, 1);
		mpo->mpo_offset = seg_offset;

		tiff_parse_IFD_table(data + seg_offset,  offset , seg_size, bo, &next_offset, mpo_parse_Index_IFD_entry, (gpointer)mpo);
		if (!mpo->images) mpo->num_images = 0;


		for (i = 0; i < mpo->num_images; i++)
			{
			if (mpo->images[i].offset + mpo->images[i].length > size)
				{
				mpo->num_images = i;
				DEBUG_1("MPO file truncated to %d valid images, %d %d", i, mpo->images[i].offset + mpo->images[i].length, size);
				break;
				}
			}

		for (i = 0; i < mpo->num_images; i++)
			{
			if (i == 0)
				{
				offset = next_offset;
				}
			else
				{
				if (!jpeg_segment_find(data + mpo->images[i].offset, mpo->images[i].length, JPEG_MARKER_APP2, "MPF\x00", 4, &seg_offset, &seg_size) || seg_size <=16)
					{
					DEBUG_1("MPO image %d: MPO signature not found", i);
					continue;
					}

				seg_offset += 4;
				seg_size -= 4;
				if (!tiff_directory_offset(data + mpo->images[i].offset + seg_offset, seg_size, &offset, &bo))
					{
					DEBUG_1("MPO image %d: invalid directory offset", i);
					continue;
					}

				}
			tiff_parse_IFD_table(data + mpo->images[i].offset + seg_offset,  offset , seg_size, bo, NULL, mpo_parse_Attributes_IFD_entry, (gpointer)&mpo->images[i]);
			}

		return mpo;
		}
	return NULL;
}

void jpeg_mpo_data_free(MPOData *mpo)
{
	if (mpo)
		{
		if (mpo->images) g_free(mpo->images);
		g_free(mpo);
		}
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
