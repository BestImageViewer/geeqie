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

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "main.h"
#include "pixbuf-renderer.h"

#include "renderer-tiles.h"

/* comment this out if not using this from within Geeqie
 * defining GQ_BUILD does these things:
 *   - Sets the shift-click scroller pixbuf to a nice icon instead of a black box
 */
#define GQ_BUILD 1

#ifdef GQ_BUILD
#include "main.h"
#include "pixbuf-util.h"
#include "exif.h"
#else
typedef enum {
	EXIF_ORIENTATION_UNKNOWN	= 0,
	EXIF_ORIENTATION_TOP_LEFT	= 1,
	EXIF_ORIENTATION_TOP_RIGHT	= 2,
	EXIF_ORIENTATION_BOTTOM_RIGHT	= 3,
	EXIF_ORIENTATION_BOTTOM_LEFT	= 4,
	EXIF_ORIENTATION_LEFT_TOP	= 5,
	EXIF_ORIENTATION_RIGHT_TOP	= 6,
	EXIF_ORIENTATION_RIGHT_BOTTOM	= 7,
	EXIF_ORIENTATION_LEFT_BOTTOM	= 8
} ExifOrientationType;
#endif


/* default min and max zoom */
#define PR_ZOOM_MIN -32.0
#define PR_ZOOM_MAX 32.0

/* distance to drag mouse to disable image flip */
#define PR_DRAG_SCROLL_THRESHHOLD 4

/* increase pan rate when holding down shift */
#define PR_PAN_SHIFT_MULTIPLIER 6

/* scroller config */
#define PR_SCROLLER_UPDATES_PER_SEC 30
#define PR_SCROLLER_DEAD_ZONE 6

/* when scaling image to below this size, use nearest pixel for scaling
 * (below about 4, the other scale types become slow generating their conversion tables)
 */
#define PR_MIN_SCALE_SIZE 8

enum {
	SIGNAL_ZOOM = 0,
	SIGNAL_CLICKED,
	SIGNAL_SCROLL_NOTIFY,
	SIGNAL_RENDER_COMPLETE,
	SIGNAL_DRAG,
	SIGNAL_UPDATE_PIXEL,
	SIGNAL_COUNT
};

enum {
	PROP_0,
	PROP_ZOOM_MIN,
	PROP_ZOOM_MAX,
	PROP_ZOOM_QUALITY,
	PROP_ZOOM_2PASS,
	PROP_ZOOM_EXPAND,
	PROP_SCROLL_RESET,
	PROP_DELAY_FLIP,
	PROP_LOADING,
	PROP_COMPLETE,
	PROP_CACHE_SIZE_DISPLAY,
	PROP_CACHE_SIZE_TILES,
	PROP_WINDOW_FIT,
	PROP_WINDOW_LIMIT,
	PROP_WINDOW_LIMIT_VALUE,
	PROP_AUTOFIT_LIMIT,
	PROP_AUTOFIT_LIMIT_VALUE,
	PROP_ENLARGEMENT_LIMIT_VALUE
};

typedef enum {
	PR_ZOOM_NONE		= 0,
	PR_ZOOM_FORCE 		= 1 << 0,
	PR_ZOOM_NEW		= 1 << 1,
	PR_ZOOM_CENTER		= 1 << 2,
	PR_ZOOM_INVALIDATE	= 1 << 3,
	PR_ZOOM_LAZY		= 1 << 4  /* wait with redraw for pixbuf_renderer_area_changed */
} PrZoomFlags;

static guint signals[SIGNAL_COUNT] = { 0 };
static GtkEventBoxClass *parent_class = NULL;



static void pixbuf_renderer_class_init(PixbufRendererClass *renderer_class);
static void pixbuf_renderer_init(PixbufRenderer *pr);
static void pixbuf_renderer_finalize(GObject *object);
static void pixbuf_renderer_set_property(GObject *object, guint prop_id,
					 const GValue *value, GParamSpec *pspec);
static void pixbuf_renderer_get_property(GObject *object, guint prop_id,
					 GValue *value, GParamSpec *pspec);
static void pr_scroller_timer_set(PixbufRenderer *pr, gboolean start);


static void pr_source_tile_free_all(PixbufRenderer *pr);

static void pr_zoom_sync(PixbufRenderer *pr, gdouble zoom,
			 PrZoomFlags flags, gint px, gint py);

static void pr_signals_connect(PixbufRenderer *pr);
static void pr_size_cb(GtkWidget *widget, GtkAllocation *allocation, gpointer data);
static void pr_stereo_temp_disable(PixbufRenderer *pr, gboolean disable);


/*
 *-------------------------------------------------------------------
 * Pixbuf Renderer object
 *-------------------------------------------------------------------
 */

static void pixbuf_renderer_class_init_wrapper(void *g_class, void *UNUSED(class_data))
{
	pixbuf_renderer_class_init(static_cast<PixbufRendererClass *>(g_class));
}

static void pixbuf_renderer_init_wrapper(PixbufRenderer *pr, void *UNUSED(class_data))
{
	pixbuf_renderer_init(pr);
}

GType pixbuf_renderer_get_type(void)
{
	static GType pixbuf_renderer_type = 0;

	if (!pixbuf_renderer_type)
		{
		static const GTypeInfo pixbuf_renderer_info =
			{
			sizeof(PixbufRendererClass), /* class_size */
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			static_cast<GClassInitFunc>(pixbuf_renderer_class_init_wrapper),
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof(PixbufRenderer), /* instance_size */
			0,		/* n_preallocs */
			reinterpret_cast<GInstanceInitFunc>(pixbuf_renderer_init_wrapper), /* instance_init */
			NULL,		/* value_table */
			};

		pixbuf_renderer_type = g_type_register_static(GTK_TYPE_EVENT_BOX, "PixbufRenderer",
							      &pixbuf_renderer_info, GTypeFlags(0));
		}

	return pixbuf_renderer_type;
}

