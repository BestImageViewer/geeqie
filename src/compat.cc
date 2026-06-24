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

#include "compat-deprecated.h"
#if HAVE_GTK4
#  include "main-defines.h"
#endif

namespace
{

#if HAVE_GTK4
constexpr auto GTK4_DRAG_SOURCE_CONTROLLER_DATA_KEY = "gq-gtk4-drag-source-controller";
constexpr auto GTK4_DROP_TARGET_CONTROLLER_DATA_KEY = "gq-gtk4-drop-target-controller";
constexpr auto GTK4_BOX_PACK_END_DATA_KEY = "gq-gtk4-box-pack-end";
constexpr auto GTK4_WINDOW_POSITION_DATA_KEY = "gq-gtk4-window-position";
constexpr auto GTK4_WINDOW_POSITION_POLICY_DATA_KEY = "gq-gtk4-window-position-policy";
constexpr auto GTK4_WINDOW_KEEP_ABOVE_DATA_KEY = "gq-gtk4-window-keep-above";

struct GqWindowPosition
{
	gint x;
	gint y;
};

guint start_button_mask_to_button(GdkModifierType start_button_mask)
{
	if (start_button_mask & GDK_BUTTON1_MASK) return GDK_BUTTON_PRIMARY;
	if (start_button_mask & GDK_BUTTON2_MASK) return GDK_BUTTON_MIDDLE;
	if (start_button_mask & GDK_BUTTON3_MASK) return GDK_BUTTON_SECONDARY;

	return 0;
}

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
#endif

} // namespace

#if HAVE_GTK4
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

gboolean gq_gtk_window_get_position(GtkWindow *window, gint *x, gint *y)
{
	auto *position = static_cast<GqWindowPosition *>(g_object_get_data(G_OBJECT(window), GTK4_WINDOW_POSITION_DATA_KEY));
	if (position)
		{
		if (x) *x = position->x;
		if (y) *y = position->y;
		return TRUE;
		}

	GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
	if (!surface)
		{
		return FALSE;
		}

	gint surface_x;
	gint surface_y;
	gdk_surface_get_position(surface, &surface_x, &surface_y);
	if (x) *x = surface_x;
	if (y) *y = surface_y;

	return TRUE;
}

void gq_gtk_window_move(GtkWindow *window, gint x, gint y)
{
	auto *position = g_new(GqWindowPosition, 1);
	position->x = x;
	position->y = y;
	g_object_set_data_full(G_OBJECT(window), GTK4_WINDOW_POSITION_DATA_KEY, position, g_free);

	if (gtk_widget_get_visible(GTK_WIDGET(window)))
		{
		gtk_window_present(window);
		}
}

void gq_gtk_window_set_keep_above(GtkWindow *window, gboolean setting)
{
	g_object_set_data(G_OBJECT(window), GTK4_WINDOW_KEEP_ABOVE_DATA_KEY, GINT_TO_POINTER(setting));

	if (setting && gtk_widget_get_visible(GTK_WIDGET(window)))
		{
		gtk_window_present(window);
		}
}

