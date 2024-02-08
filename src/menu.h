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

#ifndef MENU_H
#define MENU_H

#include <gtk/gtk.h>

#include "typedefs.h"

gpointer submenu_item_get_data(GtkWidget *menu);

GtkWidget *submenu_add_edit(GtkWidget *menu, GtkWidget **menu_item, GCallback func, gpointer data, GList *fd_list);

gchar *sort_type_get_text(SortType method);
GtkWidget *submenu_add_sort(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean include_none, gboolean include_path,
			    gboolean show_current, SortType type);
GtkWidget *submenu_add_dir_sort(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean include_none, gboolean include_path,
			    gboolean show_current, SortType type);
GtkWidget *submenu_add_zoom(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean include_none, gboolean include_path,
			    gboolean show_current, ZoomMode mode);
gchar *alter_type_get_text(AlterType type);
GtkWidget *submenu_add_alter(GtkWidget *menu, GCallback func, gpointer data);

GtkWidget *submenu_add_collections(GtkWidget *menu, GtkWidget **menu_item,
										GCallback func, gpointer data);
void pop_menu_collections(GList *selection_list, gpointer data);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
