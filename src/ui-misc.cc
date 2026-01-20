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

#include "ui-misc.h"

#include <langinfo.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <pango/pango.h>

#include <config.h>

#include "compat-deprecated.h"
#include "compat.h"
#include "geometry.h"
#include "history-list.h"
#include "layout.h"
#include "main-defines.h"
#include "misc.h"

namespace
{

void pref_link_sensitivity_cb(GtkWidget *watch, GtkStateFlags, gpointer data)
{
	auto *widget = static_cast<GtkWidget *>(data);

	gtk_widget_set_sensitive(widget, gtk_widget_is_sensitive(watch));
}

inline void pref_link_sensitivity(GtkWidget *widget, GtkWidget *watch)
{
	g_signal_connect(G_OBJECT(watch), "state-flags-changed",
	                 G_CALLBACK(pref_link_sensitivity_cb), widget);
}

} // namespace

/*
 *-----------------------------------------------------------------------------
 * widget and layout utilities
 *-----------------------------------------------------------------------------
 */

GtkWidget *pref_box_new(GtkWidget *parent_box, gboolean fill,
			GtkOrientation orientation, gboolean padding)
{
	GtkWidget *box = gtk_box_new(orientation, padding);

	gq_gtk_box_pack_start(GTK_BOX(parent_box), box, fill, fill, 0);
	gtk_widget_show(box);

	return box;
}

GtkWidget *pref_group_new(GtkWidget *parent_box, gboolean fill,
			  const gchar *text, GtkOrientation orientation)
{
	GtkWidget *box;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *label;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	/* add additional spacing if necessary */
	if (GTK_IS_ORIENTABLE(parent_box) &&
	    gtk_orientable_get_orientation(GTK_ORIENTABLE(parent_box)) == GTK_ORIENTATION_VERTICAL)
		{
		g_autoptr(GList) list = gq_gtk_widget_get_children(GTK_WIDGET(parent_box));
		if (list)
			{
			pref_spacer(vbox, PREF_PAD_GROUP - PREF_PAD_GAP);
			}
		}

	gq_gtk_box_pack_start(GTK_BOX(parent_box), vbox, fill, fill, 0);
	gtk_widget_show(vbox);

	label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);
	pref_label_bold(label, TRUE, FALSE);

	gq_gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_INDENT);
	gq_gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	/* indent using empty box */
	pref_spacer(hbox, 0);

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		{
		box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
		}
	else
		{
		box = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
		}
	gq_gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);
	gtk_widget_show(box);

	g_object_set_data(G_OBJECT(box), "pref_group", vbox);

	return box;
}

GtkWidget *pref_group_parent(GtkWidget *child)
{
	GtkWidget *parent;

	parent = child;
	while (parent)
		{
		GtkWidget *group;

		group = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(parent), "pref_group"));
		if (group && GTK_IS_WIDGET(group)) return group;

		parent = gtk_widget_get_parent(parent);
		}

	return child;
}

GtkWidget *pref_frame_new(GtkWidget *parent_box, gboolean fill,
			  const gchar *text,
			  GtkOrientation orientation, gboolean padding)
{
	GtkWidget *box;
	GtkWidget *frame = nullptr;

	frame = gtk_frame_new(text);
	gq_gtk_box_pack_start(GTK_BOX(parent_box), frame, fill, fill, 0);
	gtk_widget_show(frame);

	box = gtk_box_new(orientation, padding);
	gq_gtk_container_add(frame, box);
	gtk_container_set_border_width(GTK_CONTAINER(box), PREF_PAD_BORDER);
	gtk_widget_show(box);

	return box;
}

GtkWidget *pref_spacer(GtkWidget *parent_box, gboolean padding)
{
	GtkWidget *spacer;

	spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(parent_box), spacer, FALSE, FALSE, padding / 2);
	gtk_widget_show(spacer);

	return spacer;
}

