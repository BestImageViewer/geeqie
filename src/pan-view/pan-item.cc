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

#include "pan-item.h"

#include <algorithm>
#include <cstring>

#include <glib-object.h>
#include <gtk/gtk.h>
#include <pango/pango.h>

#include "cache.h"
#include "filedata.h"
#include "image.h"
#include "pan-types.h"
#include "pixbuf-util.h"
#include "ui-misc.h"

namespace
{

constexpr gint PAN_OUTLINE_THICKNESS = 1;
#define PAN_OUTLINE_ALPHA 180
#define PAN_OUTLINE_COLOR_1 255, 255, 255, PAN_OUTLINE_ALPHA
#define PAN_OUTLINE_COLOR_2 64, 64, 64, PAN_OUTLINE_ALPHA

/* popup info box */
constexpr PanColor PAN_POPUP_TEXT_COLOR{0, 0, 0, 225};

} // namespace

/*
 *-----------------------------------------------------------------------------
 * item base functions
 *-----------------------------------------------------------------------------
 */

void pan_item_free(PanItem *pi)
{
	if (!pi) return;

	if (pi->pixbuf) g_object_unref(pi->pixbuf);
	if (pi->fd) file_data_unref(pi->fd);
	g_free(pi->text);
	g_free(pi->key);
	g_free(pi->data);

	g_free(pi);
}

void pan_item_set_key(PanItem *pi, const gchar *key)
{
	gchar *tmp;

	if (!pi) return;

	tmp = pi->key;
	pi->key = g_strdup(key);
	g_free(tmp);
}

void pan_item_added(PanWindow *pw, PanItem *pi)
{
	if (!pi) return;
	image_area_changed(pw->imd, pi->x, pi->y, pi->width, pi->height);
}

void pan_item_remove(PanWindow *pw, PanItem *pi)
{
	if (!pi) return;

	if (pw->click_pi == pi) pw->click_pi = nullptr;
	if (pw->queue_pi == pi)	pw->queue_pi = nullptr;
	if (pw->search_pi == pi) pw->search_pi = nullptr;
	pw->queue = g_list_remove(pw->queue, pi);

	pw->list = g_list_remove(pw->list, pi);
	image_area_changed(pw->imd, pi->x, pi->y, pi->width, pi->height);
	pan_item_free(pi);
}

void pan_item_size_by_item(PanItem *pi, PanItem *child, gint border)
{
	if (!pi || !child) return;

	if (pi->x + pi->width < child->x + child->width + border)
		pi->width = child->x + child->width + border - pi->x;

	if (pi->y + pi->height < child->y + child->height + border)
		pi->height = child->y + child->height + border - pi->y;
}

void pan_item_size_coordinates(PanItem *pi, gint border, gint &w, gint &h)
{
	if (!pi) return;

	w = std::max(w, pi->x + pi->width + border);
	h = std::max(h, pi->y + pi->height + border);
}


