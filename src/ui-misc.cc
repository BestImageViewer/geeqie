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

#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "ui-misc.h"

#include "history-list.h"

#include <langinfo.h>

/*
 *-----------------------------------------------------------------------------
 * widget and layout utilities
 *-----------------------------------------------------------------------------
 */

GtkWidget *pref_box_new(GtkWidget *parent_box, gboolean fill,
			GtkOrientation orientation, gboolean padding)
{
	GtkWidget *box;

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		{
		box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, padding);
		}
	else
		{
		box = gtk_box_new(GTK_ORIENTATION_VERTICAL, padding);
		}

	gtk_box_pack_start(GTK_BOX(parent_box), box, fill, fill, 0);
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
	if (GTK_IS_VBOX(parent_box))
		{
		GList *list = gtk_container_get_children(GTK_CONTAINER(parent_box));
		if (list)
			{
			pref_spacer(vbox, PREF_PAD_GROUP - PREF_PAD_GAP);
			}
		g_list_free(list);
		}

	gtk_box_pack_start(GTK_BOX(parent_box), vbox, fill, fill, 0);
	gtk_widget_show(vbox);

	label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);
	pref_label_bold(label, TRUE, FALSE);

	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_INDENT);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
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
	gtk_box_pack_start(GTK_BOX(hbox), box, TRUE, TRUE, 0);
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
	gtk_box_pack_start(GTK_BOX(parent_box), frame, fill, fill, 0);
	gtk_widget_show(frame);

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		{
		box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, padding);
		}
	else
		{
		box = gtk_box_new(GTK_ORIENTATION_VERTICAL, padding);
		}
	gtk_container_add(GTK_CONTAINER(frame), box);
	gtk_container_set_border_width(GTK_CONTAINER(box), PREF_PAD_BORDER);
	gtk_widget_show(box);

	return box;
}

GtkWidget *pref_spacer(GtkWidget *parent_box, gboolean padding)
{
	GtkWidget *spacer;

	spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(parent_box), spacer, FALSE, FALSE, padding / 2);
	gtk_widget_show(spacer);

	return spacer;
}

GtkWidget *pref_line(GtkWidget *parent_box, gboolean padding)
{
	GtkWidget *spacer;

	if (GTK_IS_HBOX(parent_box))
		{
		spacer = gtk_vseparator_new();
		}
	else
		{
		spacer = gtk_hseparator_new();
		}

	gtk_box_pack_start(GTK_BOX(parent_box), spacer, FALSE, FALSE, padding / 2);
	gtk_widget_show(spacer);

	return spacer;
}

