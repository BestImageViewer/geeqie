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

#include "main.h"
#ifdef HAVE_LIBCHAMPLAIN
#ifdef HAVE_LIBCHAMPLAIN_GTK

#include "bar-gps.h"

#include "bar.h"
#include "filedata.h"
#include "layout.h"
#include "metadata.h"
#include "menu.h"
#include "misc.h"
#include "rcfile.h"
#include "thumb.h"
#include "ui-menu.h"
#include "uri-utils.h"
#include "ui-utildlg.h"

#include <clutter-gtk/clutter-gtk.h>
#include <champlain/champlain.h>
#include <champlain-gtk/champlain-gtk.h>

#define MARKER_COLOUR 0x00, 0x00, 0xff, 0xff
#define TEXT_COLOUR 0x00, 0x00, 0x00, 0xff
#define THUMB_COLOUR 0xff, 0xff, 0xff, 0xff
#define THUMB_SIZE 100

#define DIRECTION_SIZE 300

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
	ClutterActor *gps_view;
	ChamplainMarkerLayer *icon_layer;
	GList *selection_list;
	GList *not_added;
	ChamplainBoundingBox *bbox;
	guint num_added;
	guint create_markers_id;
	GtkWidget *progress;
	GtkWidget *slider;
	GtkWidget *state;
	gint selection_count;
	gboolean centre_map_checked;
	gboolean enable_markers_checked;
	gdouble dest_latitude;
	gdouble dest_longitude;
	GList *geocode_list;
};

/*
 *-------------------------------------------------------------------
 * drag-and-drop
 *-------------------------------------------------------------------
 */
enum {
	TARGET_APP_COLLECTION_MEMBER,
	TARGET_APP_EXIF_ENTRY,
	TARGET_APP_KEYWORD_PATH,
	TARGET_URI_LIST,
	TARGET_TEXT_PLAIN
};

static GtkTargetEntry bar_pane_gps_drop_types[] = {
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
};
static gint n_gps_entry_drop_types = 2;

static void bar_pane_gps_close_cancel_cb(GenericDialog *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	g_list_free(pgd->geocode_list);
}

static void bar_pane_gps_close_save_cb(GenericDialog *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);
	FileData *fd;
	GList *work;

	work = g_list_first(pgd->geocode_list);
	while (work)
		{
		fd = static_cast<FileData *>(work->data);
		if (fd->name && !fd->parent)
			{
			work = work->next;
			metadata_write_GPS_coord(fd, "Xmp.exif.GPSLatitude", pgd->dest_latitude);
			metadata_write_GPS_coord(fd, "Xmp.exif.GPSLongitude", pgd->dest_longitude);
			}
		}
	g_list_free(pgd->geocode_list);
}

 static void bar_pane_gps_dnd_receive(GtkWidget *pane, GdkDragContext *,
									  gint x, gint y,
									  GtkSelectionData *selection_data, guint info,
									  guint, gpointer)
{
	PaneGPSData *pgd;
	GenericDialog *gd;
	FileData *fd, *fd_found;
	GList *work, *list;
	gint count, geocoded_count;
	gdouble latitude, longitude;
	GString *message;
	gchar *location;
	gchar **latlong;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pgd) return;

	if (info == TARGET_URI_LIST)
		{
		pgd->dest_longitude = champlain_view_x_to_longitude(CHAMPLAIN_VIEW(pgd->gps_view), x);
		pgd->dest_latitude = champlain_view_y_to_latitude(CHAMPLAIN_VIEW(pgd->gps_view), y);

		count = 0;
		geocoded_count = 0;
		pgd->geocode_list = nullptr;

		list = uri_filelist_from_gtk_selection_data(selection_data);

		if (list)
			{
			work = list;
			while (work)
				{
				fd = static_cast<FileData *>(work->data);
				work = work->next;
				if (fd->name && !fd->parent)
					{
					count++;
					pgd->geocode_list = g_list_append(pgd->geocode_list, fd);
					latitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 1000);
					longitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 1000);
					if (latitude != 1000 && longitude != 1000)
						{
						geocoded_count++;
						}
					}
				}
			g_list_free(list);

			if(count)
				{
				message = g_string_new("");
				if (count == 1)
					{
					fd_found = static_cast<FileData *>(g_list_first(pgd->geocode_list)->data);
					g_string_append_printf(message,
							_("\nDo you want to geocode image %s?"), fd_found->name);
					}
				else
					{
					g_string_append_printf(message,
							_("\nDo you want to geocode %i images?"), count);
					}
				if (geocoded_count == 1 && count == 1)
					{
					g_string_append_printf(message,
							_("\nThis image is already geocoded!"));
					}
				else if (geocoded_count == 1 && count > 1)
					{
					g_string_append_printf(message,
							_("\nOne image is already geocoded!"));
					}
				else if (geocoded_count > 1 && count > 1)
					{
					g_string_append_printf(message,
							_("\n%i Images are already geocoded!"), geocoded_count);
					}

				location = g_strdup_printf("%lf %lf", pgd->dest_latitude,
														pgd->dest_longitude);
				g_string_append_printf(message, _("\n\nPosition: %s \n"), location);

				gd = generic_dialog_new(_("Geocode images"),
							"geocode_images", nullptr, TRUE,
							bar_pane_gps_close_cancel_cb, pgd);
				generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION,
							_("Write lat/long to meta-data?"),
							message->str, TRUE);

				generic_dialog_add_button(gd, GQ_ICON_SAVE, _("Save"),
												bar_pane_gps_close_save_cb, TRUE);

				gtk_widget_show(gd->dialog);
				g_free(location);
				g_string_free(message, TRUE);
				}
			}
		}

	if (info == TARGET_TEXT_PLAIN)
		{
		location = decode_geo_parameters(reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data)));
		if (!(g_strstr_len(location,-1,"Error")))
			{
			latlong = g_strsplit(location, " ", 2);
			champlain_view_center_on(CHAMPLAIN_VIEW(pgd->gps_view),
							g_ascii_strtod(latlong[0],nullptr),
							g_ascii_strtod(latlong[1],nullptr));
			g_strfreev(latlong);
			}
		g_free(location);
		}
}

