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
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"

/** Implements the user-definable toolbar function
 * Called from the Preferences/toolbar tab
 **/

namespace
{

struct ToolbarData
{
	GtkWidget *vbox;
};

const gchar *action_name_key = "action_name";

ToolbarData *toolbarlist[2];

} // namespace

/**
 * @brief
 * @param widget Not used
 * @param data Pointer to vbox list item
 * @param up Up/Down movement
 * @param single_step Move up/down one step, or to top/bottom
 *
 */
static void toolbar_item_move(GtkWidget *, gpointer data, gboolean up, gboolean single_step)
{
	auto list_item = static_cast<GtkWidget *>(data);
	GtkWidget *box;
	gint pos = 0;

	if (!list_item) return;
	box = gtk_widget_get_ancestor(list_item, GTK_TYPE_BOX);
	if (!box) return;

	gtk_container_child_get(GTK_CONTAINER(box), list_item, "position", &pos, NULL);

	if (single_step)
		{
		pos = up ? (pos - 1) : (pos + 1);
		if (pos < 0) pos = 0;
		}
	else
		{
		pos = up ? 0 : -1;
		}

	gtk_box_reorder_child(GTK_BOX(box), list_item, pos);
}

static void toolbar_item_move_up_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, TRUE, TRUE);
}

static void toolbar_item_move_down_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, FALSE, TRUE);
}

static void toolbar_item_move_top_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, TRUE, FALSE);
}

static void toolbar_item_move_bottom_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, FALSE, FALSE);
}

static void toolbar_item_delete_cb(GtkWidget *, gpointer data)
{
	gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(GTK_WIDGET(data))), GTK_WIDGET(data));
}

static void toolbar_menu_popup(GtkWidget *widget)
{
	GtkWidget *menu;

	menu = popup_menu_short_lived();

	if (widget)
		{
		menu_item_add_icon(menu, _("Move to _top"), GQ_ICON_GO_TOP, G_CALLBACK(toolbar_item_move_top_cb), widget);
		menu_item_add_icon(menu, _("Move _up"), GQ_ICON_GO_UP, G_CALLBACK(toolbar_item_move_up_cb), widget);
		menu_item_add_icon(menu, _("Move _down"), GQ_ICON_GO_DOWN, G_CALLBACK(toolbar_item_move_down_cb), widget);
		menu_item_add_icon(menu, _("Move to _bottom"), GQ_ICON_GO_BOTTOM, G_CALLBACK(toolbar_item_move_bottom_cb), widget);
		menu_item_add_divider(menu);
		menu_item_add_icon(menu, _("Remove"), GQ_ICON_DELETE, G_CALLBACK(toolbar_item_delete_cb), widget);
		menu_item_add_divider(menu);
		}

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static gboolean toolbar_press_cb(GtkGesture *, int, double, double, gpointer data)
{
	auto *button = static_cast<GtkWidget *>(data);

	toolbar_menu_popup(button);

	return TRUE;
}

static void get_toolbar_item(const gchar *name, gchar **label, gchar **stock_id)
{
	*label = nullptr;
	*stock_id = nullptr;

	std::vector<ActionItem> list = get_action_items();

	const auto action_item_has_name = [name](const ActionItem &action_item)
	{
		return g_strcmp0(action_item.name, name) == 0;
	};
	const auto work = std::find_if(list.cbegin(), list.cend(), action_item_has_name);
	if (work != list.cend())
		{
		*label = g_strdup(work->label);
		*stock_id = g_strdup(work->icon_name);
		}
}

static void toolbarlist_add_button(const gchar *name, const gchar *label,
									const gchar *stock_id, GtkBox *box)
{
	GtkWidget *hbox;
	GtkGesture *gesture;

	GtkWidget *button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gq_gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	g_object_set_data_full(G_OBJECT(button), action_name_key, g_strdup(name), g_free);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	gq_gtk_container_add(GTK_WIDGET(button), hbox);
	gtk_widget_show(hbox);

#if HAVE_GTK4
	gesture = gtk_gesture_click_new();
	gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(gesture));
#else
	gesture = gtk_gesture_multi_press_new(button);
#endif
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), MOUSE_BUTTON_RIGHT);
	g_signal_connect(gesture, "released", G_CALLBACK(toolbar_press_cb), button);

	GtkWidget *image;
	if (stock_id)
		{
		GdkPixbuf *pixbuf;
		gchar *iconl;
		iconl = path_from_utf8(stock_id);
		pixbuf = gdk_pixbuf_new_from_file(iconl, nullptr);
		g_free(iconl);
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
			image = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_BUTTON);
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
	auto name = static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "toolbar_add_name"));
	auto label = static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "toolbar_add_label"));
	auto stock_id = static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "toolbar_add_stock_id"));
	auto tbbd = static_cast<ToolbarData *>(data);

	toolbarlist_add_button(name, label, stock_id, GTK_BOX(tbbd->vbox));
}

