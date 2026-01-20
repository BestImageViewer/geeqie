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

#include <algorithm>
#include <cstddef>

#include <pango/pango.h>

#include "compat-deprecated.h"
#include "compat.h"
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
static void menu_item_add_accelerator(GtkWidget *menu, GtkAccelGroup *accel_group, const HardcodedWindowKeyList &window_keys)
{
	const gchar *label = gtk_menu_item_get_label(GTK_MENU_ITEM(menu));

	g_autofree gchar *label_text = nullptr;
	pango_parse_markup(label, -1, '_', nullptr, &label_text, nullptr, nullptr);

	g_auto(GStrv) label_stripped = g_strsplit(label_text, "...", 2);

	const auto has_text = [text = label_stripped[0]](const HardcodedWindowKey &window_key)
	{
		return g_strcmp0(window_key.text, text) == 0;
	};

	const auto it = std::find_if(window_keys.cbegin(), window_keys.cend(), has_text);
	if (it == window_keys.cend()) return;

	gtk_widget_add_accelerator(menu, "activate", accel_group, it->key_value, it->mask, GTK_ACCEL_VISIBLE);
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

	accel_path_a = deprecated_gtk_action_get_accel_path(deprecated_GTK_ACTION(a));
	accel_path_b = deprecated_gtk_action_get_accel_path(deprecated_GTK_ACTION(b));

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
	GList *groups;
	const gchar *accel_path;

	const gchar *menu_label = gtk_menu_item_get_label(GTK_MENU_ITEM(menu));

	g_autofree gchar *menu_label_text = nullptr;
	pango_parse_markup(menu_label, -1, '_', nullptr, &menu_label_text, nullptr, nullptr);

	LayoutWindow *lw = layout_window_first(); /* get the actions from the first window, it should not matter, they should be the same in all windows */

	g_assert(lw && lw->ui_manager);
	groups = deprecated_gtk_ui_manager_get_action_groups(lw->ui_manager);

	while (groups)
		{
		g_autoptr(GList) actions = deprecated_gtk_action_group_list_actions(deprecated_GTK_ACTION_GROUP(groups->data));
		actions = g_list_sort(actions, actions_sort_cb);

		for (GList *work = actions; work; work = work->next)
			{
			GtkAction *action = deprecated_GTK_ACTION(work->data);
			accel_path = deprecated_gtk_action_get_accel_path(action);
			GtkAccelKey key;
			if (accel_path && gtk_accel_map_lookup_entry(accel_path, &key) && key.accel_key != 0)
				{
				g_autofree gchar *action_label = nullptr;
				g_object_get(action, "label", &action_label, NULL);

				g_autofree gchar *action_label_text = nullptr;
				pango_parse_markup(action_label, -1, '_', nullptr, &action_label_text, nullptr, nullptr);

				if (g_strcmp0(action_label_text, menu_label_text) == 0)
					{
					gtk_widget_add_accelerator(menu, "activate", accel_group, key.accel_key, key.accel_mods, GTK_ACCEL_VISIBLE);
					break;
					}
				}
			}

		groups = groups->next;
		}
}

static void menu_item_add_accelerator(GtkWidget *menu, GtkWidget *item)
{
	auto *accel_group = static_cast<GtkAccelGroup *>(g_object_get_data(G_OBJECT(menu), "accel_group"));
	if (!accel_group) return;

	auto *window_keys = static_cast<HardcodedWindowKeyList *>(g_object_get_data(G_OBJECT(menu), "window_keys"));
	if (window_keys)
		{
		menu_item_add_accelerator(item, accel_group, *window_keys);
		}
	else
		{
		menu_item_add_main_window_accelerator(item, accel_group);
		}
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

	item = gtk_menu_item_new_with_mnemonic(label);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_stock(GtkWidget *menu, const gchar *label, const gchar *stock_id,
			       GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkWidget *image;

	item = deprecated_gtk_image_menu_item_new_with_mnemonic(label);

	image = gq_gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_MENU);
	deprecated_gtk_image_menu_item_set_image(deprecated_GTK_IMAGE_MENU_ITEM(item), image);
	gtk_widget_show(image);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_icon(GtkWidget *menu, const gchar *label, const gchar *icon_name,
			       GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkWidget *image;

	item = deprecated_gtk_image_menu_item_new_with_mnemonic(label);

	image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
	deprecated_gtk_image_menu_item_set_image(deprecated_GTK_IMAGE_MENU_ITEM(item), image);
	gtk_widget_show(image);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_sensitive(GtkWidget *menu, const gchar *label, gboolean sensitive,
				   GCallback func, gpointer data)
{
	GtkWidget *item;

	item = menu_item_add(menu, label, func, data);
	gtk_widget_set_sensitive(item, sensitive);

	return item;
}

GtkWidget *menu_item_add_icon_sensitive(GtkWidget *menu, const gchar *label, const gchar *icon_name, gboolean sensitive,
					 GCallback func, gpointer data)
{
	GtkWidget *item;

	item = menu_item_add_icon(menu, label, icon_name, func, data);
	gtk_widget_set_sensitive(item, sensitive);

	return item;
}

GtkWidget *menu_item_add_check(GtkWidget *menu, const gchar *label, gboolean active,
			       GCallback func, gpointer data)
{
	GtkWidget *item;

	item = gtk_check_menu_item_new_with_mnemonic(label);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *menu_item_add_radio(GtkWidget *menu, const gchar *label, gpointer item_data, gboolean active,
			       GCallback func, gpointer data)
{
	GtkWidget *item = menu_item_add_check(menu, label, active, func, data);
	gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(item), TRUE);

	if (item_data) g_object_set_data(G_OBJECT(item), "menu_item_radio_data", item_data);

	return item;
}

gpointer menu_item_radio_get_data(GtkWidget *menu_item)
{
	return g_object_get_data(G_OBJECT(menu_item), "menu_item_radio_data");
}

void menu_item_add_divider(GtkWidget *menu)
{
	GtkWidget *item = gtk_separator_menu_item_new();
	gtk_widget_set_sensitive(item, FALSE);
	menu_item_finish(menu, item, nullptr, nullptr);
}

/**
 * @brief Use to avoid mnemonics, for example filenames
 */
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

GtkWidget *popup_menu_short_lived()
{
	GtkWidget *menu;

	menu = gtk_menu_new();

	/* take ownership of menu */
	g_object_ref_sink(G_OBJECT(menu));

	g_signal_connect(G_OBJECT(menu), "selection_done",
	                 G_CALLBACK(g_object_unref), NULL); // destroy the menu
	return menu;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