GtkWidget *pref_line(GtkWidget *parent_box, gboolean padding)
{
	GtkOrientation orientation;
	GtkWidget *spacer;

	orientation = gtk_orientable_get_orientation(GTK_ORIENTABLE(parent_box));
	spacer = gtk_separator_new((orientation == GTK_ORIENTATION_HORIZONTAL) ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
	gq_gtk_box_pack_start(GTK_BOX(parent_box), spacer, FALSE, FALSE, padding / 2);
	gtk_widget_show(spacer);

	return spacer;
}

GtkWidget *pref_label_new(GtkWidget *parent_box, const gchar *text)
{
	GtkWidget *label;

	label = gtk_label_new(text);
	gq_gtk_box_pack_start(GTK_BOX(parent_box), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	return label;
}

GtkWidget *pref_label_new_mnemonic(GtkWidget *parent_box, const gchar *text, GtkWidget *widget)
{
	GtkWidget *label;

	label = gtk_label_new_with_mnemonic(text);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
	gq_gtk_box_pack_start(GTK_BOX(parent_box), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	return label;
}

void pref_label_bold(GtkWidget *label, gboolean bold, gboolean increase_size)
{
	PangoAttrList *pal;
	PangoAttribute *pa;

	if (!bold && !increase_size) return;

	pal = pango_attr_list_new();

	if (bold)
		{
		pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
		pa->start_index = 0;
		pa->end_index = G_MAXINT;
		pango_attr_list_insert(pal, pa);
		}

	if (increase_size)
		{
		pa = pango_attr_scale_new(PANGO_SCALE_LARGE);
		pa->start_index = 0;
		pa->end_index = G_MAXINT;
		pango_attr_list_insert(pal, pa);
		}

	gtk_label_set_attributes(GTK_LABEL(label), pal);
	pango_attr_list_unref(pal);
}

GtkWidget *pref_button_new(GtkWidget *parent_box, const gchar *icon_name,
			   const gchar *text, GCallback func, gpointer data)
{
	GtkWidget *button;

	if (icon_name)
		{
		button = gtk_button_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
		}
	else
		{
		button = gtk_button_new();
		}

	if (text)
		{
		gtk_button_set_use_underline(GTK_BUTTON(button), TRUE);
		gtk_button_set_label(GTK_BUTTON(button), text);
		}

	if (func) g_signal_connect(G_OBJECT(button), "clicked", func, data);

	if (parent_box)
		{
		gq_gtk_box_pack_start(GTK_BOX(parent_box), button, FALSE, FALSE, 0);
		gtk_widget_show(button);
		}

	return button;
}

static GtkWidget *real_pref_checkbox_new(GtkWidget *parent_box, const gchar *text, gboolean mnemonic_text,
					 gboolean active, GCallback func, gpointer data)
{
	GtkWidget *button;

	if (mnemonic_text)
		{
		button = gtk_check_button_new_with_mnemonic(text);
		}
	else
		{
		button = gtk_check_button_new_with_label(text);
		}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), active);
	if (func) g_signal_connect(G_OBJECT(button), "clicked", func, data);

	gq_gtk_box_pack_start(GTK_BOX(parent_box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	return button;
}

GtkWidget *pref_checkbox_new(GtkWidget *parent_box, const gchar *text, gboolean active,
			     GCallback func, gpointer data)
{
	return real_pref_checkbox_new(parent_box, text, FALSE, active, func, data);
}

static void pref_checkbox_int_cb(GtkWidget *widget, gpointer data)
{
	auto result = static_cast<gboolean *>(data);

	*result = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

GtkWidget *pref_checkbox_new_int(GtkWidget *parent_box, const gchar *text, gboolean active,
				 gboolean *result)
{
	GtkWidget *button;

	button = pref_checkbox_new(parent_box, text, active,
				   G_CALLBACK(pref_checkbox_int_cb), result);
	*result = active;

	return button;
}

static void pref_checkbox_link_sensitivity_cb(GtkWidget *button, gpointer data)
{
	auto widget = static_cast<GtkWidget *>(data);

	gtk_widget_set_sensitive(widget, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

void pref_checkbox_link_sensitivity(GtkWidget *button, GtkWidget *widget)
{
	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(pref_checkbox_link_sensitivity_cb), widget);

	pref_checkbox_link_sensitivity_cb(button, widget);
}

static GtkWidget *real_pref_radiobutton_new(GtkWidget *parent_box, GtkWidget *sibling,
					    const gchar *text, gboolean mnemonic_text, gboolean active,
					    GCallback func, gpointer data)
{
	GtkWidget *button;
#if HAVE_GTK4
	GtkToggleButton *group;
#else
	GSList *group;
#endif

	if (sibling)
		{
#if HAVE_GTK4
		group = sibling;
#else
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(sibling));
#endif
		}
	else
		{
		group = nullptr;
		}

	if (mnemonic_text)
		{
#if HAVE_GTK4
		button = gtk_toggle_button_new_with_mnemonic(text);
		gtk_toggle_button_set_group(button, group);
#else
		button = gtk_radio_button_new_with_mnemonic(group, text);
#endif
		}
	else
		{
#if HAVE_GTK4
		button = gtk_toggle_button_new_with_label(text);
		gtk_toggle_button_set_group(button, group);
#else
		button = gtk_radio_button_new_with_label(group, text);
#endif
		}

	if (active) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), active);
	if (func) g_signal_connect(G_OBJECT(button), "clicked", func, data);

	gq_gtk_box_pack_start(GTK_BOX(parent_box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	return button;
}

GtkWidget *pref_radiobutton_new(GtkWidget *parent_box, GtkWidget *sibling,
				const gchar *text, gboolean active,
				GCallback func, gpointer data)
{
	return real_pref_radiobutton_new(parent_box, sibling, text, FALSE, active, func, data);
}

static GtkWidget *real_pref_spin_new(GtkWidget *parent_box, const gchar *text, const gchar *suffix,
				     gboolean mnemonic_text,
				     gdouble min, gdouble max, gdouble step, gint digits,
				     gdouble value,
				     GCallback func, gpointer data)
{
	GtkWidget *spin;
	GtkWidget *box;
	GtkWidget *label;

	box = pref_box_new(parent_box, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	spin = gtk_spin_button_new_with_range(min, max, step);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), digits);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);

	if (func)
		{
		g_signal_connect(G_OBJECT(spin), "value_changed", G_CALLBACK(func), data);
		}

	if (text)
		{
		if (mnemonic_text)
			{
			label = pref_label_new_mnemonic(box, text, spin);
			}
		else
			{
			label = pref_label_new(box, text);
			}
		pref_link_sensitivity(label, spin);
		}

	gq_gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
	gtk_widget_show(spin);

	/* perhaps this should only be PREF_PAD_GAP distance from spinbutton ? */
	if (suffix)
		{
		label =  pref_label_new(box, suffix);
		pref_link_sensitivity(label, spin);
		}

	return spin;
}

GtkWidget *pref_spin_new(GtkWidget *parent_box, const gchar *text, const gchar *suffix,
			 gdouble min, gdouble max, gdouble step, gint digits,
			 gdouble value,
			 GCallback func, gpointer data)
{
	return real_pref_spin_new(parent_box, text, suffix, FALSE,
				  min, max, step, digits, value, func, data);
}

static void pref_spin_int_cb(GtkWidget *widget, gpointer data)
{
	auto var = static_cast<gint *>(data);
	*var = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
}

GtkWidget *pref_spin_new_int(GtkWidget *parent_box, const gchar *text, const gchar *suffix,
			     gint min, gint max, gint step,
			     gint value, gint *value_var)
{
	*value_var = value;
	return pref_spin_new(parent_box, text, suffix,
			     static_cast<gdouble>(min), static_cast<gdouble>(max), static_cast<gdouble>(step), 0,
			     value,
			     G_CALLBACK(pref_spin_int_cb), value_var);
}

void pref_signal_block_data(GtkWidget *widget, gpointer data)
{
	g_signal_handlers_block_matched(widget, G_SIGNAL_MATCH_DATA,
					0, 0, nullptr, nullptr, data);
}

void pref_signal_unblock_data(GtkWidget *widget, gpointer data)
{
	g_signal_handlers_unblock_matched(widget, G_SIGNAL_MATCH_DATA,
					  0, 0, nullptr, nullptr, data);
}

GtkWidget *pref_table_new(GtkWidget *parent_box, gint, gint, gboolean, gboolean fill)
{
	GtkWidget *table;

	table = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(table), PREF_PAD_GAP);
	gtk_grid_set_column_spacing(GTK_GRID(table), PREF_PAD_SPACE);

	if (parent_box)
		{
		gq_gtk_box_pack_start(GTK_BOX(parent_box), table, fill, fill, 0);
		gtk_widget_show(table);
		}

	return table;
}

GtkWidget *pref_table_box(GtkWidget *table, gint column, gint row,
			  GtkOrientation orientation, const gchar *text)
{
	GtkWidget *box;
	GtkWidget *shell;

	if (text)
		{
		shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		box = pref_group_new(shell, TRUE, text, orientation);
		}
	else
		{
		if (orientation == GTK_ORIENTATION_HORIZONTAL)
			{
			box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
			}
		else
			{
			box = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
			}
		shell = box;
		}

	gq_gtk_grid_attach(GTK_GRID(table), shell, column, column + 1, row, row + 1, static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), static_cast<GtkAttachOptions>(0), 0, 0);

	gtk_widget_show(shell);

	return box;
}

