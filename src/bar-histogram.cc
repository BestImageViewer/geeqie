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

#include "main.h"
#include "bar-histogram.h"

#include "bar.h"
#include "filedata.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "histogram.h"
#include "rcfile.h"

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
	Histogram *histogram;
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

	gtk_label_set_text(GTK_LABEL(phd->pane.title), histogram_label(phd->histogram));

	if (!phd->histogram_width || !phd->histogram_height || !phd->fd) return;

	/** histmap_get is relatively expensive, run it only when we really need it
	   and with lower priority than pixbuf_renderer
	   @FIXME this does not work for fullscreen */
	if (gtk_widget_is_drawable(phd->drawing_area))
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
	const HistMap *histmap;
	auto phd = static_cast<PaneHistogramData *>(data);

	phd->idle_id = 0;
	phd->need_update = FALSE;

	gtk_widget_queue_draw_area(GTK_WIDGET(phd->drawing_area), 0, 0, phd->histogram_width, phd->histogram_height);

	if (phd->fd == nullptr) return G_SOURCE_REMOVE;
	histmap = histmap_get(phd->fd);

	if (!histmap)
		{
		histmap_start_idle(phd->fd);
		return G_SOURCE_REMOVE;
		}

	phd->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, phd->histogram_width, phd->histogram_height);
	gdk_pixbuf_fill(phd->pixbuf, 0xffffffff);
	histogram_draw(phd->histogram, histmap, phd->pixbuf, 0, 0, phd->histogram_width, phd->histogram_height);

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
	write_char_option(outstr, indent, "id", phd->pane.id);
	write_char_option(outstr, indent, "title", gtk_label_get_text(GTK_LABEL(phd->pane.title)));
	WRITE_BOOL(phd->pane, expanded);
	WRITE_INT(*phd->histogram, histogram_channel);
	WRITE_INT(*phd->histogram, histogram_mode);
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

static gboolean bar_pane_histogram_draw_cb(GtkWidget *, cairo_t *cr, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	if (!phd) return TRUE;

	if (phd->need_update)
		{
		bar_pane_histogram_update(phd);
		}

	if (!phd->pixbuf) return TRUE;

	gdk_cairo_set_source_pixbuf(cr, phd->pixbuf, 0, 0);
	cairo_paint (cr);

	return TRUE;
}

static void bar_pane_histogram_size_cb(GtkWidget *, GtkAllocation *allocation, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);

	phd->histogram_width = allocation->width;
	phd->histogram_height = allocation->height;
	bar_pane_histogram_update(phd);
}

static void bar_pane_histogram_destroy(GtkWidget *, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);

	if (phd->idle_id) g_source_remove(phd->idle_id);
	file_data_unregister_notify_func(bar_pane_histogram_notify_cb, phd);

	file_data_unref(phd->fd);
	histogram_free(phd->histogram);
	if (phd->pixbuf) g_object_unref(phd->pixbuf);
	g_free(phd->pane.id);

	g_free(phd);
}

static void bar_pane_histogram_popup_channels_cb(GtkWidget *widget, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	gint channel;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	if (!phd) return;

	channel = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "menu_item_radio_data"));
	if (channel == histogram_get_channel(phd->histogram)) return;

	histogram_set_channel(phd->histogram, channel);
	bar_pane_histogram_update(phd);
}

static void bar_pane_histogram_popup_mode_cb(GtkWidget *widget, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	gint logmode;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	if (!phd) return;

	logmode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "menu_item_radio_data"));
	if (logmode == histogram_get_mode(phd->histogram)) return;

	histogram_set_mode(phd->histogram, logmode);
	bar_pane_histogram_update(phd);
}

static GtkWidget *bar_pane_histogram_menu(PaneHistogramData *phd)
{
	GtkWidget *menu;
	gint channel = histogram_get_channel(phd->histogram);
	gint mode = histogram_get_mode(phd->histogram);

	menu = popup_menu_short_lived();

	/* use the same strings as in layout-util.cc */
	menu_item_add_radio(menu, _("Histogram on _Red"),   GINT_TO_POINTER(HCHAN_R), (channel == HCHAN_R), G_CALLBACK(bar_pane_histogram_popup_channels_cb), phd);
	menu_item_add_radio(menu, _("Histogram on _Green"), GINT_TO_POINTER(HCHAN_G), (channel == HCHAN_G), G_CALLBACK(bar_pane_histogram_popup_channels_cb), phd);
	menu_item_add_radio(menu, _("Histogram on _Blue"),  GINT_TO_POINTER(HCHAN_B), (channel == HCHAN_B), G_CALLBACK(bar_pane_histogram_popup_channels_cb), phd);
	menu_item_add_radio(menu, _("_Histogram on RGB"),   GINT_TO_POINTER(HCHAN_RGB), (channel == HCHAN_RGB), G_CALLBACK(bar_pane_histogram_popup_channels_cb), phd);
	menu_item_add_radio(menu, _("Histogram on _Value"), GINT_TO_POINTER(HCHAN_MAX), (channel == HCHAN_MAX), G_CALLBACK(bar_pane_histogram_popup_channels_cb), phd);

	menu_item_add_divider(menu);

	menu_item_add_radio(menu, _("Li_near Histogram"), GINT_TO_POINTER(0), (mode == 0), G_CALLBACK(bar_pane_histogram_popup_mode_cb), phd);
	menu_item_add_radio(menu, _("L_og Histogram"),    GINT_TO_POINTER(1), (mode == 1), G_CALLBACK(bar_pane_histogram_popup_mode_cb), phd);

	return menu;
}

