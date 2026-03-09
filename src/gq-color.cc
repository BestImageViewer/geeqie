/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "gq-color.h"

void GqColor::from_gdk_rgba(const GdkRGBA &color)
{
	r = color.red * 255;
	g = color.green * 255;
	b = color.blue * 255;
	a = color.alpha * 255;
}

GdkRGBA GqColor::to_gdk_rgba() const
{
	return { static_cast<double>(r) / 255,
	         static_cast<double>(g) / 255,
	         static_cast<double>(b) / 255,
	         static_cast<double>(a) / 255 };
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
