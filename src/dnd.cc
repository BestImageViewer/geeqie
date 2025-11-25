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

#include <glib-object.h>
#include <pango/pango.h>

#include "compat.h"
#include "options.h"
#include "pixbuf-util.h"


constexpr std::array<GtkTargetEntry, 2> dnd_file_drag_types{{
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
}};

constexpr std::array<GtkTargetEntry, 3> dnd_file_drop_types{{
	{ const_cast<gchar *>(TARGET_APP_COLLECTION_MEMBER_STRING), 0, TARGET_APP_COLLECTION_MEMBER },
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN },
}};


#define DND_ICON_SIZE (options->dnd_icon_size)


static void pixbuf_draw_border(GdkPixbuf *pixbuf, gint w, gint h)
{
	gboolean alpha;
	gint rs;
	guchar *pix;
	guchar *p;
	gint i;

	alpha = gdk_pixbuf_get_has_alpha(pixbuf);
	rs = gdk_pixbuf_get_rowstride(pixbuf);
	pix = gdk_pixbuf_get_pixels(pixbuf);

	p = pix;
	for (i = 0; i < w; i++)
		{
		*p = 0; p++; *p = 0; p++; *p = 0; p++;
		if (alpha) { *p= 255; p++; }
		}

	const gint p_step = alpha ? 4 : 3;
	for (i = 1; i < h - 1; i++)
		{
		p = pix + rs * i;
		*p = 0; p++; *p = 0; p++; *p = 0; p++;
		if (alpha) *p= 255;

		p = pix + rs * i + (w - 1) * p_step;
		*p = 0; p++; *p = 0; p++; *p = 0; p++;
		if (alpha) *p= 255;
		}
	p = pix + rs * (h - 1);
	for (i = 0; i < w; i++)
		{
		*p = 0; p++; *p = 0; p++; *p = 0; p++;
		if (alpha) { *p= 255; p++; }
		}
}

/**
 * @brief Sets a drag icon to pixbuf, if items is > 1, text is drawn onto icon to indicate value
 */
void dnd_set_drag_icon(GtkWidget *widget, GdkDragContext *context, GdkPixbuf *pixbuf, gint items)
{
	gint w;
	gint h;
	gint sw;
	gint sh;
	PangoLayout *layout = nullptr;
	gint x;
	gint y;

	x = y = 0;

	sw = gdk_pixbuf_get_width(pixbuf);
	sh = gdk_pixbuf_get_height(pixbuf);

	if (sw <= DND_ICON_SIZE && sh <= DND_ICON_SIZE)
		{
		w = sw;
		h = sh;
		}
	else if (sw < sh)
		{
		w = sw * DND_ICON_SIZE / sh;
		h = DND_ICON_SIZE;
		}
	else
		{
		w = DND_ICON_SIZE;
		h = sh * DND_ICON_SIZE / sw;
		}

	const gint dest_width = std::max(1, w);
	const gint dest_height = std::max(1, h);

	g_autoptr(GdkPixbuf) dest = gdk_pixbuf_scale_simple(pixbuf, dest_width, dest_height, GDK_INTERP_BILINEAR);
	pixbuf_draw_border(dest, dest_width, dest_height);

	if (items > 1)
		{
		gint lw;
		gint lh;

		layout = gtk_widget_create_pango_layout(widget, nullptr);

		g_autofree gchar *buf = g_strdup_printf("<small> %d </small>", items);
		pango_layout_set_markup(layout, buf, -1);

		pango_layout_get_pixel_size(layout, &lw, &lh);

		x = std::max(0, w - lw);
		y = std::max(0, h - lh);
		lw = std::clamp(lw, 0, w - x - 1);
		lh = std::clamp(lh, 0, h - y - 1);

		pixbuf_draw_rect_fill(dest, {x, y, lw, lh}, 128, 128, 128, 255);
		}

	if (layout)
		{
		pixbuf_draw_layout(dest, layout, x + 1, y + 1, 0, 0, 0, 255);
		pixbuf_draw_layout(dest, layout, x, y, 255, 255, 255, 255);

		g_object_unref(G_OBJECT(layout));
		}

	gtk_drag_set_icon_pixbuf(context, dest, -8, -6);
}

static void dnd_set_drag_label_end_cb(GtkWidget *widget, GdkDragContext *, gpointer data)
{
	auto window = static_cast<GtkWidget *>(data);
	g_signal_handlers_disconnect_by_func(widget, (gpointer)dnd_set_drag_label_end_cb, data);
	gq_gtk_widget_destroy(window);
}

void dnd_set_drag_label(GtkWidget *widget, GdkDragContext *context, const gchar *text)
{
	GtkWidget *label;

	GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_realize (window);

	label = gtk_label_new(text);
	gq_gtk_container_add(window, label);
	gtk_widget_show(label);
	gtk_drag_set_icon_widget(context, window, -15, 10);
	g_signal_connect(G_OBJECT(widget), "drag_end",
			 G_CALLBACK(dnd_set_drag_label_end_cb), window);
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
