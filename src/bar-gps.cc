/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
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

#include "bar-gps.h"

#include <string>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <shumate/shumate.h>

#include <config.h>

#include "bar.h"
#include "compat.h"
#include "dnd.h"
#include "filedata.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "metadata.h"
#include "misc.h"
#include "rcfile.h"
#include "ui-menu.h"
#include "ui-utildlg.h"
#include "uri-utils.h"

namespace
{

constexpr gint DEFAULT_ZOOM = 7;
constexpr const gchar *DEFAULT_MAP_ID = SHUMATE_MAP_SOURCE_OSM_MAPNIK;

/*
 *-------------------------------------------------------------------
 * GPS Map utils
 *-------------------------------------------------------------------
 */

struct PaneGPSData
{
	PaneData pane;
	GtkWidget *widget;
	gchar *map_source;
	gint height;
	FileData *fd;
	ShumateSimpleMap *map;
	ShumateMarkerLayer *marker_layer;
	ShumateViewport *viewport;
	GtkWidget *map_popover_parent;
	GList *selection_list;
	GList *not_added;
	guint num_added;
	guint create_markers_id;
	GList *geocode_list;
	GtkWidget *progress;
	gint selection_count;
	gboolean centre_map_checked;
	gboolean enable_markers_checked;
	gdouble dest_latitude;
	gdouble dest_longitude;
};

/*
 *-------------------------------------------------------------------
 * drag-and-drop
 *-------------------------------------------------------------------
 */

void bar_pane_gps_close_cancel_cb(GenericDialog *, gpointer data)
{
	auto *pgd = static_cast<PaneGPSData *>(data);

	file_data_list_free(pgd->geocode_list);
	pgd->geocode_list = nullptr;
}

void bar_pane_gps_close_save_cb(GenericDialog *, gpointer data)
{
	auto *pgd = static_cast<PaneGPSData *>(data);

	for (GList *work = g_list_first(pgd->geocode_list); work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		if (fd->name && !fd->parent)
			{
			metadata_write_GPS_coord(fd, "Xmp.exif.GPSLatitude", pgd->dest_latitude);
			metadata_write_GPS_coord(fd, "Xmp.exif.GPSLongitude", pgd->dest_longitude);
			}
		}

	file_data_list_free(pgd->geocode_list);
	pgd->geocode_list = nullptr;
}

void bar_pane_gps_dnd_file_received(GdkDrop *drop, GList *list, gpointer data)
{
	auto *pgd = static_cast<PaneGPSData *>(data);
	auto action = GDK_ACTION_NONE;

	gint count = 0;
	gint geocoded_count = 0;

	file_data_list_free(pgd->geocode_list);
	pgd->geocode_list = nullptr;

	for (GList *work = list; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		if (fd->name && !fd->parent)
			{
			count++;
			pgd->geocode_list = g_list_append(pgd->geocode_list, file_data_ref(fd));
			gdouble latitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 1000);
			gdouble longitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 1000);
			if (latitude != 1000 && longitude != 1000)
				{
				geocoded_count++;
				}
			}
		}

	if (count)
		{
		g_autoptr(GString) message = g_string_new("");
		if (count == 1)
			{
			auto *fd_found = static_cast<FileData *>(g_list_first(pgd->geocode_list)->data);
			g_string_append_printf(message, _("\nDo you want to geocode image %s?"), fd_found->name);
			}
		else
			{
			g_string_append_printf(message, _("\nDo you want to geocode %i images?"), count);
			}

		if (geocoded_count == 1 && count == 1)
			{
			g_string_append(message, _("\nThis image is already geocoded!"));
			}
		else if (geocoded_count == 1 && count > 1)
			{
			g_string_append(message, _("\nOne image is already geocoded!"));
			}
		else if (geocoded_count > 1 && count > 1)
			{
			g_string_append_printf(message, _("\n%i Images are already geocoded!"), geocoded_count);
			}

		g_string_append_printf(message, _("\n\nPosition: %lf %lf \n"), pgd->dest_latitude, pgd->dest_longitude);

		GenericDialog *gd = generic_dialog_new(_("Geocode images"), "geocode_images", nullptr, TRUE, bar_pane_gps_close_cancel_cb, pgd);
		generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Write lat/long to meta-data?"), message->str, TRUE);
		generic_dialog_add_button(gd, GQ_ICON_SAVE, _("Save"), bar_pane_gps_close_save_cb, TRUE);

		gtk_widget_show(gd->dialog);
		action = GDK_ACTION_COPY;
		}

	gdk_drop_finish(drop, action);
}

