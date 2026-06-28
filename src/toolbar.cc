/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2017 The Geeqie Team
 *
 * Author: Colin Clark
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

#include "toolbar.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include <config.h>

#include "accelerators.h"
#include "actions.h"
#include "compat.h"
#include "editors.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "main.h"
#include "menu.h"
#include "misc.h"
#include "preferences.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"

/** Implements the user-definable toolbar function
 * Called from the Preferences/toolbar tab
 **/

namespace
{

const gchar *action_name_key = "action_name";

GtkWidget *toolbarlist[TOOLBAR_COUNT];

} // namespace

static gboolean toolbar_press_cb(GtkGesture *, int, double, double, gpointer data)
{
	popup_menu_bar(static_cast<GtkWidget *>(data), nullptr, nullptr);

	return TRUE;
}

static void toolbarlist_add_button(const gchar *name, const gchar *label,
									const gchar *stock_id, GtkBox *box)
{
	GtkWidget *hbox;
	GtkGesture *gesture;

	GtkWidget *button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gq_gtk_box_pack_start(box, button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	g_object_set_data_full(G_OBJECT(button), action_name_key, g_strdup(name), g_free);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	gq_gtk_container_add(button, hbox);
	gtk_widget_show(hbox);

	gesture = gtk_gesture_click_new();
	gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(gesture));
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
	g_signal_connect(gesture, "released", G_CALLBACK(toolbar_press_cb), button);

	GtkWidget *image;
	if (stock_id)
		{
		g_autofree gchar *iconl = path_from_utf8(stock_id);
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(iconl, nullptr);
		if (pixbuf)
			{
			GdkPixbuf *scaled;
			gint w;
			gint h;

			w = h = 16;
			gq_gtk_icon_size_lookup(&w, &h);

			scaled = gdk_pixbuf_scale_simple(pixbuf, w, h,
							 GDK_INTERP_BILINEAR);
			image = gtk_image_new_from_pixbuf(scaled);

			g_object_unref(scaled);
			g_object_unref(pixbuf);
			}
		else
			{
			image = gtk_image_new_from_icon_name(get_icon_for_action_name(name));
			}
		}
	else
		{
		image = gtk_image_new_from_icon_name(GQ_ICON_GO_JUMP);
		}
	gq_gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	gtk_widget_show(image);

	GtkWidget *button_label = gtk_label_new(label);
	gq_gtk_box_pack_start(GTK_BOX(hbox), button_label, FALSE, FALSE, 0);
	gtk_widget_show(button_label);
}

static void toolbarlist_add_button_main_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	const gchar *action_name = g_variant_get_string(parameter, nullptr);


	const gchar *label = get_description_for_action_name(action_name);
	const gchar *icon = get_icon_for_action_name(action_name);

	toolbarlist_add_button(action_name, label, icon, GTK_BOX(data));

}

static void toolbarlist_add_button_status_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	const gchar *action_name = g_variant_get_string(parameter, nullptr);


	const gchar *label = get_description_for_action_name(action_name);
	const gchar *icon = get_icon_for_action_name(action_name);

	toolbarlist_add_button(action_name, label, icon, GTK_BOX(data));

}

static void toolbar_menu_add_actions(GMenu *menu, const ActionDef *actions, ToolbarType bar)
{
	for (guint i = 0; actions[i].action_name != nullptr; i++)
		{
		if (!actions[i].description)
			{
			continue;
			}

		const gchar *icon = get_icon_for_action_name(actions[i].action_name);
		if (!icon)
			{
			continue;
			}

		g_autoptr(GMenuItem) item;

		g_autoptr(GIcon) themed_icon;

		if (bar == TOOLBAR_MAIN)
			{
			item = g_menu_item_new(actions[i].description,
			                       "win.preferences-win-main-toolbarlist-add");

			themed_icon = g_themed_icon_new(icon);
			g_menu_item_set_icon(item, themed_icon);

			g_menu_item_set_action_and_target(item,
			                                  "win.preferences-win-main-toolbarlist-add",
			                                  "s",
			                                  actions[i].action_name);
			}
		else
			{

			item = g_menu_item_new(actions[i].description,
			                       "win.preferences-win-status-toolbarlist-add");

			themed_icon = g_themed_icon_new(icon);
			g_menu_item_set_icon(item, themed_icon);

			g_menu_item_set_action_and_target(item,
			                                  "win.preferences-win-status-toolbarlist-add",
			                                  "s",
			                                  actions[i].action_name);

			}
		g_menu_append_item(menu, item);

		}
}