static void pixbuf_renderer_class_init(PixbufRendererClass *renderer_class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(renderer_class);

	parent_class = static_cast<GtkEventBoxClass *>(g_type_class_peek_parent(renderer_class));

	gobject_class->set_property = pixbuf_renderer_set_property;
	gobject_class->get_property = pixbuf_renderer_get_property;

	gobject_class->finalize = pixbuf_renderer_finalize;

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_MIN,
					g_param_spec_double("zoom_min",
							    "Zoom minimum",
							    NULL,
							    -1000.0,
							    1000.0,
							    PR_ZOOM_MIN,
							    static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_MAX,
					g_param_spec_double("zoom_max",
							    "Zoom maximum",
							    NULL,
							    -1000.0,
							    1000.0,
							    PR_ZOOM_MIN,
							    static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_QUALITY,
					g_param_spec_uint("zoom_quality",
							  "Zoom quality",
							  NULL,
							  GDK_INTERP_NEAREST,
							  GDK_INTERP_BILINEAR,
							  GDK_INTERP_BILINEAR,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_2PASS,
					g_param_spec_boolean("zoom_2pass",
							     "2 pass zoom",
							     NULL,
							     TRUE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_EXPAND,
					g_param_spec_boolean("zoom_expand",
							     "Expand image in autozoom.",
							     NULL,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property(gobject_class,
					PROP_SCROLL_RESET,
					g_param_spec_uint("scroll_reset",
							  "New image scroll reset",
							  NULL,
							  PR_SCROLL_RESET_TOPLEFT,
							  PR_SCROLL_RESET_NOCHANGE,
							  PR_SCROLL_RESET_TOPLEFT,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_DELAY_FLIP,
					g_param_spec_boolean("delay_flip",
							     "Delay image update",
							     NULL,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_LOADING,
					g_param_spec_boolean("loading",
							     "Image actively loading",
							     NULL,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_COMPLETE,
					g_param_spec_boolean("complete",
							     "Image rendering complete",
							     NULL,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_CACHE_SIZE_DISPLAY,
					g_param_spec_uint("cache_display",
							  "Display cache size MiB",
							  NULL,
							  0,
							  128,
							  PR_CACHE_SIZE_DEFAULT,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_CACHE_SIZE_TILES,
					g_param_spec_uint("cache_tiles",
							  "Tile cache count",
							  "Number of tiles to retain in memory at any one time.",
							  0,
							  256,
							  PR_CACHE_SIZE_DEFAULT,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_WINDOW_FIT,
					g_param_spec_boolean("window_fit",
							     "Fit window to image size",
							     NULL,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_WINDOW_LIMIT,
					g_param_spec_boolean("window_limit",
							     "Limit size of parent window",
							     NULL,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_WINDOW_LIMIT_VALUE,
					g_param_spec_uint("window_limit_value",
							  "Size limit of parent window",
							  NULL,
							  10,
							  150,
							  100,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_AUTOFIT_LIMIT,
					g_param_spec_boolean("autofit_limit",
							     "Limit size of image when autofitting",
							     NULL,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_AUTOFIT_LIMIT_VALUE,
					g_param_spec_uint("autofit_limit_value",
							  "Size limit of image when autofitting",
							  NULL,
							  10,
							  150,
							  100,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ENLARGEMENT_LIMIT_VALUE,
					g_param_spec_uint("enlargement_limit_value",
							  "Size increase limit of image when autofitting",
							  NULL,
							  100,
							  999,
							  500,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));


	signals[SIGNAL_ZOOM] =
		g_signal_new("zoom",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, zoom),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__DOUBLE,
			     G_TYPE_NONE, 1,
			     G_TYPE_DOUBLE);

	signals[SIGNAL_CLICKED] =
		g_signal_new("clicked",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, clicked),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__BOXED,
			     G_TYPE_NONE, 1,
			     GDK_TYPE_EVENT);

	signals[SIGNAL_SCROLL_NOTIFY] =
		g_signal_new("scroll-notify",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, scroll_notify),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);

	signals[SIGNAL_RENDER_COMPLETE] =
		g_signal_new("render-complete",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, render_complete),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);

	signals[SIGNAL_DRAG] =
		g_signal_new("drag",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, drag),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__BOXED,
			     G_TYPE_NONE, 1,
			     GDK_TYPE_EVENT);

	signals[SIGNAL_UPDATE_PIXEL] =
		g_signal_new("update-pixel",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, update_pixel),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);
}

static RendererFuncs *pr_backend_renderer_new(PixbufRenderer *pr)
{
	//~ if (options->image.use_clutter_renderer && !options->disable_gpu)
		//~ {
//~ #ifdef HAVE_CLUTTER
		//~ return renderer_clutter_new(pr);
//~ #else
		//~ log_printf("Warning: Geeqie is built without clutter renderer support");
//~ #endif
		//~ }
	return renderer_tiles_new(pr);
}


static void pixbuf_renderer_init(PixbufRenderer *pr)
{
	GtkWidget *box;

	box = GTK_WIDGET(pr);

	pr->zoom_min = PR_ZOOM_MIN;
	pr->zoom_max = PR_ZOOM_MAX;
	pr->zoom_quality = GDK_INTERP_BILINEAR;
	pr->zoom_2pass = FALSE;

	pr->zoom = 1.0;
	pr->scale = 1.0;
	pr->aspect_ratio = 1.0;

	pr->scroll_reset = PR_SCROLL_RESET_TOPLEFT;

	pr->scroller_id = 0;
	pr->scroller_overlay = -1;

	pr->x_mouse = -1;
	pr->y_mouse = -1;

	pr->source_tiles_enabled = FALSE;
	pr->source_tiles = NULL;

	pr->orientation = 1;

	pr->norm_center_x = 0.5;
	pr->norm_center_y = 0.5;

	pr->stereo_mode = PR_STEREO_NONE;

	pr->color.red =0;
	pr->color.green =0;
	pr->color.blue =0;

	pr->renderer = pr_backend_renderer_new(pr);

	pr->renderer2 = NULL;

	gtk_widget_set_double_buffered(box, FALSE);
	gtk_widget_set_app_paintable(box, TRUE);
	g_signal_connect_after(G_OBJECT(box), "size_allocate",
			       G_CALLBACK(pr_size_cb), pr);

	pr_signals_connect(pr);
}

static void pixbuf_renderer_finalize(GObject *object)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(object);

	pr->renderer->free(pr->renderer);
	if (pr->renderer2) pr->renderer2->free(pr->renderer2);


	if (pr->pixbuf) g_object_unref(pr->pixbuf);

	pr_scroller_timer_set(pr, FALSE);

	pr_source_tile_free_all(pr);
}

PixbufRenderer *pixbuf_renderer_new(void)
{
	return static_cast<PixbufRenderer *>(g_object_new(TYPE_PIXBUF_RENDERER, NULL));
}

static void pixbuf_renderer_set_property(GObject *object, guint prop_id,
					 const GValue *value, GParamSpec *pspec)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(object);

	switch (prop_id)
		{
		case PROP_ZOOM_MIN:
			pr->zoom_min = g_value_get_double(value);
			break;
		case PROP_ZOOM_MAX:
			pr->zoom_max = g_value_get_double(value);
			break;
		case PROP_ZOOM_QUALITY:
			pr->zoom_quality = static_cast<GdkInterpType>(g_value_get_uint(value));
			break;
		case PROP_ZOOM_2PASS:
			pr->zoom_2pass = g_value_get_boolean(value);
			break;
		case PROP_ZOOM_EXPAND:
			pr->zoom_expand = g_value_get_boolean(value);
			break;
		case PROP_SCROLL_RESET:
			pr->scroll_reset = static_cast<PixbufRendererScrollResetType>(g_value_get_uint(value));
			break;
		case PROP_DELAY_FLIP:
			pr->delay_flip = g_value_get_boolean(value);
			break;
		case PROP_LOADING:
			pr->loading = g_value_get_boolean(value);
			break;
		case PROP_COMPLETE:
			pr->complete = g_value_get_boolean(value);
			break;
		case PROP_CACHE_SIZE_DISPLAY:
//			pr->tile_cache_max = g_value_get_uint(value);
			break;
		case PROP_CACHE_SIZE_TILES:
			pr->source_tiles_cache_size = g_value_get_uint(value);
			break;
		case PROP_WINDOW_FIT:
			pr->window_fit = g_value_get_boolean(value);
			break;
		case PROP_WINDOW_LIMIT:
			pr->window_limit = g_value_get_boolean(value);
			break;
		case PROP_WINDOW_LIMIT_VALUE:
			pr->window_limit_size = g_value_get_uint(value);
			break;
		case PROP_AUTOFIT_LIMIT:
			pr->autofit_limit = g_value_get_boolean(value);
			break;
		case PROP_AUTOFIT_LIMIT_VALUE:
			pr->autofit_limit_size = g_value_get_uint(value);
			break;
		case PROP_ENLARGEMENT_LIMIT_VALUE:
			pr->enlargement_limit_size = g_value_get_uint(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
		}
}

static void pixbuf_renderer_get_property(GObject *object, guint prop_id,
					 GValue *value, GParamSpec *pspec)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(object);

	switch (prop_id)
		{
		case PROP_ZOOM_MIN:
			g_value_set_double(value, pr->zoom_min);
			break;
		case PROP_ZOOM_MAX:
			g_value_set_double(value, pr->zoom_max);
			break;
		case PROP_ZOOM_QUALITY:
			g_value_set_uint(value, pr->zoom_quality);
			break;
		case PROP_ZOOM_2PASS:
			g_value_set_boolean(value, pr->zoom_2pass);
			break;
		case PROP_ZOOM_EXPAND:
			g_value_set_boolean(value, pr->zoom_expand);
			break;
		case PROP_SCROLL_RESET:
			g_value_set_uint(value, pr->scroll_reset);
			break;
		case PROP_DELAY_FLIP:
			g_value_set_boolean(value, pr->delay_flip);
			break;
		case PROP_LOADING:
			g_value_set_boolean(value, pr->loading);
			break;
		case PROP_COMPLETE:
			g_value_set_boolean(value, pr->complete);
			break;
		case PROP_CACHE_SIZE_DISPLAY:
//			g_value_set_uint(value, pr->tile_cache_max);
			break;
		case PROP_CACHE_SIZE_TILES:
			g_value_set_uint(value, pr->source_tiles_cache_size);
			break;
		case PROP_WINDOW_FIT:
			g_value_set_boolean(value, pr->window_fit);
			break;
		case PROP_WINDOW_LIMIT:
			g_value_set_boolean(value, pr->window_limit);
			break;
		case PROP_WINDOW_LIMIT_VALUE:
			g_value_set_uint(value, pr->window_limit_size);
			break;
		case PROP_AUTOFIT_LIMIT:
			g_value_set_boolean(value, pr->autofit_limit);
			break;
		case PROP_AUTOFIT_LIMIT_VALUE:
			g_value_set_uint(value, pr->autofit_limit_size);
			break;
		case PROP_ENLARGEMENT_LIMIT_VALUE:
			g_value_set_uint(value, pr->enlargement_limit_size);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
		}
}


/*
 *-------------------------------------------------------------------
 * misc utilities
 *-------------------------------------------------------------------
 */

static void widget_set_cursor(GtkWidget *widget, gint icon)
{
	GdkCursor *cursor;

	if (!gtk_widget_get_window(widget)) return;

	if (icon == -1)
		{
		cursor = NULL;
		}
	else
		{
		cursor = gdk_cursor_new(static_cast<GdkCursorType>(icon));
		}

	gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);

	if (cursor) g_object_unref(G_OBJECT(cursor));
}

gboolean pr_clip_region(gint x, gint y, gint w, gint h,
			       gint clip_x, gint clip_y, gint clip_w, gint clip_h,
			       gint *rx, gint *ry, gint *rw, gint *rh)
{
	if (clip_x + clip_w <= x ||
	    clip_x >= x + w ||
	    clip_y + clip_h <= y ||
	    clip_y >= y + h)
		{
		return FALSE;
		}

	*rx = MAX(x, clip_x);
	*rw = MIN((x + w), (clip_x + clip_w)) - *rx;

	*ry = MAX(y, clip_y);
	*rh = MIN((y + h), (clip_y + clip_h)) - *ry;

	return TRUE;
}

static gboolean pr_parent_window_sizable(PixbufRenderer *pr)
{
	GdkWindowState state;

	if (!pr->parent_window) return FALSE;
	if (!pr->window_fit) return FALSE;
	if (!gtk_widget_get_window(GTK_WIDGET(pr))) return FALSE;

	if (!gtk_widget_get_window(pr->parent_window)) return FALSE;
	state = gdk_window_get_state(gtk_widget_get_window(pr->parent_window));
	if (state & GDK_WINDOW_STATE_MAXIMIZED) return FALSE;

	return TRUE;
}

static gboolean pr_parent_window_resize(PixbufRenderer *pr, gint w, gint h)
{
	GtkWidget *widget;
	GtkWidget *parent;
	gint ww, wh;
	GtkAllocation widget_allocation;
	GtkAllocation parent_allocation;

	if (!pr_parent_window_sizable(pr)) return FALSE;

	if (pr->window_limit)
		{
		gint sw = gdk_screen_width() * pr->window_limit_size / 100;
		gint sh = gdk_screen_height() * pr->window_limit_size / 100;

		if (w > sw) w = sw;
		if (h > sh) h = sh;
		}

	widget = GTK_WIDGET(pr);
	parent = GTK_WIDGET(pr->parent_window);

	gtk_widget_get_allocation(widget, &widget_allocation);
	gtk_widget_get_allocation(parent, &parent_allocation);

	w += (parent_allocation.width - widget_allocation.width);
	h += (parent_allocation.height - widget_allocation.height);

	ww = gdk_window_get_width(gtk_widget_get_window(parent));
	wh = gdk_window_get_height(gtk_widget_get_window(parent));
	if (w == ww && h == wh) return FALSE;

	gdk_window_resize(gtk_widget_get_window(parent), w, h);

	return TRUE;
}

void pixbuf_renderer_set_parent(PixbufRenderer *pr, GtkWindow *window)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	g_return_if_fail(window == NULL || GTK_IS_WINDOW(window));

	pr->parent_window = GTK_WIDGET(window);
}

//GtkWindow *pixbuf_renderer_get_parent(PixbufRenderer *pr)
//{
	//g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), NULL);

	//return GTK_WINDOW(pr->parent_window);
//}


/*
 *-------------------------------------------------------------------
 * overlays
 *-------------------------------------------------------------------
 */


gint pixbuf_renderer_overlay_add(PixbufRenderer *pr, GdkPixbuf *pixbuf, gint x, gint y,
				 OverlayRendererFlags flags)
{
	/* let's assume both renderers returns the same value */
	if (pr->renderer2) pr->renderer2->overlay_add(pr->renderer2, pixbuf, x, y, flags);
	return pr->renderer->overlay_add(pr->renderer, pixbuf, x, y, flags);
}

void pixbuf_renderer_overlay_set(PixbufRenderer *pr, gint id, GdkPixbuf *pixbuf, gint x, gint y)
{
	pr->renderer->overlay_set(pr->renderer, id, pixbuf, x, y);
	if (pr->renderer2) pr->renderer2->overlay_set(pr->renderer2, id, pixbuf, x, y);
}

gboolean pixbuf_renderer_overlay_get(PixbufRenderer *pr, gint id, GdkPixbuf **pixbuf, gint *x, gint *y)
{
	if (pr->renderer2) pr->renderer2->overlay_get(pr->renderer2, id, pixbuf, x, y);
	return pr->renderer->overlay_get(pr->renderer, id, pixbuf, x, y);
}

void pixbuf_renderer_overlay_remove(PixbufRenderer *pr, gint id)
{
	pr->renderer->overlay_set(pr->renderer, id, NULL, 0, 0);
	if (pr->renderer2) pr->renderer2->overlay_set(pr->renderer2, id, NULL, 0, 0);
}

/*
 *-------------------------------------------------------------------
 * scroller overlay
 *-------------------------------------------------------------------
 */


static gboolean pr_scroller_update_cb(gpointer data)
{
	auto pr = static_cast<PixbufRenderer *>(data);
	gint x, y;
	gint xinc, yinc;

	/* this was a simple scroll by difference between scroller and mouse position,
	 * but all this math results in a smoother result and accounts for a dead zone.
	 */

	if (abs(pr->scroller_xpos - pr->scroller_x) < PR_SCROLLER_DEAD_ZONE)
		{
		x = 0;
		}
	else
		{
		gint shift = PR_SCROLLER_DEAD_ZONE / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		x = (pr->scroller_xpos - pr->scroller_x) / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		x += (x > 0) ? -shift : shift;
		}

	if (abs(pr->scroller_ypos - pr->scroller_y) < PR_SCROLLER_DEAD_ZONE)
		{
		y = 0;
		}
	else
		{
		gint shift = PR_SCROLLER_DEAD_ZONE / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		y = (pr->scroller_ypos - pr->scroller_y) / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		y += (y > 0) ? -shift : shift;
		}

	if (abs(x) < PR_SCROLLER_DEAD_ZONE * PR_SCROLLER_UPDATES_PER_SEC)
		{
		xinc = x;
		}
	else
		{
		xinc = pr->scroller_xinc;

		if (x >= 0)
			{
			if (xinc < 0) xinc = 0;
			if (x < xinc) xinc = x;
			if (x > xinc) xinc = MIN(xinc + x / PR_SCROLLER_UPDATES_PER_SEC, x);
			}
		else
			{
			if (xinc > 0) xinc = 0;
			if (x > xinc) xinc = x;
			if (x < xinc) xinc = MAX(xinc + x / PR_SCROLLER_UPDATES_PER_SEC, x);
			}
		}

	if (abs(y) < PR_SCROLLER_DEAD_ZONE * PR_SCROLLER_UPDATES_PER_SEC)
		{
		yinc = y;
		}
	else
		{
		yinc = pr->scroller_yinc;

		if (y >= 0)
			{
			if (yinc < 0) yinc = 0;
			if (y < yinc) yinc = y;
			if (y > yinc) yinc = MIN(yinc + y / PR_SCROLLER_UPDATES_PER_SEC, y);
			}
		else
			{
			if (yinc > 0) yinc = 0;
			if (y > yinc) yinc = y;
			if (y < yinc) yinc = MAX(yinc + y / PR_SCROLLER_UPDATES_PER_SEC, y);
			}
		}

	pr->scroller_xinc = xinc;
	pr->scroller_yinc = yinc;

	xinc = xinc / PR_SCROLLER_UPDATES_PER_SEC;
	yinc = yinc / PR_SCROLLER_UPDATES_PER_SEC;

	pixbuf_renderer_scroll(pr, xinc, yinc);

	return TRUE;
}

static void pr_scroller_timer_set(PixbufRenderer *pr, gboolean start)
{
	if (pr->scroller_id)
		{
		g_source_remove(pr->scroller_id);
		pr->scroller_id = 0;
		}

	if (start)
		{
		pr->scroller_id = g_timeout_add(1000 / PR_SCROLLER_UPDATES_PER_SEC,
						pr_scroller_update_cb, pr);
		}
}

static void pr_scroller_start(PixbufRenderer *pr, gint x, gint y)
{
	if (pr->scroller_overlay == -1)
		{
		GdkPixbuf *pixbuf;
		gint w, h;

#ifdef GQ_BUILD
		gchar *resource_path;

		resource_path = g_build_filename(GQ_RESOURCE_PATH_ICONS, PIXBUF_INLINE_SCROLLER ".png", NULL);

		pixbuf = gdk_pixbuf_new_from_resource(resource_path, NULL);
		g_free(resource_path);
#else
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
		gdk_pixbuf_fill(pixbuf, 0x000000ff);
#endif
		w = gdk_pixbuf_get_width(pixbuf);
		h = gdk_pixbuf_get_height(pixbuf);

		pr->scroller_overlay = pixbuf_renderer_overlay_add(pr, pixbuf, x - w / 2, y - h / 2, OVL_NORMAL);
		g_object_unref(pixbuf);
		}

	pr->scroller_x = x;
	pr->scroller_y = y;
	pr->scroller_xpos = x;
	pr->scroller_ypos = y;

	pr_scroller_timer_set(pr, TRUE);
}

static void pr_scroller_stop(PixbufRenderer *pr)
{
	if (!pr->scroller_id) return;

	pixbuf_renderer_overlay_remove(pr, pr->scroller_overlay);
	pr->scroller_overlay = -1;

	pr_scroller_timer_set(pr, FALSE);
}

/*
 *-------------------------------------------------------------------
 * borders
 *-------------------------------------------------------------------
 */

void pixbuf_renderer_set_color(PixbufRenderer *pr, GdkColor *color)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	if (color)
		{
		pr->color.red = color->red;
		pr->color.green = color->green;
		pr->color.blue = color->blue;
		}
	else
		{
		pr->color.red = 0;
		pr->color.green = 0;
		pr->color.blue = 0;
		}

	pr->renderer->update_viewport(pr->renderer);
	if (pr->renderer2) pr->renderer2->update_viewport(pr->renderer2);
}

/*
 *-------------------------------------------------------------------
 * source tiles
 *-------------------------------------------------------------------
 */

static void pr_source_tile_free(SourceTile *st)
{
	if (!st) return;

	if (st->pixbuf) g_object_unref(st->pixbuf);
	g_free(st);
}

static void pr_source_tile_free_all(PixbufRenderer *pr)
{
	GList *work;

	work = pr->source_tiles;
	while (work)
		{
		SourceTile *st;

		st = static_cast<SourceTile *>(work->data);
		work = work->next;

		pr_source_tile_free(st);
		}

	g_list_free(pr->source_tiles);
	pr->source_tiles = NULL;
}

static void pr_source_tile_unset(PixbufRenderer *pr)
{
	pr_source_tile_free_all(pr);
	pr->source_tiles_enabled = FALSE;
}

static gboolean pr_source_tile_visible(PixbufRenderer *pr, SourceTile *st)
{
	gint x1, y1, x2, y2;

	if (!st) return FALSE;

//	x1 = ROUND_DOWN(pr->x_scroll, pr->tile_width);
//	y1 = ROUND_DOWN(pr->y_scroll, pr->tile_height);
//	x2 = ROUND_UP(pr->x_scroll + pr->vis_width, pr->tile_width);
//	y2 = ROUND_UP(pr->y_scroll + pr->vis_height, pr->tile_height);
	x1 = pr->x_scroll;
	y1 = pr->y_scroll;
	x2 = pr->x_scroll + pr->vis_width;
	y2 = pr->y_scroll + pr->vis_height;

	return !(static_cast<gdouble>(st->x) * pr->scale > static_cast<gdouble>(x2) ||
		 static_cast<gdouble>(st->x + pr->source_tile_width) * pr->scale < static_cast<gdouble>(x1) ||
		 static_cast<gdouble>(st->y) * pr->scale > static_cast<gdouble>(y2) ||
		 static_cast<gdouble>(st->y + pr->source_tile_height) * pr->scale < static_cast<gdouble>(y1));
}

static SourceTile *pr_source_tile_new(PixbufRenderer *pr, gint x, gint y)
{
	SourceTile *st = NULL;
	gint count;

	g_return_val_if_fail(pr->source_tile_width >= 1 && pr->source_tile_height >= 1, NULL);

	if (pr->source_tiles_cache_size < 4) pr->source_tiles_cache_size = 4;

	count = g_list_length(pr->source_tiles);
	if (count >= pr->source_tiles_cache_size)
		{
		GList *work;

		work = g_list_last(pr->source_tiles);
		while (work && count >= pr->source_tiles_cache_size)
			{
			SourceTile *needle;

			needle = static_cast<SourceTile *>(work->data);
			work = work->prev;

			if (!pr_source_tile_visible(pr, needle))
				{
				pr->source_tiles = g_list_remove(pr->source_tiles, needle);

				if (pr->func_tile_dispose)
					{
					pr->func_tile_dispose(pr, needle->x, needle->y,
							      pr->source_tile_width, pr->source_tile_height,
							      needle->pixbuf, pr->func_tile_data);
					}

				if (!st)
					{
					st = needle;
					}
				else
					{
					pr_source_tile_free(needle);
					}

				count--;
				}
			}
		}

	if (!st)
		{
		st = g_new0(SourceTile, 1);
		st->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
					    pr->source_tile_width, pr->source_tile_height);
		}

	st->x = ROUND_DOWN(x, pr->source_tile_width);
	st->y = ROUND_DOWN(y, pr->source_tile_height);
	st->blank = TRUE;

	pr->source_tiles = g_list_prepend(pr->source_tiles, st);

	return st;
}

static SourceTile *pr_source_tile_request(PixbufRenderer *pr, gint x, gint y)
{
	SourceTile *st;

	st = pr_source_tile_new(pr, x, y);
	if (!st) return NULL;

	if (pr->func_tile_request &&
	    pr->func_tile_request(pr, st->x, st->y,
				   pr->source_tile_width, pr->source_tile_height, st->pixbuf, pr->func_tile_data))
		{
		st->blank = FALSE;
		}

	pr->renderer->invalidate_region(pr->renderer, st->x * pr->scale, st->y * pr->scale,
				  pr->source_tile_width * pr->scale, pr->source_tile_height * pr->scale);
	if (pr->renderer2) pr->renderer2->invalidate_region(pr->renderer2, st->x * pr->scale, st->y * pr->scale,
				  pr->source_tile_width * pr->scale, pr->source_tile_height * pr->scale);
	return st;
}

static SourceTile *pr_source_tile_find(PixbufRenderer *pr, gint x, gint y)
{
	GList *work;

	work = pr->source_tiles;
	while (work)
		{
		auto st = static_cast<SourceTile *>(work->data);

		if (x >= st->x && x < st->x + pr->source_tile_width &&
		    y >= st->y && y < st->y + pr->source_tile_height)
			{
			if (work != pr->source_tiles)
				{
				pr->source_tiles = g_list_remove_link(pr->source_tiles, work);
				pr->source_tiles = g_list_concat(work, pr->source_tiles);
				}
			return st;
			}

		work = work->next;
		}

	return NULL;
}

GList *pr_source_tile_compute_region(PixbufRenderer *pr, gint x, gint y, gint w, gint h, gboolean request)
{
	gint x1, y1;
	GList *list = NULL;
	gint sx, sy;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (w > pr->image_width) w = pr->image_width;
	if (h > pr->image_height) h = pr->image_height;

	sx = ROUND_DOWN(x, pr->source_tile_width);
	sy = ROUND_DOWN(y, pr->source_tile_height);

	for (x1 = sx; x1 < x + w; x1+= pr->source_tile_width)
		{
		for (y1 = sy; y1 < y + h; y1 += pr->source_tile_height)
			{
			SourceTile *st;

			st = pr_source_tile_find(pr, x1, y1);
			if (!st && request) st = pr_source_tile_request(pr, x1, y1);

			if (st) list = g_list_prepend(list, st);
			}
		}

	return g_list_reverse(list);
}

static void pr_source_tile_changed(PixbufRenderer *pr, gint x, gint y, gint width, gint height)
{
	GList *work;

	if (width < 1 || height < 1) return;

	work = pr->source_tiles;
	while (work)
		{
		SourceTile *st;
		gint rx, ry, rw, rh;

		st = static_cast<SourceTile *>(work->data);
		work = work->next;

		if (pr_clip_region(st->x, st->y, pr->source_tile_width, pr->source_tile_height,
				   x, y, width, height,
				   &rx, &ry, &rw, &rh))
			{
			GdkPixbuf *pixbuf;

			pixbuf = gdk_pixbuf_new_subpixbuf(st->pixbuf, rx - st->x, ry - st->y, rw, rh);
			if (pr->func_tile_request &&
			    pr->func_tile_request(pr, rx, ry, rw, rh, pixbuf, pr->func_tile_data))
				{
					pr->renderer->invalidate_region(pr->renderer, rx * pr->scale, ry * pr->scale,
							      rw * pr->scale, rh * pr->scale);
					if (pr->renderer2) pr->renderer2->invalidate_region(pr->renderer2, rx * pr->scale, ry * pr->scale,
								rw * pr->scale, rh * pr->scale);
				}
			g_object_unref(pixbuf);
			}
		}
}

void pixbuf_renderer_set_tiles(PixbufRenderer *pr, gint width, gint height,
			       gint tile_width, gint tile_height, gint cache_size,
			       PixbufRendererTileRequestFunc func_request,
			       PixbufRendererTileDisposeFunc func_dispose,
			       gpointer user_data,
			       gdouble zoom)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	g_return_if_fail(tile_width >= 32 && tile_height >= 32);
	g_return_if_fail(width >= 32 && height > 32);
	g_return_if_fail(func_request != NULL);

	if (pr->pixbuf) g_object_unref(pr->pixbuf);
	pr->pixbuf = NULL;

	pr_source_tile_unset(pr);

	if (cache_size < 4) cache_size = 4;

	pr->source_tiles_enabled = TRUE;
	pr->source_tiles_cache_size = cache_size;
	pr->source_tile_width = tile_width;
	pr->source_tile_height = tile_height;

	pr->image_width = width;
	pr->image_height = height;

	pr->func_tile_request = func_request;
	pr->func_tile_dispose = func_dispose;
	pr->func_tile_data = user_data;

	pr_stereo_temp_disable(pr, TRUE);
	pr_zoom_sync(pr, zoom, static_cast<PrZoomFlags>(PR_ZOOM_FORCE | PR_ZOOM_NEW), 0, 0);
}

void pixbuf_renderer_set_tiles_size(PixbufRenderer *pr, gint width, gint height)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	g_return_if_fail(width >= 32 && height > 32);

	if (!pr->source_tiles_enabled) return;
	if (pr->image_width == width && pr->image_height == height) return;

	pr->image_width = width;
	pr->image_height = height;

	pr_zoom_sync(pr, pr->zoom, PR_ZOOM_FORCE, 0, 0);
}

gint pixbuf_renderer_get_tiles(PixbufRenderer *pr)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);

	return pr->source_tiles_enabled;
}

