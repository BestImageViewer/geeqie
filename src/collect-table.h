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

#include "collect.h"

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
