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

#include "ui-tree-edit.h"

#include <cstring>

#include <glib-object.h>

#include "compat.h"
#include "misc.h"
#include "ui-misc.h"

/*
 *-------------------------------------------------------------------
 * cell popup editor
 *-------------------------------------------------------------------
 */

static void tree_edit_close(TreeEditData *ted)
{
	gtk_grab_remove(ted->window);
	gdk_keyboard_ungrab(GDK_CURRENT_TIME);
	gdk_pointer_ungrab(GDK_CURRENT_TIME);

	gq_gtk_widget_destroy(ted->window);

	g_free(ted->old_name);
	g_free(ted->new_name);
	gtk_tree_path_free(ted->path);

	g_free(ted);
}

static void tree_edit_do(TreeEditData *ted)
{
	ted->new_name = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(ted->entry)));

	if (strcmp(ted->new_name, ted->old_name) != 0)
		{
		if (ted->edit_func)
			{
			if (ted->edit_func(ted, ted->old_name, ted->new_name, ted->edit_data))
				{
				/* hmm, should the caller be required to set text instead ? */
				}
			}
		}
}

static gboolean tree_edit_click_end_cb(GtkWidget *, GdkEventButton *, gpointer data)
{
	auto ted = static_cast<TreeEditData *>(data);

	tree_edit_do(ted);
	tree_edit_close(ted);

	return TRUE;
}

static gboolean tree_edit_click_cb(GtkWidget *, GdkEventButton *event, gpointer data)
{
	auto ted = static_cast<TreeEditData *>(data);
	GdkWindow *window = gtk_widget_get_window(ted->window);

	auto xr = static_cast<gint>(event->x_root);
	auto yr = static_cast<gint>(event->y_root);

	if (!window_received_event(window, {xr, yr}))
		{
		/* gobble the release event, so it does not propgate to an underlying widget */
		g_signal_connect(G_OBJECT(ted->window), "button_release_event",
				 G_CALLBACK(tree_edit_click_end_cb), ted);
		return TRUE;
		}
	return FALSE;
}

static gboolean tree_edit_key_press_cb(GtkWidget *, GdkEventKey *event, gpointer data)
{
	auto ted = static_cast<TreeEditData *>(data);

	switch (event->keyval)
		{
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
		case GDK_KEY_Tab: 		/* ok, we are going to intercept the focus change
					   from keyboard and act like return was hit */
		case GDK_KEY_ISO_Left_Tab:
		case GDK_KEY_Up:
		case GDK_KEY_Down:
		case GDK_KEY_KP_Up:
		case GDK_KEY_KP_Down:
		case GDK_KEY_KP_Left:
		case GDK_KEY_KP_Right:
			tree_edit_do(ted);
			tree_edit_close(ted);
			break;
		case GDK_KEY_Escape:
			tree_edit_close(ted);
			break;
		default:
			break;
		}

	return FALSE;
}

static gboolean tree_edit_by_path_idle_cb(gpointer data)
{
	auto ted = static_cast<TreeEditData *>(data);
	GdkRectangle rect;
	gint x;
	gint y;
	gint w;
	gint h;	/* geometry of cell within tree */
	gint wx;
	gint wy;		/* geometry of tree from root window */
	gint sx;
	gint sw;

	gtk_tree_view_get_cell_area(ted->tree, ted->path, ted->column, &rect);

	x = rect.x;
	y = rect.y;
	w = rect.width + 4;
	h = rect.height + 4;

	if (gtk_tree_view_column_cell_get_position(ted->column, ted->cell, &sx, &sw))
		{
		x += sx;
		w = std::max(w - sx, sw);
		}

	gdk_window_get_origin(gtk_widget_get_window(gtk_widget_get_parent(GTK_WIDGET(ted->tree))), &wx, &wy);

	x += wx - 2; /* the -val is to 'fix' alignment of entry position */
	y += wy - 2;

	/* now show it */
	gtk_widget_set_size_request(ted->window, w, h);
	gtk_widget_realize(ted->window);
	gq_gtk_window_move(GTK_WINDOW(ted->window), x, y);
	gtk_window_resize(GTK_WINDOW(ted->window), w, h);
	gtk_widget_show(ted->window);

	/* grab it */
	gtk_widget_grab_focus(ted->entry);
	/* explicitly set the focus flag for the entry, for some reason on popup windows this
	 * is not set, and causes no edit cursor to appear ( popups not allowed focus? )
	 */
	gtk_widget_grab_focus(ted->entry);
	gtk_grab_add(ted->window);
	gdk_pointer_grab(gtk_widget_get_window(ted->window), TRUE,
			 static_cast<GdkEventMask>(GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_MOTION_MASK),
			 nullptr, nullptr, GDK_CURRENT_TIME);
	gdk_keyboard_grab(gtk_widget_get_window(ted->window), TRUE, GDK_CURRENT_TIME);

	return G_SOURCE_REMOVE;
}