/*
 *-----------------------------------------------------------------------------
 * item box type
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_box_new(PanWindow *pw, FileData *fd, gint x, gint y, gint width, gint height,
                          gint border_size, const PanColor &base, const PanColor &bord)
{
	PanItem *pi;

	pi = g_new0(PanItem, 1);
	pi->type = PAN_ITEM_BOX;
	pi->fd = fd;
	pi->x = x;
	pi->y = y;
	pi->width = width;
	pi->height = height;

	pi->color = base;

	pi->color2 = bord;
	pi->border = border_size;

	pw->list = g_list_prepend(pw->list, pi);

	return pi;
}

void pan_item_box_shadow(PanItem *pi, gint offset, gint fade)
{
	gint *shadow;

	if (!pi || pi->type != PAN_ITEM_BOX) return;

	shadow = static_cast<gint *>(pi->data);
	if (shadow)
		{
		pi->width -= shadow[0];
		pi->height -= shadow[0];
		}

	shadow = g_new0(gint, 2);
	shadow[0] = offset;
	shadow[1] = fade;

	pi->width += offset;
	pi->height += offset;

	g_free(pi->data);
	pi->data = shadow;
}

gint pan_item_box_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *,
		       gint x, gint y, gint width, gint height)
{
	gint bw;
	gint bh;
	gint *shadow;
	gint rx;
	gint ry;
	gint rw;
	gint rh;

	bw = pi->width;
	bh = pi->height;

	shadow = static_cast<gint *>(pi->data);
	if (shadow)
		{
		bw -= shadow[0];
		bh -= shadow[0];

		if (pi->color.a > 254)
			{
			pixbuf_draw_shadow(pixbuf, pi->x - x + bw, pi->y - y + shadow[0],
					   shadow[0], bh - shadow[0],
					   pi->x - x + shadow[0], pi->y - y + shadow[0], bw, bh,
					   shadow[1],
					   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
			pixbuf_draw_shadow(pixbuf, pi->x - x + shadow[0], pi->y - y + bh,
					   bw, shadow[0],
					   pi->x - x + shadow[0], pi->y - y + shadow[0], bw, bh,
					   shadow[1],
					   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
			}
		else
			{
			gint a;
			a = pi->color.a * PAN_SHADOW_ALPHA >> 8;
			pixbuf_draw_shadow(pixbuf, pi->x - x + shadow[0], pi->y - y + shadow[0],
					   bw, bh,
					   pi->x - x + shadow[0], pi->y - y + shadow[0], bw, bh,
					   shadow[1],
					   PAN_SHADOW_COLOR, a);
			}
		}

	if (util_clip_region(x, y, width, height,
	                     pi->x, pi->y, bw, bh,
	                     rx, ry, rw, rh))
		{
		pixbuf_draw_rect_fill(pixbuf,
				      rx - x, ry - y, rw, rh,
				      pi->color.r, pi->color.g, pi->color.b, pi->color.a);
		}
	if (util_clip_region(x, y, width, height,
	                     pi->x, pi->y, bw, pi->border,
	                     rx, ry, rw, rh))
		{
		pixbuf_draw_rect_fill(pixbuf,
				      rx - x, ry - y, rw, rh,
				      pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
		}
	if (util_clip_region(x, y, width, height,
	                     pi->x, pi->y + pi->border, pi->border, bh - pi->border * 2,
	                     rx, ry, rw, rh))
		{
		pixbuf_draw_rect_fill(pixbuf,
				      rx - x, ry - y, rw, rh,
				      pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
		}
	if (util_clip_region(x, y, width, height,
	                     pi->x + bw - pi->border, pi->y + pi->border,
	                     pi->border, bh - pi->border * 2,
	                     rx, ry, rw, rh))
		{
		pixbuf_draw_rect_fill(pixbuf,
				      rx - x, ry - y, rw, rh,
				      pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
		}
	if (util_clip_region(x, y, width, height,
	                     pi->x, pi->y + bh - pi->border,
	                     bw,  pi->border,
	                     rx, ry, rw, rh))
		{
		pixbuf_draw_rect_fill(pixbuf,
				      rx - x, ry - y, rw, rh,
				      pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
		}

	return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 * item triangle type
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_tri_new(PanWindow *pw,
                          gint x1, gint y1, gint x2, gint y2, gint x3, gint y3,
                          const PanColor &color)
{
	PanItem *pi;
	gint *coord;

	pi = g_new0(PanItem, 1);
	pi->type = PAN_ITEM_TRIANGLE;
	pi->color = color;

	util_clip_triangle(x1, y1, x2, y2, x3, y3,
	                   pi->x, pi->y, pi->width, pi->height);

	coord = g_new0(gint, 6);
	coord[0] = x1;
	coord[1] = y1;
	coord[2] = x2;
	coord[3] = y2;
	coord[4] = x3;
	coord[5] = y3;

	pi->data = coord;

	pi->border = PAN_BORDER_NONE;

	pw->list = g_list_prepend(pw->list, pi);

	return pi;
}

void pan_item_tri_border(PanItem *pi, gint borders, const PanColor &color)
{
	if (!pi || pi->type != PAN_ITEM_TRIANGLE) return;

	pi->border = borders;

	pi->color2 = color;
}

gint pan_item_tri_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *, gint x, gint y, gint width, gint height)
{
	gint rx;
	gint ry;
	gint rw;
	gint rh;

	if (util_clip_region(x, y, width, height,
	                     pi->x, pi->y, pi->width, pi->height,
	                     rx, ry, rw, rh) && pi->data)
		{
		auto coord = static_cast<gint *>(pi->data);
		pixbuf_draw_triangle(pixbuf,
				     rx - x, ry - y, rw, rh,
				     coord[0] - x, coord[1] - y,
				     coord[2] - x, coord[3] - y,
				     coord[4] - x, coord[5] - y,
				     pi->color.r, pi->color.g, pi->color.b, pi->color.a);

		if (pi->border & PAN_BORDER_1)
			{
			pixbuf_draw_line(pixbuf,
					 rx - x, ry - y, rw, rh,
					 coord[0] - x, coord[1] - y,
					 coord[2] - x, coord[3] - y,
					 pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
			}
		if (pi->border & PAN_BORDER_2)
			{
			pixbuf_draw_line(pixbuf,
					 rx - x, ry - y, rw, rh,
					 coord[2] - x, coord[3] - y,
					 coord[4] - x, coord[5] - y,
					 pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
			}
		if (pi->border & PAN_BORDER_3)
			{
			pixbuf_draw_line(pixbuf,
					 rx - x, ry - y, rw, rh,
					 coord[4] - x, coord[5] - y,
					 coord[0] - x, coord[1] - y,
					 pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
			}
		}

	return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 * item text type
 *-----------------------------------------------------------------------------
 */

