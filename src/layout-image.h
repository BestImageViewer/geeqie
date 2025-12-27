/*
 * Copyright (C) 2006 John Ellis
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

#ifndef LAYOUT_IMAGE_H
#define LAYOUT_IMAGE_H

#include <optional>

#include <glib.h>
#include <gtk/gtk.h>

enum AlterType : gint;
enum NotifyType : gint;
enum StereoPixbufData : gint;

struct CollectInfo;
struct CollectionData;
struct ColorManStatus;
class FileData;
struct LayoutWindow;

enum ImageSplitMode : gint {
	SPLIT_NONE = 0,
	SPLIT_VERT,
	SPLIT_HOR,
	SPLIT_TRIPLE,
	SPLIT_QUAD,
};

GtkWidget *layout_image_new(LayoutWindow *lw, gint i);
void layout_image_activate(LayoutWindow *lw, gint i, gboolean force);
GtkWidget *layout_image_setup_split(LayoutWindow *lw, ImageSplitMode mode);

void layout_image_set_fd(LayoutWindow *lw, FileData *fd);
void layout_image_set_with_ahead(LayoutWindow *lw, FileData *fd, FileData *read_ahead_fd);

void layout_image_set_index(LayoutWindow *lw, gint index);
void layout_image_set_collection(LayoutWindow *lw, CollectionData *cd, CollectInfo *info);

void layout_image_refresh(LayoutWindow *lw);

void layout_image_color_profile_set(LayoutWindow *lw, gint input_type, gboolean use_image);
gboolean layout_image_color_profile_get(LayoutWindow *lw, gint &input_type, gboolean &use_image);
void layout_image_color_profile_set_use(LayoutWindow *lw, gint enable);
gboolean layout_image_color_profile_get_use(LayoutWindow *lw);
std::optional<ColorManStatus> layout_image_color_profile_get_status(LayoutWindow *lw);


const gchar *layout_image_get_path(LayoutWindow *lw);
FileData *layout_image_get_fd(LayoutWindow *lw);
CollectionData *layout_image_get_collection(LayoutWindow *lw, CollectInfo **info);
gint layout_image_get_index(LayoutWindow *lw);


void layout_image_scroll(LayoutWindow *lw, gint x, gint y, gboolean connect_scroll);
void layout_image_zoom_adjust(LayoutWindow *lw, gdouble increment, gboolean connect_zoom);
void layout_image_zoom_adjust_at_point(LayoutWindow *lw, gdouble increment, gint x, gint y, gboolean connect_zoom);
void layout_image_zoom_set(LayoutWindow *lw, gdouble zoom, gboolean connect_zoom);
void layout_image_zoom_set_fill_geometry(LayoutWindow *lw, gboolean vertical, gboolean connect_zoom);
void layout_image_alter_orientation(LayoutWindow *lw, AlterType type);
void layout_image_set_desaturate(LayoutWindow *lw, gboolean desaturate);
gboolean layout_image_get_desaturate(LayoutWindow *lw);
void layout_image_set_overunderexposed(LayoutWindow *lw, gboolean overunderexposed);
void layout_image_set_ignore_alpha(LayoutWindow *lw, gboolean ignore_alpha);

void layout_image_rating(LayoutWindow *lw, const gchar *rating);

StereoPixbufData layout_image_stereo_pixbuf_get(LayoutWindow *lw);
void layout_image_stereo_pixbuf_set(LayoutWindow *lw, StereoPixbufData stereo_mode);

void layout_image_next(LayoutWindow *lw);
void layout_image_prev(LayoutWindow *lw);
void layout_image_first(LayoutWindow *lw);
void layout_image_last(LayoutWindow *lw);

void layout_image_menu_popup(LayoutWindow *lw);

void layout_image_to_root(LayoutWindow *lw);

void layout_image_full_screen_start(LayoutWindow *lw);
void layout_image_full_screen_stop(LayoutWindow *lw);
void layout_image_full_screen_toggle(LayoutWindow *lw);
gboolean layout_image_full_screen_active(LayoutWindow *lw);

void layout_image_slideshow_start(LayoutWindow *lw);
void layout_image_slideshow_start_from_list(LayoutWindow *lw, GList *list);
void layout_image_slideshow_stop(LayoutWindow *lw);
void layout_image_slideshow_toggle(LayoutWindow *lw);
gboolean layout_image_slideshow_active(LayoutWindow *lw);
void layout_image_slideshow_pause_toggle(LayoutWindow *lw);
gboolean layout_image_slideshow_paused(LayoutWindow *lw);

void layout_image_animate_toggle(LayoutWindow *lw);

void layout_image_notify_cb(FileData *fd, NotifyType type, gpointer data);
void layout_image_reset_orientation(LayoutWindow *lw);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