static void bar_pane_gps_dnd_init(gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	gtk_drag_dest_set(pgd->widget,
			  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP),
			  bar_pane_gps_drop_types, n_gps_entry_drop_types,
			  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
	g_signal_connect(G_OBJECT(pgd->widget), "drag_data_received",
			 G_CALLBACK(bar_pane_gps_dnd_receive), NULL);

}

static gboolean bar_gps_draw_direction (ClutterCanvas *, cairo_t *cr, gpointer)
{
	cairo_set_source_rgb(cr, 255, 0, 0);

	cairo_set_line_width(cr, 2);
	cairo_move_to(cr, 0, 1);
	cairo_line_to(cr, DIRECTION_SIZE, 1);

	cairo_stroke(cr);

	return TRUE;
}

static void bar_pane_gps_thumb_done_cb(ThumbLoader *tl, gpointer data)
{
	FileData *fd;
	ClutterActor *marker;
	ClutterActor *actor;

	marker = CLUTTER_ACTOR(data);
	fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(marker), "file_fd"));
	if (fd->thumb_pixbuf != nullptr)
		{
		actor = gtk_clutter_texture_new();
		gtk_clutter_texture_set_from_pixbuf(GTK_CLUTTER_TEXTURE(actor), fd->thumb_pixbuf, nullptr);
		champlain_label_set_image(CHAMPLAIN_LABEL(marker), actor);
		}
	thumb_loader_free(tl);
}

static void bar_pane_gps_thumb_error_cb(ThumbLoader *tl, gpointer)
{
	thumb_loader_free(tl);
}

