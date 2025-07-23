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

#include <glib-object.h>
#include <glib.h>

#include "collect.h"
#include "compat.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"
#include "ui-misc.h"

namespace
{

#define SHORTCUTS     "shortcuts"

void shortcuts_add_cb(GtkWidget *, gpointer data)
{
	auto *bookmarks = static_cast<GtkWidget *>(data);

	bookmark_add_dialog(_("Add Shortcut - Geeqie"), bookmarks);
}

} // namespace

GtkWidget *shortcuts_new(LayoutWindow *lw)
{
	if (!lw) return nullptr;

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	const auto shortcuts_bookmark_select = [lw](const gchar *path)
	{
		if (file_extension_match(path, GQ_COLLECTION_EXT))
			{
			collection_window_new(path);
			}
		else
			{
			layout_set_path(lw, path);
			}
	};
	GtkWidget *bookmarks = bookmark_list_new(SHORTCUTS, shortcuts_bookmark_select);
	gq_gtk_box_pack_start(GTK_BOX(vbox), bookmarks, TRUE, TRUE, 0);
	gtk_widget_show(bookmarks);

	GtkWidget *tbar = pref_toolbar_new(vbox);

	pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE,
	                    _("Add Shortcut"),
	                    G_CALLBACK(shortcuts_add_cb), bookmarks);

	gtk_widget_show(vbox);
	return vbox;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
