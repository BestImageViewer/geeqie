/*
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

#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

enum NotifyType : gint;

class FileData;
struct HistMap;

/* Note: The order is important */
enum HistogramChannel {
	HCHAN_R = 0,
	HCHAN_G = 1,
	HCHAN_B = 2,
	HCHAN_MAX = 3,
	HCHAN_RGB = 4,
	HCHAN_DEFAULT = HCHAN_RGB,
	HCHAN_COUNT
};

enum HistogramMode {
	HMODE_LINEAR = 0,
	HMODE_LOG = 1,
	HMODE_COUNT
};

struct Histogram {
	struct Grid {
		guint v; /**< number of vertical divisions, 0 for none */
		guint h; /**< number of horizontal divisions, 0 for none */
		struct {
			guint8 R; /**< red */
			guint8 G; /**< green */
			guint8 B; /**< blue */
			guint8 A; /**< alpha */
		} color;  /**< grid color */
	};

	void set_channel(gint channel);
	gint get_channel() const;
	void set_mode(gint mode);
	gint get_mode() const;
	void toggle_channel();
	void toggle_mode();
	const gchar *label() const;
	void draw(const HistMap *histmap, GdkPixbuf *pixbuf, gint x, gint y, gint width, gint height) const;

	gint histogram_channel = HCHAN_DEFAULT; /**< drawing mode for histogram */
	gint histogram_mode = HMODE_LINEAR;     /**< linear or logarithmical */
};


void histmap_free(HistMap *histmap);
const HistMap *histmap_get(FileData *fd);
gboolean histmap_start_idle(FileData *fd);

void histogram_notify_cb(FileData *fd, NotifyType type, gpointer data);

#endif /* HISTOGRAM_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
