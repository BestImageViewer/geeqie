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

#ifndef REMOTE_H
#define REMOTE_H


struct RemoteConnection;

using RemoteReadFunc = void (RemoteConnection *, const gchar *, GIOChannel *, gpointer);

struct RemoteConnection {
	gint server;
	gint fd;
	gchar *path;

	gint channel_id;
	RemoteReadFunc *read_func;
	gpointer read_data;

	GList *clients;
};


void remote_close(RemoteConnection *rc);
GList *remote_build_list(GList *list, gint argc, gchar *argv[], GList **errors);
void remote_help();
void remote_control(const gchar *arg_exec, GList *remote_list, const gchar *path,
		    GList *cmd_list, GList *collection_list);

RemoteConnection *remote_server_init(gchar *path, CollectionData *command_collection);
gboolean remote_server_exists(const gchar *path);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
