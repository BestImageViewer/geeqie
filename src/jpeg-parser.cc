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

#include "jpeg-parser.h"

#include <algorithm>
#include <cstring>
#include <functional>

namespace
{

enum TiffByteOrder {
	TIFF_BYTE_ORDER_INTEL,
	TIFF_BYTE_ORDER_MOTOROLA
};

constexpr guint TIFF_TIFD_OFFSET_TAG = 0;
constexpr guint TIFF_TIFD_OFFSET_FORMAT = 2;
constexpr guint TIFF_TIFD_OFFSET_COUNT = 4;
constexpr guint TIFF_TIFD_OFFSET_DATA = 8;
constexpr guint TIFF_TIFD_SIZE = 12;

guint16 tiff_byte_get_int16(const guchar *tiff, TiffByteOrder bo)
{
	guint16 align_buf;

	memcpy(&align_buf, tiff, sizeof(guint16));

	if (bo == TIFF_BYTE_ORDER_INTEL)
		return GUINT16_FROM_LE(align_buf);

	return GUINT16_FROM_BE(align_buf);
}

guint32 tiff_byte_get_int32(const guchar *tiff, TiffByteOrder bo)
{
	guint32 align_buf;

	memcpy(&align_buf, tiff, sizeof(guint32));

	if (bo == TIFF_BYTE_ORDER_INTEL)
		return GUINT32_FROM_LE(align_buf);

	return GUINT32_FROM_BE(align_buf); // NOLINT
}

struct TiffTag
{
	TiffTag(const guchar *tiff, TiffByteOrder bo)
	    : tag(tiff_byte_get_int16(tiff + TIFF_TIFD_OFFSET_TAG, bo))
	    , format(tiff_byte_get_int16(tiff + TIFF_TIFD_OFFSET_FORMAT, bo))
	    , count(tiff_byte_get_int32(tiff + TIFF_TIFD_OFFSET_COUNT, bo))
	    , data_val(tiff_byte_get_int32(tiff + TIFF_TIFD_OFFSET_DATA, bo))
	{}

