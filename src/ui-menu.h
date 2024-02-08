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

#ifndef UI_MENU_H
#define UI_MENU_H


#include <gtk/gtk.h>

/**
 * @struct hard_coded_window_keys
 * @brief hard coded window shortcut keys
 *
 * Used for two purposes:\n
 * to display the shortcuts keys in popup menus\n
 * used by ./doc/create-shortcuts-xml.sh to generate shortcut documentation in the Help files
 *
 */
struct hard_coded_window_keys {
	GdkModifierType mask; /**< modifier key mask */
	guint key_value;  /**< GDK_keyval */
	const gchar *text;  /**< menu item label - NULL if end of list */
};

GtkWidget *menu_item_add(GtkWidget *menu, const gchar *label,
			 GCallback func, gpointer data);
GtkWidget *menu_item_add_stock(GtkWidget *menu, const gchar *label, const gchar *stock_id,
			       GCallback func, gpointer data);
GtkWidget *menu_item_add_icon(GtkWidget *menu, const gchar *label, const gchar *icon_name,
			       GCallback func, gpointer data);
GtkWidget *menu_item_add_sensitive(GtkWidget *menu, const gchar *label, gboolean sensitive,
				   GCallback func, gpointer data);
GtkWidget *menu_item_add_icon_sensitive(GtkWidget *menu, const gchar *label, const gchar *icon_name, gboolean sensitive,
					 GCallback func, gpointer data);
GtkWidget *menu_item_add_stock_sensitive(GtkWidget *menu, const gchar *label, const gchar *stock_id, gboolean sensitive,
					 GCallback func, gpointer data);
GtkWidget *menu_item_add_check(GtkWidget *menu, const gchar *label, gboolean active,
			       GCallback func, gpointer data);
GtkWidget *menu_item_add_radio(GtkWidget *menu, const gchar *label, gpointer item_data, gboolean active,
			       GCallback func, gpointer data);
void menu_item_add_divider(GtkWidget *menu);

/**
 * @headerfile menu_item_add_simple
 * use to avoid mnemonics, for example filenames
 */
GtkWidget *menu_item_add_simple(GtkWidget *menu, const gchar *label,
				GCallback func, gpointer data);

GtkWidget *popup_menu_short_lived();

/**
 * @headerfile popup_menu_position_clamp
 * clamp a menu's position to within the screen
 * if menu will attempt to stay out of region y to y+height
 */
gboolean popup_menu_position_clamp(GtkMenu *menu, gint *x, gint *y, gint height);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
