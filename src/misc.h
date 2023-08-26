/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#ifndef MISC_H
#define MISC_H

struct FileData;

gdouble get_zoom_increment();
gchar *utf8_validate_or_convert(const gchar *text);
gint utf8_compare(const gchar *s1, const gchar *s2, gboolean case_sensitive);
gchar *expand_tilde(const gchar *filename);
int runcmd(const gchar *cmd);
gchar *decode_geo_parameters(const gchar *input_text);
gint date_get_first_day_of_week();
gchar *date_get_abbreviated_day_name(gint day);
gchar *convert_rating_to_stars(gint rating);
gchar *get_symbolic_link(const gchar *path_utf8);
gint get_cpu_cores();
gchar *open_archive(FileData *fd);
#endif /* MISC_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
