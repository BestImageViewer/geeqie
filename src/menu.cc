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
#include "intl.h"
#include "pixbuf-util.h"
#include "ui-menu.h"

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
static void edit_item_destroy_cb(GtkWidget *, gpointer data)
{
	g_free(data);
}

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
		g_signal_connect(G_OBJECT(item), "destroy", G_CALLBACK(edit_item_destroy_cb), key);
		}
}

GtkWidget *submenu_add_edit(GtkWidget *menu, GtkWidget **menu_item, GCallback func, gpointer data, GList *fd_list)
{
	GtkWidget *item;
	GtkWidget *submenu;
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new();
	item = menu_item_add(menu, _("_Plugins"), nullptr, nullptr);

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);
	gtk_menu_set_accel_group(GTK_MENU(submenu), accel_group);
	g_object_set_data(G_OBJECT(submenu), "accel_group", accel_group);

	add_edit_items(submenu, func, fd_list);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);

	if (menu_item) *menu_item = item;

	return submenu;
}

/*
 *-----------------------------------------------------------------------------
 * sorting
 *-----------------------------------------------------------------------------
 */

gchar *sort_type_get_text(SortType method)
{
	switch (method)
		{
		case SORT_SIZE:
			return _("Sort by size");
			break;
		case SORT_TIME:
			return _("Sort by date");
			break;
		case SORT_CTIME:
			return _("Sort by file creation date");
			break;
		case SORT_EXIFTIME:
			return _("Sort by Exif date original");
			break;
		case SORT_EXIFTIMEDIGITIZED:
			return _("Sort by Exif date digitized");
			break;
		case SORT_NONE:
			return _("Unsorted");
			break;
		case SORT_PATH:
			return _("Sort by path");
			break;
		case SORT_NUMBER:
			return _("Sort by number");
			break;
		case SORT_RATING:
			return _("Sort by rating");
			break;
		case SORT_CLASS:
			return _("Sort by class");
			break;
		case SORT_NAME:
		default:
			return _("Sort by name");
			break;
		}

	return nullptr;
}

bool sort_type_requires_metadata(SortType method)
{
	return method == SORT_EXIFTIME
	    || method == SORT_EXIFTIMEDIGITIZED
	    || method == SORT_RATING;
}

static GtkWidget *submenu_add_sort_item(GtkWidget *menu,
					GCallback func, SortType type,
					gboolean show_current, SortType show_type)
{
	GtkWidget *item;

	if (show_current)
		{
		item = menu_item_add_radio(menu,
					   sort_type_get_text(type), GINT_TO_POINTER((gint)type), (type == show_type),
					   func, GINT_TO_POINTER((gint)type));
		}
	else
		{
		item = menu_item_add(menu, sort_type_get_text(type),
				     func, GINT_TO_POINTER((gint)type));
		}

	return item;
}

GtkWidget *submenu_add_sort(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean include_none, gboolean include_path,
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

	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	submenu_add_sort_item(submenu, func, SORT_NAME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_NUMBER, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_TIME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_CTIME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_EXIFTIME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_EXIFTIMEDIGITIZED, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_SIZE, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_RATING, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_CLASS, show_current, type);
	if (include_path) submenu_add_sort_item(submenu, func, SORT_PATH, show_current, type);
	if (include_none) submenu_add_sort_item(submenu, func, SORT_NONE, show_current, type);

	return submenu;
}

GtkWidget *submenu_add_dir_sort(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean include_none, gboolean include_path,
			    gboolean show_current, SortType type)
{
	GtkWidget *submenu;

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	submenu_add_sort_item(submenu, func, SORT_NAME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_NUMBER, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_TIME, show_current, type);
	if (include_path) submenu_add_sort_item(submenu, func, SORT_PATH, show_current, type);
	if (include_none) submenu_add_sort_item(submenu, func, SORT_NONE, show_current, type);

	if (menu)
		{
		GtkWidget *item = menu_item_add(menu, _("_Sort"), nullptr, nullptr);
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		}

	return submenu;
}

/*
 *-----------------------------------------------------------------------------
 * altering
 *-----------------------------------------------------------------------------
 */

static gchar *alter_type_get_text(AlterType type)
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
 * @param[out] menu_item
 * @param[in] func
 * @param[in] data
 *
 *  Used by all image windows
 */
GtkWidget *submenu_add_collections(GtkWidget *menu, GtkWidget **menu_item,
										GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkWidget *submenu;
	GList *collection_list = nullptr;

	item = menu_item_add(menu, _("_Add to Collection"), nullptr, nullptr);

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

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	if (menu_item) *menu_item = item;

	g_list_free_full(collection_list, g_free);

	return submenu;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
