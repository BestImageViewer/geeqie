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

#ifndef UI_MISC_H
#define UI_MISC_H

#include <ctime>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

/* these values are per GNOME HIG */

/* HIG 2.0 chapter 8 defines: */

/**
 * @def PREF_PAD_GAP
 * space between elements within control (ex: icon and it's text)
 */
#define PREF_PAD_GAP     6

/**
 * @def PREF_PAD_SPACE
 * space between label and control(s)
 */
#define PREF_PAD_SPACE  12

/**
 * @def PREF_PAD_BORDER
 * space between window border and controls
 */
#define PREF_PAD_BORDER 12

/**
 * @def PREF_PAD_INDENT
 * indent for group members
 */
#define PREF_PAD_INDENT 12

/**
 * @def PREF_PAD_GROUP
 * vertical space between groups
 */
#define PREF_PAD_GROUP  18

/* HIG 2.0 chapter 3.13 defines: */

/**
 * @def PREF_PAD_BUTTON_GAP
 * gap between buttons in a dialog
 */
#define PREF_PAD_BUTTON_GAP 6

/**
 * @def PREF_PAD_BUTTON_SPACE
 * space between buttons in a dialog and it's contents
 */
#define PREF_PAD_BUTTON_SPACE 24

/* and these are not in the GNOME HIG */

/**
 * @def PREF_PAD_TOOLBAR_GAP
 * gap between similar toolbar items (buttons)
 */
#define PREF_PAD_TOOLBAR_GAP 0

/**
 * @def PREF_PAD_BUTTON_ICON_GAP
 * HIG 2.0 states 6 pixels between icons and text,
 * but GTK's stock buttons ignore this (hard coded to 2), we do it too for consistency
 */
#define PREF_PAD_BUTTON_ICON_GAP 2


GtkWidget *pref_box_new(GtkWidget *parent_box, gboolean fill,
			GtkOrientation orientation, gboolean padding);

GtkWidget *pref_group_new(GtkWidget *parent_box, gboolean fill,
			  const gchar *text, GtkOrientation orientation);
GtkWidget *pref_group_parent(GtkWidget *child);

GtkWidget *pref_frame_new(GtkWidget *parent_box, gboolean fill,
			  const gchar *text,
			  GtkOrientation orientation, gboolean padding);

GtkWidget *pref_spacer(GtkWidget *parent_box, gboolean padding);
GtkWidget *pref_line(GtkWidget *parent_box, gboolean padding);

GtkWidget *pref_label_new(GtkWidget *parent_box, const gchar *text);
GtkWidget *pref_label_new_mnemonic(GtkWidget *parent_box, const gchar *text, GtkWidget *widget);
void pref_label_bold(GtkWidget *label, gboolean bold, gboolean increase_size);

GtkWidget *pref_button_new(GtkWidget *parent_box, const gchar *icon_name,
			   const gchar *text, GCallback func, gpointer data);

GtkWidget *pref_checkbox_new(GtkWidget *parent_box, const gchar *text, gboolean active,
			     GCallback func, gpointer data);

GtkWidget *pref_checkbox_new_int(GtkWidget *parent_box, const gchar *text, gboolean active,
				 gboolean *result);

void pref_checkbox_link_sensitivity(GtkWidget *button, GtkWidget *widget);

GtkWidget *pref_radiobutton_new(GtkWidget *parent_box, GtkWidget *sibling,
				const gchar *text, gboolean active,
				GCallback func, gpointer data);

GtkWidget *pref_spin_new(GtkWidget *parent_box, const gchar *text, const gchar *suffix,
			 gdouble min, gdouble max, gdouble step, gint digits,
			 gdouble value,
			 GCallback func, gpointer data);

GtkWidget *pref_spin_new_int(GtkWidget *parent_box, const gchar *text, const gchar *suffix,
			     gint min, gint max, gint step,
			     gint value, gint *value_var);

void pref_signal_block_data(GtkWidget *widget, gpointer data);
void pref_signal_unblock_data(GtkWidget *widget, gpointer data);


GtkWidget *pref_table_new(GtkWidget *parent_box, gint, gint, gboolean, gboolean);

GtkWidget *pref_table_box(GtkWidget *table, gint column, gint row,
			  GtkOrientation orientation, const gchar *text);

GtkWidget *pref_table_label(GtkWidget *table, gint column, gint row,
			    const gchar *text, GtkAlign alignment);

GtkWidget *pref_table_button(GtkWidget *table, gint column, gint row,
			     const gchar *stock_id, const gchar *text,
			     GCallback func, gpointer data);

GtkWidget *pref_table_spin(GtkWidget *table, gint column, gint row,
			   const gchar *text, const gchar *suffix,
			   gdouble min, gdouble max, gdouble step, gint digits,
			   gdouble value,
			   GCallback func, gpointer data);

GtkWidget *pref_table_spin_new_int(GtkWidget *table, gint column, gint row,
				   const gchar *text, const gchar *suffix,
				   gint min, gint max, gint step,
				   gint value, gint *value_var);


GtkWidget *pref_toolbar_new(GtkWidget *parent_box);
GtkWidget *pref_toolbar_button(GtkWidget *toolbar,
			       const gchar *icon_name, const gchar *label, gboolean toggle,
			       const gchar *description,
			       GCallback func, gpointer data);


GtkWidget *date_selection_new();

void date_selection_set(GtkWidget *widget, gint day, gint month, gint year);
GDateTime *date_selection_get(GtkWidget *widget);

void date_selection_time_set(GtkWidget *widget, time_t t);


void pref_list_int_set(const gchar *group, const gchar *key, gint value);
gint pref_list_int_get(const gchar *group, const gchar *key, gint fallback);


GtkWidget *pref_color_button_new(GtkWidget *parent_box, const gchar *title,
                                 const GdkRGBA *color, GdkRGBA *result);

gchar *text_widget_text_pull(GtkWidget *text_widget, gboolean include_hidden_chars = FALSE);
gchar *text_widget_text_pull_selected(GtkWidget *text_widget);

struct ActionItem
{
	ActionItem(const gchar *name, const gchar *label, const gchar *icon_name);
	ActionItem(const ActionItem &other);
	ActionItem(ActionItem &&other) noexcept;
	~ActionItem();
	ActionItem &operator=(const ActionItem &other);
	ActionItem &operator=(ActionItem &&other) noexcept;

	bool has_label(const gchar *label) const;

	gchar *name = nullptr; /* GtkActionEntry terminology */
	gchar *label = nullptr;
	gchar *icon_name = nullptr;
};

std::vector<ActionItem> get_action_items();

// Copy pixbuf returned by gtk_icon_theme_load_icon() to avoid GTK+ keeping the old icon theme loaded
GdkPixbuf *gq_gtk_icon_theme_load_icon_copy(GtkIconTheme *icon_theme, const gchar *icon_name, gint size, GtkIconLookupFlags flags);

gboolean window_get_pointer_position(GdkWindow *window, GdkPoint &pos);
GdkRectangle window_get_position_geometry(GdkWindow *window);
GdkRectangle window_get_root_origin_geometry(GdkWindow *window);
gboolean window_received_event(GdkWindow *window, GdkPoint event);

void widget_remove_from_parent(GtkWidget *widget);
void widget_remove_from_parent_cb(GtkWidget *, gpointer data);

void widget_input_grab(GtkWidget *widget, GdkSeatCapabilities capabilities, gboolean owner_events, GdkEventMask event_mask);
void widget_input_ungrab(GtkWidget *widget);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
