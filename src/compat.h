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

#ifndef COMPAT_H
#define COMPAT_H

#include <glib.h>
#include <gtk/gtk.h>

#include <config.h>

/* Some systems (BSD,MacOsX,HP-UX,...) define MAP_ANON and not MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define	MAP_ANONYMOUS	MAP_ANON
#elif defined(MAP_ANONYMOUS) && !defined(MAP_ANON)
#define	MAP_ANON	MAP_ANONYMOUS
#endif

struct GqMouseButtonEvent
{
	guint button;
	gdouble x;
	gdouble y;
	GdkModifierType state;
	guint press_count;
};

#if HAVE_GTK4
	#define gq_gtk_image_new_from_icon_name(icon_name, size) gtk_image_new_from_icon_name(icon_name)
	#define gq_gtk_icon_theme_has_icon(icon_theme, icon_name) gtk_icon_theme_has_icon(icon_theme, icon_name)
	#define gq_gtk_scrolled_window_new(hadjustment, vadjustment) gtk_scrolled_window_new()
	#define gq_gtk_widget_queue_draw_area(widget, x, y, width, height) gtk_widget_queue_draw(widget);
	#define gq_gtk_window_resize(window, width, height) gtk_window_set_default_size(window, width, height)
	#define gq_gtk_window_fullscreen_on_monitor(window, monitor) ;
	#define gq_icon_theme_get_default() gtk_icon_theme_get_for_display(gdk_display_get_default())
#else
	#define gq_gtk_box_pack_end(box, child, expand, fill, padding) gtk_box_pack_end(box, child, expand, fill, padding)
	#define gq_gtk_box_pack_start(box, child, expand, fill, padding) gtk_box_pack_start(box, child, expand, fill, padding)
	#define gq_gtk_image_new_from_icon_name(icon_name, size) gtk_image_new_from_icon_name(icon_name, size)
	#define gq_gtk_icon_theme_has_icon(icon_theme, icon_name) gtk_icon_theme_has_icon(icon_theme, icon_name)
	#define gq_gtk_scrolled_window_new(hadjustment, vadjustment) gtk_scrolled_window_new(hadjustment, vadjustment)
	#define gq_gtk_widget_queue_draw_area(widget, x, y, width, height) gtk_widget_queue_draw_area(widget, x, y, width, height);
	#define gq_gtk_window_resize(window, width, height)gtk_window_resize(window, width, height)
	#define gq_gtk_window_fullscreen_on_monitor(window, screen, monitor) gtk_window_fullscreen_on_monitor(window, screen, monitor)
	#define gq_icon_theme_get_default() gtk_icon_theme_get_default()
#endif

#if HAVE_GTK4
void gq_gtk_box_pack_start(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);
void gq_gtk_box_pack_end(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);
#else
	#define gq_gtk_box_pack_end(box, child, expand, fill, padding) gtk_box_pack_end(box, child, expand, fill, padding)
	#define gq_gtk_box_pack_start(box, child, expand, fill, padding) gtk_box_pack_start(box, child, expand, fill, padding)
#endif

void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget);
void gq_gtk_container_remove(GtkWidget *container, GtkWidget *widget);
void gq_gtk_container_foreach(GtkWidget *container, GtkCallback callback, gpointer callback_data);
void gq_gtk_widget_show_all(GtkWidget *widget);
void gq_gtk_frame_set_shadow_type(GtkFrame *frame, GtkShadowType type);
void gq_gtk_scrolled_window_set_shadow_type(GtkScrolledWindow *scrolled_window, GtkShadowType type);
gboolean gq_gtk_window_get_position(GtkWindow *window, gint *x, gint *y);
void gq_gtk_window_move(GtkWindow *window, gint x, gint y);
void gq_gtk_window_set_keep_above(GtkWindow *window, gboolean setting);
void gq_gtk_window_set_position(GtkWindow *window, GtkWindowPosition position);
void gq_gtk_widget_destroy(GtkWidget *widget);
void gq_gtk_widget_set_border_width(GtkWidget *widget, guint width);
gboolean gq_gtk_icon_size_lookup(GtkIconSize size, gint *width, gint *height);
GtkWidget *gq_gtk_image_new_from_stock(const gchar *stock_id, GtkIconSize size);
GtkWidget *gq_gtk_bin_get_child(GtkWidget *widget);
GList *gq_gtk_widget_get_children(GtkWidget *widget);
void gq_gtk_viewport_set_shadow_type(GtkWidget *viewport, int type);
gboolean gq_gtk_widget_event(GtkWidget *widget, GdkEvent *event);
void gq_drag_g_signal_connect(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data);
void gq_drag_g_signal_swapped(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data);
void gq_gtk_drag_dest_unset(GtkWidget *widget);

#if HAVE_GTK4
void gq_gtk_drag_source_set(GtkWidget *widget, GdkModifierType start_button_mask, gpointer, gint n_targets, GdkDragAction actions);
void gq_gtk_drag_dest_set(GtkWidget *widget, gpointer, gpointer, gint n_targets, GdkDragAction actions);
#else
void gq_gtk_drag_source_set(GtkWidget *widget, GdkModifierType start_button_mask, const GtkTargetEntry *targets, gint n_targets, GdkDragAction actions);
void gq_gtk_drag_dest_set(GtkWidget *widget, GtkDestDefaults flags, const GtkTargetEntry *targets, gint n_targets, GdkDragAction actions);
#endif

#endif /* COMPAT_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
