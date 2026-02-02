/*
 * Copyright (C) 2000 Red Hat, Inc., Jonathan Blandford <jrb@redhat.com>
 * Copyright (C) 2008 - 2016 The Geeqie Team
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

#ifndef GQV_CELL_RENDERER_ICON_H
#define GQV_CELL_RENDERER_ICON_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <pango/pango.h>

struct GQvCellRendererIcon;

#define TOGGLE_WIDTH 13
#define TOGGLE_SPACING 16


#define GQV_TYPE_CELL_RENDERER_ICON		(gqv_cell_renderer_icon_get_type())
#define GQV_CELL_RENDERER_ICON(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GQV_TYPE_CELL_RENDERER_ICON, GQvCellRendererIcon))
#define GQV_CELL_RENDERER_ICON_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GQV_TYPE_CELL_RENDERER_ICON, GQvCellRendererIconClass))
#define GQV_IS_CELL_RENDERER_ICON(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GQV_TYPE_CELL_RENDERER_ICON))
#define GQV_IS_CELL_RENDERER_ICON_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GQV_TYPE_CELL_RENDERER_ICON))
#define GQV_CELL_RENDERER_ICON_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GQV_TYPE_CELL_RENDERER_ICON, GQvCellRendererIconClass))

struct GQvCellRendererIcon
{
	GtkCellRenderer parent;

	/*< private >*/
	GdkPixbuf *pixbuf;
	gchar *text;
	PangoColor foreground;
	PangoColor background;
	gboolean focused;

	gint fixed_width;
	gint fixed_height;

	gboolean foreground_set;
	gboolean background_set;

	gint num_marks;

	gboolean show_text;
	gboolean show_marks;

	guint marks;
	guint toggled_mark;

};

struct GQvCellRendererIconClass
{
	GtkCellRendererClass parent_class;

	void (*toggled)(GQvCellRendererIcon *cell_renderer, const gchar *path);

	/* Padding for future expansion */
	void (*_gtk_reserved1)();
	void (*_gtk_reserved2)();
	void (*_gtk_reserved3)();
	void (*_gtk_reserved4)();
};

GType            gqv_cell_renderer_icon_get_type();
GtkCellRenderer *gqv_cell_renderer_icon_new();

#endif /* GQV_CELL_RENDERER_ICON_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
