/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: John Ellis, Vladimir Nadvornik, Laurent Monin
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

#ifndef URI_UTILS_H
#define URI_UTILS_H

#include <gtk/gtk.h>

void warning_dialog_dnd_uri_error(GList *uri_error_list);
GList *uri_filelist_from_uris(gchar **uris, GList **uri_error_list);
gchar **uris_from_pathlist(GList *list);
gchar **uris_from_filelist(GList *list);
GList *uri_pathlist_from_uris(gchar **uris, GList **uri_error_list);
gboolean uri_selection_data_set_uris_from_filelist(GtkSelectionData *selection_data, GList *list);
GList *uri_filelist_from_gtk_selection_data(GtkSelectionData *selection_data);

#endif /* URI_UTILS_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