GtkWidget *pref_label_new(GtkWidget *parent_box, const gchar *text)
{
	GtkWidget *label;

	label = gtk_label_new(text);
	gtk_box_pack_start(GTK_BOX(parent_box), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	return label;
}

GtkWidget *pref_label_new_mnemonic(GtkWidget *parent_box, const gchar *text, GtkWidget *widget)
{
	GtkWidget *label;

	label = gtk_label_new_with_mnemonic(text);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
	gtk_box_pack_start(GTK_BOX(parent_box), label, FALSE, FALSE, 0);
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

GtkWidget *pref_button_new(GtkWidget *parent_box, const gchar *stock_id,
			   const gchar *text, gboolean hide_stock_text,
			   GCallback func, gpointer data)
{
	GtkWidget *button;

	if (stock_id && !text && !hide_stock_text)
		{
		button = gtk_button_new_from_stock(stock_id);
		}
	else
		{
		GtkWidget *image = nullptr;
		GtkWidget *label = nullptr;

		button = gtk_button_new();

		if (stock_id)
			{
			image = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_BUTTON);
			}

		if (text)
			{
			label = gtk_label_new_with_mnemonic(text);
			gtk_label_set_xalign(GTK_LABEL(label), 0.5);
			gtk_label_set_yalign(GTK_LABEL(label), 0.5);
			gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);
			}

		if (image && label)
			{
			GtkWidget *align;
			GtkWidget *hbox;

			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_ICON_GAP);

			align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
			gtk_container_add(GTK_CONTAINER(button), align);
			gtk_widget_show(align);

			gtk_container_add(GTK_CONTAINER(align), hbox);
			gtk_widget_show(hbox);

			gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
			gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 0);
			}
		else
			{
			if (image)
				{
				gtk_container_add(GTK_CONTAINER(button), image);
				}
			else if (label)
				{
				gtk_container_add(GTK_CONTAINER(button), label);
				}
			}

		if (image) gtk_widget_show(image);
		if (label) gtk_widget_show(label);
		}

	if (func) g_signal_connect(G_OBJECT(button), "clicked", func, data);

	if (parent_box)
		{
		gtk_box_pack_start(GTK_BOX(parent_box), button, FALSE, FALSE, 0);
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

	gtk_box_pack_start(GTK_BOX(parent_box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	return button;
}

GtkWidget *pref_checkbox_new(GtkWidget *parent_box, const gchar *text, gboolean active,
			     GCallback func, gpointer data)
{
	return real_pref_checkbox_new(parent_box, text, FALSE, active, func, data);
}

//GtkWidget *pref_checkbox_new_mnemonic(GtkWidget *parent_box, const gchar *text, gboolean active,
				      //GCallback func, gpointer data)
//{
	//return real_pref_checkbox_new(parent_box, text, TRUE, active, func, data);
//}

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

//static void pref_checkbox_link_sensitivity_swap_cb(GtkWidget *button, gpointer data)
//{
	//GtkWidget *widget = static_cast<GtkWidget *>(data);

	//gtk_widget_set_sensitive(widget, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
//}

//void pref_checkbox_link_sensitivity_swap(GtkWidget *button, GtkWidget *widget)
//{
	//g_signal_connect(G_OBJECT(button), "toggled",
			 //G_CALLBACK(pref_checkbox_link_sensitivity_swap_cb), widget);

	//pref_checkbox_link_sensitivity_swap_cb(button, widget);
//}

static GtkWidget *real_pref_radiobutton_new(GtkWidget *parent_box, GtkWidget *sibling,
					    const gchar *text, gboolean mnemonic_text, gboolean active,
					    GCallback func, gpointer data)
{
	GtkWidget *button;
	GSList *group;

	if (sibling)
		{
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(sibling));
		}
	else
		{
		group = nullptr;
		}

	if (mnemonic_text)
		{
		button = gtk_radio_button_new_with_mnemonic(group, text);
		}
	else
		{
		button = gtk_radio_button_new_with_label(group, text);
		}

	if (active) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), active);
	if (func) g_signal_connect(G_OBJECT(button), "clicked", func, data);

	gtk_box_pack_start(GTK_BOX(parent_box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	return button;
}

GtkWidget *pref_radiobutton_new(GtkWidget *parent_box, GtkWidget *sibling,
				const gchar *text, gboolean active,
				GCallback func, gpointer data)
{
	return real_pref_radiobutton_new(parent_box, sibling, text, FALSE, active, func, data);
}

//GtkWidget *pref_radiobutton_new_mnemonic(GtkWidget *parent_box, GtkWidget *sibling,
					 //const gchar *text, gboolean active,
					 //GCallback func, gpointer data)
//{
	//return real_pref_radiobutton_new(parent_box, sibling, text, TRUE, active, func, data);
//}

#define PREF_RADIO_VALUE_KEY "pref_radio_value"

//static void pref_radiobutton_int_cb(GtkWidget *widget, gpointer data)
//{
	//gboolean *result = static_cast<gboolean *>(data);

	//if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		//{
		//*result = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), PREF_RADIO_VALUE_KEY));
		//}
//}

//GtkWidget *pref_radiobutton_new_int(GtkWidget *parent_box, GtkWidget *sibling,
				    //const gchar *text, gboolean active,
				    //gboolean *result, gboolean value,
				    //GCallback UNUSED(func), gpointer UNUSED(data))
//{
	//GtkWidget *button;

	//button = pref_radiobutton_new(parent_box, sibling, text, active,
				      //G_CALLBACK(pref_radiobutton_int_cb), result);
	//g_object_set_data(G_OBJECT(button), PREF_RADIO_VALUE_KEY, GINT_TO_POINTER(value));
	//if (active) *result = value;

	//return button;
//}

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

	gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
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

//GtkWidget *pref_spin_new_mnemonic(GtkWidget *parent_box, const gchar *text, const gchar *suffix,
				  //gdouble min, gdouble max, gdouble step, gint digits,
				  //gdouble value,
				  //GCallback func, gpointer data)
//{
	//return real_pref_spin_new(parent_box, text, suffix, TRUE,
				  //min, max, step, digits, value, func, data);
//}

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

static void pref_link_sensitivity_cb(GtkWidget *watch, GtkStateType UNUSED(prev_state), gpointer data)
{
	auto widget = static_cast<GtkWidget *>(data);

	gtk_widget_set_sensitive(widget, gtk_widget_is_sensitive(watch));
}

void pref_link_sensitivity(GtkWidget *widget, GtkWidget *watch)
{
	g_signal_connect(G_OBJECT(watch), "state_changed",
			 G_CALLBACK(pref_link_sensitivity_cb), widget);
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

GtkWidget *pref_table_new(GtkWidget *parent_box, gint columns, gint rows,
			  gboolean homogeneous, gboolean fill)
{
	GtkWidget *table;

	table = gtk_table_new(rows, columns, homogeneous);
	gtk_table_set_row_spacings(GTK_TABLE(table), PREF_PAD_GAP);
	gtk_table_set_col_spacings(GTK_TABLE(table), PREF_PAD_SPACE);

	if (parent_box)
		{
		gtk_box_pack_start(GTK_BOX(parent_box), table, fill, fill, 0);
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

	gtk_table_attach(GTK_TABLE(table), shell, column, column + 1, row, row + 1,
			 static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), static_cast<GtkAttachOptions>(0), 0, 0);

	gtk_widget_show(shell);

	return box;
}

GtkWidget *pref_table_label(GtkWidget *table, gint column, gint row,
			    const gchar *text, gfloat alignment)
{
	GtkWidget *label;
	GtkWidget *align;

	align = gtk_alignment_new(alignment, 0.50, 0.0, 0.0);
	gtk_table_attach(GTK_TABLE(table), align, column, column + 1, row, row + 1,
			 GTK_FILL, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(align);
	label = gtk_label_new(text);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_widget_show(label);

	return label;
}

GtkWidget *pref_table_button(GtkWidget *table, gint column, gint row,
			     const gchar *stock_id, const gchar *text, gboolean hide_stock_text,
			     GCallback func, gpointer data)
{
	GtkWidget *button;

	button = pref_button_new(nullptr, stock_id, text, hide_stock_text, func, data);
	gtk_table_attach(GTK_TABLE(table), button, column, column + 1, row, row + 1,
			 GTK_FILL, static_cast<GtkAttachOptions>(0), 0, 0);
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
		label = pref_table_label(table, column, row, text, 1.0);
		pref_link_sensitivity(label, spin);
		column++;
		}

	if (suffix)
		{
		box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
		gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
		gtk_widget_show(spin);

		label = pref_label_new(box, suffix);
		pref_link_sensitivity(label, spin);
		}
	else
		{
		box = spin;
		}

	gtk_table_attach(GTK_TABLE(table), box, column, column + 1, row, row + 1,
			 static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), 0, 0);
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


GtkWidget *pref_toolbar_new(GtkWidget *parent_box, GtkToolbarStyle style)
{
	GtkWidget *tbar;

	tbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(tbar), style);

	if (parent_box)
		{
		gtk_box_pack_start(GTK_BOX(parent_box), tbar, FALSE, FALSE, 0);
		gtk_widget_show(tbar);
		}
	return tbar;
}

GtkWidget *pref_toolbar_button(GtkWidget *toolbar,
			       const gchar *stock_id, const gchar *label, gboolean toggle,
			       const gchar *description,
			       GCallback func, gpointer data)
{
	GtkWidget *item;

	if (toggle)
		{
		if (stock_id)
			{
			item = GTK_WIDGET(gtk_toggle_tool_button_new_from_stock(stock_id));
			}
		else
			{
			item = GTK_WIDGET(gtk_toggle_tool_button_new());
			}
		}
	else
		{
		if (stock_id)
			{
			item = GTK_WIDGET(gtk_tool_button_new_from_stock(stock_id));
			}
		else
			{
			item = GTK_WIDGET(gtk_tool_button_new(nullptr, nullptr));
			}
		}
	gtk_tool_button_set_use_underline(GTK_TOOL_BUTTON(item), TRUE);

	if (label) gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), label);

	if (func) g_signal_connect(item, "clicked", func, data);
	gtk_container_add(GTK_CONTAINER(toolbar), item);
	gtk_widget_show(item);

	if (description)
		{
		gtk_widget_set_tooltip_text(item, description);
		}

	return item;
}