static PangoLayout *pan_item_text_layout(PanItem *pi, GtkWidget *widget)
{
	PangoLayout *layout;

	layout = gtk_widget_create_pango_layout(widget, nullptr);

	if (pi->text_attr & PAN_TEXT_ATTR_MARKUP)
		{
		pango_layout_set_markup(layout, pi->text, -1);
		return layout;
		}

	if (pi->text_attr & PAN_TEXT_ATTR_BOLD ||
	    pi->text_attr & PAN_TEXT_ATTR_HEADING)
		{
		PangoAttrList *pal;
		PangoAttribute *pa;

		pal = pango_attr_list_new();
		if (pi->text_attr & PAN_TEXT_ATTR_BOLD)
			{
			pa = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
			pa->start_index = 0;
			pa->end_index = G_MAXINT;
			pango_attr_list_insert(pal, pa);
			}
		if (pi->text_attr & PAN_TEXT_ATTR_HEADING)
			{
			pa = pango_attr_scale_new(PANGO_SCALE_LARGE);
			pa->start_index = 0;
			pa->end_index = G_MAXINT;
			pango_attr_list_insert(pal, pa);
			}
		pango_layout_set_attributes(layout, pal);
		pango_attr_list_unref(pal);
		}

	pango_layout_set_text(layout, pi->text, -1);
	return layout;
}

static void pan_item_text_compute_size(PanItem *pi, GtkWidget *widget)
{
	PangoLayout *layout;

	if (!pi || !pi->text || !widget) return;

	layout = pan_item_text_layout(pi, widget);
	pango_layout_get_pixel_size(layout, &pi->width, &pi->height);
	g_object_unref(G_OBJECT(layout));

	pi->width += pi->border * 2;
	pi->height += pi->border * 2;
}

PanItem *pan_item_text_new(PanWindow *pw, gint x, gint y, const gchar *text,
                           PanTextAttrType attr, PanBorderType border, const PanColor &color)
{
	PanItem *pi;

	pi = g_new0(PanItem, 1);
	pi->type = PAN_ITEM_TEXT;
	pi->x = x;
	pi->y = y;
	pi->text = g_strdup(text);
	pi->text_attr = attr;

	pi->color = color;

	pi->border = border;

	pan_item_text_compute_size(pi, pw->imd->pr);

	pw->list = g_list_prepend(pw->list, pi);

	return pi;
}