	guint tag;
	guint format;
	guint count;
	guint data_val;
};

gboolean tiff_directory_offset(const guchar *data, const guint len,
                               guint &offset, TiffByteOrder &bo)
{
	if (len < 8) return FALSE;

	if (memcmp(data, "II", 2) == 0)
		{
		bo = TIFF_BYTE_ORDER_INTEL;
		}
	else if (memcmp(data, "MM", 2) == 0)
		{
		bo = TIFF_BYTE_ORDER_MOTOROLA;
		}
	else
		{
		return FALSE;
		}

	if (tiff_byte_get_int16(data + 2, bo) != 0x002A)
		{
		return FALSE;
		}

	offset = tiff_byte_get_int32(data + 4, bo);

	return offset < len;
}

using ParseIFDEntryFunc = std::function<gint(const guchar *, guint, TiffByteOrder)>;

gint tiff_parse_IFD_table(const guchar *tiff, guint offset, guint size, TiffByteOrder bo,
                          const ParseIFDEntryFunc &parse_entry,
                          guint *next_offset = nullptr)
{
	/* We should be able to read number of entries in IFD0) */
	if (size < offset + 2) return -1;

	guint count = tiff_byte_get_int16(tiff + offset, bo);
	offset += 2;
	/* Entries and next IFD offset must be readable */
	if (size < offset + count * TIFF_TIFD_SIZE + 4) return -1;

	for (guint i = 0; i < count; i++)
		{
		parse_entry(tiff, offset + (i * TIFF_TIFD_SIZE), bo);
		}

	if (next_offset)
		{
		*next_offset = tiff_byte_get_int32(tiff + offset + (count * TIFF_TIFD_SIZE), bo);
		}

	return 0;
}

gint mpo_parse_Index_IFD_entry(const guchar *tiff, guint offset, guint size, TiffByteOrder bo,
                               MPOData &mpo)
{
	const TiffTag tt{tiff + offset, bo};
	DEBUG_1("   tag %x format %x count %x data_val %x", tt.tag, tt.format, tt.count, tt.data_val);

	if (tt.tag == 0xb000)
		{
		mpo.version = tt.data_val;
		DEBUG_1("    mpo version %x", mpo.version);
		}
	else if (tt.tag == 0xb001)
		{
		mpo.num_images = tt.data_val;
		DEBUG_1("    num images %x", mpo.num_images);
		}
	else if (tt.tag == 0xb002)
		{
		guint data_offset = tt.data_val;
		guint data_length = tt.count;
		if (size < data_offset || size < data_offset + data_length)
			{
			return -1;
			}
		if (tt.count != mpo.num_images * 16)
			{
			return -1;
			}

		mpo.images.reserve(mpo.num_images);

		for (guint i = 0; i < mpo.num_images; i++)
			{
			MPOEntry mpe{}; // mpe members are zero-initialized

			guint image_attr = tiff_byte_get_int32(tiff + data_offset + (i * 16), bo);
			mpe.type_code = image_attr & 0xffffff;
			mpe.representative = !!(image_attr & 0x20000000);
			mpe.dependent_child = !!(image_attr & 0x40000000);
			mpe.dependent_parent = !!(image_attr & 0x80000000);
			mpe.length = tiff_byte_get_int32(tiff + data_offset + (i * 16) + 4, bo);
			if (i > 0) mpe.offset = tiff_byte_get_int32(tiff + data_offset + (i * 16) + 8, bo) + mpo.mpo_offset;
			mpe.dep1 = tiff_byte_get_int16(tiff + data_offset + (i * 16) + 12, bo);
			mpe.dep2 = tiff_byte_get_int16(tiff + data_offset + (i * 16) + 14, bo);

			DEBUG_1("   image %x %x %x", image_attr, mpe.length, mpe.offset);
			mpo.images.push_back(mpe);
			}
		}

	return 0;
}

gint mpo_parse_Attributes_IFD_entry(const guchar *tiff, guint offset, TiffByteOrder bo,
                                    MPOEntry &mpe)
{
	const TiffTag tt{tiff + offset, bo};
	DEBUG_1("   tag %x format %x count %x data_val %x", tt.tag, tt.format, tt.count, tt.data_val);

	switch (tt.tag)
		{
		case 0xb000:
			mpe.MPFVersion = tt.data_val;
			DEBUG_1("    mpf version %x", mpe.MPFVersion);
			break;
		case 0xb101:
			mpe.MPIndividualNum = tt.data_val;
			DEBUG_1("    Individual Image Number %x", mpe.MPIndividualNum);
			break;
		case 0xb201:
			mpe.PanOrientation = tt.data_val;
			break;
		/**
		@FIXME See section 5.2.4. MP Attribute IFD of
		CIPA DC-007-Translation-2021 Multi-Picture Format
		https://www.cipa.jp/std/documents/download_e.html?CIPA_DC-007-2021_E
		Tag Name                    Field Name         Tag ID (Dec/Hex) Type      Count
		Panorama Horizontal Overlap PanOverlap_H       45570 B202       RATIONAL  1
		Panorama Vertical Overlap   PanOverlap_V       45571 B203       RATIONAL  1
		Base Viewpoint Number       BaseViewpointNum   45572 B204       LONG      1
		Convergence Angle           ConvergenceAngle   45573 B205       SRATIONAL 1
		Baseline Length             BaselineLength     45574 B206       RATIONAL  1
		Divergence Angle            VerticalDivergence 45575 B207       SRATIONAL 1
		Horizontal Axis Distance    AxisDistance_X     45576 B208       SRATIONAL 1
		Vertical Axis Distance      AxisDistance_Y     45577 B209       SRATIONAL 1
		Collimation Axis Distance   AxisDistance_Z     45578 B20A       SRATIONAL 1
		Yaw Angle                   YawAngle           45579 B20B       SRATIONAL 1
		Pitch Angle                 PitchAngle         45580 B20C       SRATIONAL 1
		Roll Angle                  RollAngle          45581 B20D       SRATIONAL 1
		*/
		default:
			break;
		}

	return 0;
}

} // namespace