static void get_desktop_data(const gchar *name, gchar **label, gchar **stock_id)
{
	GList *editors_list;
	GList *work;
	*label = nullptr;
	*stock_id = nullptr;

	editors_list = editor_list_get();
	const auto editor_compare_key = [](gconstpointer data, gconstpointer user_data)
	{
		return g_strcmp0(static_cast<const EditorDescription *>(data)->key, static_cast<const gchar *>(user_data));
	};
	work = g_list_find_custom(editors_list, name, editor_compare_key);
	if (work)
		{
		auto editor = static_cast<const EditorDescription *>(work->data);

		*label = g_strdup(editor->name);
		*stock_id = g_strconcat(editor->icon, ".desktop", NULL);
		}
	g_list_free(editors_list);
}

// toolbar_menu_add_popup
static gboolean toolbar_menu_add_cb(GtkWidget *, gpointer data)
{
	GtkWidget *item;
	GtkWidget *menu;

	menu = popup_menu_short_lived();

	item = menu_item_add_stock(menu, "Separator", "Separator", G_CALLBACK(toolbarlist_add_cb), data);
	g_object_set_data_full(G_OBJECT(item), "toolbar_add_name", g_strdup("Separator"), g_free);
	g_object_set_data_full(G_OBJECT(item), "toolbar_add_label", g_strdup("Separator"), g_free);
	g_object_set_data_full(G_OBJECT(item), "toolbar_add_stock_id", g_strdup("no-icon"), g_free);

	std::vector<ActionItem> list = get_action_items();

	for (const ActionItem &action_item : list)
		{
		item = menu_item_add_stock(menu, action_item.label, action_item.icon_name, G_CALLBACK(toolbarlist_add_cb), data);
		g_object_set_data_full(G_OBJECT(item), "toolbar_add_name", g_strdup(action_item.name), g_free);
		g_object_set_data_full(G_OBJECT(item), "toolbar_add_label", g_strdup(action_item.label), g_free);
		g_object_set_data_full(G_OBJECT(item), "toolbar_add_stock_id", g_strdup(action_item.icon_name), g_free);
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
	const auto layout_toolbar_apply = [](gpointer data, gpointer user_data)
	{
		auto *lw = static_cast<LayoutWindow *>(data);
		auto bar = static_cast<ToolbarType>(GPOINTER_TO_INT(user_data));

		layout_toolbar_clear(lw, bar);

		GList *work_toolbar = gtk_container_get_children(GTK_CONTAINER(toolbarlist[bar]->vbox));
		for (GList *work = work_toolbar; work; work = work->next)
			{
			auto button = static_cast<GtkButton *>(work->data);
			auto *action_name = static_cast<gchar *>(g_object_get_data(G_OBJECT(button), action_name_key));

			layout_toolbar_add(lw, bar, action_name);
			}
		g_list_free(work_toolbar);
	};

	g_list_foreach(layout_window_list, layout_toolbar_apply, GINT_TO_POINTER(bar));
}

/**
 * @brief Load the current toolbar items into the vbox
 * @param lw
 * @param box The vbox displayed in the preferences Toolbar tab
 * @param bar Main or Status toolbar
 *
 * Get the current contents of the toolbar, both menu items
 * and desktop items, and load them into the vbox
 */
static void toolbarlist_populate(LayoutWindow *lw, GtkBox *box, ToolbarType bar)
{
	GList *work = g_list_first(lw->toolbar_actions[bar]);

	while (work)
		{
		auto name = static_cast<gchar *>(work->data);
		gchar *label;
		gchar *icon;
		work = work->next;

		if (file_extension_match(name, ".desktop"))
			{
			get_desktop_data(name, &label, &icon);
			}
		else
			{
			get_toolbar_item(name, &label, &icon);
			}

		if (g_strcmp0(name, "Separator") != 0)
			{
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
	GtkWidget *scrolled;
	GtkWidget *tbar;
	GtkWidget *add_box;

	if (!lw) return nullptr;

	if (!toolbarlist[bar])
		{
		toolbarlist[bar] = g_new0(ToolbarData, 1);
		}

	GtkWidget *widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_widget_show(widget);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
							GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gq_gtk_box_pack_start(GTK_BOX(widget), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	toolbarlist[bar]->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(toolbarlist[bar]->vbox);
	gq_gtk_container_add(GTK_WIDGET(scrolled), toolbarlist[bar]->vbox);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(gtk_bin_get_child(GTK_BIN(scrolled))),
																GTK_SHADOW_NONE);

	add_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(add_box);
	gq_gtk_box_pack_end(GTK_BOX(widget), add_box, FALSE, FALSE, 0);
	tbar = pref_toolbar_new(add_box);

	GtkWidget *add_button = pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE,
	                                            _("Add Toolbar Item"),
	                                            G_CALLBACK(toolbar_menu_add_cb), toolbarlist[bar]);
	gtk_widget_show(add_button);

	toolbarlist_populate(lw,GTK_BOX(toolbarlist[bar]->vbox), bar);

	return widget;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
