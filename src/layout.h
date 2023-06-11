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

#ifndef LAYOUT_H
#define LAYOUT_H

#define LAYOUT_ID_CURRENT "_current_"

extern GList *layout_window_list;


LayoutWindow *layout_new(FileData *dir_fd, LayoutOptions *lop);
LayoutWindow *layout_new_with_geometry(FileData *dir_fd, LayoutOptions *lop,
				       const gchar *geometry);
LayoutWindow *layout_new_from_config(const gchar **attribute_names, const gchar **attribute_values, gboolean use_commandline);
void layout_update_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values);
LayoutWindow *layout_new_from_default();

void layout_close(LayoutWindow *lw);
void layout_free(LayoutWindow *lw);

gboolean layout_valid(LayoutWindow **lw);

void layout_show_config_window(LayoutWindow *lw);

void layout_apply_options(LayoutWindow *lw, LayoutOptions *lop);

void layout_sync_options_with_current_state(LayoutWindow *lw);
void layout_load_attributes(LayoutOptions *layout, const gchar **attribute_names, const gchar **attribute_values);
void layout_write_attributes(LayoutOptions *layout, GString *outstr, gint indent);
void layout_write_config(LayoutWindow *lw, GString *outstr, gint indent);


LayoutWindow *layout_find_by_image(ImageWindow *imd);
LayoutWindow *layout_find_by_image_fd(ImageWindow *imd);
LayoutWindow *layout_find_by_layout_id(const gchar *id);


const gchar *layout_get_path(LayoutWindow *lw);
gboolean layout_set_path(LayoutWindow *lw, const gchar *path);
gboolean layout_set_fd(LayoutWindow *lw, FileData *fd);

void layout_status_update_progress(LayoutWindow *lw, gdouble val, const gchar *text);
void layout_status_update_info(LayoutWindow *lw, const gchar *text);
void layout_status_update_image(LayoutWindow *lw);
void layout_status_update_all(LayoutWindow *lw);

GList *layout_list(LayoutWindow *lw);
guint layout_list_count(LayoutWindow *lw, gint64 *bytes);
FileData *layout_list_get_fd(LayoutWindow *lw, gint index);
gint layout_list_get_index(LayoutWindow *lw, FileData *fd);
void layout_list_sync_fd(LayoutWindow *lw, FileData *fd);

GList *layout_selection_list(LayoutWindow *lw);
/* return list of pointers to int for selection */
GList *layout_selection_list_by_index(LayoutWindow *lw);
guint layout_selection_count(LayoutWindow *lw, gint64 *bytes);
void layout_select_all(LayoutWindow *lw);
void layout_select_none(LayoutWindow *lw);
void layout_select_invert(LayoutWindow *lw);
void layout_select_list(LayoutWindow *lw, GList *list);

void layout_mark_to_selection(LayoutWindow *lw, gint mark, MarkToSelectionMode mode);
void layout_selection_to_mark(LayoutWindow *lw, gint mark, SelectionToMarkMode mode);

void layout_mark_filter_toggle(LayoutWindow *lw, gint mark);

void layout_refresh(LayoutWindow *lw);

void layout_thumb_set(LayoutWindow *lw, gboolean enable);
gboolean layout_thumb_get(LayoutWindow *lw);

void layout_marks_set(LayoutWindow *lw, gboolean enable);
gboolean layout_marks_get(LayoutWindow *lw);

void layout_file_filter_set(LayoutWindow *lw, gboolean enable);

void layout_sort_set(LayoutWindow *lw, SortType type, gboolean ascend);
gboolean layout_sort_get(LayoutWindow *lw, SortType *type, gboolean *ascend);

gboolean layout_geometry_get(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h);
gboolean layout_geometry_get_dividers(LayoutWindow *lw, gint *h, gint *v);

void layout_views_set(LayoutWindow *lw, DirViewType dir_view_type, FileViewType file_view_type);
gboolean layout_views_get(LayoutWindow *lw, DirViewType *dir_view_type, FileViewType *file_view_type);

void layout_views_set_sort(LayoutWindow *lw, SortType method, gboolean ascend);

void layout_status_update(LayoutWindow *lw, const gchar *text);

void layout_style_set(LayoutWindow *lw, gint style, const gchar *order);

void layout_menu_update_edit();
void layout_styles_update();
void layout_colors_update();


gboolean layout_geometry_get_tools(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h, gint *divider_pos);
void layout_tools_float_set(LayoutWindow *lw, gboolean popped, gboolean hidden);
gboolean layout_tools_float_get(LayoutWindow *lw, gboolean *popped, gboolean *hidden);

void layout_tools_float_toggle(LayoutWindow *lw);
void layout_tools_hide_toggle(LayoutWindow *lw);


void layout_toolbar_toggle(LayoutWindow *lw);
void layout_info_pixel_set(LayoutWindow *lw, gboolean show);

void layout_split_change(LayoutWindow *lw, ImageSplitMode mode);

void save_layout(LayoutWindow *lw);
gchar *layout_get_unique_id();
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