void gq_gtk_window_set_position(GtkWindow *window, GtkWindowPosition position)
{
	g_object_set_data(G_OBJECT(window), GTK4_WINDOW_POSITION_POLICY_DATA_KEY, GINT_TO_POINTER(static_cast<gint>(position)));

	switch (position)
		{
		case GTK_WIN_POS_CENTER:
		case GTK_WIN_POS_CENTER_ON_PARENT:
		case GTK_WIN_POS_MOUSE:
			if (gtk_widget_get_visible(GTK_WIDGET(window)))
				{
				gtk_window_present(window);
				}
			break;
		default:
			break;
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

void gq_gtk_frame_set_shadow_type(GtkFrame *frame, GtkShadowType type)
{
	if (type == GTK_SHADOW_NONE)
		{
		gtk_widget_remove_css_class(GTK_WIDGET(frame), "frame");
		}
	else
		{
		gtk_widget_add_css_class(GTK_WIDGET(frame), "frame");
		}
}

void gq_gtk_scrolled_window_set_shadow_type(GtkScrolledWindow *scrolled_window, GtkShadowType type)
{
	gtk_scrolled_window_set_has_frame(scrolled_window, type != GTK_SHADOW_NONE);
}

void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget)
{
	if (GTK_IS_BUTTON(container))
		{
		gtk_button_set_child(GTK_BUTTON(container), widget);
		}
	else if (GTK_IS_BUTTON_BOX(container))
		{
		gtk_box_set_child(GTK_BUTTON_BOX(container), widget);
		}
	else if (GTK_IS_EXPANDER(container))
		{
		gtk_expander_set_child(GTK_EXPANDER(container), widget);
		}
	else if (GTK_IS_FRAME(container))
		{
		gtk_frame_set_child(GTK_FRAME(container), widget);
		}
	else if (GTK_IS_MENU_ITEM(container))
		{
		gtk_frame_set_child(container, widget); /* @FIXME GTK4 menu */
		}
	else if (GTK_IS_POPOVER(container))
		{
		gtk_popover_set_child(GTK_POPOVER(container), widget);
		}
	else if (GTK_IS_TOGGLE_BUTTON(container))
		{
		gtk_toggle_button_set_child(GTK_TOGGLE_BUTTON(container), widget);
		}
	else if (GTK_IS_TOOLBAR(container))
		{
		gtk_toolbar_set_child(GTK_TOOLBAR(container), widget);
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

void gq_gtk_container_foreach(GtkWidget *container, GtkCallback callback, gpointer callback_data)
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

gboolean gq_gtk_icon_size_lookup(GtkIconSize size, gint *width, gint *height)
{
	gint dimension = 16;

	switch (size)
		{
		case GTK_ICON_SIZE_MENU:
		case GTK_ICON_SIZE_BUTTON:
			dimension = 16;
			break;
		default:
			break;
		}

	if (width) *width = dimension;
	if (height) *height = dimension;

	return TRUE;
}

GtkWidget *gq_gtk_image_new_from_stock(const gchar *stock_id, GtkIconSize size)
{
	(void)size;
	return gtk_image_new_from_icon_name(stock_id_to_icon_name(stock_id));
}

GtkWidget *gq_gtk_bin_get_child(GtkWidget *widget)
{
	return gtk_widget_get_first_child(widget);
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

gboolean gq_gtk_widget_event(GtkWidget *, GdkEvent *)
{
	static gsize warned = 0;

	if (g_once_init_enter(&warned))
		{
		g_warning("gq_gtk_widget_event() has no generic GTK4 event-dispatch equivalent; unexpected GTK4 call will be ignored");
		g_once_init_leave(&warned, 1);
		}

	return FALSE;
}

void gq_drag_g_signal_connect(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data)
{
	g_signal_connect(instance, detailed_signal, c_handler, data);
}

void gq_drag_g_signal_swapped(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data)
{
	g_signal_connect_swapped(instance, detailed_signal, c_handler, data);
}

void gq_gtk_drag_source_set(GtkWidget *widget, GdkModifierType start_button_mask, gpointer, gint n_targets, GdkDragAction actions)
{
	auto *controller = static_cast<GtkEventController *>(g_object_get_data(G_OBJECT(widget), GTK4_DRAG_SOURCE_CONTROLLER_DATA_KEY));
	if (controller)
		{
		gtk_widget_remove_controller(widget, controller);
		}

	auto *drag_source = gtk_drag_source_new();
	gtk_drag_source_set_actions(drag_source, actions);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_source), start_button_mask_to_button(start_button_mask));
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag_source));
	g_object_set_data(G_OBJECT(widget), GTK4_DRAG_SOURCE_CONTROLLER_DATA_KEY, drag_source);
}

void gq_gtk_drag_dest_set(GtkWidget *widget, gpointer, gpointer, gint n_targets, GdkDragAction actions)
{
	(void)n_targets;

	auto *controller = static_cast<GtkEventController *>(g_object_get_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY));
	if (controller)
		{
		gtk_widget_remove_controller(widget, controller);
		}

	auto *drop_target = gtk_drop_target_async_new(actions);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drop_target));
	g_object_set_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY, drop_target);
}

void gq_gtk_drag_dest_unset(GtkWidget *widget)
{
	auto *controller = static_cast<GtkEventController *>(g_object_get_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY));
	if (!controller) return;

	g_object_set_data(G_OBJECT(widget), GTK4_DROP_TARGET_CONTROLLER_DATA_KEY, nullptr);
	gtk_widget_remove_controller(widget, controller);
}