static gboolean bar_pane_gps_marker_keypress_cb(GtkWidget *widget, ClutterButtonEvent *bevent, gpointer)
{
	FileData *fd;
	ClutterActor *label_marker, *parent_marker;
	ClutterColor marker_colour = { MARKER_COLOUR };
	ClutterColor text_colour = { TEXT_COLOUR };
	ClutterColor thumb_colour = { THUMB_COLOUR };
	gchar *current_text;
	ClutterActor *actor, *direction;
	ClutterActor *current_image;
	GString *text;
	gint height, width;
	GdkPixbufRotation rotate;
	gchar *altitude = nullptr;
	ThumbLoader *tl;

	if (bevent->button == MOUSE_BUTTON_LEFT)
		{
		label_marker = CLUTTER_ACTOR(widget);
		fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(label_marker), "file_fd"));

		/* If the marker is showing a thumbnail, delete it
		 */
		current_image = champlain_label_get_image(CHAMPLAIN_LABEL(label_marker));
		if (current_image != nullptr)
			{
			clutter_actor_destroy(CLUTTER_ACTOR(current_image));
		 	champlain_label_set_image(CHAMPLAIN_LABEL(label_marker), nullptr);
			}

		current_text = g_strdup(champlain_label_get_text(CHAMPLAIN_LABEL(label_marker)));

		/* If the marker is showing only the text character, replace it with a
		 * thumbnail and date and altitude
		 */
		if (g_strcmp0(current_text, "i") == 0)
			{
			/* If a thumbail has already been generated, use that. If not try the pixbuf of the full image.
			 * If not, call the thumb_loader to generate a thumbnail and update the marker later in the
			 * thumb_loader callback
			 */
			 if (fd->thumb_pixbuf != nullptr)
				{
				actor = gtk_clutter_texture_new();
				gtk_clutter_texture_set_from_pixbuf(GTK_CLUTTER_TEXTURE(actor), fd->thumb_pixbuf, nullptr);
				champlain_label_set_image(CHAMPLAIN_LABEL(label_marker), actor);
				}
			else if (fd->pixbuf != nullptr)
				{
				actor = gtk_clutter_texture_new();
				width = gdk_pixbuf_get_width (fd->pixbuf);
				height = gdk_pixbuf_get_height (fd->pixbuf);
				switch (fd->exif_orientation)
					{
					case 8:
						rotate = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
						break;
					case 3:
						rotate = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
						break;
					case 6:
						rotate = GDK_PIXBUF_ROTATE_CLOCKWISE;
						break;
					default:
						rotate = GDK_PIXBUF_ROTATE_NONE;
					}

					gtk_clutter_texture_set_from_pixbuf(GTK_CLUTTER_TEXTURE(actor),
										gdk_pixbuf_rotate_simple(gdk_pixbuf_scale_simple(fd->pixbuf, THUMB_SIZE, height * THUMB_SIZE / width,
										GDK_INTERP_NEAREST), rotate), nullptr);
					champlain_label_set_image(CHAMPLAIN_LABEL(label_marker), actor);
				}
			else
				{
				tl = thumb_loader_new(THUMB_SIZE, THUMB_SIZE);
				thumb_loader_set_callbacks(tl,
											bar_pane_gps_thumb_done_cb,
											bar_pane_gps_thumb_error_cb,
											nullptr,
											label_marker);
				thumb_loader_start(tl, fd);
				}

			text = g_string_new(fd->name);
			g_string_append(text, "\n");
			g_string_append(text, text_from_time(fd->date));
			g_string_append(text, "\n");
			altitude = metadata_read_string(fd, "formatted.GPSAltitude", METADATA_FORMATTED);
			if (altitude != nullptr)
				{
				g_string_append(text, altitude);
				}

			champlain_label_set_text(CHAMPLAIN_LABEL(label_marker), text->str);
			champlain_label_set_font_name(CHAMPLAIN_LABEL(label_marker), "sans 8");
			champlain_marker_set_selection_color(&thumb_colour);
			champlain_marker_set_selection_text_color(&text_colour);

			g_free(altitude);
			g_string_free(text, TRUE);

			parent_marker = clutter_actor_get_parent(label_marker);
			if (clutter_actor_get_n_children(parent_marker ) > 1 )
				{
				direction = clutter_actor_get_child_at_index(parent_marker, 0);
				clutter_actor_set_opacity(direction, 255);
				}
			}
		/* otherwise, revert to the hidden text marker
		 */
		else
			{
			champlain_label_set_text(CHAMPLAIN_LABEL(label_marker), "i");
			champlain_label_set_font_name(CHAMPLAIN_LABEL(label_marker), "courier 5");
			champlain_marker_set_selection_color(&marker_colour);
			champlain_marker_set_selection_text_color(&marker_colour);

			parent_marker = clutter_actor_get_parent(label_marker);
			if (clutter_actor_get_n_children(parent_marker ) > 1 )
				{
				direction = clutter_actor_get_child_at_index(parent_marker, 0);
				clutter_actor_set_opacity(direction, 0);
				}
			}

		g_free(current_text);

		return TRUE;
		}
	return TRUE;
}

