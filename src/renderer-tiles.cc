/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2021 The Geeqie Team
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

#include "renderer-tiles.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "compat-deprecated.h"
#include "options.h"
#include "pixbuf-renderer.h"

/* comment this out if not using this from within Geeqie
 * defining GQ_BUILD does these things:
 *   - Sets the shift-click scroller pixbuf to a nice icon instead of a black box
 */
#define GQ_BUILD 1

#ifdef GQ_BUILD
#include "exif.h"
#include "pixbuf-util.h"
#else
enum ExifOrientationType {
	EXIF_ORIENTATION_UNKNOWN	= 0,
	EXIF_ORIENTATION_TOP_LEFT	= 1,
	EXIF_ORIENTATION_TOP_RIGHT	= 2,
	EXIF_ORIENTATION_BOTTOM_RIGHT	= 3,
	EXIF_ORIENTATION_BOTTOM_LEFT	= 4,
	EXIF_ORIENTATION_LEFT_TOP	= 5,
	EXIF_ORIENTATION_RIGHT_TOP	= 6,
	EXIF_ORIENTATION_RIGHT_BOTTOM	= 7,
	EXIF_ORIENTATION_LEFT_BOTTOM	= 8
};
#endif

namespace
{

struct QueueData;

enum ImageRenderType {
	TILE_RENDER_NONE = 0, /**< do nothing */
	TILE_RENDER_AREA,     /**< render an area of the tile */
	TILE_RENDER_ALL       /**< render the whole tile */
};

struct ImageTile
{
	cairo_surface_t *surface;	/* off screen buffer */
	GdkPixbuf *pixbuf;	/* pixbuf area for zooming */
	gint x;			/* x offset into image */
	gint y;			/* y offset into image */
	gint w;			/* width that is visible (may be less if at edge of image) */
	gint h;			/* height '' */

	gboolean blank;

	ImageRenderType render_todo;    /**< what to do
	                                   NONE  do nothing
	                                   AREA  render area of tile, usually only used when loading an image
	                                         note: will jump to an ALL if render_done is not ALL.
	                                   ALL   render entire tile, if never done before w/ ALL, for expose events *only* */
	ImageRenderType render_done;	/* highest that has been done before on tile */

	QueueData *qd;
	QueueData *qd2;

	guint size;		/* est. memory used by pixmap and pixbuf */
};

struct QueueData
{
	ImageTile *it;
	gint x;
	gint y;
	gint w;
	gint h;
	gboolean new_data;
};

struct OverlayData
{
	gint id;

	GdkPixbuf *pixbuf;
	GdkWindow *window;

	gint x;
	gint y;

	OverlayRendererFlags flags;
};

struct RendererTiles
{
	RendererFuncs f;
	PixbufRenderer *pr;

	gint tile_cache_max;		/* max MiB to use for offscreen buffer */

	gint tile_width;
	gint tile_height;
	GList *tiles;		/* list of buffer tiles */
	gint tile_cache_size;	/* allocated size of pixmaps/pixbufs */
	GList *draw_queue;	/* list of areas to redraw */
	GList *draw_queue_2pass;/* list when 2 pass is enabled */

	GList *overlay_list;
	cairo_surface_t *overlay_buffer;
	cairo_surface_t *surface;

	guint draw_idle_id; /* event source id */

	GdkPixbuf *spare_tile;

	gint stereo_mode;
	gint stereo_off_x;
	gint stereo_off_y;

	gint x_scroll;  /* allow local adjustment and mirroring */
	gint y_scroll;

	gint hidpi_scale;
};

constexpr size_t COLOR_BYTES = 3; /* rgb */


inline gint get_right_pixbuf_offset(RendererTiles *rt)
{
	return (!!(rt->stereo_mode & PR_STEREO_RIGHT) != !!(rt->stereo_mode & PR_STEREO_SWAP)) ?
	       rt->pr->stereo_pixbuf_offset_right : rt->pr->stereo_pixbuf_offset_left;
}

inline gint get_left_pixbuf_offset(RendererTiles *rt)
{
	return (!!(rt->stereo_mode & PR_STEREO_RIGHT) == !!(rt->stereo_mode & PR_STEREO_SWAP)) ?
	       rt->pr->stereo_pixbuf_offset_right : rt->pr->stereo_pixbuf_offset_left;
}


void rt_overlay_draw(RendererTiles *rt, GdkRectangle request_rect, ImageTile *it);

gboolean rt_tile_is_visible(RendererTiles *rt, ImageTile *it);
void rt_queue_merge(QueueData *parent, QueueData *qd);
void rt_queue(RendererTiles *rt, gint x, gint y, gint w, gint h,
              gint clamp, ImageRenderType render, gboolean new_data, gboolean only_existing);

gint rt_queue_draw_idle_cb(gpointer data);


void rt_sync_scroll(RendererTiles *rt)
{
	PixbufRenderer *pr = rt->pr;

	rt->x_scroll = (rt->stereo_mode & PR_STEREO_MIRROR) ?
	               pr->width - pr->vis_width - pr->x_scroll
	               : pr->x_scroll;

	rt->y_scroll = (rt->stereo_mode & PR_STEREO_FLIP) ?
	               pr->height - pr->vis_height - pr->y_scroll
	               : pr->y_scroll;
}

/*
 *-------------------------------------------------------------------
 * borders
 *-------------------------------------------------------------------
 */

void rt_border_draw(RendererTiles *rt, GdkRectangle border_rect)
{
	PixbufRenderer *pr = rt->pr;
	GtkWidget *box;
	GdkWindow *window;
	cairo_t *cr;

	box = GTK_WIDGET(pr);
	window = gtk_widget_get_window(box);

	if (!window) return;

	cr = cairo_create(rt->surface);

	auto draw_if_intersect = [&border_rect, rt, pr, cr](GdkRectangle rect)
	{
		GdkRectangle r;
		if (!gdk_rectangle_intersect(&border_rect, &rect, &r)) return;

		cairo_set_source_rgb(cr, pr->color.red, pr->color.green, pr->color.blue);
		cairo_rectangle(cr, r.x + rt->stereo_off_x, r.y + rt->stereo_off_y, r.width, r.height);
		cairo_fill(cr);
		rt_overlay_draw(rt, r, nullptr);
	};

	if (!pr->pixbuf && !pr->source_tiles_enabled)
		{
		draw_if_intersect({0, 0, pr->viewport_width, pr->viewport_height});
		cairo_destroy(cr);
		return;
		}

	if (pr->vis_width < pr->viewport_width)
		{
		if (pr->x_offset > 0)
			{
			draw_if_intersect({0, 0, pr->x_offset, pr->viewport_height});
			}

		gint right_edge = pr->x_offset + pr->vis_width;
		if (pr->viewport_width > right_edge)
			{
			draw_if_intersect({right_edge, 0, pr->viewport_width - right_edge, pr->viewport_height});
			}
		}

	if (pr->vis_height < pr->viewport_height)
		{
		if (pr->y_offset > 0)
			{
			draw_if_intersect({pr->x_offset, 0, pr->vis_width, pr->y_offset});
			}

		gint bottom_edge = pr->y_offset + pr->vis_height;
		if (pr->viewport_height  > bottom_edge)
			{
			draw_if_intersect({pr->x_offset, bottom_edge, pr->vis_width, pr->viewport_height - bottom_edge});
			}
		}

	cairo_destroy(cr);
}

void rt_border_clear(RendererTiles *rt)
{
	PixbufRenderer *pr = rt->pr;
	rt_border_draw(rt, {0, 0, pr->viewport_width, pr->viewport_height});
}


/*
 *-------------------------------------------------------------------
 * display tiles
 *-------------------------------------------------------------------
 */

ImageTile *rt_tile_new(gint x, gint y, gint width, gint height)
{
	auto it = g_new0(ImageTile, 1);

	it->x = x;
	it->y = y;
	it->w = width;
	it->h = height;

	it->render_done = TILE_RENDER_NONE;

	return it;
}

void rt_tile_free(ImageTile *it)
{
	if (!it) return;

	if (it->pixbuf) g_object_unref(it->pixbuf);
	if (it->surface) cairo_surface_destroy(it->surface);

	g_free(it);
}

void rt_tile_free_all(RendererTiles *rt)
{
	g_list_free_full(rt->tiles, reinterpret_cast<GDestroyNotify>(rt_tile_free));
	rt->tiles = nullptr;
	rt->tile_cache_size = 0;
}

ImageTile *rt_tile_add(RendererTiles *rt, gint x, gint y)
{
	PixbufRenderer *pr = rt->pr;
	ImageTile *it;

	it = rt_tile_new(x, y, rt->tile_width, rt->tile_height);

	if (it->x + it->w > pr->width) it->w = pr->width - it->x;
	if (it->y + it->h > pr->height) it->h = pr->height - it->y;

	rt->tiles = g_list_prepend(rt->tiles, it);
	rt->tile_cache_size += it->size;

	return it;
}

void rt_tile_remove(RendererTiles *rt, ImageTile *it)
{
	if (it->qd)
		{
		QueueData *qd = it->qd;

		it->qd = nullptr;
		rt->draw_queue = g_list_remove(rt->draw_queue, qd);
		g_free(qd);
		}

	if (it->qd2)
		{
		QueueData *qd = it->qd2;

		it->qd2 = nullptr;
		rt->draw_queue_2pass = g_list_remove(rt->draw_queue_2pass, qd);
		g_free(qd);
		}

	rt->tiles = g_list_remove(rt->tiles, it);
	rt->tile_cache_size -= it->size;

	rt_tile_free(it);
}

void rt_tile_free_space(RendererTiles *rt, guint space, ImageTile *it)
{
	PixbufRenderer *pr = rt->pr;
	GList *work;
	guint tile_max;

	work = g_list_last(rt->tiles);

	if (pr->source_tiles_enabled && pr->scale < 1.0)
		{
		gint tiles;

		tiles = (pr->vis_width / rt->tile_width + 1) * (pr->vis_height / rt->tile_height + 1);
		tile_max = std::max<gint>(tiles * rt->tile_width * rt->tile_height * 3,
		                          rt->tile_cache_max * 1048576.0 * pr->scale);
		}
	else
		{
		tile_max = rt->tile_cache_max * 1048576;
		}

	while (work && rt->tile_cache_size + space > tile_max)
		{
		ImageTile *needle;

		needle = static_cast<ImageTile *>(work->data);
		work = work->prev;
		if (needle != it &&
		    ((!needle->qd && !needle->qd2) || !rt_tile_is_visible(rt, needle))) rt_tile_remove(rt, needle);
		}
}

void rt_tile_invalidate_all(RendererTiles *rt)
{
	PixbufRenderer *pr = rt->pr;
	GList *work;

	work = rt->tiles;
	while (work)
		{
		ImageTile *it;

		it = static_cast<ImageTile *>(work->data);
		work = work->next;

		it->render_done = TILE_RENDER_NONE;
		it->render_todo = TILE_RENDER_ALL;
		it->blank = FALSE;

		it->w = std::min(rt->tile_width, pr->width - it->x);
		it->h = std::min(rt->tile_height, pr->height - it->y);
		}
}

void rt_tile_invalidate_region(RendererTiles *rt, GdkRectangle region)
{
	gint x1 = ROUND_DOWN(region.x, rt->tile_width);
	gint x2 = ROUND_UP(region.x + region.width, rt->tile_width);

	gint y1 = ROUND_DOWN(region.y, rt->tile_height);
	gint y2 = ROUND_UP(region.y + region.height, rt->tile_height);

	for (GList *work = rt->tiles; work; work = work->next)
		{
		auto *it = static_cast<ImageTile *>(work->data);

		if (it->x < x2 && it->x + it->w > x1 &&
		    it->y < y2 && it->y + it->h > y1)
			{
			it->render_done = TILE_RENDER_NONE;
			it->render_todo = TILE_RENDER_ALL;
			}
		}
}

ImageTile *rt_tile_get(RendererTiles *rt, gint x, gint y, gboolean only_existing)
{
	GList *work;

	work = rt->tiles;
	while (work)
		{
		ImageTile *it;

		it = static_cast<ImageTile *>(work->data);
		if (it->x == x && it->y == y)
			{
			rt->tiles = g_list_delete_link(rt->tiles, work);
			rt->tiles = g_list_prepend(rt->tiles, it);
			return it;
			}

		work = work->next;
		}

	if (only_existing) return nullptr;

	return rt_tile_add(rt, x, y);
}

gint pixmap_calc_size(cairo_surface_t *)
{
	return options->image.tile_size * options->image.tile_size * 4 / 8;
}

void rt_hidpi_aware_draw(RendererTiles *rt,
                         cairo_t *cr,
                         GdkPixbuf *pixbuf,
                         double x,
                         double y)
{
	cairo_surface_t *surface;
	surface = gdk_cairo_surface_create_from_pixbuf(pixbuf, rt->hidpi_scale, nullptr);
	cairo_set_source_surface(cr, surface, x, y);
	cairo_fill(cr);
	cairo_surface_destroy(surface);
}

void rt_tile_prepare(RendererTiles *rt, ImageTile *it)
{
	PixbufRenderer *pr = rt->pr;
	if (!it->surface)
		{
		cairo_surface_t *surface;
		guint size;

		surface = gdk_window_create_similar_surface(gtk_widget_get_window(GTK_WIDGET(pr)),
		                                            CAIRO_CONTENT_COLOR,
		                                            rt->tile_width, rt->tile_height);

		size = pixmap_calc_size(surface) * rt->hidpi_scale * rt->hidpi_scale;
		rt_tile_free_space(rt, size, it);

		it->surface = surface;
		it->size += size;
		rt->tile_cache_size += size;
		}

	if (!it->pixbuf)
		{
		GdkPixbuf *pixbuf;
		guint size;
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, rt->hidpi_scale * rt->tile_width, rt->hidpi_scale * rt->tile_height);

		size = gdk_pixbuf_get_rowstride(pixbuf) * rt->tile_height * rt->hidpi_scale;
		rt_tile_free_space(rt, size, it);

		it->pixbuf = pixbuf;
		it->size += size;
		rt->tile_cache_size += size;
		}
}

