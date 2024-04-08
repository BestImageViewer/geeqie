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

#include "ui-menu.h"

#include <cstddef>

#include <pango/pango.h>

#include "layout.h"

/*
 *-----------------------------------------------------------------------------
 * menu items
 *-----------------------------------------------------------------------------
 */

/**
 * @brief Add accelerator key to a window popup menu
 * @param menu
 * @param accel_group
 * @param window_keys
 *
 * This is used only so that the user can see the applicable
 * shortcut key displayed in the menu. The actual handling of
 * the keystroke is done elsewhere in the code.
 */
static void menu_item_add_accelerator(GtkWidget *menu, GtkAccelGroup *accel_group, hard_coded_window_keys *window_keys)
{
	gchar *label;
	gchar *label_text;
	gchar **label_stripped;
	gint i = 0;

	label = g_strdup(gtk_menu_item_get_label(GTK_MENU_ITEM(menu)));

	pango_parse_markup(label, -1, '_', nullptr, &label_text, nullptr, nullptr);

	label_stripped = g_strsplit(label_text, "...", 2);

	while (window_keys[i].text != nullptr)
		{
		if (g_strcmp0(window_keys[i].text, label_stripped[0]) == 0)
			{
			gtk_widget_add_accelerator(menu, "activate", accel_group, window_keys[i].key_value, window_keys[i].mask, GTK_ACCEL_VISIBLE);

			break;
			}
		i++;
		}

	g_free(label);
	g_free(label_text);
	g_strfreev(label_stripped);
}

/**
 * @brief Callback for the actions GList sort function
 * @param a
 * @param b
 * @returns
 *
 * Sort the action entries so that the non-shifted and non-control
 * entries are at the start of the list. The user then sees the basic
 * non-modified key shortcuts displayed in the menus.
 */
static gint actions_sort_cb(gconstpointer a, gconstpointer b)
{
	const gchar *accel_path_a;
	GtkAccelKey key_a;
	const gchar *accel_path_b;
	GtkAccelKey key_b;

	accel_path_a = gtk_action_get_accel_path(GTK_ACTION(a));
	accel_path_b = gtk_action_get_accel_path(GTK_ACTION(b));

	if (accel_path_a && gtk_accel_map_lookup_entry(accel_path_a, &key_a) && accel_path_b && gtk_accel_map_lookup_entry(accel_path_b, &key_b))
		{
		if (key_a.accel_mods < key_b.accel_mods) return -1;
		if (key_a.accel_mods > key_b.accel_mods) return 1;
		}

	return 0;
}

/**
 * @brief Add accelerator key to main window popup menu
 * @param menu
 * @param accel_group
 *
 * This is used only so that the user can see the applicable
 * shortcut key displayed in the menu. The actual handling of
 * the keystroke is done elsewhere in the code.
 */
static void menu_item_add_main_window_accelerator(GtkWidget *menu, GtkAccelGroup *accel_group)
{
	gchar *menu_label;
	gchar *menu_label_text;
	gchar *action_label;
	gchar *action_label_text;
	LayoutWindow *lw;
	GList *groups;
	GList *actions;
	GtkAction *action;
	const gchar *accel_path;
	GtkAccelKey key;

	menu_label = g_strdup(gtk_menu_item_get_label(GTK_MENU_ITEM(menu)));

	pango_parse_markup(menu_label, -1, '_', nullptr, &menu_label_text, nullptr, nullptr);

	lw = static_cast<LayoutWindow *>(layout_window_list->data); /* get the actions from the first window, it should not matter, they should be the same in all windows */

	g_assert(lw && lw->ui_manager);
	groups = gtk_ui_manager_get_action_groups(lw->ui_manager);

	while (groups)
		{
		actions = gtk_action_group_list_actions(GTK_ACTION_GROUP(groups->data));
		actions = g_list_sort(actions, actions_sort_cb);

		while (actions)
			{
			action = GTK_ACTION(actions->data);
			accel_path = gtk_action_get_accel_path(action);
			if (accel_path && gtk_accel_map_lookup_entry(accel_path, &key))
				{
				g_object_get(action, "label", &action_label, NULL);

				pango_parse_markup(action_label, -1, '_', nullptr, &action_label_text, nullptr, nullptr);

				if (g_strcmp0(action_label_text, menu_label_text) == 0)
					{
					if (key.accel_key != 0)
						{
						gtk_widget_add_accelerator(menu, "activate", accel_group, key.accel_key, key.accel_mods, GTK_ACCEL_VISIBLE);

						break;
						}
					}
				g_free(action_label_text);
				g_free(action_label);
				}
			actions = actions->next;
			}
		groups = groups->next;
		}

	g_free(menu_label);
	g_free(menu_label_text);
}