static gboolean bar_pane_gps_create_markers_cb(gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);
	gdouble latitude;
	gdouble longitude;
	gdouble compass;
	FileData *fd;
	ClutterActor *parent_marker, *label_marker;
	ClutterActor *direction;
	ClutterColor marker_colour = { MARKER_COLOUR };
	ClutterColor thumb_colour = { THUMB_COLOUR };
	GString *message;
	ClutterContent *canvas;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgd->progress),
							static_cast<gdouble>(pgd->selection_count - g_list_length(pgd->not_added)) /
							static_cast<gdouble>(pgd->selection_count));

	message = g_string_new("");
	g_string_printf(message, "%i/%i", (pgd->selection_count - g_list_length(pgd->not_added)),
																			pgd->selection_count);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgd->progress), message->str);
	g_string_free(message, TRUE);

	if(pgd->not_added)
		{
		fd = static_cast<FileData *>(pgd->not_added->data);
		pgd->not_added = pgd->not_added->next;

		latitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 0);
		longitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 0);
		compass = metadata_read_GPS_direction(fd, "Xmp.exif.GPSImgDirection", 1000);

		if (!(latitude == 0 && longitude == 0))
			{
			pgd->num_added++;

			parent_marker = champlain_marker_new();
			clutter_actor_set_reactive(parent_marker, FALSE);
			label_marker = champlain_label_new_with_text("i","courier 5", &marker_colour, &marker_colour);
			clutter_actor_set_reactive(label_marker, TRUE);
			champlain_marker_set_selection_color(&thumb_colour);

			if (compass != 1000)
				{
				canvas = clutter_canvas_new();
				clutter_canvas_set_size(CLUTTER_CANVAS (canvas), DIRECTION_SIZE, 3);
				g_signal_connect(canvas, "draw", G_CALLBACK(bar_gps_draw_direction), NULL);
				direction = clutter_actor_new();
				clutter_actor_set_size(direction, DIRECTION_SIZE, 3);
				clutter_actor_set_position(direction, 0, 0);
				clutter_actor_set_rotation_angle(direction, CLUTTER_Z_AXIS, compass -90.00);
				clutter_actor_set_content(direction, canvas);
				clutter_content_invalidate(canvas);
				g_object_unref(canvas);

				clutter_actor_add_child(parent_marker, direction);
				clutter_actor_set_opacity(direction, 0);
				}

			clutter_actor_add_child(parent_marker, label_marker);

			champlain_location_set_location(CHAMPLAIN_LOCATION(parent_marker), latitude, longitude);
			champlain_marker_layer_add_marker(pgd->icon_layer, CHAMPLAIN_MARKER(parent_marker));

			g_signal_connect(G_OBJECT(label_marker), "button_release_event",
	 				G_CALLBACK(bar_pane_gps_marker_keypress_cb), pgd);

			g_object_set_data(G_OBJECT(label_marker), "file_fd", fd);

			champlain_bounding_box_extend(pgd->bbox, latitude, longitude);

			}
		return G_SOURCE_CONTINUE;
		}

	if (pgd->centre_map_checked)
		{
		if (pgd->num_added == 1)
			{
		 	champlain_bounding_box_get_center(pgd->bbox, &latitude, &longitude);
		 	champlain_view_go_to(CHAMPLAIN_VIEW(pgd->gps_view), latitude, longitude);
		 	}
		 else if (pgd->num_added > 1)
		 	{
			champlain_view_ensure_visible(CHAMPLAIN_VIEW(pgd->gps_view), pgd->bbox, TRUE);
			}
		}
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgd->progress), 0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgd->progress), nullptr);
	pgd->create_markers_id = 0;

	return G_SOURCE_REMOVE;
}

static void bar_pane_gps_update(PaneGPSData *pgd)
{
	GList *list;

	/* If a create-marker background process is running, kill it
	 * and start again
	 */
	if (pgd->create_markers_id != 0)
		{
		if (g_idle_remove_by_data(pgd))
			{
			pgd->create_markers_id = 0;
			}
		else
			{
			return;
			}
		}

	/* Delete any markers currently displayed
	 */

	champlain_marker_layer_remove_all(pgd->icon_layer);

	if (!pgd->enable_markers_checked)
		{
		return;
		}

	/* For each selected photo that has GPS data, create a marker containing
	 * a single, small text character the same colour as the marker background.
	 * Use a background process in case the user selects a large number of files.
	 */
	filelist_free(pgd->selection_list);
	if (pgd->bbox) champlain_bounding_box_free(pgd->bbox);

	list = layout_selection_list(pgd->pane.lw);
	list = file_data_process_groups_in_selection(list, FALSE, nullptr);

	pgd->selection_list = list;
	pgd->not_added = list;

	pgd->bbox = champlain_bounding_box_new();
	pgd->selection_count = g_list_length(pgd->selection_list);
	pgd->create_markers_id = g_idle_add(bar_pane_gps_create_markers_cb, pgd);
	pgd->num_added = 0;
}