void bar_pane_gps_dnd_text_received(GdkDrop *drop, const gchar *text, gpointer data)
{
	auto *pgd = static_cast<PaneGPSData *>(data);
	auto action = GDK_ACTION_NONE;

	if (text)
		{
		g_autofree gchar *location = decode_geo_parameters(text);
		if (location && !g_strstr_len(location, -1, "Error"))
			{
			g_auto(GStrv) latlong = g_strsplit(location, " ", 2);
			if (latlong[0] && latlong[1])
				{
				shumate_map_center_on(shumate_simple_map_get_map(pgd->map),
				                      g_ascii_strtod(latlong[0], nullptr),
				                      g_ascii_strtod(latlong[1], nullptr));
				action = GDK_ACTION_COPY;
				}
			}
		}

	gdk_drop_finish(drop, action);
}

gboolean bar_pane_gps_dnd_drop(GtkDropTargetAsync *target, GdkDrop *drop, gdouble x, gdouble y, gpointer data)
{
	auto *pgd = static_cast<PaneGPSData *>(data);
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));

	GdkContentFormats *formats = gdk_drop_get_formats(drop);
	if (gdk_content_formats_contain_mime_type(formats, "text/uri-list"))
		{
		shumate_viewport_widget_coords_to_location(pgd->viewport, widget, x, y, &pgd->dest_latitude, &pgd->dest_longitude);
		dnd_read_file_list_async(drop, bar_pane_gps_dnd_file_received, pgd);
		return TRUE;
		}

	if (gdk_content_formats_contain_mime_type(formats, "text/plain"))
		{
		dnd_read_text_async(drop, bar_pane_gps_dnd_text_received, pgd);
		return TRUE;
		}

	return FALSE;
}

void bar_pane_gps_dnd_init(gpointer data)
{
	auto *pgd = static_cast<PaneGPSData *>(data);

	static const char *mime_types[] = {"text/uri-list", "text/plain"};
	GdkContentFormats *formats = gdk_content_formats_new(mime_types, G_N_ELEMENTS(mime_types));
	GtkDropTargetAsync *drop_target = gtk_drop_target_async_new(formats, static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
	g_signal_connect(drop_target, "drop", G_CALLBACK(bar_pane_gps_dnd_drop), pgd);
	gtk_widget_add_controller(pgd->widget, GTK_EVENT_CONTROLLER(drop_target));
}

void bar_pane_gps_add_marker(PaneGPSData *pgd, FileData *fd, gdouble latitude, gdouble longitude)
{
	(void)fd;
	ShumateMarker *marker = shumate_point_new();

	shumate_location_set_location(SHUMATE_LOCATION(marker), latitude, longitude);
	shumate_marker_layer_add_marker(pgd->marker_layer, marker);
}

gboolean bar_pane_gps_add_file_marker(PaneGPSData *pgd, FileData *fd)
{
	if (!pgd || !fd) return FALSE;

	const double lat = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 0);
	const double lon = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 0);

	if (lat == 0 && lon == 0)
		{
		return FALSE;
		}

	bar_pane_gps_add_marker(pgd, fd, lat, lon);
	if (pgd->centre_map_checked && pgd->selection_count == 1)
		{
		shumate_map_center_on(shumate_simple_map_get_map(pgd->map), lat, lon);
		}

	return TRUE;
}

