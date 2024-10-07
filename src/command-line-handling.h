/*
 * Copyright (C) 2024 The Geeqie Team
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

#ifndef COMMAND_LINE_HANDLING_H
#define COMMAND_LINE_HANDLING_H

#include <gtk/gtk.h>

gint process_command_line(GtkApplication *app, GApplicationCommandLine *app_command_line, gpointer data);
gint process_command_line_cache_maintenance(GtkApplication *app, GApplicationCommandLine *app_command_line, gpointer data);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
