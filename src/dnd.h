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

#ifndef DND_H
#define DND_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "main-defines.h"

#define TARGET_APP_COLLECTION_MEMBER_STRING "application/x-" GQ_APPNAME_LC "-collection-member"
#define TARGET_APP_EXIF_ENTRY_STRING "application/x-" GQ_APPNAME_LC "-exif-entry"
#define TARGET_APP_KEYWORD_PATH_STRING "application/x-" GQ_APPNAME_LC "-keyword-path"

enum {
	TARGET_APP_COLLECTION_MEMBER,
	TARGET_APP_EXIF_ENTRY,
	TARGET_APP_KEYWORD_PATH,
	TARGET_URI_LIST,
	TARGET_TEXT_PLAIN
};


extern GtkTargetEntry dnd_file_drag_types[];
extern gint dnd_file_drag_types_count;

extern GtkTargetEntry dnd_file_drop_types[];
extern gint dnd_file_drop_types_count;


/**
 * @headerfile dnd_set_drag_icon
 * sets a drag icon to pixbuf, if items is > 1, text is drawn onto icon to indicate value
 */
void dnd_set_drag_icon(GtkWidget *widget, GdkDragContext *context, GdkPixbuf *pixbuf, gint items);

void dnd_set_drag_label(GtkWidget *widget, GdkDragContext *context, const gchar *text);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
