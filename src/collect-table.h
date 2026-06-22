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

#ifndef COLLECT_TABLE_H
#define COLLECT_TABLE_H

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

struct CollectInfo;
struct CollectionData;

struct CollectTable
{
	GtkWidget *scrolled;
	GtkWidget *listview;
	gint columns;
	gint rows;

	CollectionData *cd;

	GList *selection;
	CollectInfo *prev_selection;

	CollectInfo *click_info;

	GtkWidget *tip_window;
	guint tip_delay_id; /**< event source id */
	CollectInfo *tip_info;

	GdkWindow *marker_window;
	CollectInfo *marker_info;

	GtkWidget *status_label;
	GtkWidget *extra_label;

	gint focus_row;
	gint focus_column;
	CollectInfo *focus_info;

	GtkWidget *popup;
	CollectInfo *drop_info;
	GList *drop_list;

	guint sync_idle_id; /**< event source id */
	guint drop_idle_id; /**< event source id */

	gboolean show_text;
	gboolean show_stars;
	gboolean show_infotext;

	GList *editmenu_fd_list; /**< file list for edit menu */

	gint last_x;
	gint last_y;
	gboolean pointer_valid;
};

void collection_table_select_all(CollectTable *ct);
void collection_table_unselect_all(CollectTable *ct);

void collection_table_add_filelist(CollectTable *ct, GList *list);

void collection_table_file_update(CollectTable *ct, CollectInfo *info);
void collection_table_file_add(CollectTable *ct, CollectInfo *ci);
void collection_table_file_insert(CollectTable *ct, CollectInfo *ci);
void collection_table_file_remove(CollectTable *ct, CollectInfo *ci);
void collection_table_refresh(CollectTable *ct);

CollectTable *collection_table_new(CollectionData *cd);

void collection_table_set_labels(CollectTable *ct, GtkWidget *status, GtkWidget *extra);

CollectInfo *collection_table_get_focus_info(CollectTable *ct);
GList *collection_table_selection_get_list(CollectTable *ct);
void collection_table_set_focus(CollectTable *ct, CollectInfo *info);
void collection_table_select(CollectTable *ct, CollectInfo *info);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