//void pref_toolbar_button_set_icon(GtkWidget *button, GtkWidget *widget, const gchar *stock_id)
//{
	//if (widget)
		//{
		//gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(button), widget);
		//}
	//else if (stock_id)
		//{
		//gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(button), stock_id);
		//}
//}

//GtkWidget *pref_toolbar_spacer(GtkWidget *toolbar)
//{
	//GtkWidget *item;

	//item = GTK_WIDGET(gtk_separator_tool_item_new());
	//gtk_container_add(GTK_CONTAINER(toolbar), item);
	//gtk_widget_show(item);

	//return item;
//}


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
		gtk_grab_remove(ds->window);
		gdk_keyboard_ungrab(GDK_CURRENT_TIME);
		gdk_pointer_ungrab(GDK_CURRENT_TIME);
		}

	gtk_widget_hide(ds->window);

	gtk_widget_destroy(ds->window);
	ds->window = nullptr;
	ds->calendar = nullptr;

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ds->button), FALSE);
}

static gboolean date_selection_popup_release_cb(GtkWidget *UNUSED(widget), GdkEventButton *UNUSED(event), gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	date_selection_popup_hide(ds);
	return TRUE;
}

static gboolean date_selection_popup_press_cb(GtkWidget *UNUSED(widget), GdkEventButton *event, gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);
	gint x, y;
	gint w, h;
	gint xr, yr;
	GdkWindow *window;

	xr = static_cast<gint>(event->x_root);
	yr = static_cast<gint>(event->y_root);

	window = gtk_widget_get_window(ds->window);
	gdk_window_get_origin(window, &x, &y);
	w = gdk_window_get_width(window);
	h = gdk_window_get_height(window);

	if (xr < x || yr < y || xr > x + w || yr > y + h)
		{
		g_signal_connect(G_OBJECT(ds->window), "button_release_event",
				 G_CALLBACK(date_selection_popup_release_cb), ds);
		return TRUE;
		}

	return FALSE;
}