static void pr_zoom_adjust_real(PixbufRenderer *pr, gdouble increment,
				PrZoomFlags flags, gint x, gint y)
{
	gdouble zoom = pr->zoom;

	if (increment == 0.0) return;

	if (zoom == 0.0)
		{
		if (pr->scale < 1.0)
			{
			zoom = 0.0 - 1.0 / pr->scale;
			}
		else
			{
			zoom = pr->scale;
			}
		}

	if (options->image.zoom_style == ZOOM_GEOMETRIC)
		{
		if (increment < 0.0)
			{
			if (zoom >= 1.0)
				{
				if (zoom / -(increment - 1.0) < 1.0)
					{
					zoom = 1.0 / (zoom / (increment - 1.0));
					}
				else
					{
					zoom = zoom / -(increment - 1.0) ;
					}
				}
			else
				{
				zoom = zoom * -(increment - 1.0);
				}
			}
		else
			{
			if (zoom <= -1.0 )
				{
				if (zoom / (increment + 1.0) > -1.0)
					{
					zoom = -(1.0 / (zoom / (increment + 1.0)));
					}
				else
					{
					zoom = zoom / (increment + 1.0) ;
					}
				}
			else
				{
				zoom = zoom * (increment + 1.0);
				}
			}
		}
	else
		{
		if (increment < 0.0)
			{
			if (zoom >= 1.0 && zoom + increment < 1.0)
				{
				zoom = zoom + increment - 2.0;
				}
			else
				{
				zoom = zoom + increment;
				}
			}
		else
			{
			if (zoom <= -1.0 && zoom + increment > -1.0)
				{
				zoom = zoom + increment + 2.0;
				}
			else
				{
				zoom = zoom + increment;
				}
			}
		}

	pr_zoom_sync(pr, zoom, flags, x, y);
}