gint pan_item_text_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *pr, gint x, gint y, gint, gint)
{
	PangoLayout *layout;

	layout = pan_item_text_layout(pi, reinterpret_cast<GtkWidget *>(pr));
	pixbuf_draw_layout(pixbuf, layout, reinterpret_cast<GtkWidget *>(pr),
			   pi->x - x + pi->border, pi->y - y + pi->border,
			   pi->color.r, pi->color.g, pi->color.b, pi->color.a);
	g_object_unref(G_OBJECT(layout));

	return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 * item thumbnail type
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_thumb_new(PanWindow *pw, FileData *fd, gint x, gint y)
{
	PanItem *pi;

	pi = g_new0(PanItem, 1);

	pi->type = PAN_ITEM_THUMB;
	pi->fd = fd;
	pi->x = x;
	pi->y = y;
	pi->width = PAN_THUMB_SIZE + PAN_SHADOW_OFFSET * 2;
	pi->height = PAN_THUMB_SIZE + PAN_SHADOW_OFFSET * 2;

	pw->list = g_list_prepend(pw->list, pi);

	return pi;
}

gint pan_item_thumb_draw(PanWindow *pw, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *, gint x, gint y, gint width, gint height)
{
	gint tx;
	gint ty;
	gint tw;
	gint th;
	gint rx;
	gint ry;
	gint rw;
	gint rh;

	if (pi->pixbuf)
		{
		tw = gdk_pixbuf_get_width(pi->pixbuf);
		th = gdk_pixbuf_get_height(pi->pixbuf);

		tx = pi->x + (pi->width - tw) / 2;
		ty = pi->y + (pi->height - th) / 2;

		if (gdk_pixbuf_get_has_alpha(pi->pixbuf))
			{
			if (util_clip_region(x, y, width, height,
			                     tx + PAN_SHADOW_OFFSET, ty + PAN_SHADOW_OFFSET, tw, th,
			                     rx, ry, rw, rh))
				{
				pixbuf_draw_shadow(pixbuf,
						   rx - x, ry - y, rw, rh,
						   tx + PAN_SHADOW_OFFSET - x, ty + PAN_SHADOW_OFFSET - y, tw, th,
						   PAN_SHADOW_FADE,
						   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
				}
			}
		else
			{
			if (util_clip_region(x, y, width, height,
			                     tx + tw, ty + PAN_SHADOW_OFFSET,
			                     PAN_SHADOW_OFFSET, th - PAN_SHADOW_OFFSET,
			                     rx, ry, rw, rh))
				{
				pixbuf_draw_shadow(pixbuf,
						   rx - x, ry - y, rw, rh,
						   tx + PAN_SHADOW_OFFSET - x, ty + PAN_SHADOW_OFFSET - y, tw, th,
						   PAN_SHADOW_FADE,
						   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
				}
			if (util_clip_region(x, y, width, height,
			                     tx + PAN_SHADOW_OFFSET, ty + th, tw, PAN_SHADOW_OFFSET,
			                     rx, ry, rw, rh))
				{
				pixbuf_draw_shadow(pixbuf,
						   rx - x, ry - y, rw, rh,
						   tx + PAN_SHADOW_OFFSET - x, ty + PAN_SHADOW_OFFSET - y, tw, th,
						   PAN_SHADOW_FADE,
						   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
				}
			}

		if (util_clip_region(x, y, width, height,
		                     tx, ty, tw, th,
		                     rx, ry, rw, rh))
			{
			gdk_pixbuf_composite(pi->pixbuf, pixbuf, rx - x, ry - y, rw, rh,
					     static_cast<gdouble>(tx) - x,
					     static_cast<gdouble>(ty) - y,
					     1.0, 1.0, GDK_INTERP_NEAREST,
					     255);
			}

		if (util_clip_region(x, y, width, height,
		                     tx, ty, tw, PAN_OUTLINE_THICKNESS,
		                     rx, ry, rw, rh))
			{
			pixbuf_draw_rect_fill(pixbuf,
					      rx - x, ry - y, rw, rh,
					      PAN_OUTLINE_COLOR_1);
			}
		if (util_clip_region(x, y, width, height,
		                     tx, ty, PAN_OUTLINE_THICKNESS, th,
		                     rx, ry, rw, rh))
			{
			pixbuf_draw_rect_fill(pixbuf,
					      rx - x, ry - y, rw, rh,
					      PAN_OUTLINE_COLOR_1);
			}
		if (util_clip_region(x, y, width, height,
		                     tx + tw - PAN_OUTLINE_THICKNESS, ty +  PAN_OUTLINE_THICKNESS,
		                     PAN_OUTLINE_THICKNESS, th - PAN_OUTLINE_THICKNESS,
		                     rx, ry, rw, rh))
			{
			pixbuf_draw_rect_fill(pixbuf,
					      rx - x, ry - y, rw, rh,
					      PAN_OUTLINE_COLOR_2);
			}
		if (util_clip_region(x, y, width, height,
		                     tx +  PAN_OUTLINE_THICKNESS, ty + th - PAN_OUTLINE_THICKNESS,
		                     tw - PAN_OUTLINE_THICKNESS * 2, PAN_OUTLINE_THICKNESS,
		                     rx, ry, rw, rh))
			{
			pixbuf_draw_rect_fill(pixbuf,
					      rx - x, ry - y, rw, rh,
					      PAN_OUTLINE_COLOR_2);
			}
		}
	else
		{
		tw = pi->width - PAN_SHADOW_OFFSET * 2;
		th = pi->height - PAN_SHADOW_OFFSET * 2;
		tx = pi->x + PAN_SHADOW_OFFSET;
		ty = pi->y + PAN_SHADOW_OFFSET;

		if (util_clip_region(x, y, width, height,
		                     tx, ty, tw, th,
		                     rx, ry, rw, rh))
			{
			gint d;

			d = (pw->size <= PAN_IMAGE_SIZE_THUMB_NONE) ? 2 : 8;
			pixbuf_draw_rect_fill(pixbuf,
					      rx - x, ry - y, rw, rh,
					      PAN_SHADOW_COLOR,
					      PAN_SHADOW_ALPHA / d);
			}
		}

	return (pi->pixbuf == nullptr);
}


/*
 *-----------------------------------------------------------------------------
 * item image type
 *-----------------------------------------------------------------------------
 */

static void pan_item_image_find_size(PanWindow *pw, const FileData *fd, gint &w, gint &h)
{
	GList *work;

	if (!fd) return;

	work = pw->cache_list;
	while (work)
		{
		PanCacheData *pc;

		pc = static_cast<PanCacheData *>(work->data);
		work = work->next;

		if (pc->cd && pc->cd->dimensions &&
		    pc->fd == fd)
			{
			w = MAX(1, pc->cd->width * pw->image_size / 100);
			h = MAX(1, pc->cd->height * pw->image_size / 100);

			pw->cache_list = g_list_remove(pw->cache_list, pc);
			pan_cache_data_free(pc);
			return;
			}
		}
}

PanItem *pan_item_image_new(PanWindow *pw, FileData *fd, gint x, gint y, gint w, gint h)
{
	PanItem *pi;

	pan_item_image_find_size(pw, fd, w, h);

	pi = g_new0(PanItem, 1);
	pi->type = PAN_ITEM_IMAGE;
	pi->fd = fd;
	pi->x = x;
	pi->y = y;
	pi->width = w;
	pi->height = h;

	pi->color.a = 255;

	pi->color2.r = 0;
	pi->color2.g = 0;
	pi->color2.b = 0;
	pi->color2.a = PAN_SHADOW_ALPHA / 2;

	pw->list = g_list_prepend(pw->list, pi);

	return pi;
}

gint pan_item_image_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *, gint x, gint y, gint width, gint height)
{
	gint rx;
	gint ry;
	gint rw;
	gint rh;

	if (util_clip_region(x, y, width, height,
	                     pi->x, pi->y, pi->width, pi->height,
	                     rx, ry, rw, rh))
		{
		if (pi->pixbuf)
			{
			gdk_pixbuf_composite(pi->pixbuf, pixbuf, rx - x, ry - y, rw, rh,
					     static_cast<gdouble>(pi->x) - x,
					     static_cast<gdouble>(pi->y) - y,
					     1.0, 1.0, GDK_INTERP_NEAREST,
					     pi->color.a);
			}
		else
			{
			pixbuf_draw_rect_fill(pixbuf,
					      rx - x, ry - y, rw, rh,
					      pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
			}
		}

	return (pi->pixbuf == nullptr);
}


/*
 *-----------------------------------------------------------------------------
 * item lookup/search
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_find_by_key(PanWindow *pw, PanItemType type, const gchar *key)
{
	GList *work;

	if (!key) return nullptr;

	work = g_list_last(pw->list);
	while (work)
		{
		PanItem *pi;

		pi = static_cast<PanItem *>(work->data);
		if ((pi->type == type || type == PAN_ITEM_NONE) &&
		     pi->key && strcmp(pi->key, key) == 0)
			{
			return pi;
			}
		work = work->prev;
		}
	work = g_list_last(pw->list_static);
	while (work)
		{
		PanItem *pi;

		pi = static_cast<PanItem *>(work->data);
		if ((pi->type == type || type == PAN_ITEM_NONE) &&
		     pi->key && strcmp(pi->key, key) == 0)
			{
			return pi;
			}
		work = work->prev;
		}

	return nullptr;
}

/* when ignore_case and partial are TRUE, path should be converted to lower case */
static GList *pan_item_find_by_path_l(GList *list, GList *search_list,
				      PanItemType type, const gchar *path,
				      gboolean ignore_case, gboolean partial)
{
	GList *work;

	work = g_list_last(search_list);
	while (work)
		{
		PanItem *pi;

		pi = static_cast<PanItem *>(work->data);
		if ((pi->type == type || type == PAN_ITEM_NONE) && pi->fd)
			{
			gboolean match = FALSE;

			if (path[0] == G_DIR_SEPARATOR)
				{
				if (pi->fd->path && strcmp(path, pi->fd->path) == 0) match = TRUE;
				}
			else if (pi->fd->name)
				{
				if (partial)
					{
					if (ignore_case)
						{
						gchar *haystack;

						haystack = g_utf8_strdown(pi->fd->name, -1);
						match = (strstr(haystack, path) != nullptr);
						g_free(haystack);
						}
					else
						{
						if (strstr(pi->fd->name, path)) match = TRUE;
						}
					}
				else if (ignore_case)
					{
					if (g_ascii_strcasecmp(path, pi->fd->name) == 0) match = TRUE;
					}
				else
					{
					if (strcmp(path, pi->fd->name) == 0) match = TRUE;
					}
				}

			if (match) list = g_list_prepend(list, pi);
			}
		work = work->prev;
		}

	return list;
}

/* when ignore_case and partial are TRUE, path should be converted to lower case */
GList *pan_item_find_by_path(PanWindow *pw, PanItemType type, const gchar *path,
			     gboolean ignore_case, gboolean partial)
{
	GList *list = nullptr;

	if (!path) return nullptr;
	if (partial && path[0] == G_DIR_SEPARATOR) return nullptr;

	list = pan_item_find_by_path_l(list, pw->list_static, type, path, ignore_case, partial);
	list = pan_item_find_by_path_l(list, pw->list, type, path, ignore_case, partial);

	return g_list_reverse(list);
}

GList *pan_item_find_by_fd(PanWindow *pw, PanItemType type, FileData *fd,
			   gboolean ignore_case, gboolean partial)
{
	if (!fd) return nullptr;
	return pan_item_find_by_path(pw, type, fd->path, ignore_case, partial);
}


static PanItem *pan_item_find_by_coord_l(GList *list, PanItemType type, gint x, gint y, const gchar *key)
{
	GList *work;

	work = list;
	while (work)
		{
		PanItem *pi;

		pi = static_cast<PanItem *>(work->data);
		if ((pi->type == type || type == PAN_ITEM_NONE) &&
		     x >= pi->x && x < pi->x + pi->width &&
		     y >= pi->y && y < pi->y + pi->height &&
		    (!key || (pi->key && strcmp(pi->key, key) == 0)))
			{
			return pi;
			}
		work = work->next;
		}

	return nullptr;
}

PanItem *pan_item_find_by_coord(PanWindow *pw, PanItemType type,
				gint x, gint y, const gchar *key)
{
	PanItem *pi;

	pi = pan_item_find_by_coord_l(pw->list, type, x, y, key);
	if (pi) return pi;

	return pan_item_find_by_coord_l(pw->list_static, type, x, y, key);
}


/*
 *-----------------------------------------------------------------------------
 * text alignments
 *-----------------------------------------------------------------------------
 */

PanTextAlignment *pan_text_alignment_new(PanWindow *pw, gint x, gint y, const gchar *key)
{
	PanTextAlignment *ta;

	ta = g_new0(PanTextAlignment, 1);

	ta->pw = pw;
	ta->x = x;
	ta->y = y;
	ta->key = g_strdup(key);

	return ta;
}

void pan_text_alignment_free(PanTextAlignment *ta)
{
	if (!ta) return;

	g_list_free(ta->column1);
	g_list_free(ta->column2);
	g_free(ta->key);
	g_free(ta);
}

PanItem *pan_text_alignment_add(PanTextAlignment *ta, const gchar *label, const gchar *text)
{
	PanItem *item;

	if (label)
		{
		item = pan_item_text_new(ta->pw, ta->x, ta->y, label,
					 PAN_TEXT_ATTR_BOLD, PAN_BORDER_NONE, PAN_POPUP_TEXT_COLOR);
		pan_item_set_key(item, ta->key);
		}
	else
		{
		item = nullptr;
		}
	ta->column1 = g_list_append(ta->column1, item);

	if (text)
		{
		item = pan_item_text_new(ta->pw, ta->x, ta->y, text,
					 PAN_TEXT_ATTR_NONE, PAN_BORDER_NONE, PAN_POPUP_TEXT_COLOR);
		pan_item_set_key(item, ta->key);
		}
	else
		{
		item = nullptr;
		}
	ta->column2 = g_list_append(ta->column2, item);

	return item;
}

void pan_text_alignment_calc(PanTextAlignment *ta, PanItem *box)
{
	gint cw1;
	gint cw2;
	gint x;
	gint y;
	GList *work1;
	GList *work2;

	cw1 = 0;
	cw2 = 0;

	work1 = ta->column1;
	while (work1)
		{
		PanItem *p;

		p = static_cast<PanItem *>(work1->data);
		work1 = work1->next;

		if (p && p->width > cw1) cw1 = p->width;
		}

	work2 = ta->column2;
	while (work2)
		{
		PanItem *p;

		p = static_cast<PanItem *>(work2->data);
		work2 = work2->next;

		if (p && p->width > cw2) cw2 = p->width;
		}

	x = ta->x;
	y = ta->y;
	work1 = ta->column1;
	work2 = ta->column2;
	while (work1 && work2)
		{
		PanItem *p1;
		PanItem *p2;
		gint height = 0;

		p1 = static_cast<PanItem *>(work1->data);
		p2 = static_cast<PanItem *>(work2->data);
		work1 = work1->next;
		work2 = work2->next;

		if (p1)
			{
			p1->x = x;
			p1->y = y;
			pan_item_size_by_item(box, p1, PREF_PAD_BORDER);
			height = p1->height;
			}
		if (p2)
			{
			p2->x = x + cw1 + PREF_PAD_SPACE;
			p2->y = y;
			pan_item_size_by_item(box, p2, PREF_PAD_BORDER);
			if (height < p2->height) height = p2->height;
			}

		if (!p1 && !p2) height = PREF_PAD_GROUP;

		y += height;
		}
}


/*
 *-----------------------------------------------------------------------------
 * cache data
 *-----------------------------------------------------------------------------
 */

void pan_cache_data_free(PanCacheData *pc)
{
	if (!pc) return;

	cache_sim_data_free(pc->cd);
	file_data_unref(pc->fd);
	g_free(pc);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
