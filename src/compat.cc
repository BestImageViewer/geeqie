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

#include "compat.h"

#include <config.h>

#include "compat-deprecated.h"

#if HAVE_GTK4
void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget)
{
	if (GTK_IS_BUTTON(container))
		{
		gtk_button_set_child(GTK_BUTTON(container), widget);
		}
	else if (GTK_IS_BUTTON_BOX(container))
		{
		gtk_box_set_child(GTK_BUTTON_BOX(container), widget);
		}
	else if (GTK_IS_EXPANDER(container))
		{
		gtk_expander_set_child(GTK_EXPANDER(container), widget);
		}
	else if (GTK_IS_FRAME(container))
		{
		gtk_frame_set_child(GTK_FRAME(container), widget);
		}
	else if (GTK_IS_MENU_ITEM(container))
		{
		gtk_frame_set_child(container, widget); /* @FIXME GTK4 menu */
		}
	else if (GTK_IS_POPOVER(container))
		{
		gtk_popover_set_child(GTK_POPOVER(container), widget);
		}
	else if (GTK_IS_TOGGLE_BUTTON(container))
		{
		gtk_toggle_button_set_child(GTK_TOGGLE_BUTTON(container), widget);
		}
	else if (GTK_IS_TOOLBAR(container))
		{
		gtk_toolbar_set_child(GTK_TOOLBAR(container), widget);
		}
	else if (GTK_IS_VIEWPORT(container))
		{
		gtk_viewport_set_child(GTK_VIEWPORT(container), widget);
		}
	else if (GTK_IS_WINDOW(container))
		{
		gtk_window_set_child(GTK_WINDOW(container), widget);
		}
	else
		{
		g_abort();
		}
}

GtkWidget *gq_gtk_image_new_from_stock(const gchar *stock_id, GtkIconSize size)
{
	return nullptr;
}

GtkWidget *gq_gtk_bin_get_child(GtkWidget *widget)
{
	return gtk_widget_get_first_child(widget);
}

GList *gq_gtk_widget_get_children(GtkWidget *widget)
{
	GList *list = NULL;

	for (GtkWidget *child = gtk_widget_get_first_child(widget);
		child;
		child = gtk_widget_get_next_sibling(child))
		{
		list = g_list_prepend(list, child);
		}

	return g_list_reverse(list);
}

void gq_gtk_viewport_set_shadow_type(GtkWidget *, int)
{
}

#else
void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget)
{
	gtk_container_add(GTK_CONTAINER(container), widget);
}

GtkWidget *gq_gtk_image_new_from_stock(const gchar *stock_id, GtkIconSize size)
{
	return deprecated_gtk_image_new_from_stock(stock_id, size);
}

GtkWidget *gq_gtk_bin_get_child(GtkWidget *widget)
{
	return gtk_bin_get_child(GTK_BIN(widget));
}

GList *gq_gtk_widget_get_children(GtkWidget *widget)
{
	return gtk_container_get_children(GTK_CONTAINER(widget));
}

void gq_gtk_viewport_set_shadow_type(GtkWidget *viewport, int type)
{
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), static_cast<GtkShadowType>(type));
}

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