/*
 *-------------------------------------------------------------------
 * signal emission
 *-------------------------------------------------------------------
 */

static void pr_update_signal(PixbufRenderer *pr)
{
	DEBUG_1("%s pixbuf renderer updated - started drawing %p, img: %dx%d", get_exec_time(), (void *)pr, pr->image_width, pr->image_height);
	pr->debug_updated = TRUE;
}

static void pr_zoom_signal(PixbufRenderer *pr)
{
	g_signal_emit(pr, signals[SIGNAL_ZOOM], 0, pr->zoom);
}

static void pr_clicked_signal(PixbufRenderer *pr, GdkEventButton *bevent)
{
	g_signal_emit(pr, signals[SIGNAL_CLICKED], 0, bevent);
}

static void pr_scroll_notify_signal(PixbufRenderer *pr)
{
	g_signal_emit(pr, signals[SIGNAL_SCROLL_NOTIFY], 0);
}

void pr_render_complete_signal(PixbufRenderer *pr)
{
	if (!pr->complete)
		{
		g_signal_emit(pr, signals[SIGNAL_RENDER_COMPLETE], 0);
		g_object_set(G_OBJECT(pr), "complete", TRUE, NULL);
		}
	if (pr->debug_updated)
		{
		DEBUG_1("%s pixbuf renderer done %p", get_exec_time(), (void *)pr);
		pr->debug_updated = FALSE;
		}
}

static void pr_drag_signal(PixbufRenderer *pr, GdkEventMotion *event)
{
	g_signal_emit(pr, signals[SIGNAL_DRAG], 0, event);
}

static void pr_update_pixel_signal(PixbufRenderer *pr)
{
	g_signal_emit(pr, signals[SIGNAL_UPDATE_PIXEL], 0);
}

/*
 *-------------------------------------------------------------------
 * sync and clamp
 *-------------------------------------------------------------------
 */


void pr_tile_coords_map_orientation(gint orientation,
				     gdouble tile_x, gdouble tile_y, /* coordinates of the tile */
				     gdouble image_w, gdouble image_h,
				     gdouble tile_w, gdouble tile_h,
				     gdouble *res_x, gdouble *res_y)
{
	*res_x = tile_x;
	*res_y = tile_y;
	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			/* normal -- nothing to do */
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			*res_x = image_w - tile_x - tile_w;
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			*res_x = image_w - tile_x - tile_w;
			*res_y = image_h - tile_y - tile_h;
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			*res_y = image_h - tile_y - tile_h;
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			*res_x = tile_y;
			*res_y = tile_x;
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			*res_x = tile_y;
			*res_y = image_w - tile_x - tile_w;
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			*res_x = image_h - tile_y - tile_h;
			*res_y = image_w - tile_x - tile_w;
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			*res_x = image_h - tile_y - tile_h;
			*res_y = tile_x;
			break;
		default:
			/* The other values are out of range */
			break;
		}
//	log_printf("tile coord y:%f, ih:%d, th:%f ry:%f\n", tile_y, image_h, tile_h, *res_x);
}

void pr_tile_region_map_orientation(gint orientation,
				     gint area_x, gint area_y, /* coordinates of the area inside tile */
				     gint tile_w, gint tile_h,
				     gint area_w, gint area_h,
				     gint *res_x, gint *res_y,
				     gint *res_w, gint *res_h)
{
	*res_x = area_x;
	*res_y = area_y;
	*res_w = area_w;
	*res_h = area_h;

	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			/* normal -- nothing to do */
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			*res_x = tile_w - area_x - area_w;
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			*res_x = tile_w - area_x - area_w;
			*res_y = tile_h - area_y - area_h;
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			*res_y = tile_h - area_y - area_h;
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			*res_x = area_y;
			*res_y = area_x;
			*res_w = area_h;
			*res_h = area_w;
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			*res_x = area_y;
			*res_y = tile_w - area_x - area_w;
			*res_w = area_h;
			*res_h = area_w;
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			*res_x = tile_h - area_y - area_h;
			*res_y = tile_w - area_x - area_w;
			*res_w = area_h;
			*res_h = area_w;
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			*res_x = tile_h - area_y - area_h;
			*res_y = area_x;
			*res_w = area_h;
			*res_h = area_w;
			break;
		default:
			/* The other values are out of range */
			break;
		}
//	log_printf("inside y:%d, th:%d, ah:%d ry:%d\n", area_y, tile_h, area_h, *res_x);
}

void pr_coords_map_orientation_reverse(gint orientation,
				     gint area_x, gint area_y,
				     gint tile_w, gint tile_h,
				     gint area_w, gint area_h,
				     gint *res_x, gint *res_y,
				     gint *res_w, gint *res_h)
{
	*res_x = area_x;
	*res_y = area_y;
	*res_w = area_w;
	*res_h = area_h;

	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			/* normal -- nothing to do */
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			*res_x = tile_w - area_x - area_w;
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			*res_x = tile_w - area_x - area_w;
			*res_y = tile_h - area_y - area_h;
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			*res_y = tile_h - area_y - area_h;
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			*res_x = area_y;
			*res_y = area_x;
			*res_w = area_h;
			*res_h = area_w;
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			*res_x = tile_w - area_y - area_h;
			*res_y = area_x;
			*res_w = area_h;
			*res_h = area_w;
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			*res_x = tile_w - area_y - area_h;
			*res_y = tile_h - area_x - area_w;
			*res_w = area_h;
			*res_h = area_w;
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			*res_x = area_y;
			*res_y = tile_h - area_x - area_w;
			*res_w = area_h;
			*res_h = area_w;
			break;
		default:
			/* The other values are out of range */
			break;
		}
}



static void pixbuf_renderer_sync_scroll_center(PixbufRenderer *pr)
{
	gint src_x, src_y;
	if (!pr->width || !pr->height) return;

	/*
	 * Update norm_center only if the image is bigger than the window.
	 * With this condition the stored center survives also a temporary display
	 * of the "broken image" icon.
	*/

	if (pr->width > pr->viewport_width)
		{
		src_x = pr->x_scroll + pr->vis_width / 2;
		pr->norm_center_x = static_cast<gdouble>(src_x) / pr->width;
		}

	if (pr->height > pr->viewport_height)
		{
		src_y = pr->y_scroll + pr->vis_height / 2;
		pr->norm_center_y = static_cast<gdouble>(src_y) / pr->height;
		}
}


static gboolean pr_scroll_clamp(PixbufRenderer *pr)
{
	gint old_xs;
	gint old_ys;

	if (pr->zoom == 0.0)
		{
		pr->x_scroll = 0;
		pr->y_scroll = 0;

		return FALSE;
		}

	old_xs = pr->x_scroll;
	old_ys = pr->y_scroll;

	if (pr->x_offset > 0)
		{
		pr->x_scroll = 0;
		}
	else
		{
		pr->x_scroll = CLAMP(pr->x_scroll, 0, pr->width - pr->vis_width);
		}

	if (pr->y_offset > 0)
		{
		pr->y_scroll = 0;
		}
	else
		{
		pr->y_scroll = CLAMP(pr->y_scroll, 0, pr->height - pr->vis_height);
		}

	pixbuf_renderer_sync_scroll_center(pr);

	return (old_xs != pr->x_scroll || old_ys != pr->y_scroll);
}

static gboolean pr_size_clamp(PixbufRenderer *pr)
{
	gint old_vw, old_vh;

	old_vw = pr->vis_width;
	old_vh = pr->vis_height;

	if (pr->width < pr->viewport_width)
		{
		pr->vis_width = pr->width;
		pr->x_offset = (pr->viewport_width - pr->width) / 2;
		}
	else
		{
		pr->vis_width = pr->viewport_width;
		pr->x_offset = 0;
		}

	if (pr->height < pr->viewport_height)
		{
		pr->vis_height = pr->height;
		pr->y_offset = (pr->viewport_height - pr->height) / 2;
		}
	else
		{
		pr->vis_height = pr->viewport_height;
		pr->y_offset = 0;
		}

	pixbuf_renderer_sync_scroll_center(pr);

	return (old_vw != pr->vis_width || old_vh != pr->vis_height);
}

