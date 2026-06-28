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

#include "dnd.h"

namespace
{

constexpr auto GTK4_DRAG_SOURCE_CONTROLLER_DATA_KEY = "gq-gtk4-drag-source-controller";
constexpr auto GTK4_DROP_TARGET_CONTROLLER_DATA_KEY = "gq-gtk4-drop-target-controller";

} // namespace name

void drag_signal_connect(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data)
{
	g_signal_connect(instance, detailed_signal, c_handler, data);
}

void drag_signal_swapped(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data)
{
	g_signal_connect_swapped(instance, detailed_signal, c_handler, data);
}

void drag_source_set(GtkWidget *widget, guint button, gpointer, gint, GdkDragAction actions)
{
	auto *controller = static_cast<GtkEventController *>(g_object_get_data(G_OBJECT(widget), GTK4_DRAG_SOURCE_CONTROLLER_DATA_KEY));
	if (controller)
		{
		gtk_widget_remove_controller(widget, controller);
		}

	GtkDragSource *drag_source = gtk_drag_source_new();
	gtk_drag_source_set_actions(drag_source, actions);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_source), button);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag_source));
	g_object_set_data(G_OBJECT(widget), GTK4_DRAG_SOURCE_CONTROLLER_DATA_KEY, drag_source);
}

void drag_dest_set(GtkWidget *widget, const char **mime_types, guint n_mime_types, GdkDragAction actions)
{
	auto *controller = static_cast<GtkEventController *>(g_object_get_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY));
	if (controller)
		{
		gtk_widget_remove_controller(widget, controller);
		}

	GdkContentFormats *formats = gdk_content_formats_new(mime_types, n_mime_types);
	GtkDropTargetAsync *drop_target = gtk_drop_target_async_new(formats, actions);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drop_target));
	g_object_set_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY, drop_target);
}

void drag_dest_unset(GtkWidget *widget)
{
	auto *controller = static_cast<GtkEventController *>(g_object_get_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY));
	if (!controller) return;

	g_object_set_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY, nullptr);
	gtk_widget_remove_controller(widget, controller);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
