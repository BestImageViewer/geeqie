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

#ifndef PAN_VIEW_PAN_VIEW_FILTER_H
#define PAN_VIEW_PAN_VIEW_FILTER_H

#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

struct PanWindow;

struct PanViewFilterUi
{
	GtkWidget *filter_box;
	GtkWidget *filter_entry;
	GtkWidget *filter_label;
	GtkWidget *filter_button;
	GtkWidget *filter_button_arrow;
	GtkWidget *filter_kw_hbox;
	GtkWidget *filter_check_buttons[FILE_FORMAT_CLASSES];
	GtkWidget *filter_mode_combo;
	GList *filter_elements;  /**< List of #PanViewFilterElement. */
	gint filter_classes;
};

/**
 * @headerfile pan_filter_ui_new
 * Creates a new #PanViewFilterUi instance and returns it.
 */
PanViewFilterUi *pan_filter_ui_new(PanWindow *pw);

/**
 * @headerfile pan_filter_ui_destroy
 * Destroys the specified #PanViewFilterUi.
 */
void pan_filter_ui_destroy(PanViewFilterUi *ui);

gboolean pan_filter_fd_list(GList **fd_list, GList *filter_elements, gint filter_classes);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
