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
	#define gq_gtk_window_set_keep_above(window, setting) ;
	#define gq_gtk_window_set_position(window, position) ;
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
	#define gq_gtk_window_set_keep_above(window, setting) gtk_window_set_keep_above(window, setting)
	#define gq_gtk_window_set_position(window, position) gtk_window_set_position(window, position)
#endif

void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget);

// Hide GtkAction deprecation warnings
// @todo Remove after porting to GAction/GMenu
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
inline gboolean GQ_GTK_IS_RADIO_ACTION(GtkAction *action) { return GTK_IS_RADIO_ACTION(action); }
inline gboolean GQ_GTK_IS_TOGGLE_ACTION(GtkAction *action) { return GTK_IS_TOGGLE_ACTION(action); }
const auto gq_gtk_action_activate = gtk_action_activate;
const auto gq_gtk_action_create_icon = gtk_action_create_icon;
const auto gq_gtk_action_get_accel_path = gtk_action_get_accel_path;
const auto gq_gtk_action_get_icon_name = gtk_action_get_icon_name;
const auto gq_gtk_action_get_name = gtk_action_get_name;
const auto gq_gtk_action_get_stock_id = gtk_action_get_stock_id;
const auto gq_gtk_action_get_tooltip = gtk_action_get_tooltip;
const auto gq_gtk_action_set_sensitive = gtk_action_set_sensitive;
const auto gq_gtk_action_set_visible = gtk_action_set_visible;
const auto gq_gtk_action_group_add_actions = gtk_action_group_add_actions;
const auto gq_gtk_action_group_add_radio_actions = gtk_action_group_add_radio_actions;
const auto gq_gtk_action_group_add_toggle_actions = gtk_action_group_add_toggle_actions;
const auto gq_gtk_action_group_get_action = gtk_action_group_get_action;
const auto gq_gtk_action_group_list_actions = gtk_action_group_list_actions;
const auto gq_gtk_action_group_new = gtk_action_group_new;
const auto gq_gtk_action_group_set_translate_func = gtk_action_group_set_translate_func;
const auto gq_gtk_radio_action_get_current_value = gtk_radio_action_get_current_value;
const auto gq_gtk_radio_action_set_current_value = gtk_radio_action_set_current_value;
const auto gq_gtk_toggle_action_get_active = gtk_toggle_action_get_active;
const auto gq_gtk_toggle_action_set_active = gtk_toggle_action_set_active;
const auto gq_gtk_ui_manager_add_ui = gtk_ui_manager_add_ui;
const auto gq_gtk_ui_manager_add_ui_from_resource = gtk_ui_manager_add_ui_from_resource;
const auto gq_gtk_ui_manager_add_ui_from_string = gtk_ui_manager_add_ui_from_string;
const auto gq_gtk_ui_manager_ensure_update = gtk_ui_manager_ensure_update;
const auto gq_gtk_ui_manager_get_accel_group = gtk_ui_manager_get_accel_group;
const auto gq_gtk_ui_manager_get_action_groups = gtk_ui_manager_get_action_groups;
const auto gq_gtk_ui_manager_get_widget = gtk_ui_manager_get_widget;
const auto gq_gtk_ui_manager_insert_action_group = gtk_ui_manager_insert_action_group;
const auto gq_gtk_ui_manager_new = gtk_ui_manager_new;
const auto gq_gtk_ui_manager_new_merge_id = gtk_ui_manager_new_merge_id;
const auto gq_gtk_ui_manager_remove_action_group = gtk_ui_manager_remove_action_group;
const auto gq_gtk_ui_manager_remove_ui = gtk_ui_manager_remove_ui;
const auto gq_gtk_ui_manager_set_add_tearoffs = gtk_ui_manager_set_add_tearoffs;
G_GNUC_END_IGNORE_DEPRECATIONS

#endif /* COMPAT_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
