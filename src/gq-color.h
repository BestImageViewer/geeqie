/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GQ_COLOR_H
#define GQ_COLOR_H

#include <gdk/gdk.h>

struct GqColor
{
	void from_gdk_rgba(const GdkRGBA &color);
	GdkRGBA to_gdk_rgba() const;

	guint8 r; /**< red */
	guint8 g; /**< green */
	guint8 b; /**< blue */
	guint8 a; /**< alpha */
};

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
