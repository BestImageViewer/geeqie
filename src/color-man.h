/*
 * Copyright (C) 2006 John Ellis
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

#ifndef COLOR_MAN_H
#define COLOR_MAN_H

#include <optional>
#include <string>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>

struct ImageWindow;

enum ColorManProfileType : int {
	COLOR_PROFILE_NONE = -1,
	COLOR_PROFILE_MEM = -2,
	COLOR_PROFILE_SRGB = 0,
	COLOR_PROFILE_ADOBERGB,
	COLOR_PROFILE_FILE,
};

struct ColorMan {
	ImageWindow *imd;
	GdkPixbuf *pixbuf;
	gint incremental_sync;
	gint row;

	gpointer profile;

	guint idle_id; /* event source id */
};


ColorMan *color_man_new(ImageWindow *imd, GdkPixbuf *pixbuf,
                        ColorManProfileType input_type, const gchar *input_file,
                        ColorManProfileType screen_type, const gchar *screen_file,
                        const guchar *screen_data, guint screen_data_len);
ColorMan *color_man_new_embedded(ImageWindow *imd, GdkPixbuf *pixbuf,
                                 const guchar *input_data, guint input_data_len,
                                 ColorManProfileType screen_type, const gchar *screen_file,
                                 const guchar *screen_data, guint screen_data_len);
void color_man_free(ColorMan *cm);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ColorMan, color_man_free)

void color_man_update();

void color_man_correct_region(const ColorMan *cm, GdkPixbuf *pixbuf, GdkRectangle region);

struct ColorManStatus {
	std::string image_profile;
	std::string screen_profile;
};

std::optional<ColorManStatus> color_man_get_status(const ColorMan *cm);

const gchar *get_profile_name(const guchar *profile_data, guint profile_len);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
