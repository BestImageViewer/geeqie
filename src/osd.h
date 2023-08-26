/*
 * Copyright (C) 2018 The Geeqie Team
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

#ifndef OSD_H
#define OSD_H

struct FileData;

enum OsdTemplateFlags {
	OSDT_NONE 	= 0,
	OSDT_FREE 	= 1 << 0,
	OSDT_NO_DUP 	= 1 << 1
};

GtkWidget *osd_new(gint max_cols, GtkWidget *template_view);
gchar *image_osd_mkinfo(const gchar *str, FileData *fd, GHashTable *vars);
void osd_template_insert(GHashTable *vars, const gchar *keyword, const gchar *value, OsdTemplateFlags flags);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
