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

#ifndef COMPAT_DEPRECATED_H
#define COMPAT_DEPRECATED_H

#include <glib.h>
#include <gtk/gtk.h>

// Hide deprecation warnings
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
// Hide GtkAction deprecation warnings
// @todo Remove after porting to GAction/GMenu
inline GtkAction *deprecated_GTK_ACTION(gconstpointer obj) { return GTK_ACTION(obj); }
inline GtkActionGroup *deprecated_GTK_ACTION_GROUP(gconstpointer obj) { return GTK_ACTION_GROUP(obj); }
inline GtkImageMenuItem *deprecated_GTK_IMAGE_MENU_ITEM(GtkWidget *widget) { return GTK_IMAGE_MENU_ITEM(widget); }
inline gboolean deprecated_GTK_IS_RADIO_ACTION(GtkAction *action) { return GTK_IS_RADIO_ACTION(action); }
inline gboolean deprecated_GTK_IS_TOGGLE_ACTION(GtkAction *action) { return GTK_IS_TOGGLE_ACTION(action); }
inline GtkRadioAction *deprecated_GTK_RADIO_ACTION(GtkAction *action) { return GTK_RADIO_ACTION(action); }
inline GtkToggleAction *deprecated_GTK_TOGGLE_ACTION(GtkAction *action) { return GTK_TOGGLE_ACTION(action); }
const auto deprecated_gtk_action_activate = gtk_action_activate;
const auto deprecated_gtk_action_create_icon = gtk_action_create_icon;
const auto deprecated_gtk_action_get_accel_path = gtk_action_get_accel_path;
const auto deprecated_gtk_action_get_icon_name = gtk_action_get_icon_name;
const auto deprecated_gtk_action_get_label = gtk_action_get_label;
const auto deprecated_gtk_action_get_name = gtk_action_get_name;
const auto deprecated_gtk_action_get_stock_id = gtk_action_get_stock_id;
const auto deprecated_gtk_action_get_tooltip = gtk_action_get_tooltip;
const auto deprecated_gtk_action_set_label = gtk_action_set_label;
const auto deprecated_gtk_action_set_sensitive = gtk_action_set_sensitive;
const auto deprecated_gtk_action_set_tooltip = gtk_action_set_tooltip;
const auto deprecated_gtk_action_set_visible = gtk_action_set_visible;
const auto deprecated_gtk_action_group_add_actions = gtk_action_group_add_actions;
const auto deprecated_gtk_action_group_add_radio_actions = gtk_action_group_add_radio_actions;
const auto deprecated_gtk_action_group_add_toggle_actions = gtk_action_group_add_toggle_actions;
const auto deprecated_gtk_action_group_get_action = gtk_action_group_get_action;
const auto deprecated_gtk_action_group_list_actions = gtk_action_group_list_actions;
const auto deprecated_gtk_action_group_new = gtk_action_group_new;
const auto deprecated_gtk_action_group_set_translate_func = gtk_action_group_set_translate_func;
const auto deprecated_gtk_radio_action_get_current_value = gtk_radio_action_get_current_value;
const auto deprecated_gtk_radio_action_set_current_value = gtk_radio_action_set_current_value;
const auto deprecated_gtk_toggle_action_get_active = gtk_toggle_action_get_active;
const auto deprecated_gtk_toggle_action_set_active = gtk_toggle_action_set_active;
const auto deprecated_gtk_ui_manager_add_ui = gtk_ui_manager_add_ui;
const auto deprecated_gtk_ui_manager_add_ui_from_resource = gtk_ui_manager_add_ui_from_resource;
const auto deprecated_gtk_ui_manager_add_ui_from_string = gtk_ui_manager_add_ui_from_string;
const auto deprecated_gtk_ui_manager_ensure_update = gtk_ui_manager_ensure_update;
const auto deprecated_gtk_ui_manager_get_accel_group = gtk_ui_manager_get_accel_group;
const auto deprecated_gtk_ui_manager_get_action_groups = gtk_ui_manager_get_action_groups;
const auto deprecated_gtk_ui_manager_get_widget = gtk_ui_manager_get_widget;
const auto deprecated_gtk_ui_manager_insert_action_group = gtk_ui_manager_insert_action_group;
const auto deprecated_gtk_ui_manager_new = gtk_ui_manager_new;
const auto deprecated_gtk_ui_manager_new_merge_id = gtk_ui_manager_new_merge_id;
const auto deprecated_gtk_ui_manager_remove_action_group = gtk_ui_manager_remove_action_group;
const auto deprecated_gtk_ui_manager_remove_ui = gtk_ui_manager_remove_ui;
const auto deprecated_gtk_ui_manager_set_add_tearoffs = gtk_ui_manager_set_add_tearoffs;

// Hide other Gdk/Gtk deprecation warnings
const auto deprecated_gdk_cairo_create = gdk_cairo_create;
const auto deprecated_gdk_flush = gdk_flush;
const auto deprecated_gdk_pixbuf_animation_get_iter = gdk_pixbuf_animation_get_iter;
const auto deprecated_gdk_pixbuf_animation_iter_advance = gdk_pixbuf_animation_iter_advance;
const auto deprecated_gdk_pixbuf_animation_iter_get_delay_time = gdk_pixbuf_animation_iter_get_delay_time;
const auto deprecated_gdk_pixbuf_animation_iter_get_pixbuf = gdk_pixbuf_animation_iter_get_pixbuf;
const auto deprecated_gdk_pixbuf_animation_is_static_image = gdk_pixbuf_animation_is_static_image;
const auto deprecated_gdk_pixbuf_animation_new_from_stream_async = gdk_pixbuf_animation_new_from_stream_async;
const auto deprecated_gdk_pixbuf_animation_new_from_stream_finish = gdk_pixbuf_animation_new_from_stream_finish;
const auto deprecated_gdk_screen_get_height = gdk_screen_get_height;
const auto deprecated_gdk_screen_get_monitor_at_window = gdk_screen_get_monitor_at_window;
const auto deprecated_gdk_screen_get_width = gdk_screen_get_width;
const auto deprecated_gdk_screen_height = gdk_screen_height;
const auto deprecated_gdk_screen_width = gdk_screen_width;
const auto deprecated_gtk_icon_factory_add = gtk_icon_factory_add;
const auto deprecated_gtk_icon_factory_add_default = gtk_icon_factory_add_default;
const auto deprecated_gtk_icon_factory_new = gtk_icon_factory_new;
const auto deprecated_gtk_icon_set_new_from_pixbuf = gtk_icon_set_new_from_pixbuf;
const auto deprecated_gtk_image_menu_item_new_with_mnemonic = gtk_image_menu_item_new_with_mnemonic;
const auto deprecated_gtk_image_menu_item_set_image = gtk_image_menu_item_set_image;
const auto deprecated_gtk_image_new_from_stock = gtk_image_new_from_stock;
const auto deprecated_gtk_style_context_get_background_color = gtk_style_context_get_background_color;
const auto deprecated_gtk_widget_get_requisition = gtk_widget_get_requisition;
const auto deprecated_gtk_widget_get_style = gtk_widget_get_style;
const auto deprecated_gtk_widget_set_double_buffered = gtk_widget_set_double_buffered;
G_GNUC_END_IGNORE_DEPRECATIONS

#endif /* COMPAT_DEPRECATED_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