static gboolean pr_zoom_clamp(PixbufRenderer *pr, gdouble zoom,
			      PrZoomFlags flags)
{
	gint w, h;
	gdouble scale;
	gboolean force = !!(flags & PR_ZOOM_FORCE);
	gboolean new_z = !!(flags & PR_ZOOM_NEW);

	zoom = CLAMP(zoom, pr->zoom_min, pr->zoom_max);

	if (pr->zoom == zoom && !force) return FALSE;

	w = pr->image_width;
	h = pr->image_height;

	if (zoom == 0.0 && !pr->pixbuf)
		{
		scale = 1.0;
		}
	else if (zoom == 0.0)
		{
		gint max_w;
		gint max_h;
		gboolean sizeable;

		sizeable = (new_z && pr_parent_window_sizable(pr));

		if (sizeable)
			{
			max_w = gdk_screen_width();
			max_h = gdk_screen_height();

			if (pr->window_limit)
				{
				max_w = max_w * pr->window_limit_size / 100;
				max_h = max_h * pr->window_limit_size / 100;
				}
			}
		else
			{
			max_w = pr->viewport_width;
			max_h = pr->viewport_height;
			}

		if ((pr->zoom_expand && !sizeable) || w > max_w || h > max_h)
			{
			if (static_cast<gdouble>(max_w) / w > static_cast<gdouble>(max_h) / h / pr->aspect_ratio)
				{
				scale = static_cast<gdouble>(max_h) / h / pr->aspect_ratio;
				h = max_h;
				w = w * scale + 0.5;
				if (w > max_w) w = max_w;
				}
			else
				{
				scale = static_cast<gdouble>(max_w) / w;
				w = max_w;
				h = h * scale * pr->aspect_ratio + 0.5;
				if (h > max_h) h = max_h;
				}

			if (pr->autofit_limit)
				{
				gdouble factor = static_cast<gdouble>(pr->autofit_limit_size) / 100;
				w = w * factor + 0.5;
				h = h * factor + 0.5;
				scale = scale * factor;
				}

			if (pr->zoom_expand)
				{
				gdouble factor = static_cast<gdouble>(pr->enlargement_limit_size) / 100;
				if (scale > factor)
					{
					w = w * factor / scale;
					h = h * factor / scale;
					scale = factor;
					}
				}

			if (w < 1) w = 1;
			if (h < 1) h = 1;
			}
		else
			{
			scale = 1.0;
			}
		}
	else if (zoom > 0.0) /* zoom orig, in */
		{
		scale = zoom;
		w = w * scale;
		h = h * scale * pr->aspect_ratio;
		}
	else /* zoom out */
		{
		scale = 1.0 / (0.0 - zoom);
		w = w * scale;
		h = h * scale * pr->aspect_ratio;
		}

	pr->zoom = zoom;
	pr->width = w;
	pr->height = h;
	pr->scale = scale;

	return TRUE;
}

static void pr_zoom_sync(PixbufRenderer *pr, gdouble zoom,
			 PrZoomFlags flags, gint px, gint py)
{
	gdouble old_scale;
	gint old_cx, old_cy;
	gboolean center_point = !!(flags & PR_ZOOM_CENTER);
	gboolean force = !!(flags & PR_ZOOM_FORCE);
	gboolean new_z = !!(flags & PR_ZOOM_NEW);
	gboolean lazy = !!(flags & PR_ZOOM_LAZY);
	PrZoomFlags clamp_flags = flags;
	gdouble old_center_x = pr->norm_center_x;
	gdouble old_center_y = pr->norm_center_y;

	old_scale = pr->scale;
	if (center_point)
		{
		px = CLAMP(px, 0, pr->width);
		py = CLAMP(py, 0, pr->height);
		old_cx = pr->x_scroll + (px - pr->x_offset);
		old_cy = pr->y_scroll + (py - pr->y_offset);
		}
	else
		{
		px = py = 0;
		old_cx = pr->x_scroll + pr->vis_width / 2;
		old_cy = pr->y_scroll + pr->vis_height / 2;
		}

	if (force) clamp_flags = static_cast<PrZoomFlags>(clamp_flags | PR_ZOOM_INVALIDATE);
	if (!pr_zoom_clamp(pr, zoom, clamp_flags)) return;

	(void) pr_size_clamp(pr);
	(void) pr_parent_window_resize(pr, pr->width, pr->height);

	if (force && new_z)
		{
		switch (pr->scroll_reset)
			{
			case PR_SCROLL_RESET_NOCHANGE:
				/* maintain old scroll position */
				pr->x_scroll = (static_cast<gdouble>(pr->image_width) * old_center_x * pr->scale) - pr->vis_width / 2;
				pr->y_scroll = (static_cast<gdouble>(pr->image_height) * old_center_y * pr->scale * pr->aspect_ratio) - pr->vis_height / 2;
				break;
			case PR_SCROLL_RESET_CENTER:
				/* center new image */
				pr->x_scroll = (static_cast<gdouble>(pr->image_width) / 2.0 * pr->scale) - pr->vis_width / 2;
				pr->y_scroll = (static_cast<gdouble>(pr->image_height) / 2.0 * pr->scale * pr->aspect_ratio) - pr->vis_height / 2;
				break;
			case PR_SCROLL_RESET_TOPLEFT:
			default:
				/* reset to upper left */
				pr->x_scroll = 0;
				pr->y_scroll = 0;
				break;
			}
		}
	else
		{
		/* user zoom does not force, so keep visible center point */
		if (center_point)
			{
			pr->x_scroll = old_cx / old_scale * pr->scale - (px - pr->x_offset);
			pr->y_scroll = old_cy / old_scale * pr->scale * pr->aspect_ratio - (py - pr->y_offset);
			}
		else
			{
			pr->x_scroll = old_cx / old_scale * pr->scale - (pr->vis_width / 2);
			pr->y_scroll = old_cy / old_scale * pr->scale * pr->aspect_ratio - (pr->vis_height / 2);
			}
		}

	pr_scroll_clamp(pr);

	pr->renderer->update_zoom(pr->renderer, lazy);
	if (pr->renderer2) pr->renderer2->update_zoom(pr->renderer2, lazy);

	pr_scroll_notify_signal(pr);
	pr_zoom_signal(pr);
	pr_update_signal(pr);
}

static void pr_size_sync(PixbufRenderer *pr, gint new_width, gint new_height)
{
	gboolean zoom_changed = FALSE;

	gint new_viewport_width = new_width;
	gint new_viewport_height = new_height;

	if (!pr->stereo_temp_disable)
		{
		if (pr->stereo_mode & PR_STEREO_HORIZ)
			{
			new_viewport_width = new_width / 2;
			}
		else if (pr->stereo_mode & PR_STEREO_VERT)
			{
			new_viewport_height = new_height / 2;
			}
		else if (pr->stereo_mode & PR_STEREO_FIXED)
			{
			new_viewport_width = pr->stereo_fixed_width;
			new_viewport_height = pr->stereo_fixed_height;
			}
		}

	if (pr->window_width == new_width && pr->window_height == new_height &&
	    pr->viewport_width == new_viewport_width && pr->viewport_height == new_viewport_height) return;

	pr->window_width = new_width;
	pr->window_height = new_height;
	pr->viewport_width = new_viewport_width;
	pr->viewport_height = new_viewport_height;

	if (pr->zoom == 0.0)
		{
		gdouble old_scale = pr->scale;
		pr_zoom_clamp(pr, 0.0, PR_ZOOM_FORCE);
		zoom_changed = (old_scale != pr->scale);
		}

	pr_size_clamp(pr);
	pr_scroll_clamp(pr);

	if (zoom_changed)
		{
		pr->renderer->update_zoom(pr->renderer, FALSE);
		if (pr->renderer2) pr->renderer2->update_zoom(pr->renderer2, FALSE);
		}

	pr->renderer->update_viewport(pr->renderer);
	if (pr->renderer2) pr->renderer2->update_viewport(pr->renderer2);


	/* ensure scroller remains visible */
	if (pr->scroller_overlay != -1)
		{
		gboolean update = FALSE;

		if (pr->scroller_x > new_width)
			{
			pr->scroller_x = new_width;
			pr->scroller_xpos = new_width;
			update = TRUE;
			}
		if (pr->scroller_y > new_height)
			{
			pr->scroller_y = new_height;
			pr->scroller_ypos = new_height;
			update = TRUE;
			}

		if (update)
			{
			GdkPixbuf *pixbuf;

			if (pixbuf_renderer_overlay_get(pr, pr->scroller_overlay, &pixbuf, NULL, NULL))
				{
				gint w, h;

				w = gdk_pixbuf_get_width(pixbuf);
				h = gdk_pixbuf_get_height(pixbuf);
				pixbuf_renderer_overlay_set(pr, pr->scroller_overlay, pixbuf,
							    pr->scroller_x - w / 2, pr->scroller_y - h / 2);
				}
			}
		}

	pr_scroll_notify_signal(pr);
	if (zoom_changed) pr_zoom_signal(pr);
	pr_update_signal(pr);
}

static void pr_size_cb(GtkWidget *UNUSED(widget), GtkAllocation *allocation, gpointer data)
{
	auto pr = static_cast<PixbufRenderer *>(data);

	pr_size_sync(pr, allocation->width, allocation->height);
}

/*
 *-------------------------------------------------------------------
 * scrolling
 *-------------------------------------------------------------------
 */

void pixbuf_renderer_scroll(PixbufRenderer *pr, gint x, gint y)
{
	gint old_x, old_y;
	gint x_off, y_off;

	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	if (!pr->pixbuf && !pr->source_tiles_enabled) return;

	old_x = pr->x_scroll;
	old_y = pr->y_scroll;

	pr->x_scroll += x;
	pr->y_scroll += y;

	pr_scroll_clamp(pr);

	pixbuf_renderer_sync_scroll_center(pr);

	if (pr->x_scroll == old_x && pr->y_scroll == old_y) return;

	pr_scroll_notify_signal(pr);

	x_off = pr->x_scroll - old_x;
	y_off = pr->y_scroll - old_y;

	pr->renderer->scroll(pr->renderer, x_off, y_off);
	if (pr->renderer2) pr->renderer2->scroll(pr->renderer2, x_off, y_off);
}

void pixbuf_renderer_scroll_to_point(PixbufRenderer *pr, gint x, gint y,
				     gdouble x_align, gdouble y_align)
{
	gint px, py;
	gint ax, ay;

	x_align = CLAMP(x_align, 0.0, 1.0);
	y_align = CLAMP(y_align, 0.0, 1.0);

	ax = static_cast<gdouble>(pr->vis_width) * x_align;
	ay = static_cast<gdouble>(pr->vis_height) * y_align;

	px = static_cast<gdouble>(x) * pr->scale - (pr->x_scroll + ax);
	py = static_cast<gdouble>(y) * pr->scale * pr->aspect_ratio - (pr->y_scroll + ay);

	pixbuf_renderer_scroll(pr, px, py);
}

/* get or set coordinates of viewport center in the image, in range 0.0 - 1.0 */

void pixbuf_renderer_get_scroll_center(PixbufRenderer *pr, gdouble *x, gdouble *y)
{
	*x = pr->norm_center_x;
	*y = pr->norm_center_y;
}

void pixbuf_renderer_set_scroll_center(PixbufRenderer *pr, gdouble x, gdouble y)
{
	gdouble dst_x, dst_y;

	dst_x = x * pr->width  - pr->vis_width  / 2 - pr->x_scroll + CLAMP(pr->subpixel_x_scroll, -1.0, 1.0);
	dst_y = y * pr->height - pr->vis_height / 2 - pr->y_scroll + CLAMP(pr->subpixel_y_scroll, -1.0, 1.0);

	pr->subpixel_x_scroll = dst_x - static_cast<gint>(dst_x);
	pr->subpixel_y_scroll = dst_y - static_cast<gint>(dst_y);

	pixbuf_renderer_scroll(pr, static_cast<gint>(dst_x), static_cast<gint>(dst_y));
}