GtkWidget *pref_table_label(GtkWidget *table, gint column, gint row,
			    const gchar *text, GtkAlign alignment)
{
	GtkWidget *label;

	label = gtk_label_new(text);
	gtk_widget_set_halign(label, alignment);
	gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
	gq_gtk_grid_attach(GTK_GRID(table), label, column, column + 1, row, row + 1,  GTK_FILL, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(label);

	return label;
}

GtkWidget *pref_table_button(GtkWidget *table, gint column, gint row,
			     const gchar *stock_id, const gchar *text,
			     GCallback func, gpointer data)
{
	GtkWidget *button;

	button = pref_button_new(nullptr, stock_id, text, func, data);
	gq_gtk_grid_attach(GTK_GRID(table), button, column, column + 1, row, row + 1,  GTK_FILL, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(button);

	return button;
}

GtkWidget *pref_table_spin(GtkWidget *table, gint column, gint row,
			   const gchar *text, const gchar *suffix,
			   gdouble min, gdouble max, gdouble step, gint digits,
			   gdouble value,
			   GCallback func, gpointer data)
{
	GtkWidget *spin;
	GtkWidget *box;
	GtkWidget *label;

	spin = gtk_spin_button_new_with_range(min, max, step);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), digits);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
	if (func)
		{
		g_signal_connect(G_OBJECT(spin), "value_changed", G_CALLBACK(func), data);
		}

	if (text)
		{
		label = pref_table_label(table, column, row, text, GTK_ALIGN_END);
		pref_link_sensitivity(label, spin);
		column++;
		}

	if (suffix)
		{
		box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
		gq_gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
		gtk_widget_show(spin);

		label = pref_label_new(box, suffix);
		pref_link_sensitivity(label, spin);
		}
	else
		{
		box = spin;
		}

	gq_gtk_grid_attach(GTK_GRID(table), box, column, column + 1, row, row + 1, static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), 0, 0);
	gtk_widget_show(box);

	return spin;
}

GtkWidget *pref_table_spin_new_int(GtkWidget *table, gint column, gint row,
				   const gchar *text, const gchar *suffix,
				   gint min, gint max, gint step,
				   gint value, gint *value_var)
{
	*value_var = value;
	return pref_table_spin(table, column, row,
			       text, suffix,
			       static_cast<gdouble>(min), static_cast<gdouble>(max), static_cast<gdouble>(step), 0,
			       value,
			       G_CALLBACK(pref_spin_int_cb), value_var);
}


GtkWidget *pref_toolbar_new(GtkWidget *parent_box)
{
	GtkWidget *tbar;

	tbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	if (parent_box)
		{
		gq_gtk_box_pack_start(GTK_BOX(parent_box), tbar, FALSE, FALSE, 0);
		gtk_widget_show(tbar);
		}
	return tbar;
}

