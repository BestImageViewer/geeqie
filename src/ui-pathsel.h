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

#ifndef UI_PATHSEL_H
#define UI_PATHSEL_H


#include <gtk/gtk.h>

GtkWidget *path_selection_new_with_files(GtkWidget *entry, const gchar *path,
					 const gchar *filter, const gchar *filter_desc);

void path_selection_sync_to_entry(GtkWidget *entry);

void path_selection_add_select_func(GtkWidget *entry,
				    void (*func)(const gchar *, gpointer), gpointer data);
void path_selection_add_filter(GtkWidget *entry, const gchar *filter, const gchar *description, gint set);
void path_selection_clear_filter(GtkWidget *entry);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
