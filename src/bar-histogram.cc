/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#include "bar-histogram.h"

#include <string>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>

#include <config.h>

#include "bar.h"
#include "compat.h"
#include "filedata.h"
#include "histogram.h"
#include "intl.h"
#include "rcfile.h"
#include "ui-menu.h"
#include "ui-misc.h"

struct HistMap;

/*
 *-------------------------------------------------------------------
 * keyword / comment utils
 *-------------------------------------------------------------------
 */

struct PaneHistogramData
{
	PaneData pane;
	GtkWidget *widget;
	GtkWidget *drawing_area;
	Histogram histogram;
	gint histogram_width;
	gint histogram_height;
	GdkPixbuf *pixbuf;
	FileData *fd;
	gboolean need_update;
	guint idle_id; /* event source id */
};

static gboolean bar_pane_histogram_update_cb(gpointer data);


static void bar_pane_histogram_update(PaneHistogramData *phd)
{
	if (phd->pixbuf) g_object_unref(phd->pixbuf);
	phd->pixbuf = nullptr;

	gtk_label_set_text(GTK_LABEL(phd->pane.title), phd->histogram.label());

	if (!phd->histogram_width || !phd->histogram_height || !phd->fd) return;

	/** histmap_get is relatively expensive, run it only when we really need it
	   and with lower priority than pixbuf_renderer
	   @FIXME this does not work for fullscreen */
	if (gtk_widget_get_mapped(phd->drawing_area))
		{
		if (!phd->idle_id)
			{
			phd->idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, bar_pane_histogram_update_cb, phd, nullptr);
			}
		}
	else
		{
		phd->need_update = TRUE;
		}
}

static gboolean bar_pane_histogram_update_cb(gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);

	phd->idle_id = 0;
	phd->need_update = FALSE;

	gtk_widget_queue_draw(phd->drawing_area);

	if (phd->fd != nullptr)
		{
		const HistMap *histmap = histmap_get(phd->fd);

		if (histmap)
			{
			phd->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, phd->histogram_width, phd->histogram_height);
			gdk_pixbuf_fill(phd->pixbuf, 0xffffffff);
			phd->histogram.draw(histmap, phd->pixbuf, 0, 0, phd->histogram_width, phd->histogram_height);
			}
		else
			{
			histmap_start_idle(phd->fd);
			}
		}

	return G_SOURCE_REMOVE;
}


static void bar_pane_histogram_set_fd(GtkWidget *pane, FileData *fd)
{
	PaneHistogramData *phd;

	phd = static_cast<PaneHistogramData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!phd) return;

	file_data_unref(phd->fd);
	phd->fd = file_data_ref(fd);

	bar_pane_histogram_update(phd);
}

static void bar_pane_histogram_write_config(GtkWidget *pane, GString *outstr, gint indent)
{
	PaneHistogramData *phd;

	phd = static_cast<PaneHistogramData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!phd) return;

	WRITE_NL(); WRITE_STRING("<pane_histogram ");
	WRITE_CHAR(phd->pane, id);
	WRITE_CHAR_FULL("title", gtk_label_get_text(GTK_LABEL(phd->pane.title)));
	WRITE_BOOL(phd->pane, expanded);
	WRITE_INT(phd->histogram, histogram_channel);
	WRITE_INT(phd->histogram, histogram_mode);
	WRITE_STRING("/>");
}

static void bar_pane_histogram_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_HISTMAP | NOTIFY_PIXBUF)) && fd == phd->fd)
		{
		DEBUG_1("Notify pane_histogram: %s %04x", fd->path, type);
		bar_pane_histogram_update(phd);
		}
}

static void bar_pane_histogram_draw_cb(GtkDrawingArea *, cairo_t *cr, gint, gint, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	if (!phd) return;

	if (phd->need_update)
		{
		bar_pane_histogram_update(phd);
		}

	if (!phd->pixbuf) return;

	gdk_cairo_set_source_pixbuf(cr, phd->pixbuf, 0, 0);
	cairo_paint (cr);
}

static void bar_pane_histogram_resize_cb(GtkWidget *, gint width, gint height, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);

	phd->histogram_width = width;
	phd->histogram_height = height;
	bar_pane_histogram_update(phd);
}

