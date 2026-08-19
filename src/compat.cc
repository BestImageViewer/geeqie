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

namespace
{

void widget_remove_from_parent(GtkWidget *widget)
{
	if (!GTK_IS_WIDGET(widget)) return;

	GtkWidget *parent = gtk_widget_get_parent(widget);
	if (!GTK_IS_WIDGET(parent)) return;

	if (GTK_IS_BOX(parent))
		{
		gtk_box_remove(GTK_BOX(parent), widget);
		}
	else if (GTK_IS_BUTTON(parent))
		{
		gtk_button_set_child(GTK_BUTTON(parent), nullptr);
		}
	else if (GTK_IS_EXPANDER(parent))
		{
		gtk_expander_set_child(GTK_EXPANDER(parent), nullptr);
		}
	else if (GTK_IS_FRAME(parent))
		{
		gtk_frame_set_child(GTK_FRAME(parent), nullptr);
		}
	else if (GTK_IS_PANED(parent))
		{
		if (gtk_paned_get_start_child(GTK_PANED(parent)) == widget)
			{
			gtk_paned_set_start_child(GTK_PANED(parent), nullptr);
			}
		else if (gtk_paned_get_end_child(GTK_PANED(parent)) == widget)
			{
			gtk_paned_set_end_child(GTK_PANED(parent), nullptr);
			}
		else
			{
			g_abort();
			}
		}
	else if (GTK_IS_POPOVER(parent))
		{
		gtk_popover_set_child(GTK_POPOVER(parent), nullptr);
		}
	else if (GTK_IS_SCROLLED_WINDOW(parent))
		{
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(parent), nullptr);
		}
	else if (GTK_IS_VIEWPORT(parent))
		{
		gtk_viewport_set_child(GTK_VIEWPORT(parent), nullptr);
		}
	else if (GTK_IS_WINDOW(parent))
		{
		gtk_window_set_child(GTK_WINDOW(parent), nullptr);
		}
	else
		{
		g_abort();
		}
}

} // namespace

void gq_gtk_widget_destroy(GtkWidget *widget)
{
	if (!GTK_IS_WIDGET(widget)) return;

	if (GTK_IS_WINDOW(widget))
		{
		gtk_window_destroy(GTK_WINDOW(widget));
		return;
		}

	widget_remove_from_parent(widget);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