static gboolean bar_pane_histogram_press_cb(GtkGesture *, gint, gdouble, gdouble, gpointer data)
{
	auto phd = static_cast<PaneHistogramData *>(data);
	GtkWidget *menu;

	menu = bar_pane_histogram_menu(phd);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);

	return TRUE;
}


static GtkWidget *bar_pane_histogram_new(const gchar *id, const gchar *title, gint height, gboolean expanded, gint histogram_channel, gint histogram_mode)
{
	PaneHistogramData *phd;
	GtkGesture *gesture;

	phd = g_new0(PaneHistogramData, 1);

	phd->pane.pane_set_fd = bar_pane_histogram_set_fd;
	phd->pane.pane_write_config = bar_pane_histogram_write_config;
	phd->pane.title = bar_pane_expander_title(title);
	phd->pane.id = g_strdup(id);
	phd->pane.type = PANE_HISTOGRAM;

	phd->pane.expanded = expanded;

	phd->histogram = histogram_new();

	histogram_set_channel(phd->histogram, histogram_channel);
	histogram_set_mode(phd->histogram, histogram_mode);

	phd->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

	g_object_set_data(G_OBJECT(phd->widget), "pane_data", phd);
	g_signal_connect(G_OBJECT(phd->widget), "destroy",
			 G_CALLBACK(bar_pane_histogram_destroy), phd);


	gtk_widget_set_size_request(GTK_WIDGET(phd->widget), -1, height);

	phd->drawing_area = gtk_drawing_area_new();
	g_signal_connect_after(G_OBJECT(phd->drawing_area), "size_allocate",
                               G_CALLBACK(bar_pane_histogram_size_cb), phd);

	g_signal_connect(G_OBJECT(phd->drawing_area), "draw",
			 G_CALLBACK(bar_pane_histogram_draw_cb), phd);

	gtk_box_pack_start(GTK_BOX(phd->widget), phd->drawing_area, TRUE, TRUE, 0);
	gtk_widget_show(phd->drawing_area);
	gtk_widget_add_events(phd->drawing_area, GDK_BUTTON_PRESS_MASK);


#ifdef HAVE_GTK4
	gesture = gtk_gesture_click_new();
	gtk_widget_add_controller(phd->drawing_area, GTK_EVENT_CONTROLLER(gesture));
#else
	gesture = gtk_gesture_multi_press_new(phd->drawing_area);
#endif
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), MOUSE_BUTTON_RIGHT);
	g_signal_connect(gesture, "pressed", G_CALLBACK(bar_pane_histogram_press_cb), phd);

	gtk_widget_show(phd->widget);

	file_data_register_notify_func(bar_pane_histogram_notify_cb, phd, NOTIFY_PRIORITY_LOW);

	return phd->widget;
}

GtkWidget *bar_pane_histogram_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
	gchar *title = nullptr;
	gchar *id = g_strdup("histogram");
	gboolean expanded = TRUE;
	gint height = 80;
	gint histogram_channel = HCHAN_RGB;
	gint histogram_mode = 0;
	GtkWidget *ret;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("id", id)) continue;
		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_BOOL_FULL("expanded", expanded)) continue;
		if (READ_INT_FULL("histogram_channel", histogram_channel)) continue;
		if (READ_INT_FULL("histogram_mode", histogram_mode)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	bar_pane_translate_title(PANE_HISTOGRAM, id, &title);
	ret = bar_pane_histogram_new(id, title, height, expanded, histogram_channel, histogram_mode);
	g_free(title);
	g_free(id);
	return ret;
}

void bar_pane_histogram_update_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
	PaneHistogramData *phd;

	phd = static_cast<PaneHistogramData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!phd) return;

	gint histogram_channel = phd->histogram->histogram_channel;
	gint histogram_mode = phd->histogram->histogram_mode;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("id", phd->pane.id)) continue;
		if (READ_BOOL_FULL("expanded", phd->pane.expanded)) continue;
		if (READ_INT_FULL("histogram_channel", histogram_channel)) continue;
		if (READ_INT_FULL("histogram_mode", histogram_mode)) continue;


		log_printf("unknown attribute %s = %s\n", option, value);
		}

	histogram_set_channel(phd->histogram, histogram_channel);
	histogram_set_mode(phd->histogram, histogram_mode);

	bar_update_expander(pane);
	bar_pane_histogram_update(phd);
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
