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

#include <memory>
#include <optional>
#include <string>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>

enum ColorManProfileType : int {
	COLOR_PROFILE_NONE = -1,
	COLOR_PROFILE_MEM = -2,
	COLOR_PROFILE_SRGB = 0,
	COLOR_PROFILE_ADOBERGB,
	COLOR_PROFILE_FILE,
};

struct ColorManStatus {
	std::string image_profile;
	std::string screen_profile;
};

struct ColorMan {
	struct Cache;

	explicit ColorMan(std::shared_ptr<Cache> profile)
	    : profile(std::move(profile))
	{}

	void correct_region(GdkPixbuf *pixbuf, GdkRectangle region) const;
	std::optional<ColorManStatus> get_status() const;

private:
	std::shared_ptr<Cache> profile;
};

struct ColorManMemData {
	std::unique_ptr<guchar, decltype(&g_free)> ptr{nullptr, g_free};
	guint len = 0;
};

ColorMan *color_man_new(const GdkPixbuf *pixbuf,
                        ColorManProfileType input_type, const gchar *input_file,
                        ColorManProfileType screen_type, const gchar *screen_file,
                        const ColorManMemData &screen_data);
ColorMan *color_man_new_embedded(const GdkPixbuf *pixbuf,
                                 const ColorManMemData &input_data,
                                 ColorManProfileType screen_type, const gchar *screen_file,
                                 const ColorManMemData &screen_data);
void color_man_free(ColorMan *cm);

void color_man_update();

const gchar *get_profile_name(const guchar *profile_data, guint profile_len);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