static gboolean toolbar_menu_add_cb(GtkWidget *, gpointer data)
{
	auto bar = static_cast<ToolbarType>(GPOINTER_TO_INT(data));
	GMenu *menu_model;
	GMenu *main_submenu;

	g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-toolbar-add.ui");

	if (bar == TOOLBAR_MAIN)
		{
		menu_model = G_MENU(gtk_builder_get_object(builder, "menu-main-toolbar-add"));
		main_submenu = G_MENU(gtk_builder_get_object(builder, "section-main"));
		}
	else
		{
		menu_model = G_MENU(gtk_builder_get_object(builder, "menu-status-toolbar-add"));
		main_submenu = G_MENU(gtk_builder_get_object(builder, "section-status"));
		}

	toolbar_menu_add_actions(main_submenu, get_app_actions(), bar);
	toolbar_menu_add_actions(main_submenu, get_main_actions(), bar);

	popup_menu(menu_model, get_config_window());

	return TRUE;
}

/**
 * @brief For each layoutwindow, clear toolbar and reload with current selection
 * @param bar Main or Status toolbar
 *
 */
void toolbar_apply(ToolbarType bar)
{
	g_autoptr(GList) list = gq_gtk_widget_get_children(toolbarlist[bar]);

	const auto layout_toolbar_apply = [bar, list](LayoutWindow *lw)
	{
		layout_toolbar_clear(lw, bar);

		for (GList *work = list; work; work = work->next)
			{
			auto button = static_cast<GtkButton *>(work->data);
			auto *action_name = static_cast<gchar *>(g_object_get_data(G_OBJECT(button), action_name_key));

			layout_toolbar_add(lw, bar, action_name);
			}
	};

	layout_window_foreach(layout_toolbar_apply);
}

/**
 * @brief Load the current toolbar items into the vbox
 * @param toolbar_items
 * @param box The vbox displayed in the preferences Toolbar tab
 *
 * Get the current contents of the toolbar, both menu items
 * and desktop items, and load them into the vbox
 */
static void toolbarlist_populate(GList *toolbar_items, GtkBox *box)
{
	std::optional<EditorsList> editors_list;

	for (GList *work = toolbar_items; work; work = work->next)
		{
		auto name = static_cast<const gchar *>(work->data);

		if (g_strcmp0(name, "Separator") == 0)
			{
			toolbarlist_add_button(name, name, "no-icon", box);
			continue;
			}

		const gchar *label = nullptr;
		g_autofree gchar *icon = nullptr;

		if (file_extension_match(name, ".desktop"))
			{
			if (!editors_list)
				{
				editors_list = editor_list_get();
				}

			const auto it = std::find_if(editors_list->cbegin(), editors_list->cend(),
			                             [name](const EditorDescription *editor)
			                             {
			                             return g_strcmp0(editor->key, name) == 0;
			                             });

			if (it != editors_list->cend())
				{
				label = (*it)->name;

				if ((*it)->icon)
					{
					icon = g_strconcat((*it)->icon, ".desktop", nullptr);
					}
				}
			}
		else
			{
			const gchar *action_icon = get_icon_for_action_name(name);

			if (action_icon)
				{
				icon = g_strdup(action_icon);
				}

			label = get_description_for_action_name(name);
			}

		toolbarlist_add_button(name,
		                       label ? label : name,
		                       icon ? icon : "no-icon",
		                       box);
		}
}

#include "toolbar-actions.inc"

GtkWidget *toolbar_select_new(LayoutWindow *lw, ToolbarType bar)
{
	GtkWidget *tbar;
	GtkWidget *add_box;

	if (!lw) return nullptr;

	GtkWidget *widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_widget_show(widget);

	GtkWidget *scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), true);
	gq_gtk_box_pack_start(GTK_BOX(widget), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	toolbarlist[bar] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(toolbarlist[bar]);
	gq_gtk_container_add(scrolled, toolbarlist[bar]);
	gq_gtk_viewport_set_shadow_type(gtk_widget_get_first_child(scrolled), GTK_SHADOW_NONE);

	add_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(add_box);
	gq_gtk_box_pack_end(GTK_BOX(widget), add_box, FALSE, FALSE, 0);
	tbar = pref_toolbar_new(add_box);

	GtkWidget *add_button = pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE, _("Add Toolbar Item"), G_CALLBACK(toolbar_menu_add_cb), GINT_TO_POINTER(bar));
	gtk_widget_show(add_button);

	GApplication *app = g_application_get_default();
	if (bar == TOOLBAR_MAIN)
		{
		register_actions_from_table(GTK_APPLICATION(app), get_config_window(), toolbar_main_actions, get_keyfile_merged(), toolbarlist[bar]);
		}
	else
		{
		register_actions_from_table(GTK_APPLICATION(app), get_config_window(), toolbar_status_actions, get_keyfile_merged(), toolbarlist[bar]);
		}

	toolbarlist_populate(lw->toolbar_actions[bar], GTK_BOX(toolbarlist[bar]));

	return widget;
}

const gchar *toolbar_type_config_name(ToolbarType type)
{
	switch (type)
		{
		case TOOLBAR_MAIN:
			return "toolbar";
		case TOOLBAR_STATUS:
			return "statusbar";
		default:
			return nullptr;
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
