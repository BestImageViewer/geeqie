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

#include "menu.h"

#include "actions.h"
#include "bar.h"
#include "collect-io.h"
#include "image.h"
#include "intl.h"
#include "main-defines.h"
#include "ui-menu.h"
#include "ui-misc.h"

#include "accelerators.h"
#include "layout.h"
/*
 *-----------------------------------------------------------------------------
 * menu utils
 *-----------------------------------------------------------------------------
 */

gpointer submenu_item_get_data(GtkWidget *submenu_item)
{
	GtkWidget *submenu = gtk_widget_get_parent(submenu_item);
	if (!submenu) return nullptr;

	return g_object_get_data(G_OBJECT(submenu), "submenu_data");
}

/*
 *-----------------------------------------------------------------------------
 * collections
 *-----------------------------------------------------------------------------
 */

void submenu_add_collections_new(GMenu *menu, gboolean, const gchar *func, gpointer)
{
	GList *collection_list = nullptr;

	collect_manager_list(&collection_list,nullptr,nullptr);

	int index = 0; /* index to existing collection list menu item selected */
	for (GList *work = collection_list; work; work = work->next, index++)
		{
		auto *collection_name = static_cast<gchar *>(work->data);

		GMenuItem *item = g_menu_item_new(collection_name, nullptr);
		g_menu_item_set_action_and_target_value(item, func, g_variant_new_int32(index));

		g_menu_append_item(menu, item);
		}

	g_list_free_full(collection_list, g_free);
}

/*
 *-----------------------------------------------------------------------------
 * bar
 *-----------------------------------------------------------------------------
 */

/**
 * @brief
 * @param widget Not used
 * @param data Pointer to vbox item
 * @param up Up/Down movement
 * @param single_step Move up/down one step, or to top/bottom
 *
 */
template<bool up, bool single_step>
static void widget_move_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *widget = static_cast<GtkWidget *>(data);
	if (!widget) return;

	GtkWidget *box = gtk_widget_get_ancestor(widget, GTK_TYPE_BOX);
	if (!box) return;

	gint pos = gq_gtk_box_get_child_position(GTK_BOX(box), widget);
	if (pos < 0) return;

	if (single_step)
		{
		pos = up ? (pos - 1) : (pos + 1);
		pos = std::max(pos, 0);
		}
	else
		{
		pos = up ? 0 : -1;
		}

	gq_gtk_box_reorder_child(GTK_BOX(box), widget, pos);
}

static const GActionEntry popup_entries[] =
{
	{ "move-to-top",    widget_move_cb<true,  false>,  nullptr, nullptr, nullptr, {} },
	{ "move-up",        widget_move_cb<true,  true>,   nullptr, nullptr, nullptr, {} },
	{ "move-down",      widget_move_cb<false, true>,   nullptr, nullptr, nullptr, {} },
	{ "move-to-bottom", widget_move_cb<false, false>,  nullptr, nullptr, nullptr, {} },
	{ "remove",         widget_remove_from_parent_cb,  nullptr, nullptr, nullptr, {} },
	{ "height",         menu_expander_height_cb,       nullptr, nullptr, nullptr, {} }
};

void popup_menu_bar(GtkWidget *widget, GCallback expander_height_cb, gpointer)
{
	GtkBuilder *builder;
	GSimpleActionGroup *group;
	GMenu *menu_model;
	GtkWidget *menu;

	builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-popup.ui");

	menu_model = G_MENU(gtk_builder_get_object(builder, "menubar-popup"));
	group = g_simple_action_group_new();

	g_action_map_add_action_entries(G_ACTION_MAP(group), popup_entries, G_N_ELEMENTS(popup_entries), widget);

	gtk_widget_insert_action_group(widget, "popup", G_ACTION_GROUP(group));

	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(group), "height");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), FALSE);

	if (expander_height_cb && gtk_expander_get_expanded(GTK_EXPANDER(widget)))
		{
		if (action)
			{
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), TRUE);
			}
		}

	g_object_unref(group);

	/* Temporary GTK4 path: use the shared popover helper. */
	menu = popup_menu(menu_model, widget);
	(void)menu;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
