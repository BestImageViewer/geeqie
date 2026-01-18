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

#include <gdk/gdk.h>

#include "collect-io.h"
#include "editors.h"
#include "image.h"
#include "intl.h"
#include "main-defines.h"
#include "pixbuf-util.h"
#include "sort-type.h"
#include "ui-menu.h"
#include "ui-misc.h"

/*
 *-----------------------------------------------------------------------------
 * menu utils
 *-----------------------------------------------------------------------------
 */

gpointer submenu_item_get_data(GtkWidget *submenu_item)
{
	GtkWidget *submenu = gtk_widget_get_parent(submenu_item);
	if (!submenu || !GTK_IS_MENU(submenu)) return nullptr;

	return g_object_get_data(G_OBJECT(submenu), "submenu_data");
}

/*
 *-----------------------------------------------------------------------------
 * edit menu
 *-----------------------------------------------------------------------------
 */

static void add_edit_items(GtkWidget *menu, GCallback func, GList *fd_list)
{
	EditorsList editors_list = editor_list_get();

	for (const EditorDescription *editor : editors_list)
		{
		if (fd_list && editor_errors(editor_command_parse(editor, fd_list, FALSE, nullptr))) continue;

		const gchar *stock_id = nullptr;
		gchar *key = g_strdup(editor->key);

		if (editor->icon && register_theme_icon_as_stock(key, editor->icon))
			{
			stock_id = key;
			}

		GtkWidget *item = menu_item_add_stock(menu, editor->name, stock_id, func, key);
		g_signal_connect_swapped(G_OBJECT(item), "destroy", G_CALLBACK(g_free), key);
		}
}

GtkWidget *submenu_add_edit(GtkWidget *menu, gboolean sensitive, GList *fd_list, GCallback func, gpointer data)
{
	GtkWidget *submenu;
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new();

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);
	gtk_menu_set_accel_group(GTK_MENU(submenu), accel_group);
	g_object_set_data(G_OBJECT(submenu), "accel_group", accel_group);

	add_edit_items(submenu, func, fd_list);

	GtkWidget *item = menu_item_add(menu, _("_Plugins"), nullptr, nullptr);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	gtk_widget_set_sensitive(item, sensitive);

	return submenu;
}

/*
 *-----------------------------------------------------------------------------
 * sorting
 *-----------------------------------------------------------------------------
 */

GtkWidget *submenu_add_sort(GtkWidget *menu, GCallback func, gpointer data,
                            gboolean show_current, SortType type)
{
	GtkWidget *submenu;

	if (menu)
		{
		submenu = gtk_menu_new();

		GtkWidget *item = menu_item_add(menu, _("_Sort"), nullptr, nullptr);
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		}
	else
		{
		submenu = popup_menu_short_lived();
		}

	if (!show_current) g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	for (const SortType sort_type : { SORT_NAME, SORT_NUMBER, SORT_TIME, SORT_CTIME, SORT_EXIFTIME,
	                                  SORT_EXIFTIMEDIGITIZED, SORT_SIZE, SORT_RATING, SORT_CLASS })
		{
		if (show_current)
			{
			menu_item_add_radio(submenu, sort_type_get_text(sort_type),
			                    GINT_TO_POINTER(sort_type), sort_type == type,
			                    func, data);
			}
		else
			{
			menu_item_add(submenu, sort_type_get_text(sort_type),
			              func, GINT_TO_POINTER(sort_type));
			}
		}

	return submenu;
}

/*
 *-----------------------------------------------------------------------------
 * altering
 *-----------------------------------------------------------------------------
 */

static const gchar *alter_type_get_text(AlterType type)
{
	switch (type)
		{
		case ALTER_ROTATE_90:
			return _("Rotate clockwise 90°");
			break;
		case ALTER_ROTATE_90_CC:
			return _("Rotate counterclockwise 90°");
			break;
		case ALTER_ROTATE_180:
			return _("Rotate 180°");
			break;
		case ALTER_MIRROR:
			return _("Mirror");
			break;
		case ALTER_FLIP:
			return _("Flip");
			break;
		case ALTER_NONE:
			return _("Original state");
			break;
		default:
			break;
		}

	return nullptr;
}

static void submenu_add_alter_item(GtkWidget *menu, GCallback func, AlterType type,
                                   GtkAccelGroup *accel_group, guint accel_key, guint accel_mods)
{
	GtkWidget *item = menu_item_add_simple(menu, alter_type_get_text(type), func, GINT_TO_POINTER(type));
	gtk_widget_add_accelerator(item, "activate", accel_group, accel_key, static_cast<GdkModifierType>(accel_mods), GTK_ACCEL_VISIBLE);
}

