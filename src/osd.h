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

#include <string>
#include <unordered_map>

#include <glib.h>
#include <gtk/gtk.h>

class FileData;

using OsdTemplate = std::unordered_map<std::string, std::string>;

GtkWidget *osd_new(gint max_cols, GtkWidget *template_view);
gchar *image_osd_mkinfo(const gchar *str, FileData *fd, const OsdTemplate &vars);
void osd_template_insert(OsdTemplate &vars, const gchar *keyword, const gchar *value);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
