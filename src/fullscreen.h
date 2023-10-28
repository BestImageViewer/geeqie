/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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

#ifndef FULLSCREEN_H
#define FULLSCREEN_H

struct ImageWindow;

struct FullScreenData
{
	GtkWidget *window;
	ImageWindow *imd;

	GtkWidget *normal_window;
	ImageWindow *normal_imd;

	guint hide_mouse_id; /**< event source id */
	guint busy_mouse_id; /**< event source id */

	gint cursor_state;

	guint saver_block_id; /**< event source id */

	using StopFunc = void (*)(FullScreenData *, gpointer);
	StopFunc stop_func;
	gpointer stop_data;

	gboolean same_region; /**< the returned region will overlap the current location of widget. */
};

FullScreenData *fullscreen_start(GtkWidget *window, ImageWindow *imd,
				 FullScreenData::StopFunc stop_func, gpointer stop_data);
void fullscreen_stop(FullScreenData *fs);


GtkWidget *fullscreen_prefs_selection_new(const gchar *text, gint *screen_value, gboolean *above_value);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