void bar_pane_gps_set_map_source(PaneGPSData *pgd, const gchar *map_id)
{
	ChamplainMapSource *map_source;
	ChamplainMapSourceFactory *map_factory;

	map_factory = champlain_map_source_factory_dup_default();
	map_source = champlain_map_source_factory_create(map_factory, map_id);

	if (map_source != nullptr)
		{
		g_object_set(G_OBJECT(pgd->gps_view), "map-source", map_source, NULL);
		}

	g_object_unref(map_factory);
}

void bar_pane_gps_enable_markers_checked_toggle_cb(GtkWidget *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	if (pgd->enable_markers_checked)
		{
		pgd->enable_markers_checked = FALSE;
		}
	else
		{
		pgd->enable_markers_checked = TRUE;
		}
}

static void bar_pane_gps_centre_map_checked_toggle_cb(GtkWidget *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	if (pgd->centre_map_checked)
		{
		pgd->centre_map_checked = FALSE;
		}
	else
		{
		pgd->centre_map_checked = TRUE;
		}
}

static void bar_pane_gps_change_map_cb(GtkWidget *widget, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);
	gchar *mapsource;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
		return;

	if (!pgd) return;

	mapsource = static_cast<gchar *>(g_object_get_data(G_OBJECT(widget), "menu_item_radio_data"));
	bar_pane_gps_set_map_source(pgd, mapsource);
}

static void bar_pane_gps_notify_selection(GtkWidget *bar, gint count)
{
	PaneGPSData *pgd;

	if (count == 0) return;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pgd) return;

	bar_pane_gps_update(pgd);
}

static void bar_pane_gps_set_fd(GtkWidget *bar, FileData *fd)
{
	PaneGPSData *pgd;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pgd) return;

	file_data_unref(pgd->fd);
	pgd->fd = file_data_ref(fd);

	bar_pane_gps_update(pgd);
}

static gint bar_pane_gps_event(GtkWidget *bar, GdkEvent *event)
{
	PaneGPSData *pgd;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pgd) return FALSE;

	if (gtk_widget_has_focus(pgd->widget)) return gtk_widget_event(GTK_WIDGET(pgd->widget), event);

	return FALSE;
}

static void bar_pane_gps_write_config(GtkWidget *pane, GString *outstr, gint indent)
{
	PaneGPSData *pgd;
	gint zoom;
	ChamplainMapSource *mapsource;
	const gchar *map_id;
	gdouble position;
	gint int_position;
	gint w, h;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pgd) return;

	WRITE_NL();
	WRITE_STRING("<pane_gps ");
	write_char_option(outstr, indent, "id", pgd->pane.id);
	write_char_option(outstr, indent, "title", gtk_label_get_text(GTK_LABEL(pgd->pane.title)));
	WRITE_BOOL(pgd->pane, expanded);

	gtk_widget_get_size_request(GTK_WIDGET(pane), &w, &h);
	pgd->height = h;

	WRITE_INT(*pgd, height);
	indent++;

	g_object_get(G_OBJECT(pgd->gps_view), "map-source", &mapsource, NULL);
	map_id = champlain_map_source_get_id(mapsource);
	WRITE_NL();
	write_char_option(outstr, indent, "map-id", map_id);

	GString *buffer = g_string_new(nullptr);

	g_object_get(G_OBJECT(pgd->gps_view), "zoom-level", &zoom, NULL);
	g_string_printf(buffer, "%d", zoom);
	WRITE_NL();
	write_char_option(outstr, indent, "zoom-level", buffer->str);

	g_object_get(G_OBJECT(pgd->gps_view), "latitude", &position, NULL);
	int_position = position * 1000000;
	g_string_printf(buffer, "%i", int_position);
	WRITE_NL();
	write_char_option(outstr, indent, "latitude", buffer->str);

	g_object_get(G_OBJECT(pgd->gps_view), "longitude", &position, NULL);
	int_position = position * 1000000;
	g_string_printf(buffer, "%i", int_position);
	WRITE_NL();
	write_char_option(outstr, indent, "longitude", buffer->str);

	indent--;
	WRITE_NL();
	WRITE_STRING("/>");

	g_string_free(buffer, TRUE);
	g_object_unref(mapsource);
}