/**
 * @brief edit_func: return TRUE if rename successful, FALSE on failure.
 */
gboolean tree_edit_by_path(GtkTreeView *tree, GtkTreePath *tpath, gint column, const gchar *text,
		           gboolean (*edit_func)(TreeEditData *, const gchar *, const gchar *, gpointer), gpointer data)
{
	TreeEditData *ted;
	GtkTreeViewColumn *tcolumn;
	GtkCellRenderer *cell = nullptr;
	GList *list;
	GList *work;

	if (!edit_func) return FALSE;
	if (!gtk_widget_get_visible(GTK_WIDGET(tree))) return FALSE;

	tcolumn = gtk_tree_view_get_column(tree, column);
	if (!tcolumn) return FALSE;

	list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(tcolumn));
	work = list;
	while (work && !cell)
		{
		cell = static_cast<GtkCellRenderer *>(work->data);
		if (!GTK_IS_CELL_RENDERER_TEXT(cell))
			{
			cell = nullptr;
			}
		work = work->next;
		}

	g_list_free(list);
	if (!cell) return FALSE;

	if (!text) text = "";

	ted = g_new0(TreeEditData, 1);

	ted->old_name = g_strdup(text);

	ted->edit_func = edit_func;
	ted->edit_data = data;

	ted->tree = tree;
	ted->path = gtk_tree_path_copy(tpath);
	ted->column = tcolumn;
	ted->cell = cell;

	gtk_tree_view_scroll_to_cell(ted->tree, ted->path, ted->column, TRUE, 0.5, 0.0);

	/* create the window */

	ted->window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_resizable(GTK_WINDOW(ted->window), FALSE);
	g_signal_connect(G_OBJECT(ted->window), "button_press_event",
			 G_CALLBACK(tree_edit_click_cb), ted);
	g_signal_connect(G_OBJECT(ted->window), "key_press_event",
			 G_CALLBACK(tree_edit_key_press_cb), ted);

	ted->entry = gtk_entry_new();
	gq_gtk_entry_set_text(GTK_ENTRY(ted->entry), ted->old_name);
	gtk_editable_select_region(GTK_EDITABLE(ted->entry), 0, strlen(ted->old_name));
	gq_gtk_container_add(GTK_WIDGET(ted->window), ted->entry);
	gtk_widget_show(ted->entry);

	/* due to the fact that gtktreeview scrolls in an idle loop, we cannot
	 * reliably get the cell position until those scroll priority signals are processed
	 */
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE - 2, tree_edit_by_path_idle_cb, ted, nullptr);

	return TRUE;
}

/*
 *-------------------------------------------------------------------
 * tree cell position retrieval
 *-------------------------------------------------------------------
 */

/**
 * @brief return 0 = row visible, -1 = row is above, 1 = row is below visible region \n
 * if fully_visible is TRUE, the behavior changes to return -1/1 if _any_ part of the cell is out of view
 * An implementation that uses gtk_tree_view_get_visible_range
 */
gint tree_view_row_get_visibility(GtkTreeView *widget, GtkTreeIter *iter, gboolean fully_visible)
{
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreePath *start_path;
	GtkTreePath *end_path;
	gint ret = 0;

	if (!gtk_tree_view_get_visible_range(widget, &start_path, &end_path)) return -1; /* we will most probably scroll down, needed for tree_view_row_make_visible */

	store = gtk_tree_view_get_model(widget);
	tpath = gtk_tree_model_get_path(store, iter);

	if (fully_visible)
		{
		if (gtk_tree_path_compare(tpath, start_path) <= 0)
			{
			ret = -1;
			}
		else if (gtk_tree_path_compare(tpath, end_path) >= 0)
			{
			ret = 1;
			}
		}
	else
		{
		if (gtk_tree_path_compare(tpath, start_path) < 0)
			{
			ret = -1;
			}
		else if (gtk_tree_path_compare(tpath, end_path) > 0)
			{
			ret = 1;
			}
		}

	gtk_tree_path_free(tpath);
	gtk_tree_path_free(start_path);
	gtk_tree_path_free(end_path);
	return ret;
}

