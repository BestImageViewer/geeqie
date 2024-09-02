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

#if HAVE_GTK4
	#define gq_gtk_box_pack_end(box, child, expand, fill, padding) gtk_box_append(box, child)
	#define gq_gtk_box_pack_start(box, child, expand, fill, padding) gtk_box_prepend(box, child)
	#define gq_gtk_frame_set_shadow_type(frame, type) ;
	#define gq_gtk_scrolled_window_new(hadjustment, vadjustment) gtk_scrolled_window_new()
	#define gq_gtk_scrolled_window_set_shadow_type(scrolled_window, type) gtk_scrolled_window_set_has_frame(scrolled_window, TRUE)
	#define gq_gtk_widget_destroy(widget) gtk_window_destroy(widget)
	#define gq_gtk_widget_queue_draw_area(widget, x, y, width, height) gtk_widget_queue_draw(widget);
	#define gq_gtk_widget_show_all(widget) ;
	#define gq_gtk_window_move(window, x, y) ;
	#define gq_gtk_window_resize(window, width, height) gtk_window_set_default_size(window, width, height)
	#define gq_gtk_window_set_keep_above(window, setting) ;
	#define gq_gtk_window_set_position(window, position) ;
	#define gq_gtk_window_fullscreen_on_monitor(window, monitor) ;
#else
	#define gq_gtk_box_pack_end(box, child, expand, fill, padding) gtk_box_pack_end(box, child, expand, fill, padding)
	#define gq_gtk_box_pack_start(box, child, expand, fill, padding) gtk_box_pack_start(box, child, expand, fill, padding)
	#define gq_gtk_frame_set_shadow_type(frame, type) gtk_frame_set_shadow_type(frame, type)
	#define gq_gtk_scrolled_window_new(hadjustment, vadjustment) gtk_scrolled_window_new(hadjustment, vadjustment)
	#define gq_gtk_scrolled_window_set_shadow_type(scrolled_window, type) gtk_scrolled_window_set_shadow_type(scrolled_window, type)
	#define gq_gtk_widget_destroy(widget) gtk_widget_destroy(widget)
	#define gq_gtk_widget_queue_draw_area(widget, x, y, width, height) gtk_widget_queue_draw_area(widget, x, y, width, height);
	#define gq_gtk_widget_show_all(widget) gtk_widget_show_all(widget)
	#define gq_gtk_window_move(window, x, y) gtk_window_move(window, x, y)
	#define gq_gtk_window_resize(window, width, height)gtk_window_resize(window, width, height)
	#define gq_gtk_window_set_keep_above(window, setting) gtk_window_set_keep_above(window, setting)
	#define gq_gtk_window_set_position(window, position) gtk_window_set_position(window, position)
	#define gq_gtk_window_fullscreen_on_monitor(window, screen, monitor) gtk_window_fullscreen_on_monitor(window, screen, monitor)
#endif

void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget);

#endif /* COMPAT_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