static void bar_pane_gps_slider_changed_cb(GtkScaleButton *slider,
					   gdouble zoom,
					   gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);
	GString *message;

	message = g_string_new("");
	g_string_printf(message, _("Zoom %i"), static_cast<gint>(zoom));

	g_object_set(G_OBJECT(CHAMPLAIN_VIEW(pgd->gps_view)), "zoom-level", static_cast<gint>(zoom), NULL);
	gtk_widget_set_tooltip_text(GTK_WIDGET(slider), message->str);
	g_string_free(message, TRUE);

}
static void bar_pane_gps_view_state_changed_cb(ChamplainView *view, GParamSpec *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);
 	ChamplainState status;
 	gint zoom;
	GString *message;

	g_object_get(G_OBJECT(view), "zoom-level", &zoom, NULL);
	message = g_string_new("");
	g_string_printf(message, _("Zoom level %i"), zoom);

	g_object_get(G_OBJECT(view), "state", &status, NULL);
	if (status == CHAMPLAIN_STATE_LOADING)
		{
		gtk_label_set_text(GTK_LABEL(pgd->state), _("Loading map"));
		}
	else
		{
		gtk_label_set_text(GTK_LABEL(pgd->state), message->str);
		}

	gtk_widget_set_tooltip_text(GTK_WIDGET(pgd->slider), message->str);
	gtk_scale_button_set_value(GTK_SCALE_BUTTON(pgd->slider), static_cast<gdouble>(zoom));

	g_string_free(message, TRUE);
}

static void bar_pane_gps_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_METADATA)) &&
	    g_list_find(pgd->selection_list, fd))
		{
		bar_pane_gps_update(pgd);
		}
}

const gchar *bar_pane_gps_get_map_id(PaneGPSData *pgd)
{
	const gchar *map_id;
	ChamplainMapSource *mapsource;

	g_object_get(G_OBJECT(pgd->gps_view), "map-source", &mapsource, NULL);
	map_id = champlain_map_source_get_id(mapsource);

	g_object_unref(mapsource);

	return map_id;
}

static GtkWidget *bar_pane_gps_menu(PaneGPSData *pgd)
{
	GtkWidget *menu;
	GtkWidget *map_centre;
	ChamplainMapSourceFactory *map_factory;
	GSList *map_list;
	ChamplainMapSourceDesc *map_desc;
	const gchar *current;

	menu = popup_menu_short_lived();

	map_factory = champlain_map_source_factory_dup_default();
	map_list = champlain_map_source_factory_get_registered(map_factory);
	current = bar_pane_gps_get_map_id(pgd);

	while (map_list)
		{
		map_desc = static_cast<ChamplainMapSourceDesc *>(map_list->data);

		menu_item_add_radio(menu,
		                    champlain_map_source_desc_get_name(map_desc),
		                    const_cast<gchar *>(champlain_map_source_desc_get_id(map_desc)),
		                    strcmp(champlain_map_source_desc_get_id(map_desc), current) == 0,
		                    G_CALLBACK(bar_pane_gps_change_map_cb), pgd);

		map_list = g_slist_next(map_list);
		}

	menu_item_add_divider(menu);
	menu_item_add_check(menu, _("Enable markers"), pgd->enable_markers_checked,
	                    G_CALLBACK(bar_pane_gps_enable_markers_checked_toggle_cb), pgd);
	map_centre = menu_item_add_check(menu, _("Centre map on marker"), pgd->centre_map_checked,
	                                 G_CALLBACK(bar_pane_gps_centre_map_checked_toggle_cb), pgd);
	if (!pgd->enable_markers_checked)
		{
		gtk_widget_set_sensitive(map_centre, FALSE);
		}

	g_slist_free(map_list);
	g_object_unref(map_factory);

	return menu;
}

/* Determine if the map is to be re-centred on the marker when another photo is selected
 */