/**
 * @brief Scrolls to make row visible, if necessary
 * return is same as above (before the scroll)
 */
gint tree_view_row_make_visible(GtkTreeView *widget, GtkTreeIter *iter, gboolean center)
{
	GtkTreePath *tpath;
	gint vis;

	vis = tree_view_row_get_visibility(widget, iter, TRUE);

	tpath = gtk_tree_model_get_path(gtk_tree_view_get_model(widget), iter);
	if (center && vis != 0)
		{
		gtk_tree_view_scroll_to_cell(widget, tpath, nullptr, TRUE, 0.5, 0.0);
		}
	else if (vis < 0)
		{
		gtk_tree_view_scroll_to_cell(widget, tpath, nullptr, TRUE, 0.0, 0.0);
		}
	else if (vis > 0)
		{
		gtk_tree_view_scroll_to_cell(widget, tpath, nullptr, TRUE, 1.0, 0.0);
		}
	gtk_tree_path_free(tpath);

	return vis;
}

/**
 * @brief If iter is location of cursor, moves cursor to nearest row
 */
gboolean tree_view_move_cursor_away(GtkTreeView *widget, GtkTreeIter *iter, gboolean only_selected)
{
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreePath *fpath;
	gboolean move = FALSE;

	if (!iter) return FALSE;

	store = gtk_tree_view_get_model(widget);
	tpath = gtk_tree_model_get_path(store, iter);
	gtk_tree_view_get_cursor(widget, &fpath, nullptr);

	if (fpath && gtk_tree_path_compare(tpath, fpath) == 0)
		{
		GtkTreeSelection *selection;

		selection = gtk_tree_view_get_selection(widget);

		if (!only_selected ||
		    gtk_tree_selection_path_is_selected(selection, tpath))
			{
			GtkTreeIter current;

			current = *iter;
			if (gtk_tree_model_iter_next(store, &current))
				{
				gtk_tree_path_next(tpath);
				move = TRUE;
				}
			else if (gtk_tree_path_prev(tpath) &&
				 gtk_tree_model_get_iter(store, &current, tpath))
				{
				move = TRUE;
				}

			if (move)
				{
				gtk_tree_view_set_cursor(widget, tpath, nullptr, FALSE);
				}
			}
		}

	gtk_tree_path_free(tpath);
	if (fpath) gtk_tree_path_free(fpath);

	return move;
}


/*
 *-------------------------------------------------------------------
 * color utilities
 *-------------------------------------------------------------------
 */

/**
 * @brief Shifts a GdkRGBA values lighter or darker \n
 * val is percent from 1 to 100, or -1 for default (usually 10%) \n
 * direction is -1 darker, 0 auto, 1 lighter
 */
void shift_color(GdkRGBA *src, gshort val, gint direction)
{
	gdouble cs;

	if (val == -1)
		{
		val = STYLE_SHIFT_STANDARD;
		}
	else
		{
		val = CLAMP(val, 1, 100);
		}

	cs = 1.0 / 100 * val;

	/* up or down ? */
	if (direction < 0 || (direction == 0 &&(src->red + src->green + src->blue) / 3 > 1.0 / 2))
		{
		src->red = std::max(0.0, src->red - cs);
		src->green = std::max(0.0, src->green - cs);
		src->blue = std::max(0.0, src->blue - cs);
		}
	else
		{
		src->red = std::min(1.0, src->red + cs);
		src->green = std::min(1.0, src->green + cs);
		src->blue = std::min(1.0, src->blue + cs);
		}
}

/*
 *-------------------------------------------------------------------
 * auto scroll by mouse position
 *-------------------------------------------------------------------
 */

enum {
	AUTO_SCROLL_DEFAULT_SPEED = 100,
	AUTO_SCROLL_DEFAULT_REGION = 20
};

struct AutoScrollData
{
	guint timer_id; /* event source id */
	gint region_size;
	GtkWidget *widget;
	GtkAdjustment *adj;
	gint max_step;

	gint (*notify_func)(GtkWidget *, gint, gint, gpointer);
	gpointer notify_data;
};

void widget_auto_scroll_stop(GtkWidget *widget)
{
	AutoScrollData *sd;

	sd = static_cast<AutoScrollData *>(g_object_get_data(G_OBJECT(widget), "autoscroll"));
	if (!sd) return;
	g_object_set_data(G_OBJECT(widget), "autoscroll", nullptr);

	if (sd->timer_id) g_source_remove(sd->timer_id);
	g_free(sd);
}

