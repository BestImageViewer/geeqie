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
	return
		{
		static_cast<float>(r) / 255.f,
		static_cast<float>(g) / 255.f,
		static_cast<float>(b) / 255.f,
		static_cast<float>(a) / 255.f
		};
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
