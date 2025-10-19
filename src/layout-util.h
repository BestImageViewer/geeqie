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

#ifndef LAYOUT_UTIL_H
#define LAYOUT_UTIL_H

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

enum ToolbarType : gint;

struct LayoutWindow;

void keyboard_scroll_calc(gint &x, gint &y, const GdkEventKey *event);

gboolean layout_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);

bool layout_handle_user_defined_mouse_buttons(LayoutWindow *lw, GdkEventButton *event);

void layout_util_sync_thumb(LayoutWindow *lw);
void layout_util_sync_marks(LayoutWindow *lw);
void layout_util_sync_file_filter(LayoutWindow *lw);
void layout_util_sync_color(LayoutWindow *lw);
void layout_util_sync(LayoutWindow *lw);

void layout_util_status_update_write(LayoutWindow *lw);
void layout_util_status_update_write_all();

void layout_recent_update_all();
void layout_recent_add_path(const gchar *path);

void layout_copy_path_update_all();

void layout_editors_reload_start();
void layout_editors_reload_finish();
void layout_actions_setup(LayoutWindow *lw);
void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window);
GtkWidget *layout_actions_menu_bar(LayoutWindow *lw);
void layout_toolbar_add_from_config(LayoutWindow *lw, ToolbarType type, const gchar **attribute_names, const gchar **attribute_values);

GtkWidget *layout_actions_toolbar(LayoutWindow *lw, ToolbarType type);

void layout_toolbar_write_config(LayoutWindow *lw, ToolbarType type, GString *outstr, gint indent);
void layout_toolbar_clear(LayoutWindow *lw, ToolbarType type);
void layout_toolbar_add(LayoutWindow *lw, ToolbarType type, const gchar *action);
void layout_toolbar_add_default(LayoutWindow *lw, ToolbarType type);
void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window);

void layout_bar_toggle(LayoutWindow *lw);
void layout_bar_set(LayoutWindow *lw, GtkWidget *bar);

void layout_bar_sort_toggle(LayoutWindow *lw);
void layout_bar_sort_set(LayoutWindow *lw, GtkWidget *bar);

void layout_bars_new_image(LayoutWindow *lw);
void layout_bars_new_selection(LayoutWindow *lw, gint count);

GtkWidget *layout_bars_prepare(LayoutWindow *lw, GtkWidget *image);
void layout_bars_close(LayoutWindow *lw);

void layout_exif_window_new(LayoutWindow *lw);

gboolean is_help_key(GdkEventKey *event);
void layout_menu_close_cb(GtkAction *action, gpointer data);
GtkWidget *layout_actions_menu_tool_bar(LayoutWindow *lw);

void create_toolbars(LayoutWindow *lw);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