static gboolean widget_auto_scroll_cb(gpointer data)
{
	auto sd = static_cast<AutoScrollData *>(data);
	gint amt = 0;

	if (sd->max_step < sd->region_size)
		{
		sd->max_step = std::min(sd->region_size, sd->max_step + 2);
		}

	GdkWindow *window = gtk_widget_get_window(sd->widget);

	GdkPoint pos;
	if (!window_get_pointer_position(window, pos))
		{
		sd->timer_id = 0;
		widget_auto_scroll_stop(sd->widget);
		return FALSE;
		}

	gint h = gdk_window_get_height(window);

	if (h < sd->region_size * 3)
		{
		/* height is cramped, nicely divide into three equal regions */
		if (pos.y < h / 3 || pos.y > h / 3 * 2)
			{
			amt = (pos.y < h / 2) ? 0 - ((h / 2) - pos.y) : pos.y - (h / 2);
			}
		}
	else if (pos.y < sd->region_size)
		{
		amt = 0 - (sd->region_size - pos.y);
		}
	else if (pos.y >= h - sd->region_size)
		{
		amt = pos.y - (h - sd->region_size);
		}

	if (amt != 0)
		{
		amt = CLAMP(amt, 0 - sd->max_step, sd->max_step);

		if (gtk_adjustment_get_value(sd->adj) != CLAMP(gtk_adjustment_get_value(sd->adj) + amt, gtk_adjustment_get_lower(sd->adj), gtk_adjustment_get_upper(sd->adj) - gtk_adjustment_get_page_size(sd->adj)))
			{
			/* only notify when scrolling is needed */
			if (sd->notify_func && !sd->notify_func(sd->widget, pos.x, pos.y, sd->notify_data))
				{
				sd->timer_id = 0;
				widget_auto_scroll_stop(sd->widget);
				return FALSE;
				}

			gtk_adjustment_set_value(sd->adj,
				CLAMP(gtk_adjustment_get_value(sd->adj) + amt, gtk_adjustment_get_lower(sd->adj), gtk_adjustment_get_upper(sd->adj) - gtk_adjustment_get_page_size(sd->adj)));
			}
		}

	return TRUE;
}

/**
 * @brief Auto scroll, set scroll_speed or region_size to -1 to their respective the defaults
 * notify_func will be called before a scroll, return FALSE to turn off autoscroll
 */
gint widget_auto_scroll_start(GtkWidget *widget, GtkAdjustment *v_adj, gint scroll_speed, gint region_size,
			      gint (*notify_func)(GtkWidget *widget, gint x, gint y, gpointer data), gpointer notify_data)
{
	AutoScrollData *sd;

	if (!widget || !v_adj) return 0;
	if (g_object_get_data(G_OBJECT(widget), "autoscroll")) return 0;
	if (scroll_speed < 1) scroll_speed = AUTO_SCROLL_DEFAULT_SPEED;
	if (region_size < 1) region_size = AUTO_SCROLL_DEFAULT_REGION;

	sd = g_new0(AutoScrollData, 1);
	sd->widget = widget;
	sd->adj = v_adj;
	sd->region_size = region_size;
	sd->max_step = 1;
	sd->timer_id = g_timeout_add(scroll_speed, widget_auto_scroll_cb, sd);

	sd->notify_func = notify_func;
	sd->notify_data = notify_data;

	g_object_set_data(G_OBJECT(widget), "autoscroll", sd);

	return scroll_speed;
}


/*
 *-------------------------------------------------------------------
 * GList utils
 *-------------------------------------------------------------------
 */

GList *uig_list_insert_link(GList *list, GList *link, gpointer data)
{
	GList *new_list;

	if (!list || link == list) return g_list_prepend(list, data);
	if (!link) return g_list_append(list, data);

	new_list = g_list_alloc();
	new_list->data = data;

	if (link->prev)
		{
		link->prev->next = new_list;
		new_list->prev = link->prev;
		}
	else
		{
		list = new_list;
		}
	link->prev = new_list;
	new_list->next = link;

	return list;
}

GList *uig_list_insert_list(GList *parent, GList *insert_link, GList *list)
{
	GList *end;

	if (!insert_link) return g_list_concat(parent, list);
	if (insert_link == parent) return g_list_concat(list, parent);
	if (!parent) return list;
	if (!list) return parent;

	end  = g_list_last(list);

	if (insert_link->prev) insert_link->prev->next = list;
	list->prev = insert_link->prev;
	insert_link->prev = end;
	end->next = insert_link;

	return parent;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
