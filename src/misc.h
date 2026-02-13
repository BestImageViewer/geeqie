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

#ifndef MISC_H
#define MISC_H

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <config.h>

template<typename T>
void delete_cb(gpointer data)
{
	delete static_cast<T *>(data);
}

const gchar *gq_gtk_entry_get_text(GtkEntry *entry);
gchar *convert_rating_to_stars(gint rating);
gchar *date_get_abbreviated_day_name(gint day);
gchar *decode_geo_parameters(const gchar *input_text);
gchar *expand_tilde(const gchar *filename);
gchar *get_symbolic_link(const gchar *path_utf8);
gchar *get_file_group(const gchar *path_utf8);
gchar *get_file_owner(const gchar *path_utf8);
gchar *utf8_validate_or_convert(const gchar *text);
gdouble get_zoom_increment();
gint date_get_first_day_of_week();
gint get_cpu_cores();
gint utf8_compare(const gchar *s1, const gchar *s2, gboolean case_sensitive);
gint gq_gtk_tree_iter_utf8_collate(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gint sort_column_id);
int runcmd(const gchar *cmd);
void gq_gtk_entry_set_text(GtkEntry *entry, const gchar *text);
void gq_gtk_grid_attach_default(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach);
void gq_gtk_grid_attach(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach, GtkAttachOptions, GtkAttachOptions, guint, guint);

#if HAVE_GTK4
GdkRGBA convert_gdkcolor_to_gdkrgba(/* GdkColor * */ gpointer data);
#else
GdkRGBA convert_gdkcolor_to_gdkrgba(/* GdkColor * */ gpointer data);
#endif /* HAVE_GTK4 */

void shift_color(GdkRGBA &src, gshort val = -1, gint direction = 0);

void cell_renderer_height_override(GtkCellRenderer *renderer); /**< cell max with/height hack utility */
void widget_set_cursor(GtkWidget *widget, gint icon);

GtkWidget *widget_get_toplevel(GtkWidget *widget);

#endif /* MISC_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
