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
#define HCHAN_R 0
#define HCHAN_G 1
#define HCHAN_B 2
#define HCHAN_MAX 3
#define HCHAN_RGB 4
#define HCHAN_COUNT 5
#define HCHAN_DEFAULT HCHAN_RGB

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

	gint histogram_channel; /**< drawing mode for histogram */
	gint histogram_mode;     /**< logarithmical or not */
};


Histogram *histogram_new();
void histogram_free(Histogram *histogram);
void histogram_set_channel(Histogram *histogram, gint chan);
gint histogram_get_channel(const Histogram *histogram);
void histogram_set_mode(Histogram *histogram, gint mode);
gint histogram_get_mode(const Histogram *histogram);
void histogram_toggle_channel(Histogram *histogram);
void histogram_toggle_mode(Histogram *histogram);
const gchar *histogram_label(const Histogram *histogram);

void histmap_free(HistMap *histmap);

const HistMap *histmap_get(FileData *fd);
gboolean histmap_start_idle(FileData *fd);

gboolean histogram_draw(const Histogram *histogram, const HistMap *histmap, GdkPixbuf *pixbuf, gint x, gint y, gint width, gint height);

void histogram_notify_cb(FileData *fd, NotifyType type, gpointer data);

#endif /* HISTOGRAM_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
