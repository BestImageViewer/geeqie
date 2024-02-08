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

#ifndef COLLECT_DLG_H
#define COLLECT_DLG_H

struct CollectionData;

void collection_dialog_save_as(CollectionData *cd);
void collection_dialog_save_close(CollectionData *cd);

//~ void collection_dialog_load(gchar *path);
void collection_dialog_append(CollectionData *cd);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