#else
gboolean gq_gtk_window_get_position(GtkWindow *window, gint *x, gint *y)
{
	GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
	if (!gdk_window)
		{
		return FALSE;
		}

	gint window_x;
	gint window_y;
	gdk_window_get_position(gdk_window, &window_x, &window_y);
	if (x) *x = window_x;
	if (y) *y = window_y;

	return TRUE;
}

void gq_gtk_window_move(GtkWindow *window, gint x, gint y)
{
	gtk_window_move(window, x, y);
}

void gq_gtk_window_set_keep_above(GtkWindow *window, gboolean setting)
{
	gtk_window_set_keep_above(window, setting);
}

void gq_gtk_window_set_position(GtkWindow *window, GtkWindowPosition position)
{
	gtk_window_set_position(window, position);
}

void gq_gtk_widget_show_all(GtkWidget *widget)
{
	gtk_widget_show_all(widget);
}

void gq_gtk_frame_set_shadow_type(GtkFrame *frame, GtkShadowType type)
{
	gtk_frame_set_shadow_type(frame, type);
}

void gq_gtk_scrolled_window_set_shadow_type(GtkScrolledWindow *scrolled_window, GtkShadowType type)
{
	gtk_scrolled_window_set_shadow_type(scrolled_window, type);
}

void gq_gtk_container_add(GtkWidget *container, GtkWidget *widget)
{
	gtk_container_add(GTK_CONTAINER(container), widget);
}

void gq_gtk_container_remove(GtkWidget *container, GtkWidget *widget)
{
	gtk_container_remove(GTK_CONTAINER(container), widget);
}

void gq_gtk_container_foreach(GtkWidget *container, GtkCallback callback, gpointer callback_data)
{
	gtk_container_foreach(GTK_CONTAINER(container), callback, callback_data);
}

void gq_gtk_widget_destroy(GtkWidget *widget)
{
	gtk_widget_destroy(widget);
}

void gq_gtk_widget_set_border_width(GtkWidget *widget, guint width)
{
	gtk_container_set_border_width(GTK_CONTAINER(widget), width);
}

gboolean gq_gtk_icon_size_lookup(GtkIconSize size, gint *width, gint *height)
{
	return gtk_icon_size_lookup(size, width, height);
}

GtkWidget *gq_gtk_image_new_from_stock(const gchar *stock_id, GtkIconSize size)
{
	return deprecated_gtk_image_new_from_stock(stock_id, size);
}

GtkWidget *gq_gtk_bin_get_child(GtkWidget *widget)
{
	return gtk_bin_get_child(GTK_BIN(widget));
}

GList *gq_gtk_widget_get_children(GtkWidget *widget)
{
	return gtk_container_get_children(GTK_CONTAINER(widget));
}

void gq_gtk_viewport_set_shadow_type(GtkWidget *viewport, int type)
{
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), static_cast<GtkShadowType>(type));
}

gboolean gq_gtk_widget_event(GtkWidget *widget, GdkEvent *event)
{
	return gtk_widget_event(widget, event);
}

void gq_drag_g_signal_connect(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data)
{
	g_signal_connect(instance, detailed_signal, c_handler, data);
}

void gq_drag_g_signal_swapped(GObject *instance, const gchar *detailed_signal, GCallback c_handler, gpointer data)
{
	g_signal_connect_swapped(instance, detailed_signal, c_handler, data);
}

void gq_gtk_drag_source_set(GtkWidget *widget, GdkModifierType start_button_mask, const GtkTargetEntry *targets, gint n_targets, GdkDragAction actions)
{
	gtk_drag_source_set(widget, start_button_mask, targets, n_targets, actions);
}

void gq_gtk_drag_dest_set(GtkWidget *widget, GtkDestDefaults flags, const GtkTargetEntry *targets, gint n_targets, GdkDragAction actions)
{
	gtk_drag_dest_set(widget, flags, targets, n_targets, actions);
}

void gq_gtk_drag_dest_unset(GtkWidget *widget)
{
	gtk_drag_dest_unset(widget);
}

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
