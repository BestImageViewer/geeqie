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

#include <config.h>
#include "intl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "main.h"
#include "ui-spinner.h"

#include "ui_icons.h"
#include "ui-fileops.h"


#define SPINNER_FRAMES 19


/*
 *-----------------------------------------------------------------------------
 * spinner utility
 *-----------------------------------------------------------------------------
 */

typedef struct _SpinnerData SpinnerData;
struct _SpinnerData {
	GtkWidget *image;
	GList *list;		/* list of pixbufs */
	guint frame;
	guint timer_id; /* event source id */
};

static void spinner_set_frame(SpinnerData *sp, guint frame)
{
	GdkPixbuf *pb;

	pb = g_list_nth_data(sp->list, frame);
	if (pb) gtk_image_set_from_pixbuf(GTK_IMAGE(sp->image), pb);

	sp->frame = frame;
}

static void spinner_increment_frame(SpinnerData *sp)
{
	sp->frame++;
	if (sp->frame >= g_list_length(sp->list)) sp->frame = 1;
	spinner_set_frame(sp, sp->frame);
}

static gboolean spinner_loop_cb(gpointer data)
{
	SpinnerData *sp = data;

	if (sp->list) spinner_increment_frame(sp);

	return TRUE;
}

static void spinner_set_timeout(SpinnerData *sp, gint interval)
{
	if (!sp) return;

	if (sp->timer_id)
		{
		g_source_remove(sp->timer_id);
		sp->timer_id = 0;
		}

	if (interval > 0)
		{
		sp->timer_id = g_timeout_add(interval, spinner_loop_cb, sp);
		}
	else if (interval < 0)
		{
		spinner_set_frame(sp, 0);
		}

	gtk_widget_set_sensitive(sp->image, (interval >= 0));
}

static void spinner_destroy_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	SpinnerData *sp = data;
	GList *work;

	spinner_set_timeout(sp, 0);

	work = sp->list;
	while (work)
		{
		GdkPixbuf *pb = work->data;
		work = work->next;

		g_object_unref(pb);
		}
	g_list_free(sp->list);
	g_free(sp);
}

GtkWidget *spinner_new(const gchar *path, gint interval)
{
	SpinnerData *sp;

	sp = g_new0(SpinnerData, 1);

	if (path)
		{
		gchar *pathl;
		GdkPixbuf *pb;
		gint n;
		gchar *buf;

		pathl = path_from_utf8(path);

		n = 0;
		buf = g_strdup_printf("%s%02d.png", pathl, n);
		while ((pb = gdk_pixbuf_new_from_file(buf, NULL)))
			{
			sp->list = g_list_append(sp->list, pb);

			n++;
			g_free(buf);
			buf = g_strdup_printf("%s%02d.png", pathl, n);
			}
		g_free(buf);

		g_free(pathl);
		}

	if (!sp->list)
		{
		GdkPixbuf *pb;
		gint n;
		gint w, h;

		pb = gdk_pixbuf_new_from_inline(-1, icon_spinner, FALSE, NULL);
		w = gdk_pixbuf_get_width(pb);
		h = gdk_pixbuf_get_height(pb) / SPINNER_FRAMES;
		for (n = 0; n < SPINNER_FRAMES; n++)
			{
			sp->list = g_list_append(sp->list,
						 gdk_pixbuf_new_subpixbuf(pb, 0, n * h, w, h));
			}
		/* pb pixels is inline static, so the subpixbufs in sp->list will be ok */
		g_object_unref(pb);
		}

	if (sp->list)
		{
		GdkPixbuf *pb;

		pb = sp->list->data;
		sp->image = gtk_image_new_from_pixbuf(pb);
		}
	else
		{
		sp->image = gtk_image_new_from_stock(GTK_STOCK_MISSING_IMAGE, GTK_ICON_SIZE_DIALOG);
		}

	g_object_set_data(G_OBJECT(sp->image), "spinner", sp);

	g_signal_connect(G_OBJECT(sp->image), "destroy",
			 G_CALLBACK(spinner_destroy_cb), sp);

	spinner_set_timeout(sp, interval);

	return sp->image;
}

void spinner_set_interval(GtkWidget *spinner, gint interval)
{
	SpinnerData *sp;

	sp = g_object_get_data(G_OBJECT(spinner), "spinner");

	spinner_set_timeout(sp, interval);
}

void spinner_step(GtkWidget *spinner, gboolean reset)
{
	SpinnerData *sp;

	sp = g_object_get_data(G_OBJECT(spinner), "spinner");
	if (sp->timer_id)
		{
		log_printf("spinner warning: attempt to step with timer set\n");
		return;
		}

	if (reset)
		{
		spinner_set_frame(sp, 0);
		}
	else
		{
		spinner_increment_frame(sp);
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
