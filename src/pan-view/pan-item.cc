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

#include "filedata.h"
#include "image.h"
#include "pan-types.h"
#include "pan-view.h"
#include "pixbuf-util.h"
#include "ui-misc.h"

namespace
{

struct PanItemBoxShadow
{
	gint offset;
	gint fade;
};

constexpr gint PAN_OUTLINE_THICKNESS = 1;
constexpr guint8 PAN_OUTLINE_ALPHA = 180;
constexpr PanColor PAN_OUTLINE_COLOR_1{255, 255, 255, PAN_OUTLINE_ALPHA};
constexpr PanColor PAN_OUTLINE_COLOR_2{64, 64, 64, PAN_OUTLINE_ALPHA};

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
	if (!pi) return;

	g_free(pi->key);
	pi->key = g_strdup(key);
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
                          gint border_size, PanColor base, PanColor bord)
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
	if (!pi || pi->type != PAN_ITEM_BOX) return;

	auto *shadow = static_cast<PanItemBoxShadow *>(pi->data);
	if (shadow)
		{
		pi->width -= shadow->offset;
		pi->height -= shadow->offset;
		}

	shadow = g_new0(PanItemBoxShadow, 1);
	shadow->offset = offset;
	shadow->fade = fade;

	pi->width += offset;
	pi->height += offset;

	g_free(pi->data);
	pi->data = shadow;
}

