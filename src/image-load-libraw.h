/*
 * Copyright (C) 2021 The Geeqie Team
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

#ifndef IMAGE_LOAD_RAW_H
#define IMAGE_LOAD_RAW_H

#include <glib.h>

guchar *libraw_get_preview(const gchar *path, gsize &data_len);
void libraw_free_preview(const guchar *buf);

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
