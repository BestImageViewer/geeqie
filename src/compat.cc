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

#include "main-defines.h"

namespace
{

constexpr auto GTK4_BOX_PACK_END_DATA_KEY = "gq-gtk4-box-pack-end";
constexpr auto GTK4_WINDOW_POSITION_DATA_KEY = "gq-gtk4-window-position";
constexpr auto GTK4_WINDOW_POSITION_POLICY_DATA_KEY = "gq-gtk4-window-position-policy";
constexpr auto GTK4_WINDOW_KEEP_ABOVE_DATA_KEY = "gq-gtk4-window-keep-above";

struct GqWindowPosition
{
	gint x;
	gint y;
};

const gchar *stock_id_to_icon_name(const gchar *stock_id)
{
	if (!stock_id) return GQ_ICON_MISSING_IMAGE;

	if (g_str_equal(stock_id, "gtk-ok")) return GQ_ICON_OK;
	if (g_str_equal(stock_id, "gtk-cancel")) return GQ_ICON_CANCEL;
	if (g_str_equal(stock_id, "gtk-close")) return GQ_ICON_CLOSE;
	if (g_str_equal(stock_id, "gtk-open")) return GQ_ICON_OPEN;
	if (g_str_equal(stock_id, "gtk-save")) return GQ_ICON_SAVE;
	if (g_str_equal(stock_id, "gtk-save-as")) return GQ_ICON_SAVE_AS;
	if (g_str_equal(stock_id, "gtk-new")) return GQ_ICON_NEW;
	if (g_str_equal(stock_id, "gtk-delete")) return GQ_ICON_DELETE;
	if (g_str_equal(stock_id, "gtk-copy")) return GQ_ICON_COPY;
	if (g_str_equal(stock_id, "gtk-find")) return GQ_ICON_FIND;
	if (g_str_equal(stock_id, "gtk-preferences")) return GQ_ICON_PREFERENCES;
	if (g_str_equal(stock_id, "gtk-print")) return GQ_ICON_PRINT;
	if (g_str_equal(stock_id, "gtk-quit")) return GQ_ICON_QUIT;
	if (g_str_equal(stock_id, "gtk-stop")) return GQ_ICON_STOP;
	if (g_str_equal(stock_id, "gtk-refresh")) return GQ_ICON_REFRESH;
	if (g_str_equal(stock_id, "gtk-help")) return GQ_ICON_HELP;
	if (g_str_equal(stock_id, "gtk-apply")) return GQ_ICON_APPLY;
	if (g_str_equal(stock_id, "gtk-go-up")) return GQ_ICON_GO_UP;
	if (g_str_equal(stock_id, "gtk-go-down")) return GQ_ICON_GO_DOWN;
	if (g_str_equal(stock_id, "gtk-go-back")) return GQ_ICON_GO_PREV;
	if (g_str_equal(stock_id, "gtk-go-forward")) return GQ_ICON_GO_NEXT;
	if (g_str_equal(stock_id, "gtk-home")) return GQ_ICON_HOME;
	if (g_str_equal(stock_id, "gtk-jump-to")) return GQ_ICON_GO_JUMP;
	if (g_str_equal(stock_id, "gtk-missing-image")) return GQ_ICON_MISSING_IMAGE;
	if (g_str_has_prefix(stock_id, "gtk-")) return GQ_ICON_MISSING_IMAGE;

	return stock_id;
}

void gtk4_box_apply_child_packing(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding)
{
	GtkOrientation orientation = gtk_orientable_get_orientation(GTK_ORIENTABLE(box));

	gtk_widget_set_hexpand(child, orientation == GTK_ORIENTATION_HORIZONTAL ? expand : FALSE);
	gtk_widget_set_vexpand(child, orientation == GTK_ORIENTATION_VERTICAL ? expand : FALSE);

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		{
		gtk_widget_set_halign(child, fill ? GTK_ALIGN_FILL : GTK_ALIGN_START);
		gtk_widget_set_margin_end(child, padding);
		}
	else
		{
		gtk_widget_set_valign(child, fill ? GTK_ALIGN_FILL : GTK_ALIGN_START);
		gtk_widget_set_margin_bottom(child, padding);
		}
}

} // namespace

void gq_gtk_box_pack_start(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding)
{
	gtk_box_append(box, child);
	gtk4_box_apply_child_packing(box, child, expand, fill, padding);
}

