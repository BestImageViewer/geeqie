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

gint gq_gtk_box_get_child_position(GtkBox *box, GtkWidget *child)
{
	gint position = 0;

	for (GtkWidget *work = gtk_widget_get_first_child(GTK_WIDGET(box));
	     work != nullptr;
	     work = gtk_widget_get_next_sibling(work), position++)
		{
		if (work == child) return position;
		}

	return -1;
}

void gq_gtk_box_reorder_child(GtkBox *box, GtkWidget *child, gint position)
{
	if (!GTK_IS_BOX(box) || !child) return;
	if (gq_gtk_box_get_child_position(box, child) < 0) return;

	GtkWidget *previous = nullptr;
	if (position >= 0)
		{
		gint index = 0;

		for (GtkWidget *work = gtk_widget_get_first_child(GTK_WIDGET(box));
		     work != nullptr;
		     work = gtk_widget_get_next_sibling(work))
			{
			if (work == child) continue;
			if (index >= position) break;

			previous = work;
			index++;
			}
		}
	else
		{
		for (GtkWidget *work = gtk_widget_get_first_child(GTK_WIDGET(box));
		     work != nullptr;
		     work = gtk_widget_get_next_sibling(work))
			{
			if (work != child) previous = work;
			}
		}

	gtk_box_reorder_child_after(box, child, previous);
}

void gq_gtk_container_remove(GtkWidget *container, GtkWidget *widget)
{
	if (!GTK_IS_WIDGET(container) || !GTK_IS_WIDGET(widget)) return;
	if (gtk_widget_get_parent(widget) != container) return;

	if (GTK_IS_BOX(container))
		{
		gtk_box_remove(GTK_BOX(container), widget);
		}
	else if (GTK_IS_BUTTON(container))
		{
		gtk_button_set_child(GTK_BUTTON(container), nullptr);
		}
	else if (GTK_IS_EXPANDER(container))
		{
		gtk_expander_set_child(GTK_EXPANDER(container), nullptr);
		}
	else if (GTK_IS_FRAME(container))
		{
		gtk_frame_set_child(GTK_FRAME(container), nullptr);
		}
	else if (GTK_IS_PANED(container))
		{
		if (gtk_paned_get_start_child(GTK_PANED(container)) == widget)
			{
			gtk_paned_set_start_child(GTK_PANED(container), nullptr);
			}
		else if (gtk_paned_get_end_child(GTK_PANED(container)) == widget)
			{
			gtk_paned_set_end_child(GTK_PANED(container), nullptr);
			}
		else
			{
			g_abort();
			}
		}
	else if (GTK_IS_POPOVER(container))
		{
		gtk_popover_set_child(GTK_POPOVER(container), nullptr);
		}
	else if (GTK_IS_SCROLLED_WINDOW(container))
		{
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(container), nullptr);
		}
	else if (GTK_IS_VIEWPORT(container))
		{
		gtk_viewport_set_child(GTK_VIEWPORT(container), nullptr);
		}
	else if (GTK_IS_WINDOW(container))
		{
		gtk_window_set_child(GTK_WINDOW(container), nullptr);
		}
	else
		{
		g_abort();
		}
}

void gq_gtk_widget_destroy(GtkWidget *widget)
{
	if (!GTK_IS_WIDGET(widget)) return;

	if (GTK_IS_WINDOW(widget))
		{
		gtk_window_destroy(GTK_WINDOW(widget));
		return;
		}

	GtkWidget *parent = gtk_widget_get_parent(widget);
	if (parent)
		{
		gq_gtk_container_remove(parent, widget);
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