static void date_selection_popup_sync(DateSelection *ds)
{
	guint day, month, year;

	gtk_calendar_get_date(GTK_CALENDAR(ds->calendar), &year, &month, &day);
	date_selection_set(ds->box, day, month + 1, year);
}

static gboolean date_selection_popup_keypress_cb(GtkWidget *UNUSED(widget), GdkEventKey *event, gpointer data)
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

static void date_selection_day_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	date_selection_popup_sync(ds);
}

static void date_selection_doubleclick_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	date_selection_popup_hide(ds);
}

static void date_selection_popup(DateSelection *ds)
{
	gint x, y;
	gint wx, wy;
	gint day, month, year;
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
	gtk_container_add(GTK_CONTAINER(ds->window), ds->calendar);
	gtk_widget_show(ds->calendar);

	date_selection_get(ds->box, &day, &month, &year);
	gtk_calendar_select_month(GTK_CALENDAR(ds->calendar), month - 1, year);
	gtk_calendar_select_day(GTK_CALENDAR(ds->calendar), day);

	g_signal_connect(G_OBJECT(ds->calendar), "day_selected",
			 G_CALLBACK(date_selection_day_cb), ds);
	g_signal_connect(G_OBJECT(ds->calendar), "day_selected_double_click",
			G_CALLBACK(date_selection_doubleclick_cb), ds);

	gtk_widget_realize(ds->window);

	gdk_window_get_origin(gtk_widget_get_window(ds->button), &wx, &wy);

	gtk_widget_get_allocation(ds->button, &button_allocation);
	gtk_widget_get_allocation(ds->window, &window_allocation);

	x = wx + button_allocation.x + button_allocation.width - window_allocation.width;
	y = wy + button_allocation.y + button_allocation.height;

	if (y + window_allocation.height > gdk_screen_height())
		{
		y = wy + button_allocation.y - window_allocation.height;
		}
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	gtk_window_move(GTK_WINDOW(ds->window), x, y);
	gtk_widget_show(ds->window);

	gtk_widget_grab_focus(ds->calendar);
	gdk_pointer_grab(gtk_widget_get_window(ds->window), TRUE,
			 static_cast<GdkEventMask>(GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_MOTION_MASK),
			 nullptr, nullptr, GDK_CURRENT_TIME);
	gdk_keyboard_grab(gtk_widget_get_window(ds->window), TRUE, GDK_CURRENT_TIME);
	gtk_grab_add(ds->window);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ds->button), TRUE);
}