/*
 *-------------------------------------------------------------------
 * overlays
 *-------------------------------------------------------------------
 */

GdkRectangle rt_overlay_get_position(const RendererTiles *rt, const OverlayData *od)
{
	GdkRectangle od_rect;

	od_rect.x = od->x;
	od_rect.y = od->y;
	od_rect.width = gdk_pixbuf_get_width(od->pixbuf);
	od_rect.height = gdk_pixbuf_get_height(od->pixbuf);

	if (od->flags & OVL_RELATIVE)
		{
		PixbufRenderer *pr = rt->pr;
		if (od_rect.x < 0) od_rect.x += pr->viewport_width - od_rect.width;
		if (od_rect.y < 0) od_rect.y += pr->viewport_height - od_rect.height;
		}

	return od_rect;
}

void rt_overlay_init_window(RendererTiles *rt, OverlayData *od)
{
	PixbufRenderer *pr = rt->pr;
	GdkWindowAttr attributes;
	gint attributes_mask;

	GdkRectangle od_rect = rt_overlay_get_position(rt, od);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.width = od_rect.width;
	attributes.height = od_rect.height;
	attributes.event_mask = GDK_EXPOSURE_MASK;
	attributes_mask = 0;

	od->window = gdk_window_new(gtk_widget_get_window(GTK_WIDGET(pr)), &attributes, attributes_mask);
	gdk_window_set_user_data(od->window, pr);
	gdk_window_move(od->window, od_rect.x + rt->stereo_off_x, od_rect.y + rt->stereo_off_y);
	gdk_window_show(od->window);
}

void rt_overlay_draw(RendererTiles *rt, GdkRectangle request_rect, ImageTile *it)
{
	for (GList *work = rt->overlay_list; work; work = work->next)
		{
		auto *od = static_cast<OverlayData *>(work->data);

		if (!od->window) rt_overlay_init_window(rt, od);

		GdkRectangle od_rect = rt_overlay_get_position(rt, od);

		GdkRectangle r;
		if (gdk_rectangle_intersect(&request_rect, &od_rect, &r))
			{
			if (!rt->overlay_buffer)
				{
				rt->overlay_buffer = gdk_window_create_similar_surface(gtk_widget_get_window(GTK_WIDGET(rt->pr)),
				                                                       CAIRO_CONTENT_COLOR,
				                                                       rt->tile_width, rt->tile_height);
				}

			const auto draw = [rt, od, &od_rect](GdkRectangle r, const std::function<void(cairo_t *)> &set_source)
			{
				cairo_t *cr = cairo_create(rt->overlay_buffer);
				set_source(cr);
				cairo_rectangle(cr, 0, 0, r.width, r.height);
				cairo_fill_preserve(cr);

				gdk_cairo_set_source_pixbuf(cr, od->pixbuf, od_rect.x - r.x, od_rect.y - r.y);
				cairo_fill(cr);
				cairo_destroy (cr);

				cr = deprecated_gdk_cairo_create(od->window);
				cairo_set_source_surface(cr, rt->overlay_buffer, r.x - od_rect.x, r.y - od_rect.y);
				cairo_rectangle (cr, r.x - od_rect.x, r.y - od_rect.y, r.width, r.height);
				cairo_fill (cr);
				cairo_destroy (cr);
			};

			if (it)
				{
				const auto set_source = [rt, pr = rt->pr, it, &r](cairo_t *cr)
				{
					cairo_set_source_surface(cr, it->surface, pr->x_offset + (it->x - rt->x_scroll) - r.x, pr->y_offset + (it->y - rt->y_scroll) - r.y);
				};
				draw(r, set_source);
				}
			else
				{
				/* no ImageTile means region may be larger than our scratch buffer */
				for (gint sx = r.x; sx < r.x + r.width; sx += rt->tile_width)
				    for (gint sy = r.y; sy < r.y + r.height; sy += rt->tile_height)
					{
					gint sw = std::min(r.x + r.width - sx, rt->tile_width);
					gint sh = std::min(r.y + r.height - sy, rt->tile_height);

					draw({sx, sy, sw, sh}, [](cairo_t *cr){ cairo_set_source_rgb(cr, 0, 0, 0); });
					}
				}
			}
		}
}