GtkWidget *pref_toolbar_button(GtkWidget *toolbar,
			       const gchar *icon_name, const gchar *label, gboolean toggle,
			       const gchar *description,
			       GCallback func, gpointer data)
{
	GtkWidget *item;

	if (toggle) // TODO: TG seems no function uses toggle now
		{
		item = GTK_WIDGET(gtk_toggle_tool_button_new());
		if (icon_name) gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(item), icon_name);
		if (label) gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), label);
		}
	else
		{
		GtkWidget *icon = nullptr;
		if (icon_name)
			{
			icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR); // TODO: TG which size?
			gtk_widget_show(icon);
			}
		item = GTK_WIDGET(gtk_tool_button_new(icon, label));
		}
	gtk_tool_button_set_use_underline(GTK_TOOL_BUTTON(item), TRUE);

	if (func) g_signal_connect(item, "clicked", func, data);
	gq_gtk_container_add(toolbar, item);
	gtk_widget_show(item);

	if (description)
		{
		gtk_widget_set_tooltip_text(item, description);
		}

	return item;
}


/*
 *-----------------------------------------------------------------------------
 * date selection entry
 *-----------------------------------------------------------------------------
 */

#define DATE_SELECION_KEY "date_selection_data"


struct DateSelection
{
	GtkWidget *box;

	GtkWidget *spin_d;
	GtkWidget *spin_m;
	GtkWidget *spin_y;

	GtkWidget *button;

	GtkWidget *window;
	GtkWidget *calendar;
};


static void date_selection_popup_hide(DateSelection *ds)
{
	if (!ds->window) return;

	if (gtk_widget_has_grab(ds->window))
		{
		widget_input_ungrab(ds->window);
		}

	gtk_widget_hide(ds->window);

	gq_gtk_widget_destroy(ds->window);
	ds->window = nullptr;
	ds->calendar = nullptr;

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ds->button), FALSE);
}

static gboolean date_selection_popup_release_cb(GtkWidget *, GdkEventButton *, gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	date_selection_popup_hide(ds);
	return TRUE;
}

static gboolean date_selection_popup_press_cb(GtkWidget *, GdkEventButton *event, gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	auto xr = static_cast<gint>(event->x_root);
	auto yr = static_cast<gint>(event->y_root);

	if (!widget_received_event(ds->window, {xr, yr}))
		{
		g_signal_connect(G_OBJECT(ds->window), "button_release_event",
				 G_CALLBACK(date_selection_popup_release_cb), ds);
		return TRUE;
		}

	return FALSE;
}

static void date_selection_popup_sync(DateSelection *ds)
{
	guint day;
	guint month;
	guint year;

#if HAVE_GTK4
	GDateTime *date_selected;

	date_selected = gtk_calendar_get_date(GTK_CALENDAR(ds->calendar));
	g_date_time_get_ymd(date_selected, static_cast<guint>(&year), static_cast<guint>(&month), static_cast<guint>(&day));

	g_date_time_unref(date_selected);
#else
	gtk_calendar_get_date(GTK_CALENDAR(ds->calendar), &year, &month, &day);
	/* month is range 0 to 11 */
	month = month + 1;
#endif
	date_selection_set(ds->box, day, month, year);
}

static gboolean date_selection_popup_keypress_cb(GtkWidget *, GdkEventKey *event, gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	switch (event->keyval)
		{
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
		case GDK_KEY_Tab:
		case GDK_KEY_ISO_Left_Tab:
			date_selection_popup_sync(ds);
			date_selection_popup_hide(ds);
			break;
		case GDK_KEY_Escape:
			date_selection_popup_hide(ds);
			break;
		default:
			break;
		}

	return FALSE;
}

static void date_selection_popup(DateSelection *ds)
{
	GDateTime *date;
	gint wx;
	gint wy;
	gint x;
	gint y;
	GtkAllocation button_allocation;
	GtkAllocation window_allocation;

	if (ds->window) return;

	ds->window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_resizable(GTK_WINDOW(ds->window), FALSE);
	g_signal_connect(G_OBJECT(ds->window), "button_press_event",
			 G_CALLBACK(date_selection_popup_press_cb), ds);
	g_signal_connect(G_OBJECT(ds->window), "key_press_event",
			 G_CALLBACK(date_selection_popup_keypress_cb), ds);

	ds->calendar = gtk_calendar_new();
	gq_gtk_container_add(ds->window, ds->calendar);
	gtk_widget_show(ds->calendar);

	date = date_selection_get(ds->box);
#if HAVE_GTK4
	gtk_calendar_select_day(GTK_CALENDAR(ds->calendar), date);
#else
	gtk_calendar_select_month(GTK_CALENDAR(ds->calendar), g_date_time_get_month(date), g_date_time_get_year(date));
	gtk_calendar_select_day(GTK_CALENDAR(ds->calendar), g_date_time_get_day_of_month(date));
#endif
	g_date_time_unref(date);

	g_signal_connect_swapped(G_OBJECT(ds->calendar), "day-selected",
	                         G_CALLBACK(date_selection_popup_sync), ds);
	g_signal_connect_swapped(G_OBJECT(ds->calendar), "day-selected-double-click",
	                         G_CALLBACK(date_selection_popup_hide), ds);

	gtk_widget_realize(ds->window);

	gdk_window_get_origin(gtk_widget_get_window(ds->button), &wx, &wy);

	gtk_widget_get_allocation(ds->button, &button_allocation);
	gtk_widget_get_allocation(ds->window, &window_allocation);

	x = wx + button_allocation.x + button_allocation.width - window_allocation.width;
	y = wy + button_allocation.y + button_allocation.height;

	if (y + window_allocation.height > deprecated_gdk_screen_height())
		{
		y = wy + button_allocation.y - window_allocation.height;
		}
	x = std::max(x, 0);
	y = std::max(y, 0);

	gq_gtk_window_move(GTK_WINDOW(ds->window), x, y);
	gtk_widget_show(ds->window);

	gtk_widget_grab_focus(ds->calendar);
	widget_input_grab(ds->window, GDK_SEAT_CAPABILITY_ALL, TRUE,
	                  static_cast<GdkEventMask>(GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_MOTION_MASK));

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ds->button), TRUE);
}