void bar_pane_gps_set_status(PaneGPSData *pgd)
{
	if (!pgd->viewport) return;
}

void bar_pane_gps_clear_marker_queue(PaneGPSData *pgd)
{
	g_list_free(pgd->not_added);
	pgd->not_added = nullptr;
}

gboolean bar_pane_gps_create_markers_cb(gpointer data)
{
	auto *pgd = static_cast<PaneGPSData *>(data);

	if (!pgd || !pgd->not_added)
		{
		if (pgd)
			{
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgd->progress), 0);
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgd->progress), nullptr);
			pgd->create_markers_id = 0;
			}
		return G_SOURCE_REMOVE;
		}

	const gint selection_added = pgd->selection_count - g_list_length(pgd->not_added);
	if (pgd->selection_count > 0)
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgd->progress), static_cast<gdouble>(selection_added) / static_cast<gdouble>(pgd->selection_count));
		}
	g_autofree gchar *message = g_strdup_printf("%u/%i", selection_added, pgd->selection_count);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgd->progress), message);

	GList *work = pgd->not_added;
	auto *fd = static_cast<FileData *>(work->data);
	pgd->not_added = work->next;
	g_list_free_1(work);

	bar_pane_gps_add_file_marker(pgd, fd);

	bar_pane_gps_set_status(pgd);

	if (pgd->not_added)
		{
		pgd->create_markers_id = g_idle_add(bar_pane_gps_create_markers_cb, pgd);
		}
	else
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgd->progress), 0);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgd->progress), nullptr);
		bar_pane_gps_clear_marker_queue(pgd);
		pgd->create_markers_id = 0;
		}

	return G_SOURCE_REMOVE;
}

void bar_pane_gps_update(PaneGPSData *pgd, gboolean use_selection)
{
	if (!pgd) return;

	if (pgd->create_markers_id != 0)
		{
		g_source_remove(pgd->create_markers_id);
		pgd->create_markers_id = 0;
		}

	bar_pane_gps_clear_marker_queue(pgd);
	file_data_list_free(pgd->selection_list);
	pgd->selection_list = use_selection ? layout_selection_list(pgd->pane.lw) : nullptr;
	if (!pgd->selection_list && pgd->fd)
		{
		pgd->selection_list = g_list_append(nullptr, file_data_ref(pgd->fd));
		}
	pgd->selection_list = file_data_process_groups_in_selection(pgd->selection_list, FALSE, nullptr);
	pgd->selection_count = g_list_length(pgd->selection_list);
	pgd->num_added = 0;

	shumate_marker_layer_remove_all(pgd->marker_layer);
	if (!pgd->enable_markers_checked)
		{
		return;
		}

	bar_pane_gps_set_status(pgd);

	if (pgd->selection_list)
		{
		if (pgd->selection_count == 1)
			{
			auto *fd = static_cast<FileData *>(pgd->selection_list->data);
			bar_pane_gps_add_file_marker(pgd, fd);
			}
		else
			{
			pgd->not_added = g_list_copy(pgd->selection_list);
			pgd->create_markers_id = g_idle_add(bar_pane_gps_create_markers_cb, pgd);
			}
		}
}

void bar_pane_gps_set_map_source(PaneGPSData *pgd, const gchar *map_id)
{
	if (!map_id || !*map_id) map_id = DEFAULT_MAP_ID;
	if (g_strcmp0(pgd->map_source, map_id) == 0) return;

	ShumateMapSource *source;
	ShumateMapSourceRegistry *registry = shumate_map_source_registry_new_with_defaults();

	source = shumate_map_source_registry_get_by_id(registry, map_id);

	if (source)
		{
		shumate_simple_map_set_map_source(pgd->map, source);
		g_free(pgd->map_source);
		pgd->map_source = g_strdup(map_id);
		}

	g_object_unref(registry);
}

