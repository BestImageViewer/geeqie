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
#include <pango/pango.h>
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
#include "thumb.h"
#include "ui-menu.h"
#include "ui-utildlg.h"
#include "uri-utils.h"

namespace
{

constexpr gint THUMB_SIZE = 100;
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
	GtkWidget *map;
	ShumateMarkerLayer *marker_layer;
	ShumateViewport *viewport;
	GList *selection_list;
	GtkWidget *progress;
	GtkWidget *slider;
	GtkWidget *state;
	gint selection_count;
	gboolean centre_map_checked;
	gboolean enable_markers_checked;
};

/*
 *-------------------------------------------------------------------
 * drag-and-drop
 *-------------------------------------------------------------------
 */

void bar_pane_gps_dnd_init(gpointer data)
{
	(void)data;
}

void bar_pane_gps_thumb_done_cb(ThumbLoader *tl, gpointer data)
{
	FileData *fd;
	GtkImage *image;

	image = GTK_IMAGE(data);
	fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(image), "file_fd"));
	if (fd && fd->thumb_pixbuf) gtk_image_set_from_pixbuf(image, fd->thumb_pixbuf);
	thumb_loader_free(tl);
}

void bar_pane_gps_thumb_error_cb(ThumbLoader *tl, gpointer)
{
	thumb_loader_free(tl);
}

GtkWidget *bar_pane_gps_marker_child_new(FileData *fd)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget *image = gtk_image_new();
	GtkWidget *label = gtk_label_new("i");

	gtk_widget_add_css_class(box, "map-marker");
	gtk_widget_set_visible(image, FALSE);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

	g_object_set_data(G_OBJECT(box), "file_fd", fd);
	g_object_set_data(G_OBJECT(image), "file_fd", fd);
	g_object_set_data(G_OBJECT(label), "short-label", label);
	g_object_set_data(G_OBJECT(label), "thumb-image", image);

	gtk_box_append(GTK_BOX(box), image);
	gtk_box_append(GTK_BOX(box), label);

	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
	g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick *gesture, gint, gdouble, gdouble, gpointer)
	{
		GtkWidget *child = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
		auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(child), "file_fd"));
		GtkWidget *label = GTK_WIDGET(gtk_widget_get_last_child(child));
		GtkWidget *image = GTK_WIDGET(gtk_widget_get_first_child(child));
		const gchar *text = gtk_label_get_text(GTK_LABEL(label));

		if (!fd) return;

		if (g_strcmp0(text, "i") == 0)
			{
			if (fd->thumb_pixbuf)
				{
				gtk_image_set_from_pixbuf(GTK_IMAGE(image), fd->thumb_pixbuf);
				gtk_widget_set_visible(image, TRUE);
				}
			else if (fd->pixbuf)
				{
				gint width = gdk_pixbuf_get_width(fd->pixbuf);
				gint height = gdk_pixbuf_get_height(fd->pixbuf);
				g_autoptr(GdkPixbuf) scaled = gdk_pixbuf_scale_simple(fd->pixbuf, THUMB_SIZE, height * THUMB_SIZE / width, GDK_INTERP_NEAREST);
				gtk_image_set_from_pixbuf(GTK_IMAGE(image), scaled);
				gtk_widget_set_visible(image, TRUE);
				}
			else
				{
				ThumbLoader *tl = thumb_loader_new(THUMB_SIZE, THUMB_SIZE);
				thumb_loader_set_callbacks(tl, bar_pane_gps_thumb_done_cb, bar_pane_gps_thumb_error_cb, nullptr, image);
				thumb_loader_start(tl, fd);
				gtk_widget_set_visible(image, TRUE);
				}

			g_autoptr(GString) marker_text = g_string_new(fd->name);
			g_string_append(marker_text, "\n");
			g_string_append(marker_text, text_from_time(fd->date));

			g_autofree gchar *altitude = metadata_read_string(fd, "formatted.GPSAltitude", METADATA_FORMATTED);
			if (altitude)
				{
				g_string_append(marker_text, "\n");
				g_string_append(marker_text, altitude);
				}

			gtk_label_set_text(GTK_LABEL(label), marker_text->str);
			}
		else
			{
			gtk_widget_set_visible(image, FALSE);
			gtk_label_set_text(GTK_LABEL(label), "i");
			}
	}), nullptr);
	gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));

	return box;
}