void rt_overlay_queue_draw(RendererTiles *rt, OverlayData *od, gint x1, gint y1, gint x2, gint y2)
{
	PixbufRenderer *pr = rt->pr;

	GdkRectangle od_rect = rt_overlay_get_position(rt, od);

	/* add borders */
	od_rect.x -= x1;
	od_rect.y -= y1;
	od_rect.width += x1 + x2;
	od_rect.height += y1 + y2;

	rt_queue(rt,
	         rt->x_scroll - pr->x_offset + od_rect.x,
	         rt->y_scroll - pr->y_offset + od_rect.y,
	         od_rect.width, od_rect.height,
	         FALSE, TILE_RENDER_ALL, FALSE, FALSE);

	rt_border_draw(rt, od_rect);
}

void rt_overlay_queue_all(RendererTiles *rt, gint x1, gint y1, gint x2, gint y2)
{
	GList *work;

	work = rt->overlay_list;
	while (work)
		{
		auto od = static_cast<OverlayData *>(work->data);
		work = work->next;

		rt_overlay_queue_draw(rt, od, x1, y1, x2, y2);
		}
}

void rt_overlay_update_sizes(RendererTiles *rt)
{
	for (GList *work = rt->overlay_list; work; work = work->next)
		{
		auto od = static_cast<OverlayData *>(work->data);

		if (!od->window) rt_overlay_init_window(rt, od);

		if (od->flags & OVL_RELATIVE)
			{
			GdkRectangle od_rect = rt_overlay_get_position(rt, od);
			gdk_window_move_resize(od->window,
			                       od_rect.x + rt->stereo_off_x,
			                       od_rect.y + rt->stereo_off_y,
			                       od_rect.width,
			                       od_rect.height);
			}
		}
}

OverlayData *rt_overlay_find(RendererTiles *rt, gint id)
{
	GList *work;

	work = rt->overlay_list;
	while (work)
		{
		auto od = static_cast<OverlayData *>(work->data);
		work = work->next;

		if (od->id == id) return od;
		}

	return nullptr;
}


gint renderer_tiles_overlay_add(void *renderer, GdkPixbuf *pixbuf, gint x, gint y,
                                OverlayRendererFlags flags)
{
	auto rt = static_cast<RendererTiles *>(renderer);
	PixbufRenderer *pr = rt->pr;
	gint id;

	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), -1);
	g_return_val_if_fail(pixbuf != nullptr, -1);

	id = 1;
	while (rt_overlay_find(rt, id)) id++;

	auto od = g_new0(OverlayData, 1);
	od->id = id;
	od->pixbuf = g_object_ref(pixbuf);
	od->x = x;
	od->y = y;
	od->flags = flags;

	rt_overlay_init_window(rt, od);

	rt->overlay_list = g_list_append(rt->overlay_list, od);

	gtk_widget_queue_draw(GTK_WIDGET(rt->pr));

	return od->id;
}

void rt_overlay_list_reset_window(RendererTiles *rt)
{
	GList *work;

	g_clear_pointer(&rt->overlay_buffer, cairo_surface_destroy);

	work = rt->overlay_list;
	while (work)
		{
		auto od = static_cast<OverlayData *>(work->data);
		work = work->next;
		if (od->window) gdk_window_destroy(od->window);
		od->window = nullptr;
		}
}

void overlay_data_free(OverlayData *od)
{
	if (!od) return;

	if (od->pixbuf) g_object_unref(od->pixbuf);
	if (od->window) gdk_window_destroy(od->window);
	g_free(od);
}

void renderer_tiles_overlay_set(void *renderer, gint id, GdkPixbuf *pixbuf, gint, gint)
{
	auto *rt = static_cast<RendererTiles *>(renderer);
	PixbufRenderer *pr = rt->pr;

	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	OverlayData *od = rt_overlay_find(rt, id);
	if (!od) return;

	if (pixbuf)
		{
		g_object_unref(od->pixbuf);
		od->pixbuf = g_object_ref(pixbuf);
		}
	else
		{
		rt->overlay_list = g_list_remove(rt->overlay_list, od);
		overlay_data_free(od);

		if (!rt->overlay_list)
			{
			g_clear_pointer(&rt->overlay_buffer, cairo_surface_destroy);
			}
		}

	gtk_widget_queue_draw(GTK_WIDGET(rt->pr));
}

gboolean renderer_tiles_overlay_get(void *renderer, gint id, GdkPixbuf **pixbuf, gint *x, gint *y)
{
	auto rt = static_cast<RendererTiles *>(renderer);
	PixbufRenderer *pr = rt->pr;
	OverlayData *od;

	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);

	od = rt_overlay_find(rt, id);
	if (!od) return FALSE;

	if (pixbuf) *pixbuf = od->pixbuf;
	if (x) *x = od->x;
	if (y) *y = od->y;

	return TRUE;
}

void rt_hierarchy_changed_cb(GtkWidget *, GtkWidget *, gpointer data)
{
	auto rt = static_cast<RendererTiles *>(data);
	rt_overlay_list_reset_window(rt);
}

/*
 *-------------------------------------------------------------------
 * drawing
 *-------------------------------------------------------------------
 */

GdkPixbuf *rt_get_spare_tile(RendererTiles *rt)
{
	if (!rt->spare_tile) rt->spare_tile = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, rt->tile_width * rt->hidpi_scale, rt->tile_height * rt->hidpi_scale);
	return rt->spare_tile;
}

void rt_tile_rotate_90_clockwise(RendererTiles *rt, GdkPixbuf **tile, gint x, gint y, gint w, gint h)
{
	GdkPixbuf *src = *tile;
	GdkPixbuf *dest;
	gint srs;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	guchar *sp;
	guchar *dp;
	guchar *ip;
	guchar *spi;
	guchar *dpi;
	gint i;
	gint j;
	gint tw = rt->tile_width * rt->hidpi_scale;

	srs = gdk_pixbuf_get_rowstride(src);
	s_pix = gdk_pixbuf_get_pixels(src);
	spi = s_pix + (x * COLOR_BYTES);

	dest = rt_get_spare_tile(rt);
	drs = gdk_pixbuf_get_rowstride(dest);
	d_pix = gdk_pixbuf_get_pixels(dest);
	dpi = d_pix + (tw - 1) * COLOR_BYTES;

	for (i = y; i < y + h; i++)
		{
		sp = spi + (i * srs);
		ip = dpi - (i * COLOR_BYTES);
		for (j = x; j < x + w; j++)
			{
			dp = ip + (j * drs);
			memcpy(dp, sp, COLOR_BYTES);
			sp += COLOR_BYTES;
			}
		}

	rt->spare_tile = src;
	*tile = dest;
}

void rt_tile_rotate_90_counter_clockwise(RendererTiles *rt, GdkPixbuf **tile, gint x, gint y, gint w, gint h)
{
	GdkPixbuf *src = *tile;
	GdkPixbuf *dest;
	gint srs;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	guchar *sp;
	guchar *dp;
	guchar *ip;
	guchar *spi;
	guchar *dpi;
	gint i;
	gint j;
	gint th = rt->tile_height * rt->hidpi_scale;

	srs = gdk_pixbuf_get_rowstride(src);
	s_pix = gdk_pixbuf_get_pixels(src);
	spi = s_pix + (x * COLOR_BYTES);

	dest = rt_get_spare_tile(rt);
	drs = gdk_pixbuf_get_rowstride(dest);
	d_pix = gdk_pixbuf_get_pixels(dest);
	dpi = d_pix + (th - 1) * drs;

	for (i = y; i < y + h; i++)
		{
		sp = spi + (i * srs);
		ip = dpi + (i * COLOR_BYTES);
		for (j = x; j < x + w; j++)
			{
			dp = ip - (j * drs);
			memcpy(dp, sp, COLOR_BYTES);
			sp += COLOR_BYTES;
			}
		}

	rt->spare_tile = src;
	*tile = dest;
}

void rt_tile_mirror_only(RendererTiles *rt, GdkPixbuf **tile, gint x, gint y, gint w, gint h)
{
	GdkPixbuf *src = *tile;
	GdkPixbuf *dest;
	gint srs;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	guchar *sp;
	guchar *dp;
	guchar *spi;
	guchar *dpi;
	gint i;
	gint j;

	gint tw = rt->tile_width * rt->hidpi_scale;

	srs = gdk_pixbuf_get_rowstride(src);
	s_pix = gdk_pixbuf_get_pixels(src);
	spi = s_pix + (x * COLOR_BYTES);

	dest = rt_get_spare_tile(rt);
	drs = gdk_pixbuf_get_rowstride(dest);
	d_pix = gdk_pixbuf_get_pixels(dest);
	dpi =  d_pix + (tw - x - 1) * COLOR_BYTES;

	for (i = y; i < y + h; i++)
		{
		sp = spi + (i * srs);
		dp = dpi + (i * drs);
		for (j = 0; j < w; j++)
			{
			memcpy(dp, sp, COLOR_BYTES);
			sp += COLOR_BYTES;
			dp -= COLOR_BYTES;
			}
		}

	rt->spare_tile = src;
	*tile = dest;
}