/*
 *-------------------------------------------------------------------
 * mouse
 *-------------------------------------------------------------------
 */

static gboolean pr_mouse_motion_cb(GtkWidget *widget, GdkEventMotion *event, gpointer UNUSED(data))
{
	PixbufRenderer *pr;
	gint accel;
	GdkSeat *seat;
	GdkDevice *device;

	/* This is a hack, but work far the best, at least for single pointer systems.
	 * See https://bugzilla.gnome.org/show_bug.cgi?id=587714 for more. */
	gint x, y;
	seat = gdk_display_get_default_seat(gdk_window_get_display(event->window));
	device = gdk_seat_get_pointer(seat);

	gdk_window_get_device_position(event->window, device, &x, &y, NULL);

	event->x = x;
	event->y = y;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id)
		{
		pr->scroller_xpos = event->x;
		pr->scroller_ypos = event->y;
		}

	pr->x_mouse = event->x;
	pr->y_mouse = event->y;
	pr_update_pixel_signal(pr);

	if (!pr->in_drag || !gdk_pointer_is_grabbed()) return FALSE;

	if (pr->drag_moved < PR_DRAG_SCROLL_THRESHHOLD)
		{
		pr->drag_moved++;
		}
	else
		{
		widget_set_cursor(widget, GDK_FLEUR);
		}

	if (event->state & GDK_CONTROL_MASK)
		{
		accel = PR_PAN_SHIFT_MULTIPLIER;
		}
	else
		{
		accel = 1;
		}

	/* do the scroll - not when drawing rectangle*/
	if (!options->draw_rectangle)
		{
		pixbuf_renderer_scroll(pr, (pr->drag_last_x - event->x) * accel,
					(pr->drag_last_y - event->y) * accel);
		}
	pr_drag_signal(pr, event);

	pr->drag_last_x = event->x;
	pr->drag_last_y = event->y;

	/* This is recommended by the GTK+ documentation, but does not work properly.
	 * Use deprecated way until GTK+ gets a solution for correct motion hint handling:
	 * https://bugzilla.gnome.org/show_bug.cgi?id=587714
	 */
	/* gdk_event_request_motions (event); */
	return FALSE;
}

static gboolean pr_leave_notify_cb(GtkWidget *widget, GdkEventCrossing *UNUSED(cevent), gpointer UNUSED(data))
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);
	pr->x_mouse = -1;
	pr->y_mouse = -1;

	pr_update_pixel_signal(pr);
	return FALSE;
}

static gboolean pr_mouse_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer UNUSED(data))
{
	PixbufRenderer *pr;
	GtkWidget *parent;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id) return TRUE;

	switch (bevent->button)
		{
		case MOUSE_BUTTON_LEFT:
			pr->in_drag = TRUE;
			pr->drag_last_x = bevent->x;
			pr->drag_last_y = bevent->y;
			pr->drag_moved = 0;
			gdk_pointer_grab(gtk_widget_get_window(widget), FALSE,
					 static_cast<GdkEventMask>(GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_RELEASE_MASK),
					 NULL, NULL, bevent->time);
			gtk_grab_add(widget);
			break;
		case MOUSE_BUTTON_MIDDLE:
			pr->drag_moved = 0;
			break;
		case MOUSE_BUTTON_RIGHT:
			pr_clicked_signal(pr, bevent);
			break;
		default:
			break;
		}

	parent = gtk_widget_get_parent(widget);
	if (widget && gtk_widget_get_can_focus(parent))
		{
		gtk_widget_grab_focus(parent);
		}

	return FALSE;
}

static gboolean pr_mouse_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer UNUSED(data))
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id)
		{
		pr_scroller_stop(pr);
		return TRUE;
		}

	if (gdk_pointer_is_grabbed() && gtk_widget_has_grab(GTK_WIDGET(pr)))
		{
		gtk_grab_remove(widget);
		gdk_pointer_ungrab(bevent->time);
		widget_set_cursor(widget, -1);
		}

	if (pr->drag_moved < PR_DRAG_SCROLL_THRESHHOLD)
		{
		if (bevent->button == MOUSE_BUTTON_LEFT && (bevent->state & GDK_CONTROL_MASK))
			{
			pr_scroller_start(pr, bevent->x, bevent->y);
			}
		else if (bevent->button == MOUSE_BUTTON_LEFT || bevent->button == MOUSE_BUTTON_MIDDLE)
			{
			pr_clicked_signal(pr, bevent);
			}
		}

	pr->in_drag = FALSE;

	return FALSE;
}

static gboolean pr_mouse_leave_cb(GtkWidget *widget, GdkEventCrossing *UNUSED(event), gpointer UNUSED(data))
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id)
		{
		pr->scroller_xpos = pr->scroller_x;
		pr->scroller_ypos = pr->scroller_y;
		pr->scroller_xinc = 0;
		pr->scroller_yinc = 0;
		}

	return FALSE;
}

static void pr_mouse_drag_cb(GtkWidget *widget, GdkDragContext *UNUSED(context), gpointer UNUSED(data))
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);

	pr->drag_moved = PR_DRAG_SCROLL_THRESHHOLD;
}

static void pr_signals_connect(PixbufRenderer *pr)
{
	g_signal_connect(G_OBJECT(pr), "motion_notify_event",
			 G_CALLBACK(pr_mouse_motion_cb), pr);
	g_signal_connect(G_OBJECT(pr), "button_press_event",
			 G_CALLBACK(pr_mouse_press_cb), pr);
	g_signal_connect(G_OBJECT(pr), "button_release_event",
			 G_CALLBACK(pr_mouse_release_cb), pr);
	g_signal_connect(G_OBJECT(pr), "leave_notify_event",
			 G_CALLBACK(pr_mouse_leave_cb), pr);
	g_signal_connect(G_OBJECT(pr), "leave_notify_event",
			 G_CALLBACK(pr_leave_notify_cb), pr);

	gtk_widget_set_events(GTK_WIDGET(pr), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
					      static_cast<GdkEventMask>(GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK |
					      GDK_LEAVE_NOTIFY_MASK));

	g_signal_connect(G_OBJECT(pr), "drag_begin",
			 G_CALLBACK(pr_mouse_drag_cb), pr);

}

/*
 *-------------------------------------------------------------------
 * stereo support
 *-------------------------------------------------------------------
 */

#define COLOR_BYTES 3   /* rgb */
#define RC 0            /* Red-Cyan */
#define GM 1            /* Green-Magenta */
#define YB 2            /* Yellow-Blue */

static void pr_create_anaglyph_color(GdkPixbuf *pixbuf, GdkPixbuf *right, gint x, gint y, gint w, gint h, guint mode)
{
	gint srs, drs;
	guchar *s_pix, *d_pix;
	guchar *sp, *dp;
	guchar *spi, *dpi;
	gint i, j;

	srs = gdk_pixbuf_get_rowstride(right);
	s_pix = gdk_pixbuf_get_pixels(right);
	spi = s_pix + (x * COLOR_BYTES);

	drs = gdk_pixbuf_get_rowstride(pixbuf);
	d_pix = gdk_pixbuf_get_pixels(pixbuf);
	dpi =  d_pix + x * COLOR_BYTES;

	for (i = y; i < y + h; i++)
		{
		sp = spi + (i * srs);
		dp = dpi + (i * drs);
		for (j = 0; j < w; j++)
			{
			switch(mode)
				{
				case RC:
					dp[0] = sp[0]; /* copy red channel */
					break;
				case GM:
					dp[1] = sp[1];
					break;
				case YB:
					dp[0] = sp[0];
					dp[1] = sp[1];
					break;
				}
			sp += COLOR_BYTES;
			dp += COLOR_BYTES;
			}
		}
}

static void pr_create_anaglyph_gray(GdkPixbuf *pixbuf, GdkPixbuf *right, gint x, gint y, gint w, gint h, guint mode)
{
	gint srs, drs;
	guchar *s_pix, *d_pix;
	guchar *sp, *dp;
	guchar *spi, *dpi;
	gint i, j;
	const double gc[3] = {0.299, 0.587, 0.114};

	srs = gdk_pixbuf_get_rowstride(right);
	s_pix = gdk_pixbuf_get_pixels(right);
	spi = s_pix + (x * COLOR_BYTES);

	drs = gdk_pixbuf_get_rowstride(pixbuf);
	d_pix = gdk_pixbuf_get_pixels(pixbuf);
	dpi =  d_pix + x * COLOR_BYTES;

	for (i = y; i < y + h; i++)
		{
		sp = spi + (i * srs);
		dp = dpi + (i * drs);
		for (j = 0; j < w; j++)
			{
			guchar g1 = dp[0] * gc[0] + dp[1] * gc[1] + dp[2] * gc[2];
			guchar g2 = sp[0] * gc[0] + sp[1] * gc[1] + sp[2] * gc[2];
			switch(mode)
				{
				case RC:
					dp[0] = g2; /* red channel from sp */
					dp[1] = g1; /* green and blue from dp */
					dp[2] = g1;
					break;
				case GM:
					dp[0] = g1;
					dp[1] = g2;
					dp[2] = g1;
					break;
				case YB:
					dp[0] = g2;
					dp[1] = g2;
					dp[2] = g1;
					break;
				}
			sp += COLOR_BYTES;
			dp += COLOR_BYTES;
			}
		}
}

static void pr_create_anaglyph_dubois(GdkPixbuf *pixbuf, GdkPixbuf *right, gint x, gint y, gint w, gint h, guint mode)
{
	gint srs, drs;
	guchar *s_pix, *d_pix;
	guchar *sp, *dp;
	guchar *spi, *dpi;
	gint i, j, k;
	double pr_dubois_matrix[3][6];
	static const double pr_dubois_matrix_RC[3][6] = {
		{ 0.456,  0.500,  0.176, -0.043, -0.088, -0.002},
		{-0.040, -0.038, -0.016,  0.378,  0.734, -0.018},
		{-0.015, -0.021, -0.005, -0.072, -0.113,  1.226}};
	static const double pr_dubois_matrix_GM[3][6] = {
		{-0.062, -0.158, -0.039,  0.529,  0.705,  0.024},
		{ 0.284,  0.668,  0.143, -0.016, -0.015, -0.065},
		{-0.015, -0.027,  0.021,  0.009,  0.075,  0.937}};
	static const double pr_dubois_matrix_YB[3][6] = {
		{ 1.000, -0.193,  0.282, -0.015, -0.116, -0.016},
		{-0.024,  0.855,  0.064,  0.006,  0.058, -0.016},
		{-0.036, -0.163,  0.021,  0.089,  0.174,  0.858}};

	switch(mode)
		{
		case RC:
			memcpy(pr_dubois_matrix, pr_dubois_matrix_RC, sizeof pr_dubois_matrix);
			break;
		case GM:
			memcpy(pr_dubois_matrix, pr_dubois_matrix_GM, sizeof pr_dubois_matrix);
			break;
		case YB:
			memcpy(pr_dubois_matrix, pr_dubois_matrix_YB, sizeof pr_dubois_matrix);
			break;
		}

	srs = gdk_pixbuf_get_rowstride(right);
	s_pix = gdk_pixbuf_get_pixels(right);
	spi = s_pix + (x * COLOR_BYTES);

	drs = gdk_pixbuf_get_rowstride(pixbuf);
	d_pix = gdk_pixbuf_get_pixels(pixbuf);
	dpi =  d_pix + x * COLOR_BYTES;

	for (i = y; i < y + h; i++)
		{
		sp = spi + (i * srs);
		dp = dpi + (i * drs);
		for (j = 0; j < w; j++)
			{
			double res[3];
			for (k = 0; k < 3; k++)
				{
				const double *m = pr_dubois_matrix[k];
				res[k] = sp[0] * m[0] + sp[1] * m[1] + sp[2] * m[2] + dp[0] * m[3] + dp[1] * m[4] + dp[2] * m[5];
				if (res[k] < 0.0) res[k] = 0;
				if (res[k] > 255.0) res[k] = 255.0;
				}
			dp[0] = res[0];
			dp[1] = res[1];
			dp[2] = res[2];
			sp += COLOR_BYTES;
			dp += COLOR_BYTES;
			}
		}
}

