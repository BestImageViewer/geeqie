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

#include "shortcuts.h"

#include <string>

#include <unistd.h>

#include <glib-object.h>
#include <glib.h>

#include "collect-io.h"
#include "filedata.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "utilops.h"

namespace
{

#define SHORTCUTS     "shortcuts"

struct ShortcutDrop
{
	std::string path;
	GList *files;
	GtkWidget *parent;
	~ShortcutDrop() { file_data_list_free(files); }
};

void shortcuts_transfer_cb(GSimpleAction *action, GVariant *, gpointer data)
{
	auto *pending = static_cast<ShortcutDrop *>(data);
	// File operations can change the widget tree and dismiss the menu.
	const auto path = pending->path;
	auto *parent = pending->parent;
	auto *files = filelist_copy(pending->files);
	if (g_str_equal(g_action_get_name(G_ACTION(action)), "move"))
		file_util_move_simple(files, path.c_str(), parent);
	else
		file_util_copy_simple(files, path.c_str(), parent);
}

void shortcuts_add_cb(GtkWidget *, gpointer data)
{
	auto *bookmarks = static_cast<GtkWidget *>(data);

	bookmark_add_dialog(_("Add Shortcut - Geeqie"), bookmarks);
}

} // namespace

GdkDragAction shortcuts_drop_files(const gchar *path, GList *list, const BookmarkDropInfo &info)
{
	if (!path || !list || !gtk_widget_get_mapped(info.widget)) return GDK_ACTION_NONE;
	if (isfile(path) && file_extension_match(path, GQ_COLLECTION_EXT))
		{
		if (!(info.allowed_actions & GDK_ACTION_COPY) || !access_file(path, R_OK | W_OK)) return GDK_ACTION_NONE;
		bool added = false;
		for (auto *work = list; work; work = work->next)
			{
			auto *fd = static_cast<FileData *>(work->data);
			if (!isfile(fd->path)) continue;
			collect_manager_add(fd, path);
			added = true;
			}
		return added ? GDK_ACTION_COPY : GDK_ACTION_NONE;
		}
	if (!isdir(path) || !access_file(path, W_OK | X_OK)) return GDK_ACTION_NONE;
	if (info.action == GDK_ACTION_COPY && (info.allowed_actions & GDK_ACTION_COPY))
		{
		file_util_copy_simple(filelist_copy(list), path, info.widget);
		return GDK_ACTION_COPY;
		}
	if (info.action == GDK_ACTION_MOVE && (info.allowed_actions & GDK_ACTION_MOVE))
		{
		file_util_move_simple(filelist_copy(list), path, info.widget);
		return GDK_ACTION_MOVE;
		}
	// Finish deferred menu drops as COPY so the source never removes its files.
	if (!(info.allowed_actions & GDK_ACTION_COPY)) return GDK_ACTION_NONE;
	auto *parent = GTK_WIDGET(gtk_widget_get_root(info.widget));
	g_autoptr(GMenu) menu = g_menu_new();
	g_menu_append(menu, _("_Copy"), "shortcut-drop.copy");
	g_menu_append(menu, _("_Move"), "shortcut-drop.move");
	g_menu_append(menu, _("Cancel"), nullptr);
	auto *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	g_object_set_data_full(G_OBJECT(popover), "shortcut-drop-data",
	                       new ShortcutDrop{path, filelist_copy(list), parent},
	                       +[](gpointer data) { delete static_cast<ShortcutDrop *>(data); });
	auto *pending = g_object_get_data(G_OBJECT(popover), "shortcut-drop-data");
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new();
	for (const auto *name : {"copy", "move"})
		{
		g_autoptr(GSimpleAction) action = g_simple_action_new(name, nullptr);
		g_simple_action_set_enabled(action, g_str_equal(name, "copy") || (info.allowed_actions & GDK_ACTION_MOVE));
		g_signal_connect(action, "activate", G_CALLBACK(shortcuts_transfer_cb), pending);
		g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));
		}
	gtk_widget_insert_action_group(popover, "shortcut-drop", G_ACTION_GROUP(group));
	popover_set_parent(popover, parent);
	graphene_point_t point{};
	if (!gtk_widget_compute_point(info.widget, parent, &info.position, &point)) point = info.position;
	GdkRectangle anchor{static_cast<int>(point.x), static_cast<int>(point.y), 1, 1};
	gtk_popover_set_pointing_to(GTK_POPOVER(popover), &anchor);
	popover_popup(popover);
	return GDK_ACTION_COPY;
}

GtkWidget *shortcuts_new(LayoutWindow *lw)
{
	if (!lw) return nullptr;

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	const auto shortcuts_bookmark_select = [lw](const gchar *path) { layout_set_path(lw, path); };
	GtkWidget *bookmarks = bookmark_list_new(SHORTCUTS, shortcuts_bookmark_select);
	bookmark_list_set_drop_func(bookmarks, shortcuts_drop_files);
	gtk_widget_set_hexpand(bookmarks, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(vbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(bookmarks, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(vbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(vbox), bookmarks);

	GtkWidget *tbar = pref_toolbar_new(vbox);

	pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE,
	                    _("Add Shortcut"),
	                    G_CALLBACK(shortcuts_add_cb), bookmarks);

	return vbox;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