void rt_tile_mirror_and_flip(RendererTiles *rt, GdkPixbuf **tile, gint x, gint y, gint w, gint h)
{
	GdkPixbuf *src = *tile;
	GdkPixbuf *dest;
	gint srs;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	guchar *sp;
	guchar *dp;
	guchar *dpi;
	gint i;
	gint j;
	gint tw = rt->tile_width * rt->hidpi_scale;
	gint th = rt->tile_height * rt->hidpi_scale;

	srs = gdk_pixbuf_get_rowstride(src);
	s_pix = gdk_pixbuf_get_pixels(src);

	dest = rt_get_spare_tile(rt);
	drs = gdk_pixbuf_get_rowstride(dest);
	d_pix = gdk_pixbuf_get_pixels(dest);
	dpi = d_pix + (th - 1) * drs + (tw - 1) * COLOR_BYTES;

	for (i = y; i < y + h; i++)
		{
		sp = s_pix + (i * srs) + (x * COLOR_BYTES);
		dp = dpi - (i * drs) - (x * COLOR_BYTES);
		for (j = 0; j < w; j++)
			{
			memcpy(dp, sp, COLOR_BYTES);
			sp += COLOR_BYTES;
			dp -= COLOR_BYTES;
			}
		}

	rt->spare_tile = src;
	*tile = dest;
}

void rt_tile_flip_only(RendererTiles *rt, GdkPixbuf **tile, gint x, gint y, gint w, gint h)
{
	GdkPixbuf *src = *tile;
	GdkPixbuf *dest;
	gint srs;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	guchar *sp;
	guchar *dp;
	guchar *spi;
	guchar *dpi;
	gint i;
	gint th = rt->tile_height * rt->hidpi_scale;

	srs = gdk_pixbuf_get_rowstride(src);
	s_pix = gdk_pixbuf_get_pixels(src);
	spi = s_pix + (x * COLOR_BYTES);

	dest = rt_get_spare_tile(rt);
	drs = gdk_pixbuf_get_rowstride(dest);
	d_pix = gdk_pixbuf_get_pixels(dest);
	dpi = d_pix + (th - 1) * drs + (x * COLOR_BYTES);

	for (i = y; i < y + h; i++)
		{
		sp = spi + (i * srs);
		dp = dpi - (i * drs);
		memcpy(dp, sp, w * COLOR_BYTES);
		}

	rt->spare_tile = src;
	*tile = dest;
}

void rt_tile_apply_orientation(RendererTiles *rt, gint orientation, GdkPixbuf **pixbuf, gint x, gint y, gint w, gint h)
{
	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			/* normal -- nothing to do */
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			{
				rt_tile_mirror_only(rt, pixbuf, x, y, w, h);
			}
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			{
				rt_tile_mirror_and_flip(rt, pixbuf, x, y, w, h);
			}
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			{
				rt_tile_flip_only(rt, pixbuf, x, y, w, h);
			}
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			{
				rt_tile_flip_only(rt, pixbuf, x, y, w, h);
				rt_tile_rotate_90_clockwise(rt, pixbuf, x, rt->tile_height - y - h, w, h);
			}
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			{
				rt_tile_rotate_90_clockwise(rt, pixbuf, x, y, w, h);
			}
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			{
				rt_tile_flip_only(rt, pixbuf, x, y, w, h);
				rt_tile_rotate_90_counter_clockwise(rt, pixbuf, x, rt->tile_height - y - h, w, h);
			}
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			{
				rt_tile_rotate_90_counter_clockwise(rt, pixbuf, x, y, w, h);
			}
			break;
		default:
			/* The other values are out of range */
			break;
		}
}

/**
 * @brief Renders the contents of the specified region of the specified ImageTile, using
 *        SourceTiles that the RendererTiles knows how to create/access.
 * @param rt The RendererTiles object.
 * @param it The ImageTile to render.
 * @param x,y,w,h The sub-region of the ImageTile to render.
 * @retval TRUE We rendered something that needs to be drawn.
 * @retval FALSE We didn't render anything that needs to be drawn.
 */
gboolean rt_source_tile_render(RendererTiles *rt, ImageTile *it,
                               gint x, gint y, gint w, gint h,
                               gboolean, gboolean)
{
	PixbufRenderer *pr = rt->pr;
	gboolean draw = FALSE;

	if (pr->image_width == 0 || pr->image_height == 0) return FALSE;

	// This is the scale due to zooming.  So if the user is zoomed in 2x (we're
	// rendering twice as large), these numbers will be 2.  Note that these values
	// can definitely be fractional (becomes important below).
	const gdouble scale_x = static_cast<gdouble>(pr->width) / pr->image_width;
	const gdouble scale_y = static_cast<gdouble>(pr->height) / pr->image_height;

	// And these are the unscaled coordinates where our tile data should originate from.
	const gint sx = static_cast<gdouble>(it->x + x) / scale_x;
	const gint sy = static_cast<gdouble>(it->y + y) / scale_y;
	const gint sw = static_cast<gdouble>(w) / scale_x;
	const gint sh = static_cast<gdouble>(h) / scale_y;

	/* HACK: The pixbuf scalers get kinda buggy(crash) with extremely
	 * small sizes for anything but GDK_INTERP_NEAREST
	 */
	const gboolean force_nearest = pr->width < PR_MIN_SCALE_SIZE || pr->height < PR_MIN_SCALE_SIZE;
	GdkInterpType interp_type = force_nearest ? GDK_INTERP_NEAREST : pr->zoom_quality;

#if 0
	// Draws red over draw region, to check for leaks (regions not filled)
	pixbuf_set_rect_fill(it->pixbuf, x, y, rt->hidpi_scale * w, rt->hidpi_scale * h, 255, 0, 0, 255);
#endif

	// Since the RendererTiles ImageTiles and PixbufRenderer SourceTiles are different
	// sizes and may not exactly overlap, we now determine which SourceTiles are needed
	// to cover the ImageTile that we're being asked to render.
	//
	// This will render the relevant SourceTiles if needed, or pull from the cache if
	// they've already been generated.
	GList *list = pr_source_tile_compute_region(pr, sx, sy, sw, sh, TRUE);
	const GdkRectangle it_rect{it->x + x, it->y + y, w, h};
	GdkRectangle st_rect;
	for (GList *work = list; work; work = work->next)
		{
		const auto st = static_cast<SourceTile *>(work->data);

		// The scaled (output) coordinates that are covered by this SourceTile.
		// To avoid aliasing line artifacts due to under-drawing, we expand the
		// render area to the nearest whole pixel.
		st_rect.x = floor(st->x * scale_x);
		st_rect.y = floor(st->y * scale_y);
		st_rect.width = ceil((st->x + pr->source_tile_width) * scale_x) - st_rect.x;
		st_rect.height = ceil((st->y + pr->source_tile_height) * scale_y) - st_rect.y;

		// We find the overlapping region r between the ImageTile (output)
		// region and the region that's covered by this SourceTile (input).
		GdkRectangle r;
		if (gdk_rectangle_intersect(&st_rect, &it_rect, &r))
			{
			if (st->blank)
				{
				// If this SourceTile has no contents, we just paint a black rect
				// of the appropriate size.
				cairo_t *cr = cairo_create(it->surface);
				cairo_rectangle (cr, r.x - st->x, r.y - st->y, rt->hidpi_scale * r.width, rt->hidpi_scale * r.height);
				cairo_set_source_rgb(cr, 0, 0, 0);
				cairo_fill (cr);
				cairo_destroy (cr);
				// TODO(xsdg): We almost certainly need to set draw = TRUE in this branch.
				// This may explain the smearing that we sometimes get when panning the view while drawing.
				}
			else
				{
				// Note that the ImageTile it contains its own solitary pixbuf, it->pixbuf.
				// This means that the region covered by this function (possibly split across
				// multiple SourceTiles) has origin (0, 0).  Additionally, the width and height
				// of that pixbuf will reflect the value of GDK_SCALE (which is stored by the
				// RendererTiles rt).  The following is an invariant:
				// it->pixbuf->width  = rt->hidpi_scale * it->width
				// it->pixbuf->height = rt->hidpi_scale * it->height
				//
				// So for hidpi rendering, we need to multiply the scale factor from the zoom by
				// the additional scale factor for hidpi.  This combined scale factor is then
				// applied to the offset (explained below), width, and height.

				// (May need to use unfloored stx,sty values here)
				const gdouble offset_x = rt->hidpi_scale * static_cast<gdouble>(st_rect.x - it->x);
				const gdouble offset_y = rt->hidpi_scale * static_cast<gdouble>(st_rect.y - it->y);

				// TODO(xsdg): Just draw instead of usign scale-draw for the case where
				// (pr->zoom == 1.0 || pr->scale == 1.0)

				// The order of operations in this function is scale, offset, clip, copy.
				// So we start with the data from st->pixbuf.  First we scale that data by
				// the scale factors.  Then we offset that intermediate image by the offsets.
				// Next, we clip that offsetted image to the (x,y,w,h) region specified.  And
				// lastly, we copy the resulting region into the _region with the same
				// coordinates_ in it->pixbuf.
				//
				// At this point, recall that we may need to render into ImageTile from multiple
				// SourceTiles.  The region specified by r accounts for this, and thus,
				// those are the coordinates _within the current SourceTile_ that need to be
				// rendered into the ImageTile.
				//
				// The offsets translate the region from wherever it may be in the actual image
				// to the ImageTile pixbuf coordinate system.  Because ImageTile and SourceTile
				// coordinates are not necessarily aligned, an offset will be negative if this
				// SourceTile starts left of or above the ImageTile, positive if it starts in
				// the middle of the ImageTile, or zero if the left or top edges are aligned.
				gdk_pixbuf_scale(st->pixbuf, it->pixbuf,
				                 r.x - it->x, r.y - it->y, rt->hidpi_scale * r.width, rt->hidpi_scale * r.height,
				                 offset_x, offset_y,
				                 rt->hidpi_scale * scale_x, rt->hidpi_scale * scale_y,
				                 interp_type);
				draw = TRUE;
				}
			}
		}

	g_list_free(list);

	return draw;
}