void bar_pane_gps_enable_markers_checked_toggle_cb(GtkWidget *button, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	pgd->enable_markers_checked = gtk_check_button_get_active(GTK_CHECK_BUTTON(button));
	bar_pane_gps_update(pgd, pgd->selection_count > 1);
}

void bar_pane_gps_centre_map_checked_toggle_cb(GtkWidget *button, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	pgd->centre_map_checked = gtk_check_button_get_active(GTK_CHECK_BUTTON(button));
}

void bar_pane_gps_notify_selection(GtkWidget *bar, gint count)
{
	(void)count;
	PaneGPSData *pgd;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pgd) return;

	bar_pane_gps_update(pgd, count > 1);
}

void bar_pane_gps_set_fd(GtkWidget *bar, FileData *fd)
{
	PaneGPSData *pgd;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pgd) return;

	file_data_unref(pgd->fd);
	pgd->fd = file_data_ref(fd);

	bar_pane_gps_update(pgd, FALSE);
}

gint bar_pane_gps_event(GtkWidget *bar, GdkEvent *event)
{
	(void)bar;
	(void)event;
	return FALSE;
}

const gchar *bar_pane_gps_get_map_id(const PaneGPSData *pgd)
{
	return pgd->map_source ? pgd->map_source : DEFAULT_MAP_ID;
}

void bar_pane_gps_write_config(GtkWidget *pane, GString *outstr, gint indent)
{
	auto *pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pgd) return;

	WRITE_NL();
	WRITE_STRING("<pane_gps ");
	WRITE_CHAR(pgd->pane, id);
	WRITE_CHAR_FULL("title", gtk_label_get_text(GTK_LABEL(pgd->pane.title)));
	WRITE_BOOL(pgd->pane, expanded);

	gint w;
	gtk_widget_get_size_request(pane, &w, &pgd->height);
	WRITE_INT(*pgd, height);
	indent++;

	const gchar *map_id = bar_pane_gps_get_map_id(pgd);
	WRITE_NL();
	WRITE_CHAR_FULL("map-id", map_id);

	gdouble zoom = DEFAULT_ZOOM;
	g_object_get(pgd->viewport, "zoom-level", &zoom, NULL);
	WRITE_NL();
	WRITE_INT_FULL("zoom-level", static_cast<gint>(zoom));

	const auto write_lat_long_option = [pgd, outstr, indent](const gchar *option)
	{
		gdouble position = g_strcmp0(option, "latitude") == 0
		                    ? shumate_location_get_latitude(SHUMATE_LOCATION(pgd->viewport))
		                    : shumate_location_get_longitude(SHUMATE_LOCATION(pgd->viewport));
		const gint int_position = position * 1000000;
		WRITE_NL();
		WRITE_INT_FULL(option, int_position);
	};
	write_lat_long_option("latitude");
	write_lat_long_option("longitude");

	indent--;
	WRITE_NL();
	WRITE_STRING("/>");
}

void bar_pane_gps_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_METADATA)) &&
	    g_list_find(pgd->selection_list, fd))
		{
		bar_pane_gps_update(pgd, pgd->selection_count > 1);
		}
}

GtkWidget *bar_pane_gps_menu(PaneGPSData *pgd)
{
	GtkWidget *popover;
	GtkWidget *menu_box;
	GtkWidget *map_centre;

	popover = gtk_popover_new();
	popover_set_parent(popover, pgd->map_popover_parent);
	menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_popover_set_child(GTK_POPOVER(popover), menu_box);

	GtkWidget *enable_markers = gtk_check_button_new_with_label(_("Enable markers"));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(enable_markers), pgd->enable_markers_checked);
	g_signal_connect(enable_markers, "toggled", G_CALLBACK(bar_pane_gps_enable_markers_checked_toggle_cb), pgd);
	gtk_box_append(GTK_BOX(menu_box), enable_markers);

	map_centre = gtk_check_button_new_with_label(_("Centre map on marker"));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(map_centre), pgd->centre_map_checked);
	g_signal_connect(map_centre, "toggled", G_CALLBACK(bar_pane_gps_centre_map_checked_toggle_cb), pgd);
	gtk_box_append(GTK_BOX(menu_box), map_centre);
	gtk_widget_set_sensitive(map_centre, pgd->enable_markers_checked);

	return popover;
}