void pr_create_anaglyph(guint mode, GdkPixbuf *pixbuf, GdkPixbuf *right, gint x, gint y, gint w, gint h)
{
	if (mode & PR_STEREO_ANAGLYPH_RC)
		pr_create_anaglyph_color(pixbuf, right, x, y, w, h, RC);
	else if (mode & PR_STEREO_ANAGLYPH_GM)
		pr_create_anaglyph_color(pixbuf, right, x, y, w, h, GM);
	else if (mode & PR_STEREO_ANAGLYPH_YB)
		pr_create_anaglyph_color(pixbuf, right, x, y, w, h, YB);
	else if (mode & PR_STEREO_ANAGLYPH_GRAY_RC)
		pr_create_anaglyph_gray(pixbuf, right, x, y, w, h, RC);
	else if (mode & PR_STEREO_ANAGLYPH_GRAY_GM)
		pr_create_anaglyph_gray(pixbuf, right, x, y, w, h, GM);
	else if (mode & PR_STEREO_ANAGLYPH_GRAY_YB)
		pr_create_anaglyph_gray(pixbuf, right, x, y, w, h, YB);
	else if (mode & PR_STEREO_ANAGLYPH_DB_RC)
		pr_create_anaglyph_dubois(pixbuf, right, x, y, w, h, RC);
	else if (mode & PR_STEREO_ANAGLYPH_DB_GM)
		pr_create_anaglyph_dubois(pixbuf, right, x, y, w, h, GM);
	else if (mode & PR_STEREO_ANAGLYPH_DB_YB)
		pr_create_anaglyph_dubois(pixbuf, right, x, y, w, h, YB);
}

/*
 *-------------------------------------------------------------------
 * public
 *-------------------------------------------------------------------
 */
static void pr_pixbuf_size_sync(PixbufRenderer *pr)
{
	pr->stereo_pixbuf_offset_left = 0;
	pr->stereo_pixbuf_offset_right = 0;
	if (!pr->pixbuf) return;
	switch (pr->orientation)
		{
		case EXIF_ORIENTATION_LEFT_TOP:
		case EXIF_ORIENTATION_RIGHT_TOP:
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			pr->image_width = gdk_pixbuf_get_height(pr->pixbuf);
			pr->image_height = gdk_pixbuf_get_width(pr->pixbuf);
			if (pr->stereo_data == STEREO_PIXBUF_SBS)
				{
				pr->image_height /= 2;
				pr->stereo_pixbuf_offset_right = pr->image_height;
				}
			else if (pr->stereo_data == STEREO_PIXBUF_CROSS)
				{
				pr->image_height /= 2;
				pr->stereo_pixbuf_offset_left = pr->image_height;
				}

			break;
		default:
			pr->image_width = gdk_pixbuf_get_width(pr->pixbuf);
			pr->image_height = gdk_pixbuf_get_height(pr->pixbuf);
			if (pr->stereo_data == STEREO_PIXBUF_SBS)
				{
				pr->image_width /= 2;
				pr->stereo_pixbuf_offset_right = pr->image_width;
				}
			else if (pr->stereo_data == STEREO_PIXBUF_CROSS)
				{
				pr->image_width /= 2;
				pr->stereo_pixbuf_offset_left = pr->image_width;
				}
		}
}

static void pr_set_pixbuf(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom, PrZoomFlags flags)
{
	if (pixbuf) g_object_ref(pixbuf);
	if (pr->pixbuf) g_object_unref(pr->pixbuf);
	pr->pixbuf = pixbuf;

	if (!pr->pixbuf)
		{
		/* no pixbuf so just clear the window */
		pr->image_width = 0;
		pr->image_height = 0;
		pr->scale = 1.0;
		pr->zoom = zoom; /* don't throw away the zoom value, it is set by pixbuf_renderer_move, among others,
				    and used for pixbuf_renderer_zoom_get */

		pr->renderer->update_pixbuf(pr->renderer, flags & PR_ZOOM_LAZY);
		if (pr->renderer2) pr->renderer2->update_pixbuf(pr->renderer2, flags & PR_ZOOM_LAZY);

		pr_update_signal(pr);

		return;
		}

	if (pr->stereo_mode & PR_STEREO_TEMP_DISABLE)
		{
		gint disable = !pr->pixbuf || ! pr->stereo_data;
		pr_stereo_temp_disable(pr, disable);
		}

	pr_pixbuf_size_sync(pr);
	pr->renderer->update_pixbuf(pr->renderer, flags & PR_ZOOM_LAZY);
	if (pr->renderer2) pr->renderer2->update_pixbuf(pr->renderer2, flags & PR_ZOOM_LAZY);
	pr_zoom_sync(pr, zoom, static_cast<PrZoomFlags>(flags | PR_ZOOM_FORCE | PR_ZOOM_NEW), 0, 0);
}

void pixbuf_renderer_set_pixbuf(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_source_tile_unset(pr);

	pr_set_pixbuf(pr, pixbuf, zoom, PR_ZOOM_NONE);

	pr_update_signal(pr);
}

void pixbuf_renderer_set_pixbuf_lazy(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom, gint orientation, StereoPixbufData stereo_data)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_source_tile_unset(pr);

	pr->orientation = orientation;
	pr->stereo_data = stereo_data;
	pr_set_pixbuf(pr, pixbuf, zoom, PR_ZOOM_LAZY);

	pr_update_signal(pr);
}

GdkPixbuf *pixbuf_renderer_get_pixbuf(PixbufRenderer *pr)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), NULL);

	return pr->pixbuf;
}

void pixbuf_renderer_set_orientation(PixbufRenderer *pr, gint orientation)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr->orientation = orientation;

	pr_pixbuf_size_sync(pr);
	pr_zoom_sync(pr, pr->zoom, PR_ZOOM_FORCE, 0, 0);
}

//gint pixbuf_renderer_get_orientation(PixbufRenderer *pr)
//{
	//if (!pr) return 1;
	//return pr->orientation;
//}

void pixbuf_renderer_set_stereo_data(PixbufRenderer *pr, StereoPixbufData stereo_data)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	if (pr->stereo_data == stereo_data) return;


	pr->stereo_data = stereo_data;

	if (pr->stereo_mode & PR_STEREO_TEMP_DISABLE)
		{
		gint disable = !pr->pixbuf || ! pr->stereo_data;
		pr_stereo_temp_disable(pr, disable);
		}
	pr_pixbuf_size_sync(pr);
	pr->renderer->update_pixbuf(pr->renderer, FALSE);
	if (pr->renderer2) pr->renderer2->update_pixbuf(pr->renderer2, FALSE);
	pr_zoom_sync(pr, pr->zoom, PR_ZOOM_FORCE, 0, 0);
}

void pixbuf_renderer_set_post_process_func(PixbufRenderer *pr, PixbufRendererPostProcessFunc func, gpointer user_data, gboolean slow)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr->func_post_process = func;
	pr->post_process_user_data = user_data;
	pr->post_process_slow = func && slow;

}


void pixbuf_renderer_move(PixbufRenderer *pr, PixbufRenderer *source)
{
	GObject *object;
	PixbufRendererScrollResetType scroll_reset;

	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	g_return_if_fail(IS_PIXBUF_RENDERER(source));

	if (pr == source) return;

	object = G_OBJECT(pr);

	g_object_set(object, "zoom_min", source->zoom_min, NULL);
	g_object_set(object, "zoom_max", source->zoom_max, NULL);
	g_object_set(object, "loading", source->loading, NULL);

	pr->complete = source->complete;

	pr->x_scroll = source->x_scroll;
	pr->y_scroll = source->y_scroll;
	pr->x_mouse  = source->x_mouse;
	pr->y_mouse  = source->y_mouse;

	scroll_reset = pr->scroll_reset;
	pr->scroll_reset = PR_SCROLL_RESET_NOCHANGE;

	pr->func_post_process = source->func_post_process;
	pr->post_process_user_data = source->post_process_user_data;
	pr->post_process_slow = source->post_process_slow;
	pr->orientation = source->orientation;
	pr->stereo_data = source->stereo_data;

	if (source->source_tiles_enabled)
		{
		pr_source_tile_unset(pr);

		pr->source_tiles_enabled = source->source_tiles_enabled;
		pr->source_tiles_cache_size = source->source_tiles_cache_size;
		pr->source_tile_width = source->source_tile_width;
		pr->source_tile_height = source->source_tile_height;
		pr->image_width = source->image_width;
		pr->image_height = source->image_height;

		pr->func_tile_request = source->func_tile_request;
		pr->func_tile_dispose = source->func_tile_dispose;
		pr->func_tile_data = source->func_tile_data;

		pr->source_tiles = source->source_tiles;
		source->source_tiles = NULL;

		pr_zoom_sync(pr, source->zoom, static_cast<PrZoomFlags>(PR_ZOOM_FORCE | PR_ZOOM_NEW), 0, 0);
		}
	else
		{
		pixbuf_renderer_set_pixbuf(pr, source->pixbuf, source->zoom);
		}

	pr->scroll_reset = scroll_reset;

	pixbuf_renderer_set_pixbuf(source, NULL, source->zoom);
}

void pixbuf_renderer_copy(PixbufRenderer *pr, PixbufRenderer *source)
{
	GObject *object;
	PixbufRendererScrollResetType scroll_reset;

	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	g_return_if_fail(IS_PIXBUF_RENDERER(source));

	if (pr == source) return;

	object = G_OBJECT(pr);

	g_object_set(object, "zoom_min", source->zoom_min, NULL);
	g_object_set(object, "zoom_max", source->zoom_max, NULL);
	g_object_set(object, "loading", source->loading, NULL);

	pr->complete = source->complete;

	pr->x_scroll = source->x_scroll;
	pr->y_scroll = source->y_scroll;
	pr->x_mouse  = source->x_mouse;
	pr->y_mouse  = source->y_mouse;

	scroll_reset = pr->scroll_reset;
	pr->scroll_reset = PR_SCROLL_RESET_NOCHANGE;

	pr->orientation = source->orientation;
	pr->stereo_data = source->stereo_data;

	if (source->source_tiles_enabled)
		{
		pr->source_tiles_enabled = source->source_tiles_enabled;
		pr->source_tiles_cache_size = source->source_tiles_cache_size;
		pr->source_tile_width = source->source_tile_width;
		pr->source_tile_height = source->source_tile_height;
		pr->image_width = source->image_width;
		pr->image_height = source->image_height;

		pr->func_tile_request = source->func_tile_request;
		pr->func_tile_dispose = source->func_tile_dispose;
		pr->func_tile_data = source->func_tile_data;

		pr->source_tiles = source->source_tiles;
		source->source_tiles = NULL;

		pr_zoom_sync(pr, source->zoom, static_cast<PrZoomFlags>(PR_ZOOM_FORCE | PR_ZOOM_NEW), 0, 0);
		}
	else
		{
		pixbuf_renderer_set_pixbuf(pr, source->pixbuf, source->zoom);
		}

	pr->scroll_reset = scroll_reset;
}