static void date_selection_button_cb(GtkWidget *UNUSED(widget), gpointer data)
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
	gtk_widget_get_requisition(spin, &spin_requisition);

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

static void spin_increase(GtkWidget *spin, gint value)
{
	GtkRequisition req;

	gtk_widget_size_request(spin, &req);
	gtk_widget_set_size_request(spin, req.width + value, -1);
}

static void date_selection_destroy_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	auto ds = static_cast<DateSelection *>(data);

	date_selection_popup_hide(ds);

	g_free(ds);
}

GtkWidget *date_selection_new()
{
	DateSelection *ds;
	GtkWidget *arrow;

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

	spin_increase(ds->spin_y, 5);

	ds->button = gtk_toggle_button_new();
	g_signal_connect(G_OBJECT(ds->button), "size_allocate",
			 G_CALLBACK(button_size_allocate_cb), ds->spin_y);

	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(ds->button), arrow);
	gtk_widget_show(arrow);

	gtk_box_pack_start(GTK_BOX(ds->box), ds->button, FALSE, FALSE, 0);
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


void date_selection_get(GtkWidget *widget, gint *day, gint *month, gint *year)
{
	DateSelection *ds;

	ds = static_cast<DateSelection *>(g_object_get_data(G_OBJECT(widget), DATE_SELECION_KEY));
	if (!ds) return;

	if (day) *day = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ds->spin_d));
	if (month) *month = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ds->spin_m));
	if (year) *year = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ds->spin_y));
}

void date_selection_time_set(GtkWidget *widget, time_t t)
{
	struct tm *lt;

	lt = localtime(&t);
	if (!lt) return;

	date_selection_set(widget, lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900);
}

//time_t date_selection_time_get(GtkWidget *widget)
//{
	//struct tm lt;
	//gint day = 0;
	//gint month = 0;
	//gint year = 0;

	//date_selection_get(widget, &day, &month ,&year);

	//lt.tm_sec = 0;
	//lt.tm_min = 0;
	//lt.tm_hour = 0;
	//lt.tm_mday = day;
	//lt.tm_mon = month - 1;
	//lt.tm_year = year - 1900;
	//lt.tm_isdst = 0;

	//return mktime(&lt);
//}

/*
 *-----------------------------------------------------------------------------
 * storing data in a history list with key,data pairs
 *-----------------------------------------------------------------------------
 */

#define PREF_LIST_MARKER_INT "[INT]:"
#define PREF_LIST_MARKER_DOUBLE "[DOUBLE]:"
#define PREF_LIST_MARKER_STRING "[STRING]:"

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
	gchar *token;
	GList *work;
	gboolean ret;

	if (!group || !key || !marker)
		{
		*result = nullptr;
		return FALSE;
		}

	token = g_strconcat(key, marker, NULL);

	work = pref_list_find(group, token);
	if (work)
		{
		*result = static_cast<const gchar *>(work->data) + strlen(token);
		if (strlen(*result) == 0) *result = nullptr;
		ret = TRUE;
		}
	else
		{
		*result = nullptr;
		ret = FALSE;
		}

	g_free(token);

	return ret;
}