void gq_gtk_box_pack_end(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding)
{
	g_object_set_data(G_OBJECT(child), GTK4_BOX_PACK_END_DATA_KEY, GINT_TO_POINTER(TRUE));

	GtkWidget *first_end_child = nullptr;
	for (GtkWidget *work = gtk_widget_get_first_child(GTK_WIDGET(box));
	     work != nullptr;
	     work = gtk_widget_get_next_sibling(work))
		{
		if (g_object_get_data(G_OBJECT(work), GTK4_BOX_PACK_END_DATA_KEY))
			{
			first_end_child = work;
			break;
			}
		}

	if (!first_end_child)
		{
		gtk_box_append(box, child);
		return;
		}

	GtkWidget *previous = gtk_widget_get_prev_sibling(first_end_child);
	if (previous)
		{
		gtk_box_insert_child_after(box, child, previous);
		}
	else
		{
		gtk_box_prepend(box, child);
		}

	gtk4_box_apply_child_packing(box, child, expand, fill, padding);
}

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

void gq_gtk_window_set_keep_above(GtkWindow *window, gboolean setting)
{
	g_object_set_data(G_OBJECT(window), GTK4_WINDOW_KEEP_ABOVE_DATA_KEY, GINT_TO_POINTER(setting));

	if (setting && gtk_widget_get_visible(GTK_WIDGET(window)))
		{
		gtk_window_present(window);
		}
}

void gq_gtk_widget_show_all(GtkWidget *widget)
{
	if (!widget) return;

	gtk_widget_set_visible(widget, TRUE);

	for (GtkWidget *child = gtk_widget_get_first_child(widget);
	     child != nullptr;
	     child = gtk_widget_get_next_sibling(child))
		{
		gq_gtk_widget_show_all(child);
		}
}

void gq_gtk_frame_set_shadow_type(GtkFrame *frame, int type)
{
	if (type == 1)
		{
		gtk_widget_remove_css_class(GTK_WIDGET(frame), "frame");
		}
	else
		{
		gtk_widget_add_css_class(GTK_WIDGET(frame), "frame");
		}
}

void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget)
{
	if (GTK_IS_BUTTON(container))
		{
		gtk_button_set_child(GTK_BUTTON(container), widget);
		}
	else if (GTK_IS_EXPANDER(container))
		{
		gtk_expander_set_child(GTK_EXPANDER(container), widget);
		}
	else if (GTK_IS_FRAME(container))
		{
		gtk_frame_set_child(GTK_FRAME(container), widget);
		}
	else if (GTK_IS_POPOVER(container))
		{
		gtk_popover_set_child(GTK_POPOVER(container), widget);
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

void gq_gtk_container_remove(GtkWidget *container, GtkWidget *widget)
{
	if (!widget || gtk_widget_get_parent(widget) != container) return;

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

void gq_gtk_container_foreach(GtkWidget *container, GqGtkCallback callback, gpointer callback_data)
{
	for (GtkWidget *child = gtk_widget_get_first_child(container);
	     child;
	     child = gtk_widget_get_next_sibling(child))
		{
		callback(child, callback_data);
		}
}

void gq_gtk_widget_destroy(GtkWidget *widget)
{
	if (!widget) return;

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

void gq_gtk_widget_set_border_width(GtkWidget *widget, guint width)
{
	gtk_widget_set_margin_top(widget, width);
	gtk_widget_set_margin_bottom(widget, width);
	gtk_widget_set_margin_start(widget, width);
	gtk_widget_set_margin_end(widget, width);
}

gboolean gq_gtk_icon_size_lookup(gint *width, gint *height)
{
	if (width)
		{
		*width = 16;
		}

	if (height)
		{
		*height = 16;
		}

	return TRUE;
}

GtkWidget *gq_gtk_image_new_from_stock(const gchar *stock_id, GtkIconSize size)
{
	(void)size;
	return gtk_image_new_from_icon_name(stock_id_to_icon_name(stock_id));
}

GtkWidget *gq_gtk_widget_get_focus_child(GtkWidget *widget)
{
	GtkRoot *root = gtk_widget_get_root(widget);
	if (!root) return nullptr;

	GtkWidget *focus = gtk_root_get_focus(root);
	for (GtkWidget *work = focus; work; work = gtk_widget_get_parent(work))
		{
		if (work == widget) return focus;
		}

	return nullptr;
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

void gq_gtk_viewport_set_shadow_type(GtkWidget *viewport, int type)
{
	if (type == GTK_SHADOW_NONE)
		{
		gtk_widget_remove_css_class(viewport, "frame");
		}
	else
		{
		gtk_widget_add_css_class(viewport, "frame");
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
