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

#include "collect-io.h"
#include "main-defines.h"
#include "ui-menu.h"
#include "ui-misc.h"

/*
 *-----------------------------------------------------------------------------
 * collections
 *-----------------------------------------------------------------------------
 */

void submenu_add_collections_new(GMenu *menu, const gchar *action_name)
{
	GList *collection_list = nullptr;
	collect_manager_list(&collection_list,nullptr,nullptr);

	int index = 0; /* index to existing collection list menu item selected */
	for (GList *work = collection_list; work; work = work->next, index++)
		{
		auto *collection_name = static_cast<gchar *>(work->data);

		g_autoptr(GMenuItem) item = g_menu_item_new(collection_name, nullptr);
		g_menu_item_set_action_and_target_value(item, action_name, g_variant_new_int32(index));

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

	if (single_step)
		{
		if (up)
			{
			GtkWidget *previous = gtk_widget_get_prev_sibling(widget);
			if (!previous) return;

			gtk_box_reorder_child_after(GTK_BOX(box), widget, gtk_widget_get_prev_sibling(previous));
			}
		else
			{
			GtkWidget *next = gtk_widget_get_next_sibling(widget);
			if (!next) return;

			gtk_box_reorder_child_after(GTK_BOX(box), widget, next);
			}
		}
	else if (up)
		{
		gtk_box_reorder_child_after(GTK_BOX(box), widget, nullptr);
		}
	else
		{
		GtkWidget *previous = nullptr;
		for (GtkWidget *work = gtk_widget_get_first_child(box);
		     work;
		     work = gtk_widget_get_next_sibling(work))
			{
			if (work != widget) previous = work;
			}

		gtk_box_reorder_child_after(GTK_BOX(box), widget, previous);
		}
}

static void height_spin_changed_cb(GtkSpinButton *spin, gpointer data)
{
	gtk_widget_set_size_request(static_cast<GtkWidget *>(data), -1, gtk_spin_button_get_value_as_int(spin));
}

static void height_spin_key_press_cb(GtkEventControllerKey *, gint keyval, guint, GdkModifierType, gpointer data)
{
	if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_Escape))
		{
		gtk_window_destroy(GTK_WINDOW(data));
		}
}

static gboolean expander_height_cb(GtkEventControllerKey *controller, guint, guint, GdkModifierType, gpointer)
{
	GtkWidget *window = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	gtk_window_destroy(GTK_WINDOW(window));

	return TRUE;
}

static void menu_expander_height_cb(GSimpleAction *, GVariant *, gpointer data)
{
	GtkWidget *window = gtk_window_new();
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(window), 50, 30); //** @FIXME set these values in a more sensible way */
	GtkEventController *controller = gtk_event_controller_key_new();
	g_signal_connect(controller, "key-pressed", G_CALLBACK(expander_height_cb), nullptr);
	gtk_widget_add_controller(window, controller);

	gtk_window_present(GTK_WINDOW(window));

	GtkWidget *data_box = gtk_expander_get_child(GTK_EXPANDER(data));
	gint w;
	gint h;
	gtk_widget_get_size_request(data_box, &w, &h);

	GtkWidget *spin = gtk_spin_button_new_with_range(1, 1000, 1);
	g_signal_connect(G_OBJECT(spin), "value-changed", G_CALLBACK(height_spin_changed_cb), data_box);
	controller = gtk_event_controller_key_new();
	g_signal_connect(controller, "key-pressed", G_CALLBACK(height_spin_key_press_cb), window);
	gtk_widget_add_controller(spin, controller);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), h);
	gtk_window_set_child(GTK_WINDOW(window), spin);
	gtk_widget_grab_focus(spin);
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

void popup_menu_bar(GtkWidget *widget, bool display_height_option)
{
	g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-popup.ui");
	GMenu *menu_model = G_MENU(gtk_builder_get_object(builder, "menubar-popup"));

	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new();
	g_action_map_add_action_entries(G_ACTION_MAP(group), popup_entries, G_N_ELEMENTS(popup_entries), widget);

	gtk_widget_insert_action_group(widget, "popup", G_ACTION_GROUP(group));

	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(group), "height");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
	                            display_height_option && gtk_expander_get_expanded(GTK_EXPANDER(widget)));

	popup_menu(menu_model, widget);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
