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

#ifndef UI_TREE_EDIT_H
#define UI_TREE_EDIT_H


struct TreeEditData
{
	GtkWidget *window;
	GtkWidget *entry;

	gchar *old_name;
	gchar *new_name;

	gint (*edit_func)(TreeEditData *ted, const gchar *oldname, const gchar *newname, gpointer data);
	gpointer edit_data;

	GtkTreeView *tree;
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
};


/**
 * @headerfile  tree_edit_by_path
 * edit_func: return TRUE if rename successful, FALSE on failure.
 */
gboolean tree_edit_by_path(GtkTreeView *tree, GtkTreePath *tpath, gint column, const gchar *text,
		           gboolean (*edit_func)(TreeEditData *, const gchar *, const gchar *, gpointer), gpointer data);


/**
 * @headerfile tree_view_get_cell_origin
 * returns location of cell in screen coordinates
 */
gboolean tree_view_get_cell_origin(GtkTreeView *widget, GtkTreePath *tpath, gint column, gboolean text_cell_only,
			           gint *x, gint *y, gint *width, gint *height);

/**
 * @headerfile tree_view_get_cell_clamped
 * similar to above, but limits the returned area to that of the tree window
 */
void tree_view_get_cell_clamped(GtkTreeView *widget, GtkTreePath *tpath, gint column, gboolean text_cell_only,
			       gint *x, gint *y, gint *width, gint *height);

/**
 * @headerfile tree_view_row_get_visibility
 * return 0 = row visible, -1 = row is above, 1 = row is below visible region \n
 * if fully_visible is TRUE, the behavior changes to return -1/1 if _any_ part of the cell is out of view
 */
gint tree_view_row_get_visibility(GtkTreeView *widget, GtkTreeIter *iter, gboolean fully_visible);

/**
 * @headerfile tree_view_row_make_visible
 * scrolls to make row visible, if necessary
 * return is same as above (before the scroll)
 */
gint tree_view_row_make_visible(GtkTreeView *widget, GtkTreeIter *iter, gboolean center);

/**
 * @headerfile tree_view_move_cursor_away
 * if iter is location of cursor, moves cursor to nearest row
 */
gboolean tree_view_move_cursor_away(GtkTreeView *widget, GtkTreeIter *iter, gboolean only_selected);

/**
 * @headerfile tree_path_to_row
 * utility to return row position of given GtkTreePath
 */
gint tree_path_to_row(GtkTreePath *tpath);


/**
 * @headerfile shift_color
 * shifts a GdkColor values lighter or darker \n
 * val is percent from 1 to 100, or -1 for default (usually 10%) \n
 * direction is -1 darker, 0 auto, 1 lighter
 */
void shift_color(GdkColor *src, gshort val, gint direction);

/**
 * @headerfile style_shift_color
 * Shifts a style's color for given state
 * Useful for alternating dark/light rows in lists. \n
 *
 * shift_value is 1 to 100, representing the percent of the shift.
 */
void style_shift_color(GtkStyle *style, GtkStateType type, gshort shift_value, gint direction);

/**
 * @def STYLE_SHIFT_STANDARD
 * The standard shift percent for alternating list row colors
 */
#define STYLE_SHIFT_STANDARD 10

/**
 * @headerfile widget_auto_scroll_start
 * auto scroll, set scroll_speed or region_size to -1 to their respective the defaults
 * notify_func will be called before a scroll, return FALSE to turn off autoscroll
 */
gint widget_auto_scroll_start(GtkWidget *widget, GtkAdjustment *v_adj, gint scroll_speed, gint region_size,
			      gint (*notify_func)(GtkWidget *widget, gint x, gint y, gpointer data), gpointer notify_data);
void widget_auto_scroll_stop(GtkWidget *widget);


/*
 * Various g_list utils, do not really fit anywhere, so they are here.
 */
GList *uig_list_insert_link(GList *list, GList *link, gpointer data);
GList *uig_list_insert_list(GList *parent, GList *insert_link, GList *list);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
