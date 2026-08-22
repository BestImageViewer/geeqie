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

#include <algorithm>
#include <cstring>

#include <gio/gio.h>

#include "filedata.h"
#include "options.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "uri-utils.h"

namespace
{

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

GdkContentProvider *dnd_file_list_content_provider(GList *list)
{
	g_autofree gchar *uri_text = uri_text_from_filelist(list);
	if (!uri_text || uri_text[0] == '\0') return nullptr;

	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_object_unref);
	for (GList *work = list; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		g_ptr_array_add(files, g_file_new_for_path(fd->path));
		}

	GdkFileList *file_list = gdk_file_list_new_from_array(reinterpret_cast<GFile **>(files->pdata), files->len);
	g_autoptr(GBytes) bytes = g_bytes_new(uri_text, strlen(uri_text));
	GdkContentProvider *providers[] = {
		gdk_content_provider_new_typed(GDK_TYPE_FILE_LIST, file_list),
		gdk_content_provider_new_for_bytes("text/uri-list", bytes)
	};
	g_boxed_free(GDK_TYPE_FILE_LIST, file_list);

	return gdk_content_provider_new_union(providers, G_N_ELEMENTS(providers));
}

GdkContentFormats *dnd_file_drop_formats(gboolean include_text)
{
	GdkContentFormatsBuilder *builder = gdk_content_formats_builder_new();
	gdk_content_formats_builder_add_gtype(builder, GDK_TYPE_FILE_LIST);
	gdk_content_formats_builder_add_mime_type(builder, "text/uri-list");
	if (include_text) gdk_content_formats_builder_add_mime_type(builder, "text/plain");

	return gdk_content_formats_builder_free_to_formats(builder);
}

void dnd_set_drag_icon(GtkDragSource *source, GdkPixbuf *pixbuf, guint items, FileData *fd)
{
	g_autoptr(GdkPixbuf) fallback = nullptr;
	if (!pixbuf && fd)
		{
		fallback = pixbuf_fallback(fd, options->dnd_icon_size, options->dnd_icon_size);
		pixbuf = fallback;
		}
	if (!pixbuf) return;

	const gint source_width = gdk_pixbuf_get_width(pixbuf);
	const gint source_height = gdk_pixbuf_get_height(pixbuf);
	const gint max_size = options->dnd_icon_size;
	const gdouble scale = std::min(1.0, static_cast<gdouble>(max_size) / std::max(source_width, source_height));
	const gint width = std::max(1, static_cast<gint>(source_width * scale));
	const gint height = std::max(1, static_cast<gint>(source_height * scale));

	g_autoptr(GdkPixbuf) icon = gdk_pixbuf_scale_simple(pixbuf, width, height, GDK_INTERP_BILINEAR);
	if (!icon) return;

	const GqColor border_color{0, 0, 0, 255};
	pixbuf_draw_rect_fill(icon, {0, 0, width, 1}, border_color);
	pixbuf_draw_rect_fill(icon, {0, height - 1, width, 1}, border_color);
	pixbuf_draw_rect_fill(icon, {0, 0, 1, height}, border_color);
	pixbuf_draw_rect_fill(icon, {width - 1, 0, 1, height}, border_color);

	if (items > 1)
		{
		GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
		g_autoptr(PangoLayout) layout = gtk_widget_create_pango_layout(widget, nullptr);
		g_autofree gchar *text = g_strdup_printf("<small> %u </small>", items);
		pango_layout_set_markup(layout, text, -1);

		gint label_width;
		gint label_height;
		pango_layout_get_pixel_size(layout, &label_width, &label_height);
		const gint x = std::max(0, width - label_width);
		const gint y = std::max(0, height - label_height);
		label_width = std::clamp(label_width, 0, width - x);
		label_height = std::clamp(label_height, 0, height - y);

		pixbuf_draw_rect_fill(icon, {x, y, label_width, label_height}, {128, 128, 128, 255});
		pixbuf_draw_layout(icon, layout, x + 1, y + 1, {0, 0, 0, 255});
		pixbuf_draw_layout(icon, layout, x, y, {255, 255, 255, 255});
		}

	g_autoptr(GdkTexture) texture = pixbuf_to_texture(icon);
	gtk_drag_source_set_icon(source, GDK_PAINTABLE(texture), -8, -6);
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

static void dnd_read_file_list_value_cb(GObject *source_object, GAsyncResult *result, gpointer data)
{
	auto *read_data = static_cast<DndFileListReadData *>(data);
	GdkDrop *drop = GDK_DROP(source_object);
	g_autoptr(GError) error = nullptr;
	const GValue *value = gdk_drop_read_value_finish(drop, result, &error);
	GList *list = nullptr;

	if (value && G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST))
		{
		auto *file_list = static_cast<GdkFileList *>(g_value_get_boxed(value));
		for (GSList *work = gdk_file_list_get_files(file_list); work; work = work->next)
			{
			g_autofree gchar *path = g_file_get_path(G_FILE(work->data));
			if (path) list = g_list_prepend(list, file_data_new_no_grouping(path));
			}
		list = g_list_reverse(list);
		}

	read_data->callback(drop, list, read_data->data);
	file_data_list_free(list);
	g_free(read_data);
}

void dnd_read_file_list_async(GdkDrop *drop, DndFileListCallback callback, gpointer data)
{
	auto *read_data = g_new0(DndFileListReadData, 1);
	read_data->callback = callback;
	read_data->data = data;

	if (gdk_content_formats_contain_gtype(gdk_drop_get_formats(drop), GDK_TYPE_FILE_LIST))
		{
		gdk_drop_read_value_async(drop, GDK_TYPE_FILE_LIST, G_PRIORITY_DEFAULT, nullptr,
		                          dnd_read_file_list_value_cb, read_data);
		}
	else
		{
		static const gchar *mime_types[] = {"text/uri-list", nullptr};
		gdk_drop_read_async(drop, mime_types, G_PRIORITY_DEFAULT, nullptr, dnd_read_file_list_cb, read_data);
		}
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