gboolean is_jpeg_container(const guchar *data, guint size)
{
	return data != nullptr && size >= 2
	    && data[0] == JPEG_MARKER
	    && data[1] == JPEG_MARKER_SOI;
}

bool jpeg_segment_find(const guchar *data, guint size,
                       guchar app_marker, std::string_view magic,
                       JpegSegment &seg)
{
	guchar marker = 0;
	guint offset = 0;
	guint length = 0;

	while (marker != JPEG_MARKER_EOI)
		{
		offset += length;
		length = 2;

		if (offset + 2 >= size ||
		    data[offset] != JPEG_MARKER) return false;

		marker = data[offset + 1];
		if (marker != JPEG_MARKER_SOI &&
		    marker != JPEG_MARKER_EOI)
			{
			if (offset + 4 >= size) return false;
			length += (static_cast<guint>(data[offset + 2]) << 8) + data[offset + 3];

			if (marker == app_marker &&
			    offset + length < size &&
			    length >= 4 + magic.size() &&
			    memcmp(data + offset + 4, magic.data(), magic.size()) == 0)
				{
				seg.offset = offset + 4;
				seg.length = length - 4;
				return true;
				}
			}
		}

	return false;
}

MPOData jpeg_get_mpo_data(const guchar *data, guint size)
{
	constexpr std::string_view magic{ "MPF\x00" };
	JpegSegment seg;
	if (!jpeg_segment_find(data, size, JPEG_MARKER_APP2, magic, seg) || seg.length <= 16) return {};

	DEBUG_1("mpo signature found at %x", seg.offset);
	seg.offset += magic.size();
	seg.length -= magic.size();

	guint offset;
	TiffByteOrder bo;
	if (!tiff_directory_offset(data + seg.offset, seg.length, offset, bo)) return {};

	MPOData mpo{};
	mpo.mpo_offset = seg.offset;

	guint next_offset = 0;
	const auto parse_mpo_data = [&seg, &mpo](const guchar *tiff, guint offset, TiffByteOrder bo)
	{
		return mpo_parse_Index_IFD_entry(tiff, offset, seg.length, bo, mpo);
	};
	tiff_parse_IFD_table(data + seg.offset, offset, seg.length, bo, parse_mpo_data, &next_offset);

	auto it = std::find_if(mpo.images.begin(), mpo.images.end(),
	                       [size](MPOEntry &mpe){ return mpe.offset + mpe.length > size; });
	if (it != mpo.images.end())
		{
		[[maybe_unused]] const guint mpe_size = it->offset + it->length;
		mpo.images.erase(it, mpo.images.end());
		DEBUG_1("MPO file truncated to %u valid images, %u %u", mpo.images.size(), mpe_size, size);
		}

	mpo.num_images = mpo.images.size();

	for (guint i = 0; i < mpo.num_images; i++)
		{
		if (i == 0)
			{
			offset = next_offset;
			}
		else
			{
			if (!jpeg_segment_find(data + mpo.images[i].offset, mpo.images[i].length, JPEG_MARKER_APP2, magic, seg) || seg.length <= 16)
				{
				DEBUG_1("MPO image %u: MPO signature not found", i);
				continue;
				}

			seg.offset += magic.size();
			seg.length -= magic.size();
			if (!tiff_directory_offset(data + mpo.images[i].offset + seg.offset, seg.length, offset, bo))
				{
				DEBUG_1("MPO image %u: invalid directory offset", i);
				continue;
				}

			}

		const auto parse_mpo_entry = [&mpe = mpo.images[i]](const guchar *tiff, guint offset, TiffByteOrder bo)
		{
			return mpo_parse_Attributes_IFD_entry(tiff, offset, bo, mpe);
		};
		tiff_parse_IFD_table(data + mpo.images[i].offset + seg.offset, offset, seg.length, bo, parse_mpo_entry);
		}

	return mpo;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