void bar_pane_gps_destroy(gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	file_data_unregister_notify_func(bar_pane_gps_notify_cb, pgd);

	g_idle_remove_by_data(pgd);
	bar_pane_gps_clear_marker_queue(pgd);

	file_data_list_free(pgd->selection_list);

	file_data_unref(pgd->fd);
	g_free(pgd->map_source);
	g_free(pgd->pane.id);
	g_free(pgd);
}

GtkWidget *bar_pane_gps_new(const gchar *id, const gchar *title, const gchar *map_id,
         					const gint zoom, const gdouble latitude, const gdouble longitude,
            				gboolean expanded, gint height)
{
	PaneGPSData *pgd;
	GtkWidget *vbox;
	GtkWidget *status;
	GtkWidget *progress;

	pgd = g_new0(PaneGPSData, 1);

	pgd->pane.pane_set_fd = bar_pane_gps_set_fd;
	pgd->pane.pane_notify_selection = bar_pane_gps_notify_selection;
	pgd->pane.pane_event = bar_pane_gps_event;
	pgd->pane.pane_write_config = bar_pane_gps_write_config;
	pgd->pane.title = bar_pane_expander_title(title);
	pgd->pane.id = g_strdup(id);
	pgd->pane.type = PANE_GPS;
	pgd->pane.expanded = expanded;
	pgd->height = height;

	GtkWidget *frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	pgd->map = shumate_simple_map_new();
	pgd->map_popover_parent = popover_parent_new(GTK_WIDGET(pgd->map));
	pgd->viewport = shumate_simple_map_get_viewport(pgd->map);

	gtk_widget_set_hexpand(pgd->map_popover_parent, TRUE);
	gtk_widget_set_vexpand(pgd->map_popover_parent, TRUE);

	gtk_box_append(GTK_BOX(vbox), pgd->map_popover_parent);

	gq_gtk_container_add(frame, vbox);

	status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	progress = gtk_progress_bar_new();
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), TRUE);

	gtk_box_append(GTK_BOX(status), progress);
	gtk_box_append(GTK_BOX(vbox), status);

	pgd->widget = frame;
	pgd->progress = progress;

	bar_pane_gps_set_map_source(pgd, map_id);

	pgd->marker_layer = shumate_marker_layer_new(pgd->viewport);
	shumate_simple_map_insert_overlay_layer_behind(pgd->map, SHUMATE_LAYER(pgd->marker_layer), nullptr);

	shumate_viewport_set_zoom_level(pgd->viewport, zoom);
	shumate_map_center_on(shumate_simple_map_get_map(pgd->map), latitude, longitude);
	pgd->centre_map_checked = TRUE;
	g_object_set_data_full(G_OBJECT(pgd->widget), "pane_data", pgd, bar_pane_gps_destroy);

	gtk_widget_add_css_class(frame, "frame");

	gtk_widget_set_size_request(pgd->widget, -1, height);

	/* The licence data is in the About page
	 */
	ShumateLicense *license = shumate_simple_map_get_license(SHUMATE_SIMPLE_MAP(pgd->map));
	gtk_widget_add_css_class(GTK_WIDGET(license), "hidden-license");

	auto *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);

	g_signal_connect(click, "pressed",
	    G_CALLBACK(+[](GtkGestureClick*, int, double, double, gpointer data){
		        auto *pgd = static_cast<PaneGPSData*>(data);
		        GtkWidget *menu = bar_pane_gps_menu(pgd);
		        popover_popup(menu);
		    }), pgd);
	gtk_widget_add_controller(GTK_WIDGET(pgd->map), GTK_EVENT_CONTROLLER(click));

	bar_pane_gps_dnd_init(pgd);

	file_data_register_notify_func(bar_pane_gps_notify_cb, pgd, NOTIFY_PRIORITY_LOW);

	pgd->enable_markers_checked = TRUE;
	pgd->centre_map_checked = TRUE;

	return pgd->widget;
}

} // namespace

