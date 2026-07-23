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
#include <optional>
#include <string>
#include <utility>

#include <pango/pango.h>

#include <config.h>

#include "actions.h"
#include "compat.h"
#include "geometry.h"
#include "history-list.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"

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

GtkNative *widget_get_native_safe(GtkWidget *widget)
{
	if (!widget) return nullptr;

	GtkNative *native = gtk_widget_get_native(widget);

	return GTK_IS_NATIVE(native) ? native : nullptr;
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
	    gtk_orientable_get_orientation(GTK_ORIENTABLE(parent_box)) == GTK_ORIENTATION_VERTICAL &&
	    gtk_widget_get_first_child(parent_box))
		{
		pref_spacer(vbox, PREF_PAD_GROUP - PREF_PAD_GAP);
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
	gq_gtk_widget_set_border_width(box, PREF_PAD_BORDER);
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
	g_autoptr(PangoAttrList) pal = get_pango_attr_list(bold, increase_size);
	if (!pal) return;

	gtk_label_set_attributes(GTK_LABEL(label), pal);
}

GtkWidget *pref_button_new(GtkWidget *parent_box, const gchar *icon_name,
			   const gchar *text, GCallback func, gpointer data)
{
	GtkWidget *button;

	if (icon_name)
		{
		button = gtk_button_new_from_icon_name(icon_name);
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
	gtk_check_button_set_active(GTK_CHECK_BUTTON(button), active);
	if (func) g_signal_connect(G_OBJECT(button), "toggled", func, data);

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

	*result = gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
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

	gtk_widget_set_sensitive(widget, gtk_check_button_get_active(GTK_CHECK_BUTTON(button)));
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
	GtkCheckButton *group;

	if (sibling)
		{
		group = GTK_CHECK_BUTTON(sibling);
		}
	else
		{
		group = nullptr;
		}

	if (mnemonic_text)
		{
		button = gtk_check_button_new_with_mnemonic(text);
		gtk_check_button_set_group(GTK_CHECK_BUTTON(button), group);
		}
	else
		{
		button = gtk_check_button_new_with_label(text);
		gtk_check_button_set_group(GTK_CHECK_BUTTON(button), group);
		}

	if (active) gtk_check_button_set_active(GTK_CHECK_BUTTON(button), active);
	if (func) g_signal_connect(G_OBJECT(button), "toggled", func, data);

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
		g_signal_connect(G_OBJECT(spin), "value-changed", G_CALLBACK(func), data);
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

static void pref_spin_int_cb(GtkSpinButton *spin_button, gpointer data)
{
	auto *var = static_cast<gint *>(data);
	*var = gtk_spin_button_get_value_as_int(spin_button);
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

	gtk_grid_attach(GTK_GRID(table), shell, column, row, 1, 1);

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
	gtk_grid_attach(GTK_GRID(table), label, column, row, 1, 1);
	gtk_widget_show(label);

	return label;
}

GtkWidget *pref_table_button(GtkWidget *table, gint column, gint row,
			     const gchar *stock_id, const gchar *text,
			     GCallback func, gpointer data)
{
	GtkWidget *button;

	button = pref_button_new(nullptr, stock_id, text, func, data);
	gtk_grid_attach(GTK_GRID(table), button, column, row, 1, 1);
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
		g_signal_connect(G_OBJECT(spin), "value-changed", G_CALLBACK(func), data);
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

	gtk_grid_attach(GTK_GRID(table), box, column, row, 1, 1);
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
		item = gtk_toggle_button_new();
		}
	else
		{
		item = gtk_button_new();
		}

	if (icon_name)
		{
		gtk_button_set_icon_name(GTK_BUTTON(item), icon_name);
		}

	if (label)
		{
		gtk_button_set_label(GTK_BUTTON(item), label);
		}

	gtk_button_set_use_underline(GTK_BUTTON(item), TRUE);

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

	GtkWidget *popover;
	GtkWidget *calendar;
};

static void date_selection_popup_hide(DateSelection *ds)
{
	if (!ds->popover) return;

	gtk_popover_popdown(GTK_POPOVER(ds->popover));
}

static void date_selection_popup_sync(DateSelection *ds)
{
	g_autoptr(GDateTime) date_selected = gtk_calendar_get_date(GTK_CALENDAR(ds->calendar));

	gint year;
	gint month;
	gint day;

	g_date_time_get_ymd(date_selected, &year, &month, &day);

	date_selection_set(ds->box, day, month, year);
}

static void date_selection_popup(DateSelection *ds)
{
	if (ds->popover)
		{
		g_autoptr(GDateTime) date = date_selection_get(ds->box);
		gtk_calendar_select_day(GTK_CALENDAR(ds->calendar), date);
		return;
		}

	ds->popover = gtk_popover_new();

	ds->calendar = gtk_calendar_new();

	gtk_popover_set_child(GTK_POPOVER(ds->popover), ds->calendar);

	g_autoptr(GDateTime) date = date_selection_get(ds->box);

	gtk_calendar_select_day(GTK_CALENDAR(ds->calendar), date);

	g_signal_connect_swapped(ds->calendar,
	                         "day-selected",
	                         G_CALLBACK(date_selection_popup_sync),
	                         ds);

	gtk_menu_button_set_popover(GTK_MENU_BUTTON(ds->button), ds->popover);
}

static void date_selection_button_active_cb(GtkMenuButton *button, GParamSpec *, gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	if (gtk_menu_button_get_active(button))
		{
		date_selection_popup(ds);
		gtk_widget_grab_focus(ds->calendar);
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

	ds->button = gtk_menu_button_new();
	/* Temporary GTK4 fallback: the old requisition/size_allocate hack used by
	 * this button depended on GTK3 layout internals, so the button currently
	 * uses its natural size until this widget is restyled for GTK4. */

	icon = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN);
	gtk_menu_button_set_child(GTK_MENU_BUTTON(ds->button), icon);
	gtk_widget_show(icon);

	gq_gtk_box_pack_start(GTK_BOX(ds->box), ds->button, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(ds->button), "notify::active",
			 G_CALLBACK(date_selection_button_active_cb), ds);
	g_object_set_data(G_OBJECT(ds->box), DATE_SELECION_KEY, ds);
	date_selection_popup(ds);
	gtk_widget_show(ds->button);

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

static std::optional<HistoryList::iterator> pref_list_find(const gchar *group, const gchar *token, size_t token_len)
{
	HistoryList *history_list = history_list_find_by_key(group);
	if (!history_list) return {};

	auto work = std::find_if(history_list->begin(), history_list->end(),
	                         [token, token_len](const std::string &text){ return text.compare(0, token_len, token) == 0; });
	if (work == history_list->end()) return {};

	return work;
}

static const gchar *pref_list_get(const gchar *group, const gchar *key, const gchar *marker)
{
	if (!group || !key || !marker) return nullptr;

	g_autofree gchar *token = g_strconcat(key, marker, NULL);
	const size_t token_len = strlen(token);

	auto work = pref_list_find(group, token, token_len);
	if (!work) return nullptr;

	const gchar *result = work.value()->c_str() + token_len;

	return (result[0] != '\0') ? result : nullptr;
}

static void pref_list_set(const gchar *group, const gchar *key, const gchar *marker, const gchar *text)
{
	if (!group || !key || !marker) return;

	g_autofree gchar *token = g_strconcat(key, marker, NULL);
	g_autofree gchar *path = g_strconcat(token, text, NULL);

	auto work = pref_list_find(group, token, strlen(token));
	if (work)
		{
		auto &it = work.value();

		if (text)
			{
			*it = path;
			}
		else
			{
			history_list_item_remove(group, it->c_str());
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

	const gchar *text = pref_list_get(group, key, PREF_LIST_MARKER_INT);

	return text ? static_cast<gint>(strtol(text, nullptr, 10)) : fallback;
}

static void color_button_rgba_cb(GtkColorDialogButton *button, GParamSpec *, gpointer user_data)
{
	auto *color = static_cast<GdkRGBA *>(user_data);

	*color = *(gtk_color_dialog_button_get_rgba(button));
}

GtkWidget *pref_color_button_new(GtkWidget *parent_box, const gchar *title, const GdkRGBA *color, GdkRGBA *result)
{
	GtkColorDialog *dialog = gtk_color_dialog_new();

	GtkWidget *button = gtk_color_dialog_button_new(dialog);

	if (color)
		{
		gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(button), color);
		}

	if (result)
		{
		g_signal_connect(G_OBJECT(button), "notify::rgba", G_CALLBACK(color_button_rgba_cb), result);
		*result = *color;
		}

	if (title)
		{
		gtk_color_dialog_set_title(dialog, title);

		GtkWidget *label = gtk_label_new(title);

		GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gq_gtk_box_pack_start(GTK_BOX(parent_box), hbox, TRUE, TRUE, 0);

		gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
		gq_gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

		gtk_widget_set_visible(hbox, TRUE);
		}
	else
		{
		gtk_widget_set_visible(button, TRUE);
		}

	return button;
}

/*
 *-----------------------------------------------------------------------------
 * text widget
 *-----------------------------------------------------------------------------
 */

char *text_buffer_get_text(GtkTextBuffer *buffer, gboolean include_hidden_chars)
{
	GtkTextIter start;
	GtkTextIter end;
	gtk_text_buffer_get_bounds(buffer, &start, &end);

	return gtk_text_buffer_get_text(buffer, &start, &end, include_hidden_chars);
}

gchar *text_widget_text_pull(GtkWidget *text_widget, gboolean include_hidden_chars)
{
	if (GTK_IS_TEXT_VIEW(text_widget))
		{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_widget));

		return text_buffer_get_text(buffer, include_hidden_chars);
		}

	if (GTK_IS_ENTRY(text_widget))
		{
		return g_strdup(gtk_editable_get_text(GTK_EDITABLE(text_widget)));
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
		return g_strdup(gtk_editable_get_text(GTK_EDITABLE(text_widget)));
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

static gchar *get_action_label(gpointer, const gchar *action_name)
{
	const gchar *label = get_description_for_action_name(action_name);
	if (label) return g_strdup(label);

	if (!strchr(action_name, '.'))
		{
		g_autofree gchar *window_action_name = g_strdup_printf("win.%s", action_name);
		label = get_description_for_action_name(window_action_name);
		if (label) return g_strdup(label);

		g_autofree gchar *app_action_name = g_strdup_printf("app.%s", action_name);
		label = get_description_for_action_name(app_action_name);
		if (label) return g_strdup(label);
		}

	return g_strdup(action_name);
}

static void action_to_list_duplicates(gpointer data, gpointer user_data)
{
	if (!G_IS_ACTION(data) || !user_data) return;

	auto *list_duplicates = static_cast<std::vector<ActionItem> *>(user_data);
	const gchar *action_name = g_action_get_name(G_ACTION(data));
	g_autofree gchar *label = get_action_label(nullptr, action_name);
	g_autofree gchar *window_action_name = g_strdup_printf("win.%s", action_name);
	auto icon_name = get_icon_for_action_name(window_action_name);

	list_duplicates->emplace_back(action_name, label, icon_name);
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
	if (!lw) return {};

	std::vector<ActionItem> list_duplicates;
	layout_actions_foreach(lw, action_to_list_duplicates, &list_duplicates);

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
	g_autoptr(GtkIconPaintable) icon = nullptr;
	g_autoptr(GFile) file = nullptr;
	g_autofree gchar *path = nullptr;
	g_autoptr(GError) error = nullptr;

	icon = gtk_icon_theme_lookup_icon(icon_theme,
					  icon_name,
					  nullptr,
					  size,
					  1,
					  GTK_TEXT_DIR_NONE,
					  flags);
	if (!icon)
		{
		return nullptr;
		}

	file = gtk_icon_paintable_get_file(icon);
	if (!file)
		{
		return nullptr;
		}

	path = g_file_get_path(file);
	if (!path)
		{
		return nullptr;
		}

	return gdk_pixbuf_new_from_file_at_scale(path, size, size, TRUE, &error);
}

gboolean widget_get_pointer_position(GtkWidget *widget, GqPoint &pos)
{
	GtkNative *native = widget_get_native_safe(widget);
	if (!native) return FALSE;

	GdkSurface *surface = gtk_native_get_surface(native);

	if (!surface)
		{
		return FALSE;
		}

	GdkDisplay *display = gdk_surface_get_display(surface);
	if (!display)
		return FALSE;

	GdkSeat *seat = gdk_display_get_default_seat(display);
	if (!seat)
		return FALSE;

	GdkDevice *device = gdk_seat_get_pointer(seat);
	if (!device)
		return FALSE;

	double x = 0.0;
	double y = 0.0;
	auto mask = static_cast<GdkModifierType>(0);

	if (!gdk_surface_get_device_position(surface, device, &x, &y, &mask))
		return FALSE;

	pos.x = (int)x;
	pos.y = (int)y;

	int width  = gdk_surface_get_width(surface);
	int height = gdk_surface_get_height(surface);

	return 0 <= pos.x && pos.x < width && 0 <= pos.y && pos.y < height;
}

GdkRectangle widget_get_position_geometry(GtkWidget *widget)
{
	GdkRectangle rect = {};

	if (!widget)
		{
		return rect;
		}

	GtkNative *native = widget_get_native_safe(widget);
	if (!native)
		{
		return rect;
		}

	GdkSurface *surface = gtk_native_get_surface(native);
	if (!surface)
		{
		return rect;
		}

	/* GTK4/Wayland does not generally expose reliable toplevel x/y. */
	rect.x = 0;
	rect.y = 0;
	rect.width = gdk_surface_get_width(surface);
	rect.height = gdk_surface_get_height(surface);

	return rect;
}

GdkRectangle widget_get_root_origin_geometry(GtkWidget *widget)
{
	GdkRectangle rect = {};

	if (!widget)
		{
		return rect;
		}

	GtkNative *native = widget_get_native_safe(widget);
	if (!native)
		{
		return rect;
		}

	GdkSurface *surface = gtk_native_get_surface(native);
	if (!surface)
		{
		return rect;
		}

	rect.width = gdk_surface_get_width(surface);
	rect.height = gdk_surface_get_height(surface);

	/* GTK4/Wayland: window position is compositor controlled. */
	rect.x = 0;
	rect.y = 0;

	return rect;
}

gboolean widget_received_event(GtkWidget *widget, GqPoint event)
{
	GtkNative *native = widget_get_native_safe(widget);
	if (!native) return FALSE;

	GdkSurface *surface = gtk_native_get_surface(native);

	if (!surface)
		{
		return FALSE;
		}

	int width  = gdk_surface_get_width(surface);
	int height = gdk_surface_get_height(surface);

	return 0 <= event.x && event.x <= width && 0 <= event.y && event.y <= height;
}

void widget_remove_from_parent(GtkWidget *widget)
{
	if (!GTK_IS_WIDGET(widget)) return;

	gq_gtk_container_remove(gtk_widget_get_parent(widget), widget);
}

void widget_remove_from_parent_cb(GSimpleAction *, GVariant *, gpointer data)
{
	widget_remove_from_parent(static_cast<GtkWidget *>(data));
}

gboolean get_pointer_position(GtkWidget *widget, GdkDevice *device, int *x, int *y, GdkModifierType *mask)
{
	GtkNative *native = widget_get_native_safe(widget);
	if (!native)
		{
		return FALSE;
		}

	GdkSurface *surface = gtk_native_get_surface(native);
	double dx;
	double dy;

	if (!surface)
		{
		return FALSE;
		}

	auto local_mask = static_cast<GdkModifierType>(0);
	if (!gdk_surface_get_device_position(surface, device, &dx, &dy, &local_mask))
		{
		return FALSE;
		}

	*x = (int)dx;
	*y = (int)dy;
	if (mask) *mask = local_mask;

	return TRUE;
}

void get_device_position(GdkDevice *device, int &x, int &y)
{
	double dx = 0.0;
	double dy = 0.0;
	GdkSurface *surface = nullptr;

	if (!device)
		{
		x = y = -1;
		return;
		}

	surface = gdk_device_get_surface_at_position(device, &dx, &dy);

	if (!surface)
		{
		/* Pointer not over any surface */
		x = y = -1;
		return;
		}

	x = (int)dx;
	y = (int)dy;
}

PangoAttrList *get_pango_attr_list(gboolean weight, gboolean scale)
{
	if (!weight && !scale) return nullptr;

	PangoAttrList *pal = pango_attr_list_new();

	if (weight)
		{
		PangoAttribute *pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
		pa->start_index = 0;
		pa->end_index = G_MAXINT;
		pango_attr_list_insert(pal, pa);
		}

	if (scale)
		{
		PangoAttribute *pa = pango_attr_scale_new(PANGO_SCALE_LARGE);
		pa->start_index = 0;
		pa->end_index = G_MAXINT;
		pango_attr_list_insert(pal, pa);
		}

	return pal;
}

gboolean get_alternative_button_order(GtkWidget *widget)
{
	(void)widget;

	/* GTK4 no longer exposes the old alternative button order setting.
	 * Use the standard application-defined order consistently. */
	return FALSE;
}

namespace
{

bool widget_is_editable_text(GtkWidget *widget)
{
	return GTK_IS_EDITABLE(widget) || GTK_IS_TEXT_VIEW(widget);
}

bool widget_or_descendant_is_editable_text(GtkWidget *widget)
{
	if (!widget)
		{
		return false;
		}

	if (widget_is_editable_text(widget))
		{
		return true;
		}

	for (GtkWidget *child = gtk_widget_get_first_child(widget);
	     child;
	     child = gtk_widget_get_next_sibling(child))
		{
		if (widget_or_descendant_is_editable_text(child))
			{
			return true;
			}
		}

	return false;
}

bool focus_widget_is_editable_text(GtkWidget *focus)
{
	for (GtkWidget *widget = focus; widget; widget = gtk_widget_get_parent(widget))
		{
		if (widget_is_editable_text(widget))
			{
			return true;
			}
		}

	return widget_or_descendant_is_editable_text(focus);
}

} // namespace

bool focus_is_text_editable(GtkWindow *window)
{
	if (!window)
		{
		return false;
		}

	if (focus_widget_is_editable_text(gtk_window_get_focus(window)))
		{
		return true;
		}

	return focus_widget_is_editable_text(gtk_root_get_focus(GTK_ROOT(window)));
}

bool focus_is_editable(GtkWindow *window)
{
	return focus_is_text_editable(window);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