/**
 * @brief
 * @param has_alpha
 * @param ignore_alpha
 * @param src
 * @param dest
 * @param pb_rect
 * @param offset_x
 * @param offset_y
 * @param scale_x
 * @param scale_y
 * @param interp_type
 * @param check_x
 * @param check_y
 * @param wide_image Used as a work-around for a GdkPixbuf problem. Set when image width is > 32767.
 *        Problem exhibited with gdk_pixbuf_copy_area() and GDK_INTERP_NEAREST.
 *        See https://github.com/BestImageViewer/geeqie/issues/772
 */
void rt_tile_get_region(gboolean has_alpha, gboolean ignore_alpha,
                        const GdkPixbuf *src, GdkPixbuf *dest,
                        GdkRectangle pb_rect,
                        double offset_x, double offset_y, double scale_x, double scale_y,
                        GdkInterpType interp_type,
                        int check_x, int check_y, gboolean wide_image)
{
	if (!has_alpha)
		{
		if (scale_x == 1.0 && scale_y == 1.0)
			{
			if (wide_image)
				{
				const gint srs = gdk_pixbuf_get_rowstride(src);
				const gint drs = gdk_pixbuf_get_rowstride(dest);
				const guchar *s_pix = gdk_pixbuf_get_pixels(src);
				guchar *d_pix = gdk_pixbuf_get_pixels(dest);

				for (gint y = 0; y < pb_rect.height; y++)
					{
					const gint sy = -static_cast<int>(offset_y) + pb_rect.y + y;
					for (gint x = 0; x < pb_rect.width; x++)
						{
						const gint sx = -static_cast<int>(offset_x) + pb_rect.x + x;
						const guchar *sp = s_pix + (sy * srs) + (sx * COLOR_BYTES);
						guchar *dp = d_pix + (y * drs) + (x * COLOR_BYTES);

						memcpy(dp, sp, COLOR_BYTES);
						}
					}
				}
			else
				{
				gdk_pixbuf_copy_area(src,
				                     -offset_x + pb_rect.x, -offset_y + pb_rect.y,
				                     pb_rect.width, pb_rect.height,
				                     dest,
				                     pb_rect.x, pb_rect.y);
				}
			}
		else
			{
			gdk_pixbuf_scale(src, dest,
			                 pb_rect.x, pb_rect.y, pb_rect.width, pb_rect.height,
			                 offset_x, offset_y,
			                 scale_x, scale_y,
			                 (wide_image && interp_type == GDK_INTERP_NEAREST) ? GDK_INTERP_TILES : interp_type);
			}
		}
	else
		{
		const auto convert_alpha_color = [](const GdkRGBA &alpha_color)
		{
			const auto red = static_cast<guint32>(alpha_color.red * 255) << 16 & 0x00FF0000;
			const auto green = static_cast<guint32>(alpha_color.green * 255) << 8 & 0x0000FF00;
			const auto blue = static_cast<guint32>(alpha_color.blue * 255) & 0x000000FF;
			return red + green + blue;
		};
		guint32 alpha_1 = convert_alpha_color(options->image.alpha_color_1);
		guint32 alpha_2 = convert_alpha_color(options->image.alpha_color_2);

		if (scale_x == 1.0 && scale_y == 1.0) interp_type = GDK_INTERP_NEAREST;

		if (ignore_alpha)
			{
			GdkPixbuf *tmppixbuf = gdk_pixbuf_add_alpha(src, FALSE, 0, 0, 0);

			pixbuf_ignore_alpha_rect(tmppixbuf, 0, 0, gdk_pixbuf_get_width(src), gdk_pixbuf_get_height(src));

			gdk_pixbuf_composite_color(tmppixbuf, dest,
			                           pb_rect.x, pb_rect.y, pb_rect.width, pb_rect.height,
			                           offset_x, offset_y,
			                           scale_x, scale_y,
			                           (wide_image && interp_type == GDK_INTERP_NEAREST) ? GDK_INTERP_TILES : interp_type,
			                           255, check_x, check_y,
			                           PR_ALPHA_CHECK_SIZE,
			                           alpha_1,
			                           alpha_2);
			g_object_unref(tmppixbuf);
			}
		else
			{
			gdk_pixbuf_composite_color(src, dest,
			                           pb_rect.x, pb_rect.y, pb_rect.width, pb_rect.height,
			                           offset_x, offset_y,
			                           scale_x, scale_y,
			                           (wide_image && interp_type == GDK_INTERP_NEAREST) ? GDK_INTERP_TILES : interp_type,
			                           255, check_x, check_y,
			                           PR_ALPHA_CHECK_SIZE,
			                           alpha_1,
			                           alpha_2);
			}
		}
}


gint rt_get_orientation(RendererTiles *rt)
{
	PixbufRenderer *pr = rt->pr;

	gint orientation = pr->orientation;
	static const gint mirror[]       = {1,   2, 1, 4, 3, 6, 5, 8, 7};
	static const gint flip[]         = {1,   4, 3, 2, 1, 8, 7, 6, 5};

	if (rt->stereo_mode & PR_STEREO_MIRROR) orientation = mirror[orientation];
	if (rt->stereo_mode & PR_STEREO_FLIP) orientation = flip[orientation];
        return orientation;
}


void rt_tile_render(RendererTiles *rt, ImageTile *it,
                    gint x, gint y, gint w, gint h,
                    gboolean new_data, gboolean fast)
{
	PixbufRenderer *pr = rt->pr;
	gboolean has_alpha;
	gboolean draw = FALSE;
	gint orientation = rt_get_orientation(rt);
	gboolean wide_image = FALSE;

	if (it->render_todo == TILE_RENDER_NONE && it->surface && !new_data) return;

	if (it->render_done != TILE_RENDER_ALL)
		{
		x = 0;
		y = 0;
		w = it->w;
		h = it->h;
		if (!fast) it->render_done = TILE_RENDER_ALL;
		}
	else if (it->render_todo != TILE_RENDER_AREA)
		{
		if (!fast) it->render_todo = TILE_RENDER_NONE;
		return;
		}

	if (!fast) it->render_todo = TILE_RENDER_NONE;

	if (new_data) it->blank = FALSE;

	rt_tile_prepare(rt, it);
	has_alpha = (pr->pixbuf && gdk_pixbuf_get_has_alpha(pr->pixbuf));

	/** @FIXME checker colors for alpha should be configurable,
	 * also should be drawn for blank = TRUE
	 */

	if (it->blank)
		{
		/* no data, do fast rect fill */
		cairo_t *cr;
		cr = cairo_create(it->surface);
		cairo_rectangle (cr, 0, 0, it->w, it->h);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_fill (cr);
		cairo_destroy (cr);
		}
	else if (pr->source_tiles_enabled)
		{
		draw = rt_source_tile_render(rt, it, x, y, w, h, new_data, fast);
		}
	else
		{
		gdouble scale_x;
		gdouble scale_y;
		gdouble src_x;
		gdouble src_y;

		if (pr->image_width == 0 || pr->image_height == 0) return;

		scale_x = rt->hidpi_scale * static_cast<gdouble>(pr->width) / pr->image_width;
		scale_y = rt->hidpi_scale * static_cast<gdouble>(pr->height) / pr->image_height;

		pr_tile_coords_map_orientation(orientation, it->x, it->y,
		                               pr->width, pr->height,
		                               rt->tile_width, rt->tile_height,
		                               src_x, src_y);
		GdkRectangle pb_rect = pr_tile_region_map_orientation(orientation,
		                                                      {x, y, w, h},
		                                                      rt->tile_width,
		                                                      rt->tile_height);

		src_x *= rt->hidpi_scale;
		src_y *= rt->hidpi_scale;
		pr_scale_region(pb_rect, rt->hidpi_scale);

		switch (orientation)
			{
			case EXIF_ORIENTATION_LEFT_TOP:
			case EXIF_ORIENTATION_RIGHT_TOP:
			case EXIF_ORIENTATION_RIGHT_BOTTOM:
			case EXIF_ORIENTATION_LEFT_BOTTOM:
				std::swap(scale_x, scale_y);
				break;
			default:
				/* nothing to do */
				break;
			}

		/* HACK: The pixbuf scalers get kinda buggy(crash) with extremely
		 * small sizes for anything but GDK_INTERP_NEAREST
		 */
		if (pr->width < PR_MIN_SCALE_SIZE || pr->height < PR_MIN_SCALE_SIZE) fast = TRUE;
		if (pr->image_width > 32767) wide_image = TRUE;

		rt_tile_get_region(has_alpha, pr->ignore_alpha,
		                   pr->pixbuf, it->pixbuf, pb_rect,
		                   static_cast<gdouble>(0.0) - src_x - (get_right_pixbuf_offset(rt) * scale_x),
		                   static_cast<gdouble>(0.0) - src_y,
		                   scale_x, scale_y,
		                   (fast) ? GDK_INTERP_NEAREST : pr->zoom_quality,
		                   it->x + pb_rect.x, it->y + pb_rect.y, wide_image);
		if (rt->stereo_mode & PR_STEREO_ANAGLYPH &&
		    (pr->stereo_pixbuf_offset_right > 0 || pr->stereo_pixbuf_offset_left > 0))
			{
			GdkPixbuf *right_pb = rt_get_spare_tile(rt);
			rt_tile_get_region(has_alpha, pr->ignore_alpha,
			                   pr->pixbuf, right_pb, pb_rect,
			                   static_cast<gdouble>(0.0) - src_x - (get_left_pixbuf_offset(rt) * scale_x),
			                   static_cast<gdouble>(0.0) - src_y,
			                   scale_x, scale_y,
			                   (fast) ? GDK_INTERP_NEAREST : pr->zoom_quality,
			                   it->x + pb_rect.x, it->y + pb_rect.y, wide_image);
			pr_create_anaglyph(rt->stereo_mode, it->pixbuf, right_pb, pb_rect.x, pb_rect.y, pb_rect.width, pb_rect.height);
			/* do not care about freeing spare_tile, it will be reused */
			}
		rt_tile_apply_orientation(rt, orientation, &it->pixbuf, pb_rect.x, pb_rect.y, pb_rect.width, pb_rect.height);
		draw = TRUE;
		}

	if (draw && it->pixbuf && !it->blank)
		{
		cairo_t *cr;

		if (pr->func_post_process && (!pr->post_process_slow || !fast))
			pr->func_post_process(pr, &it->pixbuf, x, y, w, h);

		cr = cairo_create(it->surface);
		cairo_rectangle (cr, x, y, w, h);
		rt_hidpi_aware_draw(rt, cr, it->pixbuf, 0, 0);
		cairo_destroy (cr);
		}
}


