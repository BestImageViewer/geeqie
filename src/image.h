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

#include <functional>
#include <optional>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

enum StereoPixbufData : gint;

struct CollectInfo;
struct CollectionData;
struct ColorMan;
struct ColorManStatus;
class FileData;
struct ImageLoader;

enum AlterType : gint {
	ALTER_NONE,		/**< do nothing */
	ALTER_ROTATE_90,
	ALTER_ROTATE_90_CC,	/**< counterclockwise */
	ALTER_ROTATE_180,
	ALTER_MIRROR,
	ALTER_FLIP,
};

enum ImageState {
	IMAGE_STATE_NONE	= 0,
	IMAGE_STATE_IMAGE	= 1 << 0,
	IMAGE_STATE_LOADING	= 1 << 1,
	IMAGE_STATE_ERROR	= 1 << 2,
	IMAGE_STATE_COLOR_ADJ	= 1 << 3,
	IMAGE_STATE_ROTATE_AUTO	= 1 << 4,
	IMAGE_STATE_ROTATE_USER	= 1 << 5,
	IMAGE_STATE_DELAY_FLIP	= 1 << 6
};

struct ImageWindow
{
	GtkWidget *widget;	/**< use this to add it and show it */
	GtkWidget *pr;
	GtkWidget *frame;

	FileData *image_fd;

	gboolean unknown;		/**< failed to load image */

	ImageLoader *il;        /**< @FIXME image loader should probably go to FileData, but it must first support
				   sending callbacks to multiple ImageWindows in parallel */

	gint has_frame;  /**< not boolean, see image_new() */

	/* top level (not necessarily parent) window */
	gboolean top_window_sync;	/**< resize top_window when image dimensions change */
	GtkWidget *top_window;	/**< window that gets title, and window to resize when 'fitting' */
	gchar *title;		/**< window title to display left of file name */
	gchar *title_right;	/**< window title to display right of file name */
	gboolean title_show_zoom;	/**< option to include zoom in window title */

	gboolean completed;
	ImageState state;	/**< mask of IMAGE_STATE_* flags about current image */

	void (*func_update)(ImageWindow *imd, gpointer data);
	void (*func_complete)(ImageWindow *imd, gint preload, gpointer data);
	void (*func_state)(ImageWindow *imd, ImageState state, gpointer data);

	gpointer data_update;
	gpointer data_complete;
	gpointer data_state;

	/* button, scroll functions */
	void (*func_button)(ImageWindow *, GdkEventButton *event, gpointer);
	void (*func_drag)(ImageWindow *, GdkEventMotion *event, gdouble dx, gdouble dy, gpointer);
	void (*func_scroll)(ImageWindow *, GdkEventScroll *event, gpointer);
	void (*func_focus_in)(ImageWindow *, gpointer);

	gpointer data_button;
	gpointer data_drag;
	gpointer data_scroll;
	gpointer data_focus_in;

	/**
	 * @headerfile scroll_notify_func
	 * scroll notification (for scroll bar implementation)
	 */
	using ScrollNotifyFunc = std::function<void(ImageWindow *, gint x, gint y, gint width, gint height)>;
	ScrollNotifyFunc scroll_notify_func;

	/* collection info */
	CollectionData *collection;
	CollectInfo *collection_info;

	/* color profiles */
	gboolean color_profile_enable;
	gint color_profile_input;
	gboolean color_profile_use_image;
	ColorMan *cm;

	FileData *read_ahead_fd;
	ImageLoader *read_ahead_il;

	gint prev_color_row;

	gboolean auto_refresh;

	gboolean delay_flip;
	gint orientation;
	gboolean desaturate;
	gboolean overunderexposed;
	StereoPixbufData user_stereo;

	gboolean mouse_wheel_mode;
};

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
void image_set_complete_func(ImageWindow *imd,
			     void (*func)(ImageWindow *imd, gint preload, gpointer data),
			     gpointer data);
void image_set_state_func(ImageWindow *imd,
			  void (*func)(ImageWindow *imd, ImageState state, gpointer data),
			  gpointer data);

void image_select(ImageWindow *imd, bool select);
void image_set_selectable(ImageWindow *imd, gboolean selectable);

void image_grab_focus(ImageWindow *imd);
/* path, name */
const gchar *image_get_path(ImageWindow *imd);
const gchar *image_get_name(ImageWindow *imd);
FileData *image_get_fd(ImageWindow *imd);

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
void image_mousewheel_scroll(ImageWindow *imd, GdkScrollDirection direction);
void image_scroll(ImageWindow *imd, gint x, gint y);
void image_scroll_to_point(ImageWindow *imd, gint x, gint y,
			   gdouble x_align, gdouble y_align);
void image_get_scroll_center(ImageWindow *imd, gdouble *x, gdouble *y);
void image_set_scroll_center(ImageWindow *imd, gdouble x, gdouble y);
void image_alter_orientation(ImageWindow *imd, FileData *fd, AlterType type);
void image_set_desaturate(ImageWindow *imd, gboolean desaturate);
gboolean image_get_desaturate(ImageWindow *imd);
void image_set_overunderexposed(ImageWindow *imd, gboolean overunderexposed);
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
void image_stereo_set(ImageWindow *imd, gint stereo_mode);

StereoPixbufData image_stereo_pixbuf_get(const ImageWindow *imd);
void image_stereo_pixbuf_set(ImageWindow *imd, StereoPixbufData stereo_mode);

void image_prebuffer_set(ImageWindow *imd, FileData *fd);

void image_auto_refresh_enable(ImageWindow *imd, gboolean enable);

void image_top_window_set_sync(ImageWindow *imd, gboolean allow_sync);

/* background of image */
void image_background_set_color(ImageWindow *imd, GdkRGBA *color);
void image_background_set_color_from_options(ImageWindow *imd, gboolean fullscreen);

/* color profiles */
void image_color_profile_set(ImageWindow *imd, gint input_type, gboolean use_image);
gboolean image_color_profile_get(const ImageWindow *imd, gint &input_type, gboolean &use_image);
void image_color_profile_set_use(ImageWindow *imd, gboolean enable);
gboolean image_color_profile_get_use(ImageWindow *imd);
std::optional<ColorManStatus> image_color_profile_get_status(const ImageWindow *imd);

void image_set_delay_flip(ImageWindow *imd, gint delay);

void image_to_root_window(ImageWindow *imd, gboolean scaled);


void image_options_sync();

std::tuple<int, int, int, int> image_get_rectangle();
void image_update_title(ImageWindow *imd);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
