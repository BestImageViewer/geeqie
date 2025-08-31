/*
 * Copyright (C) 2006 John Ellis
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

#ifndef PAN_VIEW_PAN_VIEW_SEARCH_H
#define PAN_VIEW_PAN_VIEW_SEARCH_H

#include <glib.h>
#include <gtk/gtk.h>

struct PanWindow;

struct PanViewSearchUi
{
	GtkWidget *search_box;
	GtkWidget *search_entry;
	GtkWidget *search_label;
	GtkWidget *search_button;
	GtkWidget *search_button_arrow;
};

void pan_search_toggle_visible(PanWindow *pw, gboolean enable);
void pan_search_activate(PanWindow *pw);

/**
 * @headerfile pan_search_ui_new
 * Creates a new #PanViewSearchUi instance and returns it.
 */
PanViewSearchUi *pan_search_ui_new(PanWindow *pw);

/**
 * @headerfile pan_search_ui_destroy
 * Destroys the specified #PanViewSearchUi.
 */
void pan_search_ui_destroy(PanViewSearchUi *ui);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