void rt_tile_expose(RendererTiles *rt, ImageTile *it,
                    gint x, gint y, gint w, gint h,
                    gboolean new_data, gboolean fast)
{
	PixbufRenderer *pr = rt->pr;
	cairo_t *cr;

	/* clamp to visible */
	if (it->x + x < rt->x_scroll)
		{
		w -= rt->x_scroll - it->x - x;
		x = rt->x_scroll - it->x;
		}
	if (it->x + x + w > rt->x_scroll + pr->vis_width)
		{
		w = rt->x_scroll + pr->vis_width - it->x - x;
		}
	if (w < 1) return;
	if (it->y + y < rt->y_scroll)
		{
		h -= rt->y_scroll - it->y - y;
		y = rt->y_scroll - it->y;
		}
	if (it->y + y + h > rt->y_scroll + pr->vis_height)
		{
		h = rt->y_scroll + pr->vis_height - it->y - y;
		}
	if (h < 1) return;

	rt_tile_render(rt, it, x, y, w, h, new_data, fast);

	cr = cairo_create(rt->surface);
	cairo_set_source_surface(cr, it->surface, pr->x_offset + (it->x - rt->x_scroll) + rt->stereo_off_x, pr->y_offset + (it->y - rt->y_scroll) + rt->stereo_off_y);
	cairo_rectangle (cr, pr->x_offset + (it->x - rt->x_scroll) + x + rt->stereo_off_x, pr->y_offset + (it->y - rt->y_scroll) + y + rt->stereo_off_y, w, h);
	cairo_fill (cr);
	cairo_destroy (cr);

	if (rt->overlay_list)
		{
		rt_overlay_draw(rt,
		                {pr->x_offset + (it->x - rt->x_scroll) + x,
		                 pr->y_offset + (it->y - rt->y_scroll) + y,
		                 w, h},
		                it);
		}

	gtk_widget_queue_draw(GTK_WIDGET(rt->pr));
}


gboolean rt_tile_is_visible(RendererTiles *rt, ImageTile *it)
{
	PixbufRenderer *pr = rt->pr;
	return (it->x + it->w >= rt->x_scroll && it->x < rt->x_scroll + pr->vis_width &&
		it->y + it->h >= rt->y_scroll && it->y < rt->y_scroll + pr->vis_height);
}

/*
 *-------------------------------------------------------------------
 * draw queue
 *-------------------------------------------------------------------
 */

gint rt_get_queued_area(GList *work)
{
	gint area = 0;

	while (work)
		{
		auto qd = static_cast<QueueData *>(work->data);
		area += qd->w * qd->h;
		work = work->next;
		}
	return area;
}


gboolean rt_queue_schedule_next_draw(RendererTiles *rt, gboolean force_set)
{
	PixbufRenderer *pr = rt->pr;
	gfloat percent;
	gint visible_area = pr->vis_width * pr->vis_height;

	if (!pr->loading)
		{
		/* 2pass prio */
		DEBUG_2("redraw priority: 2pass");
		rt->draw_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, rt_queue_draw_idle_cb, rt, nullptr);
		return G_SOURCE_REMOVE;
		}

	if (visible_area == 0)
		{
		/* not known yet */
		percent = 100.0;
		}
	else
		{
		percent = 100.0 * rt_get_queued_area(rt->draw_queue) / visible_area;
		}

	if (percent > 10.0)
		{
		/* we have enough data for starting intensive redrawing */
		DEBUG_2("redraw priority: high %.2f %%", percent);
		rt->draw_idle_id = g_idle_add_full(GDK_PRIORITY_REDRAW, rt_queue_draw_idle_cb, rt, nullptr);
		return G_SOURCE_REMOVE;
		}

	if (percent < 1.0 || force_set)
		{
		/* queue is (almost) empty, wait  50 ms*/
		DEBUG_2("redraw priority: wait %.2f %%", percent);
		rt->draw_idle_id = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 50, rt_queue_draw_idle_cb, rt, nullptr);
		return G_SOURCE_REMOVE;
		}

	/* keep the same priority as before */
	DEBUG_2("redraw priority: no change %.2f %%", percent);
	return G_SOURCE_CONTINUE;
}


gboolean rt_queue_draw_idle_cb(gpointer data)
{
	auto rt = static_cast<RendererTiles *>(data);
	PixbufRenderer *pr = rt->pr;
	QueueData *qd;
	gboolean fast;

	if ((!pr->pixbuf && !pr->source_tiles_enabled) ||
	    (!rt->draw_queue && !rt->draw_queue_2pass) ||
	    !rt->draw_idle_id)
		{
		pr_render_complete_signal(pr);

		rt->draw_idle_id = 0;
		return G_SOURCE_REMOVE;
		}

	if (rt->draw_queue)
		{
		qd = static_cast<QueueData *>(rt->draw_queue->data);
		fast = (pr->zoom_2pass && ((pr->zoom_quality != GDK_INTERP_NEAREST && pr->scale != 1.0) || pr->post_process_slow));
		}
	else
		{
		if (pr->loading)
			{
			/* still loading, wait till done (also drops the higher priority) */

			return rt_queue_schedule_next_draw(rt, FALSE);
			}

		qd = static_cast<QueueData *>(rt->draw_queue_2pass->data);
		fast = FALSE;
		}

	if (gtk_widget_get_realized(GTK_WIDGET(pr)))
		{
		if (rt_tile_is_visible(rt, qd->it))
			{
			rt_tile_expose(rt, qd->it, qd->x, qd->y, qd->w, qd->h, qd->new_data, fast);
			}
		else if (qd->new_data)
			{
			/* if new pixel data, and we already have a pixmap, update the tile */
			qd->it->blank = FALSE;
			if (qd->it->surface && qd->it->render_done == TILE_RENDER_ALL)
				{
				rt_tile_render(rt, qd->it, qd->x, qd->y, qd->w, qd->h, qd->new_data, fast);
				}
			}
		}

	if (rt->draw_queue)
		{
		qd->it->qd = nullptr;
		rt->draw_queue = g_list_remove(rt->draw_queue, qd);
		if (fast)
			{
			if (qd->it->qd2)
				{
				rt_queue_merge(qd->it->qd2, qd);
				g_free(qd);
				}
			else
				{
				qd->it->qd2 = qd;
				rt->draw_queue_2pass = g_list_append(rt->draw_queue_2pass, qd);
				}
			}
		else
			{
			g_free(qd);
			}
		}
	else
		{
		qd->it->qd2 = nullptr;
		rt->draw_queue_2pass = g_list_remove(rt->draw_queue_2pass, qd);
		g_free(qd);
		}

	if (!rt->draw_queue && !rt->draw_queue_2pass)
		{
		pr_render_complete_signal(pr);

		rt->draw_idle_id = 0;
		return G_SOURCE_REMOVE;
		}

	return rt_queue_schedule_next_draw(rt, FALSE);
}

