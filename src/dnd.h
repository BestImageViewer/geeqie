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

#ifndef DND_H
#define DND_H

#include <glib.h>
#include <gtk/gtk.h>

void drag_signal_connect(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data);
void drag_signal_swapped(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data);
void drag_dest_unset(GtkWidget *widget);

void drag_source_set(GtkWidget *widget, guint button, gpointer, gint n_targets, GdkDragAction actions);
void drag_dest_set(GtkWidget *widget, const char **mime_types, guint n_mime_types, GdkDragAction actions);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