gboolean pan_item_box_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *,
                           gint x, gint y, gint width, gint height)
{
	gint bw;
	gint bh;

	bw = pi->width;
	bh = pi->height;

	auto *shadow = static_cast<PanItemBoxShadow *>(pi->data);
	if (shadow)
		{
		bw -= shadow->offset;
		bh -= shadow->offset;

		if (pi->color.a > 254)
			{
			pixbuf_draw_shadow(pixbuf,
			                   {pi->x - x + bw, pi->y - y + shadow->offset, shadow->offset, bh - shadow->offset},
			                   pi->x - x + shadow->offset, pi->y - y + shadow->offset, bw, bh,
			                   shadow->fade,
			                   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
			pixbuf_draw_shadow(pixbuf,
			                   {pi->x - x + shadow->offset, pi->y - y + bh, bw, shadow->offset},
			                   pi->x - x + shadow->offset, pi->y - y + shadow->offset, bw, bh,
			                   shadow->fade,
			                   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
			}
		else
			{
			gint a;
			a = pi->color.a * PAN_SHADOW_ALPHA >> 8;
			pixbuf_draw_shadow(pixbuf,
			                   {pi->x - x + shadow->offset, pi->y - y + shadow->offset, bw, bh},
			                   pi->x - x + shadow->offset, pi->y - y + shadow->offset, bw, bh,
			                   shadow->fade,
			                   PAN_SHADOW_COLOR, a);
			}
		}

	const GdkRectangle request_rect{x, y, width, height};
	const auto draw_rect_if_intersect = [pixbuf, &request_rect, x, y](GdkRectangle box_rect, PanColor color)
	{
		GdkRectangle r;
		if (!gdk_rectangle_intersect(&request_rect, &box_rect, &r)) return;

		r.x -= x;
		r.y -= y;
		pixbuf_draw_rect_fill(pixbuf, r, color.r, color.g, color.b, color.a);
	};

	draw_rect_if_intersect({pi->x, pi->y, bw, bh}, pi->color);

	draw_rect_if_intersect({pi->x, pi->y, bw, pi->border}, pi->color2);
	draw_rect_if_intersect({pi->x, pi->y + pi->border, pi->border, bh - (pi->border * 2)}, pi->color2);
	draw_rect_if_intersect({pi->x + bw - pi->border, pi->y + pi->border, pi->border, bh - (pi->border * 2)}, pi->color2);
	draw_rect_if_intersect({pi->x, pi->y + bh - pi->border, bw, pi->border}, pi->color2);

	return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 * item triangle type
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_tri_new(PanWindow *pw,
                          GdkPoint c1, GdkPoint c2, GdkPoint c3,
                          PanColor color,
                          gint borders, PanColor border_color)
{
	GdkRectangle tri_rect = util_triangle_bounding_box(c1, c2, c3);

	auto *pi = g_new0(PanItem, 1);
	pi->type = PAN_ITEM_TRIANGLE;
	pi->x = tri_rect.x;
	pi->y = tri_rect.y;
	pi->width = tri_rect.width;
	pi->height = tri_rect.height;
	pi->color = color;

	auto *coord = g_new0(GdkPoint, 3);
	coord[0] = c1;
	coord[1] = c2;
	coord[2] = c3;

	pi->data = coord;

	pi->border = borders;
	pi->color2 = border_color;

	pw->list = g_list_prepend(pw->list, pi);

	return pi;
}

gboolean pan_item_tri_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *,
                           gint x, gint y, gint width, gint height)
{
	const GdkRectangle request_rect{x, y, width, height};
	const GdkRectangle pi_rect{pi->x, pi->y, pi->width, pi->height};
	GdkRectangle r;

	if (pi->data && gdk_rectangle_intersect(&request_rect, &pi_rect, &r))
		{
		auto coord = static_cast<GdkPoint *>(pi->data);
		r.x -= x;
		r.y -= y;
		pixbuf_draw_triangle(pixbuf, r,
		                     {coord[0].x - x, coord[0].y - y},
		                     {coord[1].x - x, coord[1].y - y},
		                     {coord[2].x - x, coord[2].y - y},
		                     pi->color.r, pi->color.g, pi->color.b, pi->color.a);

		const auto draw_line = [pixbuf, &r, x, y, &color = pi->color2](GdkPoint start, GdkPoint end)
		{
			pixbuf_draw_line(pixbuf, r,
			                 start.x - x, start.y - y,
			                 end.x - x, end.y - y,
			                 color.r, color.g, color.b, color.a);
		};

		if (pi->border & PAN_BORDER_1) draw_line(coord[0], coord[1]);
		if (pi->border & PAN_BORDER_2) draw_line(coord[1], coord[2]);
		if (pi->border & PAN_BORDER_3) draw_line(coord[2], coord[0]);
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
                           PanTextAttrType attr, PanBorderType border, PanColor color)
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

gboolean pan_item_text_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *pr,
                            gint x, gint y, gint, gint)
{
	PangoLayout *layout;

	layout = pan_item_text_layout(pi, GTK_WIDGET(pr));
	pixbuf_draw_layout(pixbuf, layout,
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
	pi->width = pw->thumb_size + PAN_SHADOW_OFFSET * 2;
	pi->height = pw->thumb_size + PAN_SHADOW_OFFSET * 2;

	pw->list = g_list_prepend(pw->list, pi);

	return pi;
}

gboolean pan_item_thumb_draw(PanWindow *pw, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *,
                             gint x, gint y, gint width, gint height)
{
	const GdkRectangle request_rect{x, y, width, height};
	GdkRectangle thumb_rect;
	GdkRectangle r;

	if (pi->pixbuf)
		{
		gint tw = gdk_pixbuf_get_width(pi->pixbuf);
		gint th = gdk_pixbuf_get_height(pi->pixbuf);

		gint tx = pi->x + ((pi->width - tw) / 2);
		gint ty = pi->y + ((pi->height - th) / 2);

		if (gdk_pixbuf_get_has_alpha(pi->pixbuf))
			{
			thumb_rect = {tx + PAN_SHADOW_OFFSET, ty + PAN_SHADOW_OFFSET, tw, th};
			if (gdk_rectangle_intersect(&request_rect, &thumb_rect, &r))
				{
				pixbuf_draw_shadow(pixbuf,
				                   {r.x - x, r.y - y, r.width, r.height},
				                   tx + PAN_SHADOW_OFFSET - x, ty + PAN_SHADOW_OFFSET - y, tw, th,
				                   PAN_SHADOW_FADE,
				                   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
				}
			}
		else
			{
			thumb_rect = {tx + tw, ty + PAN_SHADOW_OFFSET, PAN_SHADOW_OFFSET, th - PAN_SHADOW_OFFSET};
			if (gdk_rectangle_intersect(&request_rect, &thumb_rect, &r))
				{
				pixbuf_draw_shadow(pixbuf,
				                   {r.x - x, r.y - y, r.width, r.height},
				                   tx + PAN_SHADOW_OFFSET - x, ty + PAN_SHADOW_OFFSET - y, tw, th,
				                   PAN_SHADOW_FADE,
				                   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
				}

			thumb_rect = {tx + PAN_SHADOW_OFFSET, ty + th, tw, PAN_SHADOW_OFFSET};
			if (gdk_rectangle_intersect(&request_rect, &thumb_rect, &r))
				{
				pixbuf_draw_shadow(pixbuf,
				                   {r.x - x, r.y - y, r.width, r.height},
				                   tx + PAN_SHADOW_OFFSET - x, ty + PAN_SHADOW_OFFSET - y, tw, th,
				                   PAN_SHADOW_FADE,
				                   PAN_SHADOW_COLOR, PAN_SHADOW_ALPHA);
				}
			}

		thumb_rect = {tx, ty, tw, th};
		if (gdk_rectangle_intersect(&request_rect, &thumb_rect, &r))
			{
			gdk_pixbuf_composite(pi->pixbuf, pixbuf, r.x - x, r.y - y, r.width, r.height,
					     static_cast<gdouble>(tx) - x,
					     static_cast<gdouble>(ty) - y,
					     1.0, 1.0, GDK_INTERP_NEAREST,
					     255);
			}

		const auto draw_rect_if_intersect = [pixbuf, &request_rect, x, y](GdkRectangle thumb_rect, PanColor color)
		{
			GdkRectangle r;
			if (!gdk_rectangle_intersect(&request_rect, &thumb_rect, &r)) return;

			r.x -= x;
			r.y -= y;
			pixbuf_draw_rect_fill(pixbuf, r, color.r, color.g, color.b, color.a);
		};

		thumb_rect = {tx, ty, tw, PAN_OUTLINE_THICKNESS};
		draw_rect_if_intersect(thumb_rect, PAN_OUTLINE_COLOR_1);

		thumb_rect = {tx, ty, PAN_OUTLINE_THICKNESS, th};
		draw_rect_if_intersect(thumb_rect, PAN_OUTLINE_COLOR_1);

		thumb_rect = {tx + tw - PAN_OUTLINE_THICKNESS, ty + PAN_OUTLINE_THICKNESS,
		              PAN_OUTLINE_THICKNESS, th - PAN_OUTLINE_THICKNESS};
		draw_rect_if_intersect(thumb_rect, PAN_OUTLINE_COLOR_2);

		thumb_rect = {tx + PAN_OUTLINE_THICKNESS, ty + th - PAN_OUTLINE_THICKNESS,
		              tw - (PAN_OUTLINE_THICKNESS * 2), PAN_OUTLINE_THICKNESS};
		draw_rect_if_intersect(thumb_rect, PAN_OUTLINE_COLOR_2);
		}
	else
		{
		thumb_rect = {pi->x + PAN_SHADOW_OFFSET, pi->y + PAN_SHADOW_OFFSET,
		              pi->width - (PAN_SHADOW_OFFSET * 2), pi->height - (PAN_SHADOW_OFFSET * 2) };
		if (gdk_rectangle_intersect(&request_rect, &thumb_rect, &r))
			{
			gint d;

			r.x -= x;
			r.y -= y;
			d = (pw->size <= PAN_IMAGE_SIZE_THUMB_NONE) ? 2 : 8;
			pixbuf_draw_rect_fill(pixbuf, r,
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

PanItem *pan_item_image_new(PanWindow *pw, FileData *fd, gint x, gint y, gint w, gint h)
{
	PanItem *pi;

	pan_cache_get_image_size(pw, fd, w, h);

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

gboolean pan_item_image_draw(PanWindow *, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *,
                             gint x, gint y, gint width, gint height)
{
	const GdkRectangle request_rect{x, y, width, height};
	const GdkRectangle pi_rect{pi->x, pi->y, pi->width, pi->height};
	GdkRectangle r;

	if (gdk_rectangle_intersect(&request_rect, &pi_rect, &r))
		{
		r.x -= x;
		r.y -= y;

		if (pi->pixbuf)
			{
			gdk_pixbuf_composite(pi->pixbuf, pixbuf, r.x, r.y, r.width, r.height,
			                     static_cast<gdouble>(pi->x) - x,
			                     static_cast<gdouble>(pi->y) - y,
			                     1.0, 1.0, GDK_INTERP_NEAREST,
			                     pi->color.a);
			}
		else
			{
			pixbuf_draw_rect_fill(pixbuf, r, pi->color2.r, pi->color2.g, pi->color2.b, pi->color2.a);
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
						g_autofree gchar *haystack = g_utf8_strdown(pi->fd->name, -1);
						match = (strstr(haystack, path) != nullptr);
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

PanTextAlignment::PanTextAlignment(PanWindow *pw, gint x, gint y, const gchar *key)
	: pw(pw)
	, x(x)
	, y(y)
	, key(g_strdup(key))
{
}

PanTextAlignment::~PanTextAlignment()
{
	g_free(key);
}

void PanTextAlignment::add(const gchar *label, const gchar *text)
{
	Items items;

	if (label)
		{
		items.label = pan_item_text_new(pw, x, y, label,
		                                PAN_TEXT_ATTR_BOLD, PAN_BORDER_NONE, PAN_POPUP_TEXT_COLOR);
		pan_item_set_key(items.label, key);
		}

	if (text)
		{
		items.text = pan_item_text_new(pw, x, y, text,
		                               PAN_TEXT_ATTR_NONE, PAN_BORDER_NONE, PAN_POPUP_TEXT_COLOR);
		pan_item_set_key(items.text, key);
		}

	columns.push_back(items);
}

void PanTextAlignment::calc(PanItem *box)
{
	gint label_column_width = 0;
	for (const Items &items : columns)
		{
		if (items.label) label_column_width = std::max(label_column_width, items.label->width);
		}

	gint y = this->y;
	for (Items &items : columns)
		{
		PanItem *pi_label = items.label;
		PanItem *pi_text = items.text;
		gint height = 0;

		if (pi_label)
			{
			pi_label->x = x;
			pi_label->y = y;
			pan_item_size_by_item(box, pi_label, PREF_PAD_BORDER);
			height = pi_label->height;
			}

		if (pi_text)
			{
			pi_text->x = x + label_column_width + PREF_PAD_SPACE;
			pi_text->y = y;
			pan_item_size_by_item(box, pi_text, PREF_PAD_BORDER);
			height = std::max(height, pi_text->height);
			}

		if (!pi_label && !pi_text) height = PREF_PAD_GROUP;

		y += height;
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
