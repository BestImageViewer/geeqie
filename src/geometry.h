/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GQ_GEOMETRY_H
#define GQ_GEOMETRY_H

#include <glib-object.h>

#define GQ_TYPE_SIZE (gq_size_get_type())

struct GqPoint {
	int x;
	int y;
};

struct GqSize
{
	bool operator==(const GqSize &other) const
	{
		return width == other.width
		    && height == other.height;
	}

	bool operator!=(const GqSize &other) const { return !(*this == other); }

	bool empty() const { return width == 0 && height == 0; }

	int area() const { return width * height; }

	int width;
	int height;
};

GType gq_size_get_type();

GqSize *gq_size_copy(GqSize *size);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