void pixbuf_renderer_area_changed(PixbufRenderer *pr, gint x, gint y, gint w, gint h)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	if (pr->source_tiles_enabled)
		{
		pr_source_tile_changed(pr, x, y, w, h);
		}

	pr->renderer->area_changed(pr->renderer, x, y, w, h);
	if (pr->renderer2) pr->renderer2->area_changed(pr->renderer2, x, y, w, h);
}

void pixbuf_renderer_zoom_adjust(PixbufRenderer *pr, gdouble increment)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_zoom_adjust_real(pr, increment, PR_ZOOM_NONE, 0, 0);
}

void pixbuf_renderer_zoom_adjust_at_point(PixbufRenderer *pr, gdouble increment, gint x, gint y)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_zoom_adjust_real(pr, increment, PR_ZOOM_CENTER, x, y);
}

void pixbuf_renderer_zoom_set(PixbufRenderer *pr, gdouble zoom)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_zoom_sync(pr, zoom, PR_ZOOM_NONE, 0, 0);
}

gdouble pixbuf_renderer_zoom_get(PixbufRenderer *pr)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), 1.0);

	return pr->zoom;
}

gdouble pixbuf_renderer_zoom_get_scale(PixbufRenderer *pr)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), 1.0);

	return pr->scale;
}

void pixbuf_renderer_zoom_set_limits(PixbufRenderer *pr, gdouble min, gdouble max)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	if (min > 1.0 || max < 1.0) return;
	if (min < 1.0 && min > -1.0) return;
	if (min < -200.0 || max > 200.0) return;

	if (pr->zoom_min != min)
		{
		pr->zoom_min = min;
		g_object_notify(G_OBJECT(pr), "zoom_min");
		}
	if (pr->zoom_max != max)
		{
		pr->zoom_max = max;
		g_object_notify(G_OBJECT(pr), "zoom_max");
		}
}

static void pr_stereo_set(PixbufRenderer *pr)
{
	if (!pr->renderer) pr->renderer = pr_backend_renderer_new(pr);

	pr->renderer->stereo_set(pr->renderer, pr->stereo_mode & ~PR_STEREO_MIRROR_RIGHT & ~PR_STEREO_FLIP_RIGHT);

	if (pr->stereo_mode & (PR_STEREO_HORIZ | PR_STEREO_VERT | PR_STEREO_FIXED))
		{
		if (!pr->renderer2) pr->renderer2 = pr_backend_renderer_new(pr);
		pr->renderer2->stereo_set(pr->renderer2, (pr->stereo_mode & ~PR_STEREO_MIRROR_LEFT & ~PR_STEREO_FLIP_LEFT) | PR_STEREO_RIGHT);
		}
	else
		{
		if (pr->renderer2) pr->renderer2->free(pr->renderer2);
		pr->renderer2 = NULL;
		}
	if (pr->stereo_mode & PR_STEREO_HALF)
		{
		if (pr->stereo_mode & PR_STEREO_HORIZ) pr->aspect_ratio = 2.0;
		else if (pr->stereo_mode & PR_STEREO_VERT) pr->aspect_ratio = 0.5;
		else pr->aspect_ratio = 1.0;
		}
	else
		{
		pr->aspect_ratio = 1.0;
		}
}

void pixbuf_renderer_stereo_set(PixbufRenderer *pr, gint stereo_mode)
{
	gboolean redraw = !(pr->stereo_mode == stereo_mode) || pr->stereo_temp_disable;
	pr->stereo_mode = stereo_mode;
	if ((stereo_mode & PR_STEREO_TEMP_DISABLE) && pr->stereo_temp_disable) return;

	pr->stereo_temp_disable = FALSE;

	pr_stereo_set(pr);

	if (redraw)
		{
		pr_size_sync(pr, pr->window_width, pr->window_height); /* recalculate new viewport */
		pr_zoom_sync(pr, pr->zoom, static_cast<PrZoomFlags>(PR_ZOOM_FORCE | PR_ZOOM_NEW), 0, 0);
		}
}

void pixbuf_renderer_stereo_fixed_set(PixbufRenderer *pr, gint width, gint height, gint x1, gint y1, gint x2, gint y2)
{
	pr->stereo_fixed_width = width;
	pr->stereo_fixed_height = height;
	pr->stereo_fixed_x_left = x1;
	pr->stereo_fixed_y_left = y1;
	pr->stereo_fixed_x_right = x2;
	pr->stereo_fixed_y_right = y2;
}

gint pixbuf_renderer_stereo_get(PixbufRenderer *pr)
{
	return pr->stereo_mode;
}

static void pr_stereo_temp_disable(PixbufRenderer *pr, gboolean disable)
{
	if (pr->stereo_temp_disable == disable) return;
	pr->stereo_temp_disable = disable;
	if (disable)
		{
		if (!pr->renderer) pr->renderer = pr_backend_renderer_new(pr);
		pr->renderer->stereo_set(pr->renderer, PR_STEREO_NONE);
		if (pr->renderer2) pr->renderer2->free(pr->renderer2);
		pr->renderer2 = NULL;
		pr->aspect_ratio = 1.0;
		}
	else
		{
		pr_stereo_set(pr);
		}
	pr_size_sync(pr, pr->window_width, pr->window_height); /* recalculate new viewport */
}

gboolean pixbuf_renderer_get_pixel_colors(PixbufRenderer *pr, gint x_pixel, gint y_pixel,
                                          gint *r_mouse, gint *g_mouse, gint *b_mouse)
{
	GdkPixbuf *pb = pr->pixbuf;
	gint p_alpha, prs;
	guchar *p_pix, *pp;
	gint map_x, map_y, map_w, map_h;
	size_t xoff, yoff;

	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	g_return_val_if_fail(r_mouse != NULL && g_mouse != NULL && b_mouse != NULL, FALSE);

	if (!pr->pixbuf && !pr->source_tiles_enabled)
		{
		*r_mouse = -1;
		*g_mouse = -1;
		*b_mouse = -1;
		return FALSE;
		}

	if (!pb) return FALSE;

	pr_tile_region_map_orientation(pr->orientation,
					x_pixel, y_pixel,
					pr->image_width, pr->image_height,
					1, 1, /*single pixel */
					&map_x, &map_y,
					&map_w, &map_h);

	if (map_x < 0 || map_x > gdk_pixbuf_get_width(pr->pixbuf) - 1) return  FALSE;
	if (map_y < 0 || map_y > gdk_pixbuf_get_height(pr->pixbuf) - 1) return  FALSE;

	p_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	xoff = static_cast<size_t>(map_x) * (p_alpha ? 4 : 3);
	yoff = static_cast<size_t>(map_y) * prs;
	pp = p_pix + yoff + xoff;
	*r_mouse = *pp;
	pp++;
	*g_mouse = *pp;
	pp++;
	*b_mouse = *pp;

	return TRUE;
}

gboolean pixbuf_renderer_get_mouse_position(PixbufRenderer *pr, gint *x_pixel_return, gint *y_pixel_return)
{
	gint x_pixel, y_pixel, x_pixel_clamped, y_pixel_clamped;

	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	g_return_val_if_fail(x_pixel_return != NULL && y_pixel_return != NULL, FALSE);

	if (!pr->pixbuf && !pr->source_tiles_enabled)
		{
		*x_pixel_return = -1;
		*y_pixel_return = -1;
		return FALSE;
		}

	x_pixel = floor(static_cast<gdouble>(pr->x_mouse - pr->x_offset + pr->x_scroll) / pr->scale);
	y_pixel = floor(static_cast<gdouble>(pr->y_mouse - pr->y_offset + pr->y_scroll) / pr->scale / pr->aspect_ratio);
	x_pixel_clamped = CLAMP(x_pixel, 0, pr->image_width - 1);
	y_pixel_clamped = CLAMP(y_pixel, 0, pr->image_height - 1);

	if (x_pixel != x_pixel_clamped)
		{
		/* mouse is not on pr */
		x_pixel = -1;
		}
	if (y_pixel != y_pixel_clamped)
		{
		/* mouse is not on pr */
		y_pixel = -1;
		}

	*x_pixel_return = x_pixel;
	*y_pixel_return = y_pixel;

	return TRUE;
}

gboolean pixbuf_renderer_get_image_size(PixbufRenderer *pr, gint *width, gint *height)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	g_return_val_if_fail(width != NULL && height != NULL, FALSE);

	if (!pr->pixbuf && !pr->source_tiles_enabled && (!pr->image_width || !pr->image_height))
		{
		*width = 0;
		*height = 0;
		return FALSE;
		}

	*width = pr->image_width;
	*height = pr->image_height;
	return TRUE;
}

gboolean pixbuf_renderer_get_scaled_size(PixbufRenderer *pr, gint *width, gint *height)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	g_return_val_if_fail(width != NULL && height != NULL, FALSE);

	if (!pr->pixbuf && !pr->source_tiles_enabled && (!pr->image_width || !pr->image_height))
		{
		*width = 0;
		*height = 0;
		return FALSE;
		}

	*width = pr->width;
	*height = pr->height;
	return TRUE;
}

gboolean pixbuf_renderer_get_visible_rect(PixbufRenderer *pr, GdkRectangle *rect)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	g_return_val_if_fail(rect != NULL, FALSE);

	if ((!pr->pixbuf && !pr->source_tiles_enabled) ||
	    !pr->scale)
		{
		rect->x = 0;
		rect->y = 0;
		rect->width = 0;
		rect->height = 0;
		return FALSE;
		}

	rect->x = static_cast<gint>(static_cast<gdouble>(pr->x_scroll) / pr->scale);
	rect->y = static_cast<gint>(static_cast<gdouble>(pr->y_scroll) / pr->scale / pr->aspect_ratio);
	rect->width = static_cast<gint>(static_cast<gdouble>(pr->vis_width) / pr->scale);
	rect->height = static_cast<gint>(static_cast<gdouble>(pr->vis_height) / pr->scale / pr->aspect_ratio);
	return TRUE;
}

//gboolean pixbuf_renderer_get_virtual_rect(PixbufRenderer *pr, GdkRectangle *rect)
//{
	//g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	//g_return_val_if_fail(rect != NULL, FALSE);

	//if ((!pr->pixbuf && !pr->source_tiles_enabled))
		//{
		//rect->x = 0;
		//rect->y = 0;
		//rect->width = 0;
		//rect->height = 0;
		//return FALSE;
		//}

	//rect->x = pr->x_scroll;
	//rect->y = pr->y_scroll;
	//rect->width = pr->vis_width;
	//rect->height = pr->vis_height;
	//return TRUE;
//}

void pixbuf_renderer_set_size_early(PixbufRenderer *UNUSED(pr), guint UNUSED(width), guint UNUSED(height))
{
#if 0
	/** @FIXME this function does not consider the image orientation,
	so it probably only breaks something */
	gdouble zoom;
	gint w, h;

	zoom = pixbuf_renderer_zoom_get(pr);
	pr->image_width = width;
	pr->image_height = height;

	pr_zoom_clamp(pr, zoom, PR_ZOOM_FORCE, NULL);

	//w = width;
	//h = height;

	//pr->width = width;
	//pr->height = height;
#endif
}

void pixbuf_renderer_set_ignore_alpha(PixbufRenderer *pr, gint ignore_alpha)
{
   g_return_if_fail(IS_PIXBUF_RENDERER(pr));

   pr->ignore_alpha = ignore_alpha;
   pr_pixbuf_size_sync(pr);
   pr_zoom_sync(pr, pr->zoom, PR_ZOOM_FORCE, 0, 0);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