void rt_queue_data_free(gpointer data)
{
	auto *qd = static_cast<QueueData *>(data);

	qd->it->qd = nullptr;
	qd->it->qd2 = nullptr;
	g_free(qd);
}

void rt_queue_clear(RendererTiles *rt)
{
	g_list_free_full(rt->draw_queue, rt_queue_data_free);
	rt->draw_queue = nullptr;

	g_list_free_full(rt->draw_queue_2pass, rt_queue_data_free);
	rt->draw_queue_2pass = nullptr;

	g_clear_handle_id(&rt->draw_idle_id, g_source_remove);

	rt_sync_scroll(rt);
}

void rt_queue_merge(QueueData *parent, QueueData *qd)
{
	if (parent->x + parent->w < qd->x + qd->w)
		{
		parent->w += (qd->x + qd->w) - (parent->x + parent->w);
		}
	if (parent->x > qd->x)
		{
		parent->w += parent->x - qd->x;
		parent->x = qd->x;
		}

	if (parent->y + parent->h < qd->y + qd->h)
		{
		parent->h += (qd->y + qd->h) - (parent->y + parent->h);
		}
	if (parent->y > qd->y)
		{
		parent->h += parent->y - qd->y;
		parent->y = qd->y;
		}

	parent->new_data |= qd->new_data;
}

gboolean rt_clamp_to_visible(RendererTiles *rt, gint *x, gint *y, gint *w, gint *h)
{
	const gint vx = rt->x_scroll;
	const gint vy = rt->y_scroll;

	const PixbufRenderer *pr = rt->pr;
	const gint vw = pr->vis_width;
	const gint vh = pr->vis_height;

	if (vw < 1 || vh < 1 || *x + *w < vx || *x > vx + vw || *y + *h < vy || *y > vy + vh) return FALSE;

	/* now clamp it */
	const gint nx = std::max(*x, vx);
	*w = std::clamp(*w - (nx - *x), 1, vw);
	*x = nx;

	const gint ny = std::max(*y, vy);
	*h = std::clamp(*h - (ny - *y), 1, vh);
	*y = ny;

	return TRUE;
}

gboolean rt_queue_to_tiles(RendererTiles *rt, gint x, gint y, gint w, gint h,
                           gboolean clamp, ImageRenderType render,
                           gboolean new_data, gboolean only_existing)
{
	PixbufRenderer *pr = rt->pr;
	gint i;
	gint j;
	gint x1;
	gint x2;
	gint y1;
	gint y2;

	if (clamp && !rt_clamp_to_visible(rt, &x, &y, &w, &h)) return FALSE;

	x1 = ROUND_DOWN(x, rt->tile_width);
	x2 = ROUND_UP(x + w, rt->tile_width);

	y1 = ROUND_DOWN(y, rt->tile_height);
	y2 = ROUND_UP(y + h, rt->tile_height);

	for (j = y1; j <= y2; j += rt->tile_height)
		{
		for (i = x1; i <= x2; i += rt->tile_width)
			{
			ImageTile *it;

			it = rt_tile_get(rt, i, j,
					 (only_existing &&
					  (i + rt->tile_width < rt->x_scroll ||
					   i > rt->x_scroll + pr->vis_width ||
					   j + rt->tile_height < rt->y_scroll ||
					   j > rt->y_scroll + pr->vis_height)));
			if (it)
				{
				if ((render == TILE_RENDER_ALL && it->render_done != TILE_RENDER_ALL) ||
				    (render == TILE_RENDER_AREA && it->render_todo != TILE_RENDER_ALL))
					{
					it->render_todo = render;
					}

				auto qd = g_new(QueueData, 1);
				qd->it = it;
				qd->new_data = new_data;

				if (i < x)
					{
					qd->x = x - i;
					}
				else
					{
					qd->x = 0;
					}
				qd->w = x + w - i - qd->x;
				if (qd->x + qd->w > rt->tile_width) qd->w = rt->tile_width - qd->x;

				if (j < y)
					{
					qd->y = y - j;
					}
				else
					{
					qd->y = 0;
					}
				qd->h = y + h - j - qd->y;
				if (qd->y + qd->h > rt->tile_height) qd->h = rt->tile_height - qd->y;

				if (qd->w < 1 || qd->h < 1)
					{
					g_free(qd);
					}
				else if (it->qd)
					{
					rt_queue_merge(it->qd, qd);
					g_free(qd);
					}
				else
					{
					it->qd = qd;
					rt->draw_queue = g_list_append(rt->draw_queue, qd);
					}
				}
			}
		}

	return TRUE;
}

void rt_queue(RendererTiles *rt, gint x, gint y, gint w, gint h,
              gboolean clamp, ImageRenderType render,
              gboolean new_data, gboolean only_existing)
{
	PixbufRenderer *pr = rt->pr;

	rt_sync_scroll(rt);

	if (pr->width < 1 || pr->height < 1) return;

	const gint nx = std::clamp(x, 0, pr->width - 1);
	const gint ny = std::clamp(y, 0, pr->height - 1);

	w = std::clamp(w - (nx - x), 0, pr->width - nx);
	h = std::clamp(h - (ny - y), 0, pr->height - ny);
	if (w < 1 || h < 1) return;

	if (rt_queue_to_tiles(rt, nx, ny, w, h, clamp, render, new_data, only_existing) &&
	    ((!rt->draw_queue && !rt->draw_queue_2pass) || !rt->draw_idle_id))
		{
		g_clear_handle_id(&rt->draw_idle_id, g_source_remove);
		rt_queue_schedule_next_draw(rt, TRUE);
		}
}

void rt_scroll(void *renderer, gint x_off, gint y_off)
{
	auto rt = static_cast<RendererTiles *>(renderer);
	PixbufRenderer *pr = rt->pr;

	rt_sync_scroll(rt);
	if (rt->stereo_mode & PR_STEREO_MIRROR) x_off = -x_off;
	if (rt->stereo_mode & PR_STEREO_FLIP) y_off = -y_off;

	gint w = pr->vis_width - abs(x_off);
	gint h = pr->vis_height - abs(y_off);

	if (w < 1 || h < 1)
		{
		/* scrolled completely to new material */
		rt_queue(rt, 0, 0, pr->width, pr->height, TRUE, TILE_RENDER_ALL, FALSE, FALSE);
		return;
		}

		gint x1;
		gint y1;
		gint x2;
		gint y2;
		cairo_t *cr;
		cairo_surface_t *surface;

		if (x_off < 0)
			{
			x1 = abs(x_off);
			x2 = 0;
			}
		else
			{
			x1 = 0;
			x2 = abs(x_off);
			}

		if (y_off < 0)
			{
			y1 = abs(y_off);
			y2 = 0;
			}
		else
			{
			y1 = 0;
			y2 = abs(y_off);
			}

		cr = cairo_create(rt->surface);
		surface = rt->surface;

		/* clipping restricts the intermediate surface's size, so it's a good idea
		 * to use it. */
		cairo_rectangle(cr, x1 + pr->x_offset + rt->stereo_off_x, y1 + pr->y_offset + rt->stereo_off_y, w, h);
		cairo_clip (cr);
		/* Now push a group to change the target */
		cairo_push_group (cr);
		cairo_set_source_surface(cr, surface, x1 - x2, y1 - y2);
		cairo_paint(cr);
		/* Now copy the intermediate target back */
		cairo_pop_group_to_source(cr);
		cairo_paint(cr);
		cairo_destroy(cr);

		rt_overlay_queue_all(rt, x2, y2, x1, y1);

		w = pr->vis_width - w;
		h = pr->vis_height - h;

		if (w > 0)
			{
			rt_queue(rt,
				    x_off > 0 ? rt->x_scroll + (pr->vis_width - w) : rt->x_scroll, rt->y_scroll,
				    w, pr->vis_height, TRUE, TILE_RENDER_ALL, FALSE, FALSE);
			}
		if (h > 0)
			{
			/** @FIXME to optimize this, remove overlap */
			rt_queue(rt,
				    rt->x_scroll, y_off > 0 ? rt->y_scroll + (pr->vis_height - h) : rt->y_scroll,
				    pr->vis_width, h, TRUE, TILE_RENDER_ALL, FALSE, FALSE);
			}
}

void renderer_area_changed(void *renderer, GdkRectangle src)
{
	auto rt = static_cast<RendererTiles *>(renderer);
	PixbufRenderer *pr = rt->pr;
	gint x1;
	gint y1;
	gint x2;
	gint y2;

	gint orientation = rt_get_orientation(rt);
	src.x -= get_right_pixbuf_offset(rt);
	GdkRectangle rect = pr_coords_map_orientation_reverse(orientation, src,
	                                                      pr->image_width, pr->image_height);

	if (pr->scale != 1.0 && pr->zoom_quality != GDK_INTERP_NEAREST)
		{
		/* increase region when using a zoom quality that may access surrounding pixels */
		rect.y -= 1;
		rect.height += 2;
		}

	x1 = static_cast<gint>(floor(static_cast<gdouble>(rect.x) * pr->scale));
	y1 = static_cast<gint>(floor(static_cast<gdouble>(rect.y) * pr->scale * pr->aspect_ratio));
	x2 = static_cast<gint>(ceil(static_cast<gdouble>(rect.x + rect.width) * pr->scale));
	y2 = static_cast<gint>(ceil(static_cast<gdouble>(rect.y + rect.height) * pr->scale * pr->aspect_ratio));

	rt_queue(rt, x1, y1, x2 - x1, y2 - y1, FALSE, TILE_RENDER_AREA, TRUE, TRUE);
}

