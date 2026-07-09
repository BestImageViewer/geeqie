/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#ifndef COMPAT_H
#define COMPAT_H

#include <glib.h>
#include <gtk/gtk.h>

#include <config.h>

/* Some systems (BSD,MacOsX,HP-UX,...) define MAP_ANON and not MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define	MAP_ANONYMOUS	MAP_ANON
#elif defined(MAP_ANONYMOUS) && !defined(MAP_ANON)
#define	MAP_ANON	MAP_ANONYMOUS
#endif

#ifndef GDK_ACTION_NONE
#define GDK_ACTION_NONE static_cast<GdkDragAction>(0)
#endif

struct GqMouseButtonEvent
{
	guint button;
	gdouble x;
	gdouble y;
	GdkModifierType state;
	guint press_count;
};

struct GqKeyEvent
{
	guint keyval;
	guint keycode;
	GdkModifierType state;
	guint32 time;
};

struct GqScrollEvent
{
	gdouble x;
	gdouble y;
	gdouble dx;
	gdouble dy;
	GdkModifierType state;
	GdkScrollDirection direction;
	guint32 time;
};

struct GqPointerMotionEvent
{
	gdouble x;
	gdouble y;
	gdouble dx;
	gdouble dy;
	GdkModifierType state;
};

#define gq_icon_theme_get_default() gtk_icon_theme_get_for_display(gdk_display_get_default())

void gq_gtk_box_pack_start(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);
void gq_gtk_box_pack_end(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);

gint gq_gtk_box_get_child_position(GtkBox *box, GtkWidget *child);
void gq_gtk_box_reorder_child(GtkBox *box, GtkWidget *child, gint position);

void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget);
void gq_gtk_container_remove(GtkWidget *container, GtkWidget *widget);
void gq_gtk_widget_show_all(GtkWidget *widget);
void gq_gtk_window_set_keep_above(GtkWindow *window, gboolean setting);
void gq_gtk_widget_destroy(GtkWidget *widget);
void gq_gtk_widget_set_border_width(GtkWidget *widget, guint width);
GtkWidget *gq_gtk_image_new_from_stock(const gchar *stock_id, gint size);
GtkWidget *gq_gtk_widget_get_focus_child(GtkWidget *widget);

#endif /* COMPAT_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