static void date_selection_button_cb(GtkWidget *, gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ds->button)) == (!ds->window))
		{
		date_selection_popup(ds);
		}
}

static void button_size_allocate_cb(GtkWidget *button, GtkAllocation *allocation, gpointer data)
{
	auto spin = static_cast<GtkWidget *>(data);
	GtkRequisition spin_requisition;
	deprecated_gtk_widget_get_requisition(spin, &spin_requisition);

	if (allocation->height > spin_requisition.height)
		{
		GtkAllocation button_allocation;
		GtkAllocation spin_allocation;

		gtk_widget_get_allocation(button, &button_allocation);
		gtk_widget_get_allocation(spin, &spin_allocation);
		button_allocation.height = spin_requisition.height;
		button_allocation.y = spin_allocation.y +
			(spin_allocation.height - spin_requisition.height) / 2;
		gtk_widget_size_allocate(button, &button_allocation);
		}
}

static void date_selection_destroy_cb(GtkWidget *, gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	date_selection_popup_hide(ds);

	g_free(ds);
}

GtkWidget *date_selection_new()
{
	DateSelection *ds;
	GtkWidget *icon;

	ds = g_new0(DateSelection, 1);
	gchar *date_format;
	gint i;

	ds->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	g_signal_connect(G_OBJECT(ds->box), "destroy",
			 G_CALLBACK(date_selection_destroy_cb), ds);

	date_format = nl_langinfo(D_FMT);

	if (strlen(date_format) == 8)
		{
		for (i=1; i<8; i=i+3)
			{
			switch (date_format[i])
				{
				case 'd':
					ds->spin_d = pref_spin_new(ds->box, nullptr, nullptr, 1, 31, 1, 0, 1, nullptr, nullptr);
					break;
				case 'm':
					ds->spin_m = pref_spin_new(ds->box, nullptr, nullptr, 1, 12, 1, 0, 1, nullptr, nullptr);
					break;
				case 'y': case 'Y':
					ds->spin_y = pref_spin_new(ds->box, nullptr, nullptr, 1900, 9999, 1, 0, 1900, nullptr, nullptr);
					break;
				default:
					log_printf("Warning: Date locale %s is unknown", date_format);
					break;
				}
			}
		}
	else
		{
		ds->spin_m = pref_spin_new(ds->box, nullptr, nullptr, 1, 12, 1, 0, 1, nullptr, nullptr);
		ds->spin_d = pref_spin_new(ds->box, nullptr, nullptr, 1, 31, 1, 0, 1, nullptr, nullptr);
		ds->spin_y = pref_spin_new(ds->box, nullptr, nullptr, 1900, 9999, 1, 0, 1900, nullptr, nullptr);
		}

	ds->button = gtk_toggle_button_new();
	g_signal_connect(G_OBJECT(ds->button), "size_allocate",
			 G_CALLBACK(button_size_allocate_cb), ds->spin_y);

	icon = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);
	gq_gtk_container_add(ds->button, icon);
	gtk_widget_show(icon);

	gq_gtk_box_pack_start(GTK_BOX(ds->box), ds->button, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(ds->button), "clicked",
			 G_CALLBACK(date_selection_button_cb), ds);
	gtk_widget_show(ds->button);

	g_object_set_data(G_OBJECT(ds->box), DATE_SELECION_KEY, ds);

	return ds->box;
}

void date_selection_set(GtkWidget *widget, gint day, gint month, gint year)
{
	DateSelection *ds;

	ds = static_cast<DateSelection *>(g_object_get_data(G_OBJECT(widget), DATE_SELECION_KEY));
	if (!ds) return;

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(ds->spin_d), static_cast<gdouble>(day));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(ds->spin_m), static_cast<gdouble>(month));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(ds->spin_y), static_cast<gdouble>(year));
}

/**
 * @brief Returns date structure set to value of spin buttons
 * @param widget #DateSelection
 * @returns
 *
 * Free returned structure with g_date_time_unref();
 */
GDateTime *date_selection_get(GtkWidget *widget)
{
	DateSelection *ds;
	gint day;
	gint month;
	gint year;
	GDateTime *date;

	ds = static_cast<DateSelection *>(g_object_get_data(G_OBJECT(widget), DATE_SELECION_KEY));
	if (!ds)
		{
		return nullptr;
		}

	day = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ds->spin_d));
	month = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ds->spin_m));
	year = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ds->spin_y));

	date = g_date_time_new_local(year, month, day, 0, 0, 0);

	return date;
}

