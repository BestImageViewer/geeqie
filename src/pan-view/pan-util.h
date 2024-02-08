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

#ifndef PAN_VIEW_PAN_UTIL_H
#define PAN_VIEW_PAN_UTIL_H

#include "pan-types.h"

enum PanDateLengthType {
	PAN_DATE_LENGTH_EXACT,
	PAN_DATE_LENGTH_HOUR,
	PAN_DATE_LENGTH_DAY,
	PAN_DATE_LENGTH_WEEK,
	PAN_DATE_LENGTH_MONTH,
	PAN_DATE_LENGTH_YEAR
};

gboolean pan_date_compare(time_t a, time_t b, PanDateLengthType length);
gint pan_date_value(time_t d, PanDateLengthType length);
gchar *pan_date_value_string(time_t d,  PanDateLengthType length);
time_t pan_date_to_time(gint year, gint month, gint day);

gboolean pan_is_link_loop(const gchar *s);
gboolean pan_is_ignored(const gchar *s, gboolean ignore_symlinks);
GList *pan_list_tree(FileData *dir_fd, SortType sort, gboolean ascend, gboolean case_sensitive,
		     gboolean ignore_symlinks);

#endif
