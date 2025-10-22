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

#ifndef UI_UTILDLG_H
#define UI_UTILDLG_H

#include <glib.h>
#include <gtk/gtk.h>

struct GenericDialog
{
	GtkWidget *dialog;	/**< window */
	GtkWidget *vbox;	/**< place to add widgets */

	GtkWidget *hbox;	/**< button hbox */

	gboolean auto_close;

	void (*default_cb)(GenericDialog *, gpointer);
	void (*cancel_cb)(GenericDialog *, gpointer);
	gpointer data;

	GtkWidget *cancel_button; /**< private */

};

#define GENERIC_DIALOG(gd) ((GenericDialog *)gd)

GenericDialog *generic_dialog_new(const gchar *title,
				  const gchar *role,
				  GtkWidget *parent, gboolean auto_close,
				  void (*cancel_cb)(GenericDialog *, gpointer), gpointer data);
void generic_dialog_close(GenericDialog *gd);

void generic_dialog_dummy_cb(GenericDialog *gd, gpointer data);

GtkWidget *generic_dialog_add_button(GenericDialog *gd, const gchar *icon_name, const gchar *text,
				     void (*func_cb)(GenericDialog *, gpointer), gboolean is_default);
void generic_dialog_attach_default(GenericDialog *gd, GtkWidget *widget);

GtkWidget *generic_dialog_add_message(GenericDialog *gd, const gchar *icon_name,
				      const gchar *heading, const gchar *text, gboolean expand);

gboolean generic_dialog_get_alternative_button_order(GtkWidget *widget);

GenericDialog *warning_dialog(const gchar *heading, const gchar *text,
			      const gchar *icon_name, GtkWidget *parent);

gboolean generic_dialog_find_window(const gchar *title, const gchar *role, GdkRectangle &rect);
void generic_dialog_windows_load_config(const gchar **attribute_names, const gchar **attribute_values);
void generic_dialog_windows_write_config(GString *outstr, gint indent);

void new_appimage_notification(GtkApplication *app);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