void bar_pane_gps_add_marker(PaneGPSData *pgd, FileData *fd, gdouble latitude, gdouble longitude)
{
	ShumateMarker *marker = shumate_marker_new();
	shumate_location_set_location(SHUMATE_LOCATION(marker), latitude, longitude);
	shumate_marker_set_child(marker, bar_pane_gps_marker_child_new(fd));
	shumate_marker_layer_add_marker(pgd->marker_layer, marker);
}

void bar_pane_gps_set_status(PaneGPSData *pgd)
{
	if (!pgd->viewport) return;

	gdouble zoom = shumate_viewport_get_zoom_level(pgd->viewport);
	g_autofree gchar *message = g_strdup_printf(_("Zoom level %i"), static_cast<gint>(zoom));

	gtk_label_set_text(GTK_LABEL(pgd->state), message);
	gtk_widget_set_tooltip_text(pgd->slider, message);
	gtk_scale_button_set_value(GTK_SCALE_BUTTON(pgd->slider), zoom);
}

void bar_pane_gps_update(PaneGPSData *pgd)
{
	shumate_marker_layer_remove_all(pgd->marker_layer);

	GList *list = layout_selection_list(pgd->pane.lw);
	for (GList *l = list; l; l = l->next)
		{
		auto *fd = static_cast<FileData*>(l->data);

		double lat = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 0);
		double lon = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 0);

		if (lat == 0 && lon == 0)
			continue;

		bar_pane_gps_add_marker(pgd, fd, lat, lon);
		}

	file_data_list_free(list);
}

void bar_pane_gps_set_map_source(PaneGPSData *pgd, const gchar *map_id)
{
	if (!map_id || !*map_id) map_id = DEFAULT_MAP_ID;

	ShumateMapSource *source;
	ShumateMapSourceRegistry *registry;

	registry = shumate_map_source_registry_new_with_defaults();
	source = shumate_map_source_registry_get_by_id(registry, map_id);

	if (source)
		{
		shumate_map_set_map_source(SHUMATE_MAP(pgd->map), source);
		g_free(pgd->map_source);
		pgd->map_source = g_strdup(map_id);
		}

	g_object_unref(registry);
}

void bar_pane_gps_enable_markers_checked_toggle_cb(GtkWidget *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	pgd->enable_markers_checked = !pgd->enable_markers_checked;
}

void bar_pane_gps_centre_map_checked_toggle_cb(GtkWidget *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	pgd->centre_map_checked = !pgd->centre_map_checked;
}

void bar_pane_gps_notify_selection(GtkWidget *bar, gint count)
{
	PaneGPSData *pgd;

	if (count == 0) return;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pgd) return;

	bar_pane_gps_update(pgd);
}

void bar_pane_gps_set_fd(GtkWidget *bar, FileData *fd)
{
	PaneGPSData *pgd;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pgd) return;

	file_data_unref(pgd->fd);
	pgd->fd = file_data_ref(fd);

	bar_pane_gps_update(pgd);
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

void bar_pane_gps_slider_changed_cb(GtkScaleButton *slider,
                                    gdouble zoom,
                                    gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	g_autofree gchar *message = g_strdup_printf(_("Zoom %i"), static_cast<gint>(zoom));

	shumate_viewport_set_zoom_level(pgd->viewport, zoom);
	gtk_widget_set_tooltip_text(GTK_WIDGET(slider), message);
	bar_pane_gps_set_status(pgd);
}

void bar_pane_gps_view_state_changed_cb(GObject *, GParamSpec *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	bar_pane_gps_set_status(pgd);
}

void bar_pane_gps_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_METADATA)) &&
	    g_list_find(pgd->selection_list, fd))
		{
		bar_pane_gps_update(pgd);
		}
}

