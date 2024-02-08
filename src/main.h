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

#ifndef MAIN_H
#define MAIN_H

#include <gdk/gdk.h>
#include <gtk/gtk.h>

extern gboolean thumb_format_changed;

extern gchar *gq_prefix;
extern gchar *gq_localedir;
extern gchar *gq_helpdir;
extern gchar *gq_htmldir;
extern gchar *gq_appdir;
extern gchar *gq_bindir;
extern gchar *gq_executable_path;
extern gchar *desktop_file_template;
extern gchar *instance_identifier;

void keyboard_scroll_calc(gint *x, gint *y, GdkEventKey *event);
gint key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);

void exit_program();

#define CASE_SORT(a, b) ( (options->file_sort.case_sensitive) ? strcmp((a), (b)) : strcasecmp((a), (b)) )


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
