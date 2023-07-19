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

#define FULL_SCREEN_HIDE_MOUSE_DELAY 3000
#define FULL_SCREEN_BUSY_MOUSE_DELAY 200

FullScreenData *fullscreen_start(GtkWidget *window, ImageWindow *imd,
				 void (*stop_func)(FullScreenData *, gpointer), gpointer stop_data);
void fullscreen_stop(FullScreenData *fs);


/**
 * @struct ScreenData
 * screen numbers for fullscreen_prefs are as follows: \n
 *   0  use default display size \n
 * 101  screen 0, monitor 0 \n
 * 102  screen 0, monitor 1 \n
 * 201  screen 1, monitor 0 \n
 */
struct ScreenData {
	gint number;
	gchar *description;
	gint x;
	gint y;
	gint width;
	gint height;
};


GList *fullscreen_prefs_list();
void screen_data_free(ScreenData *sd);

ScreenData *fullscreen_prefs_list_find(GList *list, gint screen);

void fullscreen_prefs_get_geometry(gint screen, GtkWidget *widget, gint *x, gint *y, gint *width, gint *height,
				   GdkScreen **dest_screen, gboolean *same_region);

GtkWidget *fullscreen_prefs_selection_new(const gchar *text, gint *screen_value, gboolean *above_value);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