void bar_pane_gps_map_centreing(PaneGPSData *pgd)
{
	GenericDialog *gd;
	GString *message = g_string_new("");

	if (pgd->centre_map_checked)
		{
		message = g_string_append(message, _("Move map centre to marker\n is disabled"));
		pgd->centre_map_checked = FALSE;
		}
	else
		{
		message = g_string_append(message, _("Move map centre to marker\n is enabled"));
		pgd->centre_map_checked = TRUE;
		}

	gd = generic_dialog_new(_("Map centering"),
				"map_centering", nullptr, TRUE, nullptr, pgd);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_INFO,
				"Map Centering", message->str, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", NULL, TRUE);

	gtk_widget_show(gd->dialog);

	g_string_free(message, TRUE);
}

static gboolean bar_pane_gps_map_keypress_cb(GtkWidget *, GdkEventButton *bevent, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);
	GtkWidget *menu;
	GtkClipboard *clipboard;
	gchar *geo_coords;

	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		menu = bar_pane_gps_menu(pgd);
		gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
		return TRUE;
		}
	else if (bevent->button == MOUSE_BUTTON_MIDDLE)
		{
		bar_pane_gps_map_centreing(pgd);
		return TRUE;
		}
	else if (bevent->button == MOUSE_BUTTON_LEFT)
		{
		clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		geo_coords = g_strdup_printf("%lf %lf",
							champlain_view_y_to_latitude(
								CHAMPLAIN_VIEW(pgd->gps_view),bevent->y),
							champlain_view_x_to_longitude(
								CHAMPLAIN_VIEW(pgd->gps_view),bevent->x));
		gtk_clipboard_set_text(clipboard, geo_coords, -1);

		g_free(geo_coords);

		return TRUE;
		}
	else
		{
		return FALSE;
		}
}

static void bar_pane_gps_destroy(GtkWidget *, gpointer data)
{
	auto pgd = static_cast<PaneGPSData *>(data);

	file_data_unregister_notify_func(bar_pane_gps_notify_cb, pgd);

	g_idle_remove_by_data(pgd);

	filelist_free(pgd->selection_list);
	if (pgd->bbox) champlain_bounding_box_free(pgd->bbox);

	file_data_unref(pgd->fd);
	g_free(pgd->map_source);
	g_free(pgd->pane.id);
	clutter_actor_destroy(pgd->gps_view);
	g_free(pgd);
}


GtkWidget *bar_pane_gps_new(const gchar *id, const gchar *title, const gchar *map_id,
         					const gint zoom, const gdouble latitude, const gdouble longitude,
            				gboolean expanded, gint height)
{
	PaneGPSData *pgd;
	GtkWidget *vbox, *frame;
	GtkWidget *gpswidget;
	GtkWidget *status, *state, *progress, *slider;
	ChamplainMarkerLayer *layer;
	ChamplainView *view;
	const gchar *slider_list[] = {GQ_ICON_ZOOM_IN, GQ_ICON_ZOOM_OUT, nullptr};
	const gchar **slider_icons = slider_list;

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

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gpswidget = gtk_champlain_embed_new();
	view = gtk_champlain_embed_get_view(GTK_CHAMPLAIN_EMBED(gpswidget));

	gtk_box_pack_start(GTK_BOX(vbox), gpswidget, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	slider = gtk_scale_button_new(GTK_ICON_SIZE_SMALL_TOOLBAR, 1, 17, 1, slider_icons);
	gtk_widget_set_tooltip_text(slider, _("Zoom"));
	gtk_scale_button_set_value(GTK_SCALE_BUTTON(slider), static_cast<gdouble>(zoom));

	progress = gtk_progress_bar_new();
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), TRUE);

	state = gtk_label_new("");
	gtk_label_set_justify(GTK_LABEL(state), GTK_JUSTIFY_LEFT);
	gtk_label_set_ellipsize(GTK_LABEL(state), PANGO_ELLIPSIZE_START);
	gtk_widget_set_tooltip_text(state, _("Zoom level"));

	gtk_box_pack_start(GTK_BOX(status), GTK_WIDGET(slider), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(status), GTK_WIDGET(state), FALSE, FALSE, 5);
	gtk_box_pack_end(GTK_BOX(status), GTK_WIDGET(progress), FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox),GTK_WIDGET(status), FALSE, FALSE, 0);

	layer = champlain_marker_layer_new();
	champlain_view_add_layer(view, CHAMPLAIN_LAYER(layer));

	pgd->icon_layer = layer;
	pgd->gps_view = CLUTTER_ACTOR(view);
	pgd->widget = frame;
	pgd->progress = progress;
	pgd->slider = slider;
	pgd->state = state;

	bar_pane_gps_set_map_source(pgd, map_id);

	g_object_set(G_OBJECT(view), "kinetic-mode", TRUE,
				     "zoom-level", zoom,
				     "keep-center-on-resize", TRUE,
				     "deceleration", 1.1,
				     "zoom-on-double-click", FALSE,
				     "max-zoom-level", 17,
				     "min-zoom-level", 1,
				     NULL);
	champlain_view_center_on(view, latitude, longitude);
	pgd->centre_map_checked = TRUE;
	g_object_set_data(G_OBJECT(pgd->widget), "pane_data", pgd);
	g_signal_connect(G_OBJECT(pgd->widget), "destroy", G_CALLBACK(bar_pane_gps_destroy), pgd);

	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

	gtk_widget_set_size_request(pgd->widget, -1, height);

	g_signal_connect(G_OBJECT(gpswidget), "button_press_event", G_CALLBACK(bar_pane_gps_map_keypress_cb), pgd);
	g_signal_connect(pgd->gps_view, "notify::state", G_CALLBACK(bar_pane_gps_view_state_changed_cb), pgd);
	g_signal_connect(pgd->gps_view, "notify::zoom-level", G_CALLBACK(bar_pane_gps_view_state_changed_cb), pgd);
	g_signal_connect(G_OBJECT(slider), "value-changed", G_CALLBACK(bar_pane_gps_slider_changed_cb), pgd);

	bar_pane_gps_dnd_init(pgd);

	file_data_register_notify_func(bar_pane_gps_notify_cb, pgd, NOTIFY_PRIORITY_LOW);

	pgd->create_markers_id = 0;
	pgd->enable_markers_checked = TRUE;
	pgd->centre_map_checked = TRUE;

	return pgd->widget;
}