static void pref_list_set(const gchar *group, const gchar *key, const gchar *marker, const gchar *text)
{
	gchar *token;
	gchar *path;
	GList *work;

	if (!group || !key || !marker) return;

	token = g_strconcat(key, marker, NULL);
	path = g_strconcat(token, text, NULL);

	work = pref_list_find(group, token);
	if (work)
		{
		auto old_path = static_cast<gchar *>(work->data);

		if (text)
			{
			work->data = path;
			path = nullptr;

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

	g_free(path);
	g_free(token);
}

void pref_list_int_set(const gchar *group, const gchar *key, gint value)
{
	gchar *text;

	text = g_strdup_printf("%d", value);
	pref_list_set(group, key, PREF_LIST_MARKER_INT, text);
	g_free(text);
}

gboolean pref_list_int_get(const gchar *group, const gchar *key, gint *result)
{
	const gchar *text;

	if (!group || !key)
		{
		*result = 0;
		return FALSE;
		}

	if (pref_list_get(group, key, PREF_LIST_MARKER_INT, &text) && text)
		{
		*result = static_cast<gint>(strtol(text, nullptr, 10));
		return TRUE;
		}

	*result = 0;
	return FALSE;
}

//void pref_list_double_set(const gchar *group, const gchar *key, gdouble value)
//{
	//gchar text[G_ASCII_DTOSTR_BUF_SIZE];

	//g_ascii_dtostr(text, sizeof(text), value);
	//pref_list_set(group, key, PREF_LIST_MARKER_DOUBLE, text);
//}

//gboolean pref_list_double_get(const gchar *group, const gchar *key, gdouble *result)
//{
	//const gchar *text;

	//if (!group || !key)
		//{
		//*result = 0;
		//return FALSE;
		//}

	//if (pref_list_get(group, key, PREF_LIST_MARKER_DOUBLE, &text) && text)
		//{
		//*result = g_ascii_strtod(text, NULL);
		//return TRUE;
		//}

	//*result = 0;
	//return FALSE;
//}

//void pref_list_string_set(const gchar *group, const gchar *key, const gchar *value)
//{
	//pref_list_set(group, key, PREF_LIST_MARKER_STRING, value);
//}

//gboolean pref_list_string_get(const gchar *group, const gchar *key, const gchar **result)
//{
	//return pref_list_get(group, key, PREF_LIST_MARKER_STRING, result);
//}


void pref_color_button_set_cb(GtkWidget *widget, gpointer data)
{
	auto color = static_cast<GdkColor *>(data);

	gtk_color_button_get_color(GTK_COLOR_BUTTON(widget), color);
}

GtkWidget *pref_color_button_new(GtkWidget *parent_box,
				 const gchar *title, const GdkColor *color,
				 GCallback func, gpointer data)
{
	GtkWidget *button;

	if (color)
		{
		button = gtk_color_button_new_with_color(color);
		}
	else
		{
		button = gtk_color_button_new();
		}

	if (func) g_signal_connect(G_OBJECT(button), "color-set", func, data);

	if (title)
		{
		GtkWidget *label;
		GtkWidget *hbox;

		gtk_color_button_set_title(GTK_COLOR_BUTTON(button), title);
		label = gtk_label_new(title);

		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_box_pack_start(GTK_BOX(parent_box), hbox, TRUE, TRUE, 0);

		gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

		gtk_widget_show_all(hbox);
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

gchar *text_widget_text_pull(GtkWidget *text_widget)
{
	if (GTK_IS_TEXT_VIEW(text_widget))
		{
		GtkTextBuffer *buffer;
		GtkTextIter start, end;

		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_widget));
		gtk_text_buffer_get_bounds(buffer, &start, &end);

		return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
		}
	else if (GTK_IS_ENTRY(text_widget))
		{
		return g_strdup(gtk_entry_get_text(GTK_ENTRY(text_widget)));
		}
	else
		{
		return nullptr;
		}

}

gchar *text_widget_text_pull_selected(GtkWidget *text_widget)
{
	if (GTK_IS_TEXT_VIEW(text_widget))
		{
		GtkTextBuffer *buffer;
		GtkTextIter start, end;

		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_widget));
		gtk_text_buffer_get_bounds(buffer, &start, &end);

		if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
			{
			gtk_text_iter_set_line_offset(&start, 0);
			gtk_text_iter_forward_to_line_end(&end);
			}

		return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
		}
	else if (GTK_IS_ENTRY(text_widget))
		{
		return g_strdup(gtk_entry_get_text(GTK_ENTRY(text_widget)));
		}
	else
		{
		return nullptr;
		}
}

gboolean defined_mouse_buttons(GtkWidget *UNUSED(widget), GdkEventButton *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkAction *action;
	gboolean ret = FALSE;

	switch (event->button)
		{
		case MOUSE_BUTTON_8:
			if (options->mouse_button_8)
				{
				action = gtk_action_group_get_action(lw->action_group, options->mouse_button_8);
				if (action)
					{
					gtk_action_activate(action);
					}
				ret = TRUE;
				}
				break;
		case MOUSE_BUTTON_9:
			if (options->mouse_button_9)
				{
				action = gtk_action_group_get_action(lw->action_group, options->mouse_button_9);
				if (action)
					{
					gtk_action_activate(action);
					}
				ret = TRUE;
				}
			break;
		default:
			break;
		}

	return ret;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
