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

#ifndef IMAGE_H
#define IMAGE_H


void image_set_frame(ImageWindow *imd, gboolean frame);
ImageWindow *image_new(gboolean frame);

/* additional setup */
void image_attach_window(ImageWindow *imd, GtkWidget *window,
			 const gchar *title, const gchar *title_right, gboolean show_zoom);
void image_set_update_func(ImageWindow *imd,
			   void (*func)(ImageWindow *imd, gpointer data),
			   gpointer data);
void image_set_button_func(ImageWindow *imd,
	void (*func)(ImageWindow *, GdkEventButton *event, gpointer),
	gpointer data);
void image_set_drag_func(ImageWindow *imd,
	void (*func)(ImageWindow *, GdkEventMotion *event, gdouble dx, gdouble dy, gpointer),
	gpointer data);
void image_set_scroll_func(ImageWindow *imd,
	void (*func)(ImageWindow *, GdkEventScroll *event, gpointer),
	gpointer data);
void image_set_focus_in_func(ImageWindow *imd,
	void (*func)(ImageWindow *, gpointer),
	gpointer data);
void image_set_scroll_notify_func(ImageWindow *imd,
				  void (*func)(ImageWindow *imd, gint x, gint y, gint width, gint height, gpointer data),
				  gpointer data);
void image_set_complete_func(ImageWindow *imd,
			     void (*func)(ImageWindow *imd, gint preload, gpointer data),
			     gpointer data);
void image_set_state_func(ImageWindow *imd,
			  void (*func)(ImageWindow *imd, ImageState state, gpointer data),
			  gpointer data);

void image_select(ImageWindow *imd, gboolean select);
void image_set_selectable(ImageWindow *imd, gboolean selectable);

void image_grab_focus(ImageWindow *imd);
/* path, name */
const gchar *image_get_path(ImageWindow *imd);
const gchar *image_get_name(ImageWindow *imd);
FileData *image_get_fd(ImageWindow *imd);

/**
 * @headerfile image_set_fd
 * merely changes path string, does not change the image!
 */
void image_set_fd(ImageWindow *imd, FileData *fd);

/* load a new image */
void image_change_fd(ImageWindow *imd, FileData *fd, gdouble zoom);
void image_change_pixbuf(ImageWindow *imd, GdkPixbuf *pixbuf, gdouble zoom, gboolean lazy);
void image_change_from_collection(ImageWindow *imd, CollectionData *cd, CollectInfo *info, gdouble zoom);
CollectionData *image_get_collection(ImageWindow *imd, CollectInfo **info);
void image_copy_from_image(ImageWindow *imd, ImageWindow *source);
void image_move_from_image(ImageWindow *imd, ImageWindow *source);

gboolean image_get_image_size(ImageWindow *imd, gint *width, gint *height);
GdkPixbuf *image_get_pixbuf(ImageWindow *imd);

/* manipulation */
void image_area_changed(ImageWindow *imd, gint x, gint y, gint width, gint height);
void image_reload(ImageWindow *imd);
void image_scroll(ImageWindow *imd, gint x, gint y);
void image_scroll_to_point(ImageWindow *imd, gint x, gint y,
			   gdouble x_align, gdouble y_align);
void image_get_scroll_center(ImageWindow *imd, gdouble *x, gdouble *y);
void image_set_scroll_center(ImageWindow *imd, gdouble x, gdouble y);
void image_alter_orientation(ImageWindow *imd, FileData *fd, AlterType type);
void image_set_desaturate(ImageWindow *imd, gboolean desaturate);
gboolean image_get_desaturate(ImageWindow *imd);
void image_set_overunderexposed(ImageWindow *imd, gboolean overunderexposed);
gboolean image_get_overunderexposed(ImageWindow *imd);
void image_set_ignore_alpha(ImageWindow *imd, gboolean ignore_alpha);

/* zoom */
void image_zoom_adjust(ImageWindow *imd, gdouble increment);
void image_zoom_adjust_at_point(ImageWindow *imd, gdouble increment, gint x, gint y);
void image_zoom_set_limits(ImageWindow *imd, gdouble min, gdouble max);
void image_zoom_set(ImageWindow *imd, gdouble zoom);
void image_zoom_set_fill_geometry(ImageWindow *imd, gboolean vertical);
gdouble image_zoom_get(ImageWindow *imd);
gdouble image_zoom_get_real(ImageWindow *imd);
gchar *image_zoom_get_as_text(ImageWindow *imd);
gdouble image_zoom_get_default(ImageWindow *imd);

/* stereo */
gint image_stereo_get(ImageWindow *imd);
void image_stereo_set(ImageWindow *imd, gint stereo_mode);
void image_stereo_swap(ImageWindow *imd);

StereoPixbufData image_stereo_pixbuf_get(ImageWindow *imd);
void image_stereo_pixbuf_set(ImageWindow *imd, StereoPixbufData stereo_mode);

/**
 * @headerfile image_prebuffer_set
 * read ahead, pass NULL to cancel
 */
void image_prebuffer_set(ImageWindow *imd, FileData *fd);

/**
 * @headerfile image_auto_refresh_enable
 * auto refresh
 */
void image_auto_refresh_enable(ImageWindow *imd, gboolean enable);

/**
 * @headerfile image_top_window_set_sync
 * allow top window to be resized ?
 */
void image_top_window_set_sync(ImageWindow *imd, gboolean allow_sync);

/* background of image */
void image_background_set_color(ImageWindow *imd, GdkColor *color);
void image_background_set_color_from_options(ImageWindow *imd, gboolean fullscreen);

/* color profiles */
void image_color_profile_set(ImageWindow *imd,
			     gint input_type,
			     gboolean use_image);
gboolean image_color_profile_get(ImageWindow *imd,
			     gint *input_type,
			     gboolean *use_image);
void image_color_profile_set_use(ImageWindow *imd, gboolean enable);
gboolean image_color_profile_get_use(ImageWindow *imd);
gboolean image_color_profile_get_status(ImageWindow *imd, gchar **image_profile, gchar **screen_profile);

/**
 * @headerfile image_set_delay_flip
 * set delayed page flipping
 */
void image_set_delay_flip(ImageWindow *imd, gint delay);

/**
 * @headerfile image_to_root_window
 * wallpaper util
 */
void image_to_root_window(ImageWindow *imd, gboolean scaled);



void image_set_image_as_tiles(ImageWindow *imd, gint width, gint height,
			      gint tile_width, gint tile_height, gint cache_size,
			      ImageTileRequestFunc func_tile_request,
			      ImageTileDisposeFunc func_tile_dispose,
			      gpointer data,
			      gdouble zoom);

/**
 * @headerfile image_options_sync
 * reset default options
 */
void image_options_sync();

void image_get_rectangle(gint *x1, gint *y1, gint *x2, gint *y2);
void image_update_title(ImageWindow *imd);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