GtkWidget *bar_pane_gps_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
	gchar *title = g_strdup(_("GPS Map"));
	gchar *map_id = nullptr;
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
	gchar *id = g_strdup("gps");
	GtkWidget *ret;

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

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	bar_pane_translate_title(PANE_COMMENT, id, &title);
	latitude = static_cast<gdouble>(int_latitude) / 1000000;
	longitude = static_cast<gdouble>(int_longitude) / 1000000;
	ret = bar_pane_gps_new(id, title, map_id, zoom, latitude, longitude, expanded, height);
	g_free(title);
	g_free(map_id);
	g_free(id);
	return ret;
}

void bar_pane_gps_update_from_config(GtkWidget *pane, const gchar **attribute_names,
                                						const gchar **attribute_values)
{
	PaneGPSData *pgd;
	gint zoom;
	gint int_longitude, int_latitude;
	gdouble longitude, latitude;

	pgd = static_cast<PaneGPSData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pgd)
		return;

	gchar *title = nullptr;

	while (*attribute_names)
	{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title))
			continue;
		if (READ_CHAR_FULL("map-id", pgd->map_source))
			continue;
		if (READ_BOOL_FULL("expanded", pgd->pane.expanded))
			continue;
		if (READ_INT_FULL("height", pgd->height))
			continue;
		if (READ_CHAR_FULL("id", pgd->pane.id))
			continue;
		if (READ_INT_CLAMP_FULL("zoom-level", zoom, 1, 8))
			{
			g_object_set(G_OBJECT(CHAMPLAIN_VIEW(pgd->gps_view)), "zoom-level", zoom, NULL);
			continue;
			}
		if (READ_INT_CLAMP_FULL("longitude", int_longitude, -90000000, +90000000))
			{
			longitude = int_longitude / 1000000;
			g_object_set(G_OBJECT(CHAMPLAIN_VIEW(pgd->gps_view)), "longitude", longitude, NULL);
			continue;
			}
		if (READ_INT_CLAMP_FULL("latitude", int_latitude, -90000000, +90000000))
			{
			latitude = int_latitude / 1000000;
			g_object_set(G_OBJECT(CHAMPLAIN_VIEW(pgd->gps_view)), "latitude", latitude, NULL);
			continue;
			}
		log_printf("unknown attribute %s = %s\n", option, value);
	}

	if (title)
		{
		bar_pane_translate_title(PANE_COMMENT, pgd->pane.id, &title);
		gtk_label_set_text(GTK_LABEL(pgd->pane.title), title);
		g_free(title);
		}

	gtk_widget_set_size_request(pgd->widget, -1, pgd->height);
	bar_update_expander(pane);
}

#endif
#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
