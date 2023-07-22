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

#ifndef UI_TABCOMP_H
#define UI_TABCOMP_H

#include <glib.h>
#include <gtk/gtk.h>

GtkWidget *tab_completion_new_with_history(GtkWidget **entry, const gchar *text,
					   const gchar *history_key, gint max_levels,
					   void (*enter_func)(const gchar *, gpointer), gpointer data);
const gchar *tab_completion_set_to_last_history(GtkWidget *entry);
void tab_completion_append_to_history(GtkWidget *entry, const gchar *path);

GtkWidget *tab_completion_new(GtkWidget **entry, const gchar *text,
			      void (*enter_func)(const gchar *, gpointer), const gchar *filter, const gchar *filter_desc, gpointer data);
void tab_completion_add_to_entry(GtkWidget *entry, void (*enter_func)(const gchar *, gpointer), const gchar *filter, const gchar *filter_desc, gpointer data);
void tab_completion_add_tab_func(GtkWidget *entry, void (*tab_func)(const gchar *, gpointer), gpointer data);
gchar *remove_trailing_slash(const gchar *path);

void tab_completion_add_select_button(GtkWidget *entry, const gchar *title, gboolean folders_only);
void tab_completion_add_append_func(GtkWidget *entry, void (*tab_append_func)(const gchar *, gpointer, gint), gpointer data);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