GtkWidget *submenu_add_alter(GtkWidget *menu, GCallback func, gpointer data)
{
	GtkWidget *submenu;

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	GtkAccelGroup *accel_group = gtk_accel_group_new();

	submenu_add_alter_item(submenu, func, ALTER_ROTATE_90, accel_group, ']', 0);
	submenu_add_alter_item(submenu, func, ALTER_ROTATE_90_CC, accel_group, '[', 0);
	submenu_add_alter_item(submenu, func, ALTER_ROTATE_180, accel_group, 'R', GDK_SHIFT_MASK);
	submenu_add_alter_item(submenu, func, ALTER_MIRROR, accel_group, 'M', GDK_SHIFT_MASK);
	submenu_add_alter_item(submenu, func, ALTER_FLIP, accel_group, 'F', GDK_SHIFT_MASK);
	submenu_add_alter_item(submenu, func, ALTER_NONE, accel_group, 'O', GDK_SHIFT_MASK);

	if (menu)
		{
		GtkWidget *item;

		item = menu_item_add(menu, _("_Orientation"), nullptr, nullptr);
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		return item;
		}

	return submenu;
}

/*
 *-----------------------------------------------------------------------------
 * collections
 *-----------------------------------------------------------------------------
 */

/**
 * @brief Add submenu consisting of "New collection", and list of existing collections to a right-click menu.
 * @param[in] menu
 * @param[in] sensitive
 * @param[in] func
 * @param[in] data
 *
 *  Used by all image windows
 */
GtkWidget *submenu_add_collections(GtkWidget *menu, gboolean sensitive,
                                   GCallback func, gpointer data)
{
	GtkWidget *submenu;
	GList *collection_list = nullptr;

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	menu_item_add_icon_sensitive(submenu, _("New collection"), PIXBUF_INLINE_COLLECTION,
	                             TRUE, G_CALLBACK(func), GINT_TO_POINTER(-1));
	menu_item_add_divider(submenu);

	collect_manager_list(&collection_list,nullptr,nullptr);

	gint index = 0; /* index to existing collection list menu item selected */
	for (GList *work = collection_list; work; work = work->next, index++)
		{
		auto *collection_name = static_cast<gchar *>(work->data);
		menu_item_add(submenu, collection_name, func, GINT_TO_POINTER(index));
		}

	GtkWidget *item = menu_item_add(menu, _("_Add to Collection"), nullptr, nullptr);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	gtk_widget_set_sensitive(item, sensitive);

	g_list_free_full(collection_list, g_free);

	return submenu;
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
template<gboolean up, gboolean single_step>
static void widget_move_cb(GtkWidget *, gpointer data)
{
	auto *widget = static_cast<GtkWidget *>(data);
	if (!widget) return;

	GtkWidget *box = gtk_widget_get_ancestor(widget, GTK_TYPE_BOX);
	if (!box) return;

	gint pos = 0;
	gtk_container_child_get(GTK_CONTAINER(box), widget, "position", &pos, NULL);

	if (single_step)
		{
		pos = up ? (pos - 1) : (pos + 1);
		pos = std::max(pos, 0);
		}
	else
		{
		pos = up ? 0 : -1;
		}

	gtk_box_reorder_child(GTK_BOX(box), widget, pos);
}

void popup_menu_bar(GtkWidget *widget, GCallback expander_height_cb)
{
	GtkWidget *menu = popup_menu_short_lived();

	if (widget)
		{
		menu_item_add_icon(menu, _("Move to _top"), GQ_ICON_GO_TOP,
		                   (GCallback)widget_move_cb<TRUE, FALSE>, widget);
		menu_item_add_icon(menu, _("Move _up"), GQ_ICON_GO_UP,
		                   (GCallback)widget_move_cb<TRUE, TRUE>, widget);
		menu_item_add_icon(menu, _("Move _down"), GQ_ICON_GO_DOWN,
		                   (GCallback)widget_move_cb<FALSE, TRUE>, widget);
		menu_item_add_icon(menu, _("Move to _bottom"), GQ_ICON_GO_BOTTOM,
		                   (GCallback)widget_move_cb<FALSE, FALSE>, widget);
		menu_item_add_divider(menu);

		if (expander_height_cb && gtk_expander_get_expanded(GTK_EXPANDER(widget)))
			{
			menu_item_add_icon(menu, _("Height..."), GQ_ICON_PREFERENCES,
			                   G_CALLBACK(expander_height_cb), widget);
			menu_item_add_divider(menu);
			}

		menu_item_add_icon(menu, _("Remove"), GQ_ICON_DELETE,
		                   G_CALLBACK(widget_remove_from_parent_cb), widget);
		menu_item_add_divider(menu);
		}

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
