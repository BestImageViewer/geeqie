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

#include <cstring>

#include <gio/gio.h>

#include "filedata.h"
#include "ui-fileops.h"
#include "uri-utils.h"

namespace
{

constexpr auto GTK4_DRAG_SOURCE_CONTROLLER_DATA_KEY = "gq-gtk4-drag-source-controller";
constexpr auto GTK4_DROP_TARGET_CONTROLLER_DATA_KEY = "gq-gtk4-drop-target-controller";

struct DndFileListReadData
{
	DndFileListCallback callback;
	gpointer data;
	GdkDrop *drop;
	GInputStream *stream;
	GString *text;
};

struct DndTextReadData
{
	DndTextCallback callback;
	gpointer data;
};

GList *dnd_file_list_from_uri_text(const gchar *text)
{
	GList *path_list = uri_pathlist_from_text(text);
	GList *list = nullptr;

	for (GList *work = path_list; work; work = work->next)
		{
		const auto *path = static_cast<const gchar *>(work->data);
		list = g_list_prepend(list, file_data_new_no_grouping(path));
		}

	g_list_free_full(path_list, g_free);

	return g_list_reverse(list);
}

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

GdkContentProvider *dnd_file_list_content_provider(GList *list)
{
	g_autofree gchar *uri_text = uri_text_from_filelist(list);
	if (!uri_text || uri_text[0] == '\0') return nullptr;

	g_autoptr(GBytes) bytes = g_bytes_new(uri_text, strlen(uri_text));

	return gdk_content_provider_new_for_bytes("text/uri-list", bytes);
}

static void dnd_read_file_list_stream_cb(GObject *source_object, GAsyncResult *result, gpointer data)
{
	auto *read_data = static_cast<DndFileListReadData *>(data);
	auto *stream = G_INPUT_STREAM(source_object);
	g_autoptr(GError) error = nullptr;
	g_autoptr(GBytes) bytes = g_input_stream_read_bytes_finish(stream, result, &error);

	if (!error && bytes && g_bytes_get_size(bytes) > 0)
		{
		g_string_append_len(read_data->text,
		                    static_cast<const gchar *>(g_bytes_get_data(bytes, nullptr)),
		                    g_bytes_get_size(bytes));
		g_input_stream_read_bytes_async(read_data->stream, 4096, G_PRIORITY_DEFAULT, nullptr,
						dnd_read_file_list_stream_cb, read_data);
		return;
		}

	if (error)
		{
		DEBUG_1("File drop read failed: %s", error->message);
		}

	g_autoptr(FileDataList) list = nullptr;
	if (!error)
		{
		list = dnd_file_list_from_uri_text(read_data->text->str);
	}

	read_data->callback(read_data->drop, list, read_data->data);
	g_object_unref(read_data->stream);
	g_object_unref(read_data->drop);
	g_string_free(read_data->text, TRUE);
	g_free(read_data);
}

static void dnd_read_file_list_cb(GObject *source_object, GAsyncResult *result, gpointer data)
{
	auto *read_data = static_cast<DndFileListReadData *>(data);
	GdkDrop *drop = GDK_DROP(source_object);
	const gchar *mime_type = nullptr;
	g_autoptr(GError) error = nullptr;
	g_autoptr(GInputStream) stream = gdk_drop_read_finish(drop, result, &mime_type, &error);

	if (!stream || g_strcmp0(mime_type, "text/uri-list") != 0 || error)
		{
		if (error)
			{
			DEBUG_1("File drop start failed: %s", error->message);
			}

		read_data->callback(drop, nullptr, read_data->data);
		g_free(read_data);
		return;
		}

	read_data->drop = GDK_DROP(g_object_ref(drop));
	read_data->stream = g_steal_pointer(&stream);
	read_data->text = g_string_new(nullptr);

	g_input_stream_read_bytes_async(read_data->stream, 4096, G_PRIORITY_DEFAULT, nullptr,
					dnd_read_file_list_stream_cb, read_data);
}

void dnd_read_file_list_async(GdkDrop *drop, DndFileListCallback callback, gpointer data)
{
	static const gchar *mime_types[] = {"text/uri-list", nullptr};
	auto *read_data = g_new0(DndFileListReadData, 1);
	read_data->callback = callback;
	read_data->data = data;

	gdk_drop_read_async(drop, mime_types, G_PRIORITY_DEFAULT, nullptr, dnd_read_file_list_cb, read_data);
}

static void dnd_read_text_cb(GObject *source_object, GAsyncResult *result, gpointer data)
{
	g_autofree auto *read_data = static_cast<DndTextReadData *>(data);
	GdkDrop *drop = GDK_DROP(source_object);
	g_autoptr(GError) error = nullptr;
	const GValue *value = gdk_drop_read_value_finish(drop, result, &error);
	g_autofree gchar *dropped_text = nullptr;

	if (value && G_VALUE_HOLDS_STRING(value))
		{
		dropped_text = g_strdup(g_value_get_string(value));
		}

	read_data->callback(drop, dropped_text, read_data->data);
}

void dnd_read_text_async(GdkDrop *drop, DndTextCallback callback, gpointer data)
{
	auto *read_data = g_new(DndTextReadData, 1);
	read_data->callback = callback;
	read_data->data = data;

	gdk_drop_read_value_async(drop, G_TYPE_STRING, G_PRIORITY_DEFAULT, nullptr, dnd_read_text_cb, read_data);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
