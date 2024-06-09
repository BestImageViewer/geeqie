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

#ifndef PRINT_H
#define PRINT_H

#include <glib.h>
#include <gtk/gtk.h>

class FileData;

/**
 * @headerfile print_window_new
 * do not free selection or list, the print window takes control of them
 */
void print_window_new(FileData *fd, GList *selection, GList *list, GtkWidget *parent);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