void date_selection_time_set(GtkWidget *widget, time_t t)
{
	struct tm *lt;

	lt = localtime(&t);
	if (!lt) return;

	date_selection_set(widget, lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900);
}

/*
 *-----------------------------------------------------------------------------
 * storing data in a history list with key,data pairs
 *-----------------------------------------------------------------------------
 */

#define PREF_LIST_MARKER_INT "[INT]:"

static GList *pref_list_find(const gchar *group, const gchar *token)
{
	GList *work;
	gint l;

	l = strlen(token);

	work = history_list_get_by_key(group);
	while (work)
		{
		auto text = static_cast<const gchar *>(work->data);

		if (strncmp(text, token, l) == 0) return work;

		work = work->next;
		}

	return nullptr;
}

static gboolean pref_list_get(const gchar *group, const gchar *key, const gchar *marker, const gchar **result)
{
	GList *work;

	if (!group || !key || !marker)
		{
		*result = nullptr;
		return FALSE;
		}

	g_autofree gchar *token = g_strconcat(key, marker, NULL);

	work = pref_list_find(group, token);
	if (!work)
		{
		*result = nullptr;
		return FALSE;
		}

	*result = static_cast<const gchar *>(work->data) + strlen(token);
	if (*result[0] == '\0') *result = nullptr;
	return TRUE;
}

static void pref_list_set(const gchar *group, const gchar *key, const gchar *marker, const gchar *text)
{
	GList *work;

	if (!group || !key || !marker) return;

	g_autofree gchar *token = g_strconcat(key, marker, NULL);
	g_autofree gchar *path = g_strconcat(token, text, NULL);

	work = pref_list_find(group, token);
	if (work)
		{
		auto old_path = static_cast<gchar *>(work->data);

		if (text)
			{
			work->data = g_steal_pointer(&path);

			g_free(old_path);
			}
		else
			{
			history_list_item_remove(group, old_path);
			}
		}
	else if (text)
		{
		history_list_add_to_key(group, path, 0);
		}
}

void pref_list_int_set(const gchar *group, const gchar *key, gint value)
{
	pref_list_set(group, key, PREF_LIST_MARKER_INT, std::to_string(value).c_str());
}

gint pref_list_int_get(const gchar *group, const gchar *key, gint fallback)
{
	if (!group || !key) return fallback;

	const gchar *text;
	if (!pref_list_get(group, key, PREF_LIST_MARKER_INT, &text) || !text) return fallback;

	return static_cast<gint>(strtol(text, nullptr, 10));
}

GtkWidget *pref_color_button_new(GtkWidget *parent_box, const gchar *title, const GdkRGBA *color, GdkRGBA *result)
{
	GtkWidget *button;

	if (color)
		{
 		button = gtk_color_button_new_with_rgba(color);
		}
	else
		{
		button = gtk_color_button_new();
		}

	if (result)
		{
		g_signal_connect(G_OBJECT(button), "color-set", G_CALLBACK(gtk_color_chooser_get_rgba), result);
		*result = *color;
		}

	if (title)
		{
		GtkWidget *label;
		GtkWidget *hbox;

		gtk_color_button_set_title(GTK_COLOR_BUTTON(button), title);
		label = gtk_label_new(title);

		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gq_gtk_box_pack_start(GTK_BOX(parent_box), hbox, TRUE, TRUE, 0);

		gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
		gq_gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

		gq_gtk_widget_show_all(hbox);
		}
	else
		{
		gtk_widget_show(button);
		}

	return button;
}

/*
 *-----------------------------------------------------------------------------
 * text widget
 *-----------------------------------------------------------------------------
 */

gchar *text_widget_text_pull(GtkWidget *text_widget, gboolean include_hidden_chars)
{
	if (GTK_IS_TEXT_VIEW(text_widget))
		{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_widget));

		GtkTextIter start;
		GtkTextIter end;
		gtk_text_buffer_get_bounds(buffer, &start, &end);

		return gtk_text_buffer_get_text(buffer, &start, &end, include_hidden_chars);
		}

	if (GTK_IS_ENTRY(text_widget))
		{
		return g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(text_widget)));
		}

	return nullptr;
}

gchar *text_widget_text_pull_selected(GtkWidget *text_widget)
{
	if (GTK_IS_TEXT_VIEW(text_widget))
		{
		GtkTextBuffer *buffer;
		GtkTextIter start;
		GtkTextIter end;

		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_widget));
		gtk_text_buffer_get_bounds(buffer, &start, &end);

		if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
			{
			gtk_text_iter_set_line_offset(&start, 0);
			gtk_text_iter_forward_to_line_end(&end);
			}

		return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
		}

	if (GTK_IS_ENTRY(text_widget))
		{
		return g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(text_widget)));
		}

	return nullptr;
	
}

ActionItem::ActionItem(const gchar *name, const gchar *label, const gchar *icon_name)
    : name(g_strdup(name))
    , label(g_strdup(label))
    , icon_name(g_strdup(icon_name))
{}

ActionItem::ActionItem(const ActionItem &other)
    : name(g_strdup(other.name))
    , label(g_strdup(other.label))
    , icon_name(g_strdup(other.icon_name))
{}