static void menu_item_finish(GtkWidget *menu, GtkWidget *item, GCallback func, gpointer data)
{
	if (func) g_signal_connect(G_OBJECT(item), "activate", func, data);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show(item);
}

GtkWidget *menu_item_add(GtkWidget *menu, const gchar *label,
			 GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	item = gtk_menu_item_new_with_mnemonic(label);
	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));

	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_stock(GtkWidget *menu, const gchar *label, const gchar *stock_id,
			       GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkWidget *image;
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	item = gtk_image_menu_item_new_with_mnemonic(label);
	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));

	image = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);

	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	gtk_widget_show(image);
	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_icon(GtkWidget *menu, const gchar *label, const gchar *icon_name,
			       GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkWidget *image;
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	item = gtk_image_menu_item_new_with_mnemonic(label);
	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));

	image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);

	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	gtk_widget_show(image);
	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_sensitive(GtkWidget *menu, const gchar *label, gboolean sensitive,
				   GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	item = menu_item_add(menu, label, func, data);
	gtk_widget_set_sensitive(item, sensitive);
	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));
	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	return item;
}

GtkWidget *menu_item_add_stock_sensitive(GtkWidget *menu, const gchar *label, const gchar *stock_id, gboolean sensitive,
					 GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	item = menu_item_add_stock(menu, label, stock_id, func, data);
	gtk_widget_set_sensitive(item, sensitive);
	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));
	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	return item;
}

GtkWidget *menu_item_add_icon_sensitive(GtkWidget *menu, const gchar *label, const gchar *icon_name, gboolean sensitive,
					 GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	item = menu_item_add_icon(menu, label, icon_name, func, data);
	gtk_widget_set_sensitive(item, sensitive);
	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));
	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	return item;
}

GtkWidget *menu_item_add_check(GtkWidget *menu, const gchar *label, gboolean active,
			       GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	item = gtk_check_menu_item_new_with_mnemonic(label);
	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));

	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_radio(GtkWidget *menu, const gchar *label, gpointer item_data, gboolean active,
			       GCallback func, gpointer data)
{
	GtkAccelGroup *accel_group;
	hard_coded_window_keys *window_keys;

	GtkWidget *item = menu_item_add_check(menu, label, active, func, data);
	g_object_set_data(G_OBJECT(item), "menu_item_radio_data", item_data);
	g_object_set(G_OBJECT(item), "draw-as-radio", TRUE, NULL);

	window_keys = static_cast<hard_coded_window_keys *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));
	if (accel_group && window_keys)
		{
		menu_item_add_accelerator(item, accel_group, window_keys);
		}
	else if (accel_group)
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}

	return item;
}

void menu_item_add_divider(GtkWidget *menu)
{
	GtkWidget *item = gtk_separator_menu_item_new();
	gtk_widget_set_sensitive(item, FALSE);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
	gtk_widget_show(item);
}

GtkWidget *menu_item_add_simple(GtkWidget *menu, const gchar *label,
				GCallback func, gpointer data)
{
	GtkWidget *item = gtk_menu_item_new_with_label(label);
	menu_item_finish(menu, item, func, data);

	return item;
}

/*
 *-----------------------------------------------------------------------------
 * popup menus
 *-----------------------------------------------------------------------------
 */

static void popup_menu_short_lived_cb(GtkWidget *, gpointer data)
{
	/* destroy the menu */
	g_object_unref(G_OBJECT(data));
}

GtkWidget *popup_menu_short_lived()
{
	GtkWidget *menu;

	menu = gtk_menu_new();

	/* take ownership of menu */
	g_object_ref_sink(G_OBJECT(menu));

	g_signal_connect(G_OBJECT(menu), "selection_done",
			 G_CALLBACK(popup_menu_short_lived_cb), menu);
	return menu;
}

// @todo Used only in vd_menu_position_cb_unused(). Remove?
gboolean popup_menu_position_clamp(GtkMenu *menu, gint *x, gint *y, gint height)
{
	gboolean adjusted = FALSE;
	gint w;
	gint h;
	gint xw;
	gint xh;
	GtkRequisition requisition;

	G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
	gtk_widget_get_requisition(GTK_WIDGET(menu), &requisition);
	w = requisition.width;
	h = requisition.height;
	xw = gdk_screen_width();
	xh = gdk_screen_height();
	G_GNUC_END_IGNORE_DEPRECATIONS;

	if (*x + w > xw)
		{
		*x = xw - w;
		adjusted = TRUE;
		}
	if (*y + h > xh)
		{
		if (height)
			{
			*y = MAX(0, *y - h - height);
			}
		else
			{
			*y = xh - h;
			}
		adjusted = TRUE;
		};

	if (*x < 0)
		{
		*x = 0;
		adjusted = TRUE;
		}
	if (*y < 0)
		{
		*y = 0;
		adjusted = TRUE;
		}

	return adjusted;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
