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

#ifndef BAR_EXIF_H
#define BAR_EXIF_H

#include <glib.h>
#include <gtk/gtk.h>

GtkWidget *bar_pane_exif_new_from_config(const gchar **attribute_names, const gchar **attribute_values);
void bar_pane_exif_update_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values);

void bar_pane_exif_entry_add_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values);

GList *bar_pane_exif_list(); /**< these are exposed for when duplication of the exif bar's text is needed */

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