ActionItem::ActionItem(ActionItem &&other) noexcept
    : name(std::exchange(other.name, nullptr))
    , label(std::exchange(other.label, nullptr))
    , icon_name(std::exchange(other.icon_name, nullptr))
{}

ActionItem::~ActionItem()
{
	g_free(name);
	g_free(label);
	g_free(icon_name);
}

ActionItem &ActionItem::operator=(const ActionItem &other)
{
	if (this != &other)
		{
		g_free(name);
		name = g_strdup(other.name);

		g_free(label);
		label = g_strdup(other.label);

		g_free(icon_name);
		icon_name = g_strdup(other.icon_name);
		}

	return *this;
}

ActionItem &ActionItem::operator=(ActionItem &&other) noexcept
{
	if (this != &other)
		{
		g_free(name);
		name = std::exchange(other.name, nullptr);

		g_free(label);
		label = std::exchange(other.label, nullptr);

		g_free(icon_name);
		icon_name = std::exchange(other.icon_name, nullptr);
		}

	return *this;
}

bool ActionItem::has_label(const gchar *label) const
{
	return g_strcmp0(this->label, label) == 0;
}

static gchar *get_action_label(GtkAction *action, const gchar *action_name)
{
	g_autofree gchar *tooltip = nullptr;
	g_autofree gchar *label = nullptr;
	g_object_get(action, "tooltip", &tooltip, "label", &label, NULL);

	/* .desktop items need the program name, Geeqie menu items need the tooltip */
	if (g_strstr_len(action_name, -1, ".desktop") == nullptr &&
	    /* Tooltips with newlines affect output format */
	    tooltip && (g_strstr_len(tooltip, -1, "\n") == nullptr))
		{
		return g_strdup(tooltip);
		}

	return g_strdup(label);
}

/**
 * @brief Get a list of menu actions
 * @param
 * @returns std::vector<ActionItem>
 *
 * The list generated is used in the --action-list command and
 * programmable mouse buttons 8 and 9.
 */
std::vector<ActionItem> get_action_items()
{
	LayoutWindow *lw = get_current_layout();
	if (!lw)
		{
		return {};
		}

	std::vector<ActionItem> list_duplicates;

	for (GList *groups = deprecated_gtk_ui_manager_get_action_groups(lw->ui_manager); groups; groups = groups->next)
		{
		g_autoptr(GList) actions = deprecated_gtk_action_group_list_actions(deprecated_GTK_ACTION_GROUP(groups->data));
		for (GList *work = actions; work; work = work->next)
			{
			GtkAction *action = deprecated_GTK_ACTION(work->data);

			const gchar *accel_path = deprecated_gtk_action_get_accel_path(action);
			if (accel_path && gtk_accel_map_lookup_entry(accel_path, nullptr))
				{
				g_autofree gchar *action_name = g_path_get_basename(accel_path);

				/* Menu actions are irrelevant */
				if (g_strstr_len(action_name, -1, "Menu") == nullptr)
					{
					g_autofree gchar *action_label = get_action_label(action, action_name);

#if HAVE_GTK4
/* @FIXME GTK4 stub */
#else
					list_duplicates.emplace_back(action_name, action_label, deprecated_gtk_action_get_stock_id(action));
#endif
					}
				}
			}
		}

	/* Use the shortest name i.e. ignore -Alt versions. Sort makes the shortest first in the list */
	const auto action_item_compare_names = [](const ActionItem &a, const ActionItem &b)
	{
		return g_strcmp0(a.name, b.name) < 0;
	};
	std::sort(list_duplicates.begin(), list_duplicates.end(), action_item_compare_names);

	/* Ignore duplicate entries */
	std::vector<ActionItem> list_unique;
	for (const ActionItem &action_item : list_duplicates)
		{
		const auto action_item_has_label = [label = action_item.label](const ActionItem &action_item)
		{
			return action_item.has_label(label);
		};
		if (std::none_of(list_unique.cbegin(), list_unique.cend(), action_item_has_label))
			{
			list_unique.push_back(action_item);
			}
		}

	return list_unique;
}

GdkPixbuf *gq_gtk_icon_theme_load_icon_copy(GtkIconTheme *icon_theme, const gchar *icon_name, gint size, GtkIconLookupFlags flags)
{
	g_autoptr(GError) error = nullptr;
	g_autoptr(GdkPixbuf) icon = gtk_icon_theme_load_icon(icon_theme, icon_name, size, flags, &error);
	if (error) return nullptr;

	return gdk_pixbuf_copy(icon);
}

gboolean widget_get_pointer_position(GtkWidget *widget, GqPoint &pos)
{
#if HAVE_GTK4

	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));

	if (!surface)
		{
		return rect;
		}

	if (!surface)
		return FALSE;

	GdkDisplay *display = gdk_surface_get_display(surface);
	if (!display)
		return FALSE;

	GdkSeat *seat = gdk_display_get_default_seat(display);
	if (!seat)
		return FALSE;

	GdkDevice *device = gdk_seat_get_pointer(seat);
	if (!device)
		return FALSE;

	GdkSurface *pointer_surface = nullptr;
	double x = 0.0, y = 0.0;

	gdk_device_get_position(device, &pointer_surface, &x, &y);

	if (pointer_surface != surface)
		return FALSE;

	pos.x = (int)x;
	pos.y = (int)y;

	int width  = gdk_surface_get_width(surface);
	int height = gdk_surface_get_height(surface);

	return 0 <= pos.x && pos.x < width && 0 <= pos.y && pos.y < height;
