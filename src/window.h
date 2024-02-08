/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#ifndef WINDOW_H
#define WINDOW_H

#include <gtk/gtk.h>

GtkWidget *window_new(const gchar *role, const gchar *icon,
		      const gchar *icon_file, const gchar *subtitle);
void window_set_icon(GtkWidget *window, const gchar *icon, const gchar *file);
gboolean window_maximized(GtkWidget *window);

void help_window_show(const gchar *key);
void help_search_window_show();

#endif /* WINDOW_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