GtkWidget *bar_pane_gps_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
	g_autofree gchar *title = g_strdup(_("GPS Map"));
	g_autofree gchar *map_id = nullptr;
	gboolean expanded = TRUE;
	gint height = 350;
	gint zoom = 7;
	gdouble latitude;
	gdouble longitude;
	/* Latitude and longitude are stored in the config file as an integer of
	 * (actual value * 1,000,000). There is no READ_DOUBLE utility function.
	 */
	gint int_latitude = 54000000;
	gint int_longitude = -4000000;
	g_autofree gchar *id = g_strdup("gps");

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title))
			continue;
		if (READ_CHAR_FULL("map-id", map_id))
			continue;
		if (READ_INT_CLAMP_FULL("zoom-level", zoom, 1, 20))
			continue;
		if (READ_INT_CLAMP_FULL("latitude", int_latitude, -90000000, +90000000))
			continue;
		if (READ_INT_CLAMP_FULL("longitude", int_longitude, -90000000, +90000000))
			continue;
		if (READ_BOOL_FULL("expanded", expanded))
			continue;
		if (READ_INT_FULL("height", height))
			continue;
		if (READ_CHAR_FULL("id", id))
			continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	bar_pane_translate_title(PANE_GPS, id, &title);
	latitude = static_cast<gdouble>(int_latitude) / 1000000;
	longitude = static_cast<gdouble>(int_longitude) / 1000000;

	return bar_pane_gps_new(id, title, map_id, zoom, latitude, longitude, expanded, height);
}

void bar_pane_gps_update_from_config(GtkWidget *pane, const gchar **attribute_names,
                                						const gchar **attribute_values)
{
	PaneGPSData *pgd;
	gint zoom = DEFAULT_ZOOM;
	gint int_longitude = 0;
	gint int_latitude = 0;
	gdouble longitude = 0;
	gdouble latitude = 0;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pgd)
		return;

	g_autofree gchar *title = nullptr;
	g_autofree gchar *map_id = nullptr;
	latitude = shumate_location_get_latitude(SHUMATE_LOCATION(pgd->viewport));
	longitude = shumate_location_get_longitude(SHUMATE_LOCATION(pgd->viewport));

	while (*attribute_names)
	{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title))
			continue;
		if (READ_CHAR_FULL("map-id", map_id))
			{
			bar_pane_gps_set_map_source(pgd, map_id);
			continue;
			}
		if (READ_BOOL(pgd->pane, expanded))
			continue;
		if (READ_INT(*pgd, height))
			continue;
		if (READ_CHAR(pgd->pane, id))
			continue;
		if (READ_INT_CLAMP_FULL("zoom-level", zoom, 1, 8))
			{
			shumate_viewport_set_zoom_level(pgd->viewport, zoom);
			continue;
			}
		if (READ_INT_CLAMP_FULL("longitude", int_longitude, -90000000, +90000000))
			{
			longitude = int_longitude / 1000000.0;
			shumate_map_center_on(shumate_simple_map_get_map(pgd->map), latitude, longitude);
			continue;
			}
		if (READ_INT_CLAMP_FULL("latitude", int_latitude, -90000000, +90000000))
			{
			latitude = int_latitude / 1000000.0;
			shumate_map_center_on(shumate_simple_map_get_map(pgd->map), latitude, longitude);
			continue;
			}

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
	}

	if (title)
		{
		bar_pane_translate_title(PANE_GPS, pgd->pane.id, &title);
		gtk_label_set_text(GTK_LABEL(pgd->pane.title), title);
		}

	gtk_widget_set_size_request(pgd->widget, -1, pgd->height);
	bar_update_expander(pane);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
