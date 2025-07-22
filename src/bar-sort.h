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

#ifndef BAR_SORT_H
#define BAR_SORT_H

#include <glib.h>
#include <gtk/gtk.h>

struct LayoutWindow;

struct BarSort
{
	enum Action {
		COPY = 0,
		MOVE,
		FILTER,
		ACTION_COUNT
	};

	enum Mode {
		MODE_FOLDER = 0,
		MODE_COLLECTION,
		MODE_COUNT
	};

	enum Selection {
		SELECTION_IMAGE = 0,
		SELECTION_SELECTED,
		SELECTION_COUNT
	};

	BarSort::Action action;
	BarSort::Mode mode;
	BarSort::Selection selection;
	gchar *filter_key;
};

GtkWidget *bar_sort_new_default(LayoutWindow *lw);
GtkWidget *bar_sort_new_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values);
void bar_sort_close(GtkWidget *bar);

void bar_sort_write_config(GtkWidget *bar, GString *outstr, gint indent);
void bar_sort_cold_start(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