GtkWidget *bar_pane_gps_menu(PaneGPSData *pgd)
{
	GtkWidget *popover;
	GtkWidget *menu_box;
	GtkWidget *map_centre;

	popover = gtk_popover_new();
	gtk_widget_set_parent(popover, pgd->map);
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
	GtkWidget *state;
	GtkWidget *progress;
	GtkWidget *slider;
	const gchar *slider_list[] = {GQ_ICON_ZOOM_IN, GQ_ICON_ZOOM_OUT, nullptr};

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

	pgd->map = GTK_WIDGET(shumate_map_new());
	pgd->viewport = shumate_map_get_viewport(SHUMATE_MAP(pgd->map));

	gtk_widget_set_hexpand(pgd->map, TRUE);
	gtk_widget_set_vexpand(pgd->map, TRUE);

	gtk_box_append(GTK_BOX(vbox), pgd->map);

	gq_gtk_container_add(frame, vbox);

	status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	const gchar **slider_icons = slider_list;
	slider = gtk_scale_button_new(1, 17, 1, slider_icons);

	gtk_widget_set_tooltip_text(slider, _("Zoom"));
	gtk_scale_button_set_value(GTK_SCALE_BUTTON(slider), static_cast<gdouble>(zoom));

	progress = gtk_progress_bar_new();
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), TRUE);

	state = gtk_label_new("");
	gtk_label_set_justify(GTK_LABEL(state), GTK_JUSTIFY_LEFT);
	gtk_label_set_ellipsize(GTK_LABEL(state), PANGO_ELLIPSIZE_START);
	gtk_widget_set_tooltip_text(state, _("Zoom level"));

	gq_gtk_box_pack_start(GTK_BOX(status), slider, FALSE, FALSE, 0);
	gq_gtk_box_pack_start(GTK_BOX(status), state, FALSE, FALSE, 5);
	gq_gtk_box_pack_end(GTK_BOX(status), progress, FALSE, FALSE, 0);
	gq_gtk_box_pack_end(GTK_BOX(vbox), status, FALSE, FALSE, 0);

	pgd->marker_layer = shumate_marker_layer_new(pgd->viewport);
	shumate_map_add_layer(SHUMATE_MAP(pgd->map), SHUMATE_LAYER(pgd->marker_layer));

	pgd->widget = frame;
	pgd->progress = progress;
	pgd->slider = slider;
	pgd->state = state;

	bar_pane_gps_set_map_source(pgd, map_id);

	shumate_viewport_set_zoom_level(pgd->viewport, zoom);
	shumate_map_center_on(SHUMATE_MAP(pgd->map), latitude, longitude);
	pgd->centre_map_checked = TRUE;
	g_object_set_data_full(G_OBJECT(pgd->widget), "pane_data", pgd, bar_pane_gps_destroy);

	gtk_widget_add_css_class(frame, "frame");

	gtk_widget_set_size_request(pgd->widget, -1, height);

	auto *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);

	g_signal_connect(click, "pressed",
	    G_CALLBACK(+[](GtkGestureClick*, int, double, double, gpointer data){
	        auto *pgd = static_cast<PaneGPSData*>(data);
	        GtkWidget *menu = bar_pane_gps_menu(pgd);
	        gtk_popover_popup(GTK_POPOVER(menu));
	    }), pgd);

	gtk_widget_add_controller(pgd->map, GTK_EVENT_CONTROLLER(click));
	g_signal_connect(pgd->map, "notify::loading",
	    G_CALLBACK(+[](GObject *obj, GParamSpec*, gpointer data){
	        auto *pgd = static_cast<PaneGPSData*>(data);
	        gboolean loading;
	        g_object_get(obj, "loading", &loading, NULL);
	        gtk_label_set_text(GTK_LABEL(pgd->state),
	            loading ? _("Loading map") : _("Map ready"));
	    }), pgd);

	g_signal_connect(pgd->viewport, "notify::zoom-level", G_CALLBACK(bar_pane_gps_view_state_changed_cb), pgd);
	g_signal_connect(G_OBJECT(slider), "value-changed", G_CALLBACK(bar_pane_gps_slider_changed_cb), pgd);

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
			shumate_map_center_on(SHUMATE_MAP(pgd->map), latitude, longitude);
			continue;
			}
		if (READ_INT_CLAMP_FULL("latitude", int_latitude, -90000000, +90000000))
			{
			latitude = int_latitude / 1000000.0;
			shumate_map_center_on(SHUMATE_MAP(pgd->map), latitude, longitude);
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