static void bar_pane_histogram_destroy(gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);

	if (phd->idle_id) g_source_remove(phd->idle_id);
	file_data_unregister_notify_func(bar_pane_histogram_notify_cb, phd);

	file_data_unref(phd->fd);
	if (phd->pixbuf) g_object_unref(phd->pixbuf);
	g_free(phd->pane.id);

	g_free(phd);
}

static const gchar *histogram_channel_action_value(HistogramChannel channel)
{
	switch (channel)
		{
		case HCHAN_R:
			return "red";
		case HCHAN_G:
			return "green";
		case HCHAN_B:
			return "blue";
		case HCHAN_RGB:
			return "rgb";
		case HCHAN_MAX:
			return "value";
		default:
			return "rgb";
		}
}

static HistogramChannel histogram_channel_from_action_value(const gchar *value)
{
	if (g_str_equal(value, "red")) return HCHAN_R;
	if (g_str_equal(value, "green")) return HCHAN_G;
	if (g_str_equal(value, "blue")) return HCHAN_B;
	if (g_str_equal(value, "rgb")) return HCHAN_RGB;
	if (g_str_equal(value, "value")) return HCHAN_MAX;

	return HCHAN_RGB;
}

static const gchar *histogram_mode_action_value(HistogramMode mode)
{
	switch (mode)
		{
		case HMODE_LINEAR:
			return "linear";
		case HMODE_LOG:
			return "log";
		default:
			return "linear";
		}
}

static HistogramMode histogram_mode_from_action_value(const gchar *value)
{
	if (g_str_equal(value, "linear")) return HMODE_LINEAR;
	if (g_str_equal(value, "log")) return HMODE_LOG;

	return HMODE_LINEAR;
}

static void bar_pane_histogram_channel_change_state_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	if (!phd) return;

	HistogramChannel channel = histogram_channel_from_action_value(g_variant_get_string(state, nullptr));
	if (channel == phd->histogram.get_channel()) return;

	phd->histogram.set_channel(channel);
	g_simple_action_set_state(action, state);
	bar_pane_histogram_update(phd);
}

static void bar_pane_histogram_mode_change_state_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	if (!phd) return;

	HistogramMode mode = histogram_mode_from_action_value(g_variant_get_string(state, nullptr));
	if (mode == phd->histogram.get_mode()) return;

	phd->histogram.set_mode(mode);
	g_simple_action_set_state(action, state);
	bar_pane_histogram_update(phd);
}

static void bar_pane_histogram_append_radio_item(GMenu *menu, const gchar *label, const gchar *action_name, const gchar *target)
{
	g_autoptr(GMenuItem) item = g_menu_item_new(label, action_name);

	g_menu_item_set_attribute(item, G_MENU_ATTRIBUTE_TARGET, "s", target);
	g_menu_append_item(menu, item);
}

static GtkWidget *bar_pane_histogram_menu(PaneHistogramData *phd, GtkWidget *parent, gdouble x, gdouble y)
{
	g_autoptr(GSimpleActionGroup) action_group = g_simple_action_group_new();
	g_autoptr(GSimpleAction) channel_action = g_simple_action_new_stateful("channel", G_VARIANT_TYPE_STRING,
	                                                                       g_variant_new_string(histogram_channel_action_value(static_cast<HistogramChannel>(phd->histogram.get_channel()))));
	g_autoptr(GSimpleAction) mode_action = g_simple_action_new_stateful("mode", G_VARIANT_TYPE_STRING,
	                                                                    g_variant_new_string(histogram_mode_action_value(static_cast<HistogramMode>(phd->histogram.get_mode()))));

	g_signal_connect(channel_action, "change-state", G_CALLBACK(bar_pane_histogram_channel_change_state_cb), phd);
	g_signal_connect(mode_action, "change-state", G_CALLBACK(bar_pane_histogram_mode_change_state_cb), phd);
	g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(channel_action));
	g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(mode_action));
	gtk_widget_insert_action_group(parent, "histogram", G_ACTION_GROUP(action_group));

	g_autoptr(GMenu) menu = g_menu_new();
	g_autoptr(GMenu) channel_section = g_menu_new();
	g_autoptr(GMenu) mode_section = g_menu_new();

	/* use the same strings as in layout-util.cc */
	bar_pane_histogram_append_radio_item(channel_section, _("Histogram on _Red"), "histogram.channel", "red");
	bar_pane_histogram_append_radio_item(channel_section, _("Histogram on _Green"), "histogram.channel", "green");
	bar_pane_histogram_append_radio_item(channel_section, _("Histogram on _Blue"), "histogram.channel", "blue");
	bar_pane_histogram_append_radio_item(channel_section, _("_Histogram on RGB"), "histogram.channel", "rgb");
	bar_pane_histogram_append_radio_item(channel_section, _("Histogram on _Value"), "histogram.channel", "value");
	g_menu_append_section(menu, nullptr, G_MENU_MODEL(channel_section));

	bar_pane_histogram_append_radio_item(mode_section, _("Li_near Histogram"), "histogram.mode", "linear");
	bar_pane_histogram_append_radio_item(mode_section, _("L_og Histogram"), "histogram.mode", "log");
	g_menu_append_section(menu, nullptr, G_MENU_MODEL(mode_section));

	return popup_menu_at(menu, parent, x, y);
}