#else
	GdkWindow *window = gtk_widget_get_window(widget);

	if (!window)
		{
		return FALSE;
		}

	GdkSeat *seat = gdk_display_get_default_seat(gdk_window_get_display(window));
	GdkDevice *device = gdk_seat_get_pointer(seat);

	get_pointer_position(widget, device, &pos.x, &pos.y, nullptr);
	gint width = gdk_window_get_width(window);
	gint height = gdk_window_get_height(window);

	return 0 <= pos.x && pos.x < width && 0 <= pos.y && pos.y < height;
#endif
}

GdkRectangle widget_get_position_geometry(GtkWidget *widget)
{
	GdkRectangle rect = {};

#if HAVE_GTK4
	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));

	if (!surface)
		{
		return rect;
		}

	gdk_surface_get_position(surface, &rect.x, &rect.y);
	rect.width  = gdk_surface_get_width(surface);
	rect.height = gdk_surface_get_height(surface);

	return rect;
}
#else
	GdkWindow *window = gtk_widget_get_window(widget);

	gdk_window_get_position(window, &rect.x, &rect.y);
	rect.width = gdk_window_get_width(window);
	rect.height = gdk_window_get_height(window);

	return rect;
}
#endif

GdkRectangle widget_get_root_origin_geometry(GtkWidget *widget)
{
	GdkRectangle rect = {};

#if HAVE_GTK4
	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));

	if (!surface)
		{
		return rect;
		}

	rect.width  = gdk_surface_get_width(surface);
	rect.height = gdk_surface_get_height(surface);

	return rect;
#else
	GdkWindow *win = gtk_widget_get_window(widget);

	gdk_window_get_root_origin(win, &rect.x, &rect.y);
	rect.width = gdk_window_get_width(win);
	rect.height = gdk_window_get_height(win);

	return rect;
#endif
}

gboolean widget_received_event(GtkWidget *widget, GqPoint event)
{
#if HAVE_GTK4
	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));

	if (!surface)
		{
		return FALSE;
		}

	int width  = gdk_surface_get_width(surface);
	int height = gdk_surface_get_height(surface);

	return 0 <= event.x && event.x <= width && 0 <= event.y && event.y <= height;

#else
	GdkWindow *window = gtk_widget_get_window(widget);

	if (!window)
		{
		return FALSE;
		}

	gint x;
	gint y;
	gdk_window_get_origin(window, &x, &y);

	gint width  = gdk_window_get_width(window);
	gint height = gdk_window_get_height(window);

	return x <= event.x && event.x <= x + width &&
	       y <= event.y && event.y <= y + height;
}
#endif

void widget_remove_from_parent(GtkWidget *widget)
{
	gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(widget)), widget);
}

void widget_remove_from_parent_cb(GtkWidget *, gpointer data)
{
	widget_remove_from_parent(static_cast<GtkWidget *>(data));
}

void widget_input_grab(GtkWidget *widget, GdkSeatCapabilities capabilities, gboolean owner_events, GdkEventMask event_mask)
{
	GdkWindow *window = gtk_widget_get_window(widget);

	const GdkEventMask prev_event_mask = gdk_window_get_events(window);
	g_object_set_data(G_OBJECT(window), "prev_event_mask", GINT_TO_POINTER(prev_event_mask));
	gdk_window_set_events(window, event_mask);

	GdkDisplay *display = gdk_window_get_display(window);
	GdkSeat *seat = gdk_display_get_default_seat(display);

	gdk_seat_grab(seat, window, capabilities, owner_events,
	              nullptr, nullptr, nullptr, nullptr);

	gtk_grab_add(widget);
}

void widget_input_ungrab(GtkWidget *widget)
{
	GdkWindow *window = gtk_widget_get_window(widget);

	const auto prev_event_mask = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "prev_event_mask"));
	gdk_window_set_events(window, static_cast<GdkEventMask>(prev_event_mask));

	GdkDisplay *display = gdk_window_get_display(window);
	GdkSeat *seat = gdk_display_get_default_seat(display);

	gdk_seat_ungrab(seat);
	gtk_grab_remove(widget);
}

gboolean get_pointer_position(GtkWidget *widget, GdkDevice *device, int *x, int *y, GdkModifierType *mask)
{
#if HAVE_GTK4
	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));
	GdkSurface *ps = NULL;
	double dx;
	double dy;

	gdk_device_get_position(device, &ps, &dx, &dy);
	if (ps != surface)
		{
		return FALSE;
		}

	*x = (int)dx;
	*y = (int)dy;

	return TRUE;
#else
	gdk_window_get_device_position(gtk_widget_get_window(widget), device, x, y, mask);

	return TRUE;
#endif
}

void get_device_position(GdkDevice *device, int &x, int &y)
{
#if HAVE_GTK4
	double dx = 0.0;
	double dy = 0.0;
	GdkSurface *surface = nullptr;

	if (!device)
		{
		x = y = -1;
		return;
		}

	gdk_device_get_position(device, &surface, &dx, &dy);

	if (!surface)
		{
		/* Pointer not over any surface */
		x = y = -1;
		return;
		}

	x = (int)dx;
	y = (int)dy;
#else
	gdk_device_get_position(device, nullptr, &x, &y);
#endif
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
