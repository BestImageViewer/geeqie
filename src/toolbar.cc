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
#include <glib-object.h>
#include <glib.h>

#include <config.h>

#include "compat.h"
#include "editors.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"

/** Implements the user-definable toolbar function
 * Called from the Preferences/toolbar tab
 **/

namespace
{

const gchar *action_name_key = "action_name";
const gchar *toolbar_action_key = "toolbar_action";

GtkWidget *toolbarlist[TOOLBAR_COUNT];

void action_item_delete(ActionItem *action_item)
{
	delete action_item;
}

} // namespace

static gboolean toolbar_press_cb(GtkGesture *, int, double, double, gpointer data)
{
	popup_menu_bar(static_cast<GtkWidget *>(data), nullptr);

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

#if HAVE_GTK4
	gesture = gtk_gesture_click_new();
	gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(gesture));
#else
	gesture = gtk_gesture_multi_press_new(button);
#endif
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
			gtk_icon_size_lookup(GTK_ICON_SIZE_BUTTON, &w, &h);

			scaled = gdk_pixbuf_scale_simple(pixbuf, w, h,
							 GDK_INTERP_BILINEAR);
			image = gtk_image_new_from_pixbuf(scaled);

			g_object_unref(scaled);
			g_object_unref(pixbuf);
			}
		else
			{
			image = gq_gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_BUTTON);
			}
		}
	else
		{
		image = gtk_image_new_from_icon_name(GQ_ICON_GO_JUMP, GTK_ICON_SIZE_BUTTON);
		}
	gq_gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	gtk_widget_show(image);

	GtkWidget *button_label = gtk_label_new(label);
	gq_gtk_box_pack_start(GTK_BOX(hbox), button_label, FALSE, FALSE, 0);
	gtk_widget_show(button_label);
}

static void toolbarlist_add_cb(GtkWidget *widget, gpointer data)
{
	auto *action = static_cast<ActionItem *>(g_object_get_data(G_OBJECT(widget), toolbar_action_key));

	toolbarlist_add_button(action->name, action->label, action->icon_name, GTK_BOX(data));
}

// toolbar_menu_add_popup
static gboolean toolbar_menu_add_cb(GtkWidget *, gpointer data)
{
	GtkWidget *menu = popup_menu_short_lived();

	static auto *separator = new ActionItem("Separator", "Separator", "no-icon");

	GtkWidget *item = menu_item_add_stock(menu, "Separator", "Separator", G_CALLBACK(toolbarlist_add_cb), data);
	g_object_set_data(G_OBJECT(item), toolbar_action_key, separator);

	std::vector<ActionItem> list = get_action_items();

	for (const ActionItem &action_item : list)
		{
		item = menu_item_add_stock(menu, action_item.label, action_item.icon_name, G_CALLBACK(toolbarlist_add_cb), data);
		g_object_set_data_full(G_OBJECT(item), toolbar_action_key, new ActionItem(action_item),
		                       reinterpret_cast<GDestroyNotify>(action_item_delete));
		}

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);

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
	std::optional<std::vector<ActionItem>> actions_list;
	std::optional<EditorsList> editors_list;

	for (GList *work = toolbar_items; work; work = work->next)
		{
		auto name = static_cast<gchar *>(work->data);

		if (g_strcmp0(name, "Separator") != 0)
			{
			const gchar *label = nullptr;
			g_autofree gchar *icon = nullptr;

			if (file_extension_match(name, ".desktop"))
				{
				if (!editors_list) editors_list = editor_list_get();

				const auto it = std::find_if(editors_list->cbegin(), editors_list->cend(),
				                             [name](const EditorDescription *editor) { return g_strcmp0(editor->key, name) == 0; });
				if (it != editors_list->cend())
					{
					label = (*it)->name;
					icon = g_strconcat((*it)->icon, ".desktop", NULL);
					}
				}
			else
				{
				if (!actions_list) actions_list = get_action_items();

				const auto it = std::find_if(actions_list->cbegin(), actions_list->cend(),
				                             [name](const ActionItem &action_item) { return g_strcmp0(action_item.name, name) == 0; });
				if (it != actions_list->cend())
					{
					label = it->label;
					icon = g_strdup(it->icon_name);
					}
				}

			toolbarlist_add_button(name, label, icon, box);
			}
		else
			{
			toolbarlist_add_button(name, name, "no-icon", box);
			}
		}
}

GtkWidget *toolbar_select_new(LayoutWindow *lw, ToolbarType bar)
{
	GtkWidget *tbar;
	GtkWidget *add_box;

	if (!lw) return nullptr;

	GtkWidget *widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_widget_show(widget);

	GtkWidget *scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
							GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gq_gtk_box_pack_start(GTK_BOX(widget), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	toolbarlist[bar] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(toolbarlist[bar]);
	gq_gtk_container_add(scrolled, toolbarlist[bar]);
	gq_gtk_viewport_set_shadow_type(GTK_WIDGET(gq_gtk_bin_get_child(GTK_WIDGET(scrolled))),
																GTK_SHADOW_NONE);

	add_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(add_box);
	gq_gtk_box_pack_end(GTK_BOX(widget), add_box, FALSE, FALSE, 0);
	tbar = pref_toolbar_new(add_box);

	GtkWidget *add_button = pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE,
	                                            _("Add Toolbar Item"),
	                                            G_CALLBACK(toolbar_menu_add_cb), toolbarlist[bar]);
	gtk_widget_show(add_button);

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