void renderer_redraw(RendererTiles *rt, gint x, gint y, gint w, gint h,
                     gint clamp, ImageRenderType render, gboolean new_data, gboolean only_existing)
{
	PixbufRenderer *pr = rt->pr;

	x -= rt->stereo_off_x;
	y -= rt->stereo_off_y;

	rt_border_draw(rt, {x, y, w, h});

	x = std::max(0, x - pr->x_offset + pr->x_scroll);
	y = std::max(0, y - pr->y_offset + pr->y_scroll);

	rt_queue(rt,
		 x, y,
		 std::min(w, pr->width - x),
		 std::min(h, pr->height - y),
		 clamp, render, new_data, only_existing);
}

void renderer_update_pixbuf(void *renderer, gboolean)
{
	rt_queue_clear(static_cast<RendererTiles *>(renderer));
}

void renderer_update_zoom(void *renderer, gboolean lazy)
{
	auto rt = static_cast<RendererTiles *>(renderer);
	PixbufRenderer *pr = rt->pr;

	rt_tile_invalidate_all(static_cast<RendererTiles *>(renderer));
	if (!lazy)
		{
		renderer_redraw(static_cast<RendererTiles *>(renderer), 0, 0, pr->width, pr->height, TRUE, TILE_RENDER_ALL, TRUE, FALSE);
		}
	rt_border_clear(rt);
}

void renderer_invalidate_region(void *renderer, GdkRectangle region)
{
	rt_tile_invalidate_region(static_cast<RendererTiles *>(renderer), region);
}

void renderer_update_viewport(void *renderer)
{
	auto rt = static_cast<RendererTiles *>(renderer);

	rt->stereo_off_x = 0;
	rt->stereo_off_y = 0;

	if (rt->stereo_mode & PR_STEREO_RIGHT)
		{
		if (rt->stereo_mode & PR_STEREO_HORIZ)
			{
			rt->stereo_off_x = rt->pr->viewport_width;
			}
		else if (rt->stereo_mode & PR_STEREO_VERT)
			{
			rt->stereo_off_y = rt->pr->viewport_height;
			}
		else if (rt->stereo_mode & PR_STEREO_FIXED)
			{
			rt->stereo_off_x = rt->pr->stereo_fixed_x_right;
			rt->stereo_off_y = rt->pr->stereo_fixed_y_right;
			}
		}
	else
		{
		if (rt->stereo_mode & PR_STEREO_FIXED)
			{
			rt->stereo_off_x = rt->pr->stereo_fixed_x_left;
			rt->stereo_off_y = rt->pr->stereo_fixed_y_left;
			}
		}
        DEBUG_1("update size: %p  %d %d   %d %d", (void *)rt, rt->stereo_off_x, rt->stereo_off_y, rt->pr->viewport_width, rt->pr->viewport_height);
	rt_sync_scroll(rt);
	rt_overlay_update_sizes(rt);
	rt_border_clear(rt);
}

void renderer_stereo_set(void *renderer, gint stereo_mode)
{
	auto rt = static_cast<RendererTiles *>(renderer);

	rt->stereo_mode = stereo_mode;
}

void renderer_free(void *renderer)
{
	auto rt = static_cast<RendererTiles *>(renderer);
	rt_queue_clear(rt);
	rt_tile_free_all(rt);
	if (rt->spare_tile) g_object_unref(rt->spare_tile);
	g_list_free_full(rt->overlay_list, reinterpret_cast<GDestroyNotify>(overlay_data_free));
	g_clear_pointer(&rt->overlay_buffer, cairo_surface_destroy);
	/* disconnect "hierarchy-changed" */
	g_signal_handlers_disconnect_matched(G_OBJECT(rt->pr), G_SIGNAL_MATCH_DATA,
	                                     0, 0, nullptr, nullptr, rt);
	g_free(rt);
}

gboolean rt_realize_cb(GtkWidget *widget, gpointer data)
{
	auto rt = static_cast<RendererTiles *>(data);
	cairo_t *cr;

	if (!rt->surface)
		{
		rt->surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget), CAIRO_CONTENT_COLOR, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget));

		cr = cairo_create(rt->surface);
		cairo_set_source_rgb(cr, rt->pr->color.red, rt->pr->color.green, rt->pr->color.blue);
		cairo_paint(cr);
		cairo_destroy(cr);
		}

	return FALSE;
}

gboolean rt_size_allocate_cb(GtkWidget *widget,  GdkRectangle *allocation, gpointer data)
{
	auto rt = static_cast<RendererTiles *>(data);
	cairo_t *cr;
	cairo_surface_t *old_surface;

	if (gtk_widget_get_realized(GTK_WIDGET(rt->pr)))
		{
		old_surface = rt->surface;
		rt->surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget), CAIRO_CONTENT_COLOR, allocation->width, allocation->height);

		cr = cairo_create(rt->surface);

		cairo_set_source_rgb(cr, options->image.border_color.red, options->image.border_color.green, options->image.border_color.blue);
		cairo_paint(cr);
		cairo_set_source_surface(cr, old_surface, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_destroy(old_surface);

		renderer_redraw(rt, allocation->x, allocation->y, allocation->width, allocation->height, FALSE, TILE_RENDER_ALL, FALSE, FALSE);
	}

	return FALSE;
}

gboolean rt_draw_cb(GtkWidget *, cairo_t *cr, gpointer data)
{
	auto rt = static_cast<RendererTiles *>(data);

	if (rt->stereo_mode & (PR_STEREO_HORIZ | PR_STEREO_VERT))
		{
		cairo_push_group(cr);
		cairo_set_source_rgb(cr, static_cast<double>(rt->pr->color.red), static_cast<double>(rt->pr->color.green), static_cast<double>(rt->pr->color.blue));

		if (rt->stereo_mode & PR_STEREO_HORIZ)
			{
			cairo_rectangle(cr, rt->stereo_off_x, 0, rt->pr->viewport_width, rt->pr->viewport_height);
			}
		else
			{
			cairo_rectangle(cr, 0, rt->stereo_off_y, rt->pr->viewport_width, rt->pr->viewport_height);
			}
		cairo_clip(cr);
		cairo_paint(cr);

		cairo_rectangle(cr, rt->pr->x_offset + rt->stereo_off_x, rt->pr->y_offset + rt->stereo_off_y, rt->pr->vis_width, rt->pr->vis_height);
		cairo_clip(cr);
		cairo_set_source_surface(cr, rt->surface, 0, 0);
		cairo_paint(cr);

		cairo_pop_group_to_source(cr);
		cairo_paint(cr);
		}
	else
		{
		cairo_set_source_surface(cr, rt->surface, 0, 0);
		cairo_paint(cr);
		}

	for (GList *work = rt->overlay_list; work; work = work->next)
		{
		auto *od = static_cast<OverlayData *>(work->data);
		GdkRectangle od_rect = rt_overlay_get_position(rt, od);

		gdk_cairo_set_source_pixbuf(cr, od->pixbuf, od_rect.x, od_rect.y);
		cairo_paint(cr);
		}

	return FALSE;
}

} // namespace

RendererFuncs *renderer_tiles_new(PixbufRenderer *pr)
{
	auto rt = g_new0(RendererTiles, 1);

	rt->pr = pr;

	rt->f.area_changed = renderer_area_changed;
	rt->f.update_pixbuf = renderer_update_pixbuf;
	rt->f.free = renderer_free;
	rt->f.update_zoom = renderer_update_zoom;
	rt->f.invalidate_region = renderer_invalidate_region;
	rt->f.scroll = rt_scroll;
	rt->f.update_viewport = renderer_update_viewport;


	rt->f.overlay_add = renderer_tiles_overlay_add;
	rt->f.overlay_set = renderer_tiles_overlay_set;
	rt->f.overlay_get = renderer_tiles_overlay_get;

	rt->f.stereo_set = renderer_stereo_set;

	rt->tile_width = options->image.tile_size;
	rt->tile_height = options->image.tile_size;

	rt->tiles = nullptr;
	rt->tile_cache_size = 0;

	rt->tile_cache_max = PR_CACHE_SIZE_DEFAULT;

	rt->draw_idle_id = 0;

	rt->stereo_mode = 0;
	rt->stereo_off_x = 0;
	rt->stereo_off_y = 0;

	rt->hidpi_scale = gtk_widget_get_scale_factor(GTK_WIDGET(rt->pr));

	g_signal_connect(G_OBJECT(pr), "hierarchy-changed",
			 G_CALLBACK(rt_hierarchy_changed_cb), rt);

	g_signal_connect(G_OBJECT(pr), "draw",
	                 G_CALLBACK(rt_draw_cb), rt);
	g_signal_connect(G_OBJECT(pr), "realize", G_CALLBACK(rt_realize_cb), rt);
	g_signal_connect(G_OBJECT(pr), "size-allocate", G_CALLBACK(rt_size_allocate_cb), rt);

	return reinterpret_cast<RendererFuncs *>(rt);
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