static void bar_pane_histogram_press_cb(GtkGestureClick *gesture, gint, gdouble x, gdouble y, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	GtkWidget *menu;

	menu = bar_pane_histogram_menu(phd, gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)), x, y);
	(void)menu;
}


static GtkWidget *bar_pane_histogram_new(const gchar *id, const gchar *title, gint height, gboolean expanded, gint histogram_channel, gint histogram_mode)
{
	auto *phd = g_new0(PaneHistogramData, 1);

	phd->pane.pane_set_fd = bar_pane_histogram_set_fd;
	phd->pane.pane_write_config = bar_pane_histogram_write_config;
	phd->pane.title = bar_pane_expander_title(title);
	phd->pane.id = g_strdup(id);
	phd->pane.type = PANE_HISTOGRAM;
	phd->pane.expanded = expanded;

	phd->histogram = Histogram();
	phd->histogram.set_channel(histogram_channel);
	phd->histogram.set_mode(histogram_mode);

	phd->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	g_object_set_data_full(G_OBJECT(phd->widget), "pane_data", phd, bar_pane_histogram_destroy);
	gtk_widget_set_size_request(phd->widget, -1, height);

	phd->drawing_area = gtk_drawing_area_new();
	gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(phd->drawing_area), height);
	gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(phd->drawing_area), 1);
	gtk_widget_set_hexpand(phd->drawing_area, TRUE);
	gtk_widget_set_vexpand(phd->drawing_area, TRUE);

	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(phd->drawing_area),
	                               bar_pane_histogram_draw_cb,
	                               phd,
	                               nullptr);
	g_signal_connect(phd->drawing_area, "resize", G_CALLBACK(bar_pane_histogram_resize_cb), phd);

	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture),
	                              GDK_BUTTON_SECONDARY);
	g_signal_connect(gesture, "pressed", G_CALLBACK(bar_pane_histogram_press_cb), phd);
	gtk_box_append(GTK_BOX(phd->widget), phd->drawing_area);
	gtk_widget_add_controller(phd->widget, GTK_EVENT_CONTROLLER(gesture));

	file_data_register_notify_func(bar_pane_histogram_notify_cb, phd, NOTIFY_PRIORITY_LOW);

	return phd->widget;
}

GtkWidget *bar_pane_histogram_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
	g_autofree gchar *id = g_strdup("histogram");
	g_autofree gchar *title = nullptr;
	gboolean expanded = TRUE;
	constexpr gint height = 80;
	gint histogram_channel = HCHAN_RGB;
	gint histogram_mode = HMODE_LINEAR;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("id", id)) continue;
		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_BOOL_FULL("expanded", expanded)) continue;
		if (READ_INT_FULL("histogram_channel", histogram_channel)) continue;
		if (READ_INT_FULL("histogram_mode", histogram_mode)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	bar_pane_translate_title(PANE_HISTOGRAM, id, &title);

	return bar_pane_histogram_new(id, title, height, expanded, histogram_channel, histogram_mode);
}

void bar_pane_histogram_update_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
	auto *phd = static_cast<PaneHistogramData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!phd) return;

	gint histogram_channel = phd->histogram.get_channel();
	gint histogram_mode = phd->histogram.get_mode();

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR(phd->pane, id)) continue;
		if (READ_BOOL(phd->pane, expanded)) continue;
		if (READ_INT_FULL("histogram_channel", histogram_channel)) continue;
		if (READ_INT_FULL("histogram_mode", histogram_mode)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	phd->histogram.set_channel(histogram_channel);
	phd->histogram.set_mode(histogram_mode);

	bar_update_expander(pane);
	bar_pane_histogram_update(phd);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
