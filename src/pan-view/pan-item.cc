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
#include "geometry.h"
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

struct PanItemTriangleData
{
	GqPoint coord[3];
	PanBorderType borders;
};

struct PanItemTextData
{
	gchar *text;
	PanTextAttrType attr;
};

constexpr gint PAN_OUTLINE_THICKNESS = 1;
constexpr guint8 PAN_OUTLINE_ALPHA = 180;
constexpr GqColor PAN_OUTLINE_COLOR_1{255, 255, 255, PAN_OUTLINE_ALPHA};
constexpr GqColor PAN_OUTLINE_COLOR_2{64, 64, 64, PAN_OUTLINE_ALPHA};

/* popup info box */
constexpr GqColor PAN_POPUP_TEXT_COLOR{0, 0, 0, 225};

} // namespace

PanItemType get_pan_item_type(PanImageSize size)
{
	return (size > PAN_IMAGE_SIZE_THUMB_LARGE) ? PAN_ITEM_IMAGE : PAN_ITEM_THUMB;
}

/*
 *-----------------------------------------------------------------------------
 * item base functions
 *-----------------------------------------------------------------------------
 */

static PanItem *pan_item_new(PanItemType type, gint x, gint y, gint width, gint height)
{
	auto *pi = new PanItem();

	pi->type = type;
	pi->x = x;
	pi->y = y;
	pi->width = width;
	pi->height = height;
	pi->key = PanKey::None;

	return pi;
}

static void pan_item_free(PanItem *pi)
{
	if (!pi) return;

	if (pi->pixbuf) g_object_unref(pi->pixbuf);
	if (pi->fd) file_data_unref(pi->fd);
	if (pi->is_type(PAN_ITEM_TEXT))
		{
		auto *data = static_cast<PanItemTextData *>(pi->data);
		g_free(data->text);
		}
	g_free(pi->data);

	delete pi;
}

bool PanItem::is_type(PanItemType type) const
{
	return this->type == type;
}

void PanItem::set_key(PanKey key)
{
	this->key = key;
}

void pan_item_added(PanWindow *pw, PanItem *pi)
{
	if (!pi) return;
	image_area_changed(pw->imd, pi->x, pi->y, pi->width, pi->height);
}

static void pan_item_remove(PanWindow *pw, PanItem *pi)
{
	if (pw->click_pi == pi) pw->click_pi = nullptr;
	if (pw->queue_pi == pi)	pw->queue_pi = nullptr;
	if (pw->search_pi == pi) pw->search_pi = nullptr;
	pw->queue.remove(pi);

	pw->list.remove(pi);
	image_area_changed(pw->imd, pi->x, pi->y, pi->width, pi->height);
	pan_item_free(pi);
}

void PanItem::set_size_by_item(const PanItem *pi, gint border)
{
	if (!pi) return;

	width = std::max(width, pi->x + pi->width + border - x);
	height = std::max(height, pi->y + pi->height + border - y);
}

void PanItem::adjust_size(gint border, gint &w, gint &h) const
{
	w = std::max(w, x + width + border);
	h = std::max(h, y + height + border);
}

void pan_item_list_clear(PanItemList &list)
{
	for (PanItem *pi : list) pan_item_free(pi);
	list.clear();
}

/*
 *-----------------------------------------------------------------------------
 * item box type
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_box_new(PanWindow *pw, FileData *fd, gint x, gint y, gint width, gint height,
                          GqColor base, gint border_size, GqColor border_color)
{
	PanItem *pi = pan_item_new(PAN_ITEM_BOX, x, y, width, height);

	pi->fd = fd;
	pi->color = base;
	pi->border = border_size;
	pi->border_color = border_color;

	pw->list.push_front(pi);

	return pi;
}

void pan_item_box_shadow(PanItem *pi, gint offset, gint fade)
{
	if (!pi || !pi->is_type(PAN_ITEM_BOX)) return;

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

static bool pan_item_box_draw(const PanItem *pi, GdkPixbuf *pixbuf, GdkRectangle request_rect)
{
	gint bw;
	gint bh;

	bw = pi->width;
	bh = pi->height;

	auto *shadow = static_cast<PanItemBoxShadow *>(pi->data);
	if (shadow)
		{
		const gint bx = pi->x - request_rect.x;
		const gint by = pi->y - request_rect.y;

		bw -= shadow->offset;
		bh -= shadow->offset;

		if (pi->color.a == 255)
			{
			pixbuf_draw_shadow(pixbuf,
			                   {bx + bw, by + shadow->offset, shadow->offset, bh - shadow->offset},
			                   bx + shadow->offset, by + shadow->offset, bw, bh,
			                   shadow->fade, PAN_SHADOW_COLOR);
			pixbuf_draw_shadow(pixbuf,
			                   {bx + shadow->offset, by + bh, bw, shadow->offset},
			                   bx + shadow->offset, by + shadow->offset, bw, bh,
			                   shadow->fade, PAN_SHADOW_COLOR);
			}
		else
			{
			const guint8 a = pi->color.a * PAN_SHADOW_ALPHA >> 8;
			pixbuf_draw_shadow(pixbuf,
			                   {bx + shadow->offset, by + shadow->offset, bw, bh},
			                   bx + shadow->offset, by + shadow->offset, bw, bh,
			                   shadow->fade, {PAN_SHADOW_RGB, a});
			}
		}

	const auto draw_rect_if_intersect = [pixbuf, &request_rect](GdkRectangle box_rect, GqColor color)
	{
		GdkRectangle r;
		if (!gdk_rectangle_intersect(&request_rect, &box_rect, &r)) return;

		r.x -= request_rect.x;
		r.y -= request_rect.y;
		pixbuf_draw_rect_fill(pixbuf, r, color);
	};

	draw_rect_if_intersect({pi->x, pi->y, bw, bh}, pi->color);

	draw_rect_if_intersect({pi->x, pi->y, bw, pi->border}, pi->border_color);
	draw_rect_if_intersect({pi->x, pi->y + pi->border, pi->border, bh - (pi->border * 2)}, pi->border_color);
	draw_rect_if_intersect({pi->x + bw - pi->border, pi->y + pi->border, pi->border, bh - (pi->border * 2)}, pi->border_color);
	draw_rect_if_intersect({pi->x, pi->y + bh - pi->border, bw, pi->border}, pi->border_color);

	return false;
}


/*
 *-----------------------------------------------------------------------------
 * item triangle type
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_tri_new(PanWindow *pw,
                          GqPoint c1, GqPoint c2, GqPoint c3,
                          GqColor color,
                          PanBorderType borders, GqColor border_color)
{
	GdkRectangle tri_rect = util_triangle_bounding_box(c1, c2, c3);

	auto *data = g_new0(PanItemTriangleData, 1);
	data->coord[0] = c1;
	data->coord[1] = c2;
	data->coord[2] = c3;
	data->borders = borders;

	PanItem *pi = pan_item_new(PAN_ITEM_TRIANGLE, tri_rect.x, tri_rect.y, tri_rect.width, tri_rect.height);

	pi->color = color;
	pi->border_color = border_color;
	pi->data = data;

	pw->list.push_front(pi);

	return pi;
}

void pan_item_tri_shift(PanItem *pi, gint x, gint y)
{
	auto *data = static_cast<PanItemTriangleData *>(pi->data);

	for (GqPoint &coord : data->coord)
		{
		coord.x -= x;
		coord.y -= y;
		}
}

static bool pan_item_tri_draw(const PanItem *pi, GdkPixbuf *pixbuf, GdkRectangle request_rect)
{
	const GdkRectangle pi_rect{pi->x, pi->y, pi->width, pi->height};

	if (GdkRectangle r; gdk_rectangle_intersect(&request_rect, &pi_rect, &r))
		{
		r.x -= request_rect.x;
		r.y -= request_rect.y;

		auto *data = static_cast<PanItemTriangleData *>(pi->data);
		GqPoint coord[3];
		for (gint i = 0; i < 3; ++i)
			{
			coord[i].x = data->coord[i].x - request_rect.x;
			coord[i].y = data->coord[i].y - request_rect.y;
			}

		pixbuf_draw_triangle(pixbuf, r, coord[0], coord[1], coord[2], pi->color);

		if (data->borders & PAN_BORDER_1) pixbuf_draw_line(pixbuf, r, coord[0], coord[1], pi->border_color);
		if (data->borders & PAN_BORDER_2) pixbuf_draw_line(pixbuf, r, coord[1], coord[2], pi->border_color);
		if (data->borders & PAN_BORDER_3) pixbuf_draw_line(pixbuf, r, coord[2], coord[0], pi->border_color);
		}

	return false;
}


/*
 *-----------------------------------------------------------------------------
 * item text type
 *-----------------------------------------------------------------------------
 */

static PangoLayout *pan_item_text_layout(const PanItemTextData *data, GtkWidget *widget)
{
	PangoLayout *layout = gtk_widget_create_pango_layout(widget, nullptr);

	if (data->attr & PAN_TEXT_ATTR_MARKUP)
		{
		pango_layout_set_markup(layout, data->text, -1);
		return layout;
		}

	g_autoptr(PangoAttrList) pal = get_pango_attr_list(data->attr & PAN_TEXT_ATTR_BOLD,
	                                                   data->attr & PAN_TEXT_ATTR_HEADING);
	if (pal)
		{
		pango_layout_set_attributes(layout, pal);
		}

	pango_layout_set_text(layout, data->text, -1);
	return layout;
}

PanItem *pan_item_text_new(PanWindow *pw, gint x, gint y, const gchar *text,
                           PanTextAttrType attr, gint border_size, GqColor color)
{
	auto *data = g_new0(PanItemTextData, 1);
	data->text = g_strdup(text);
	data->attr = attr;

	GqSize size{};
	if (pw->imd->pr && text)
		{
		g_autoptr(PangoLayout) layout = pan_item_text_layout(data, pw->imd->pr);
		pango_layout_get_pixel_size(layout, &size.width, &size.height);

		size.width += border_size * 2;
		size.height += border_size * 2;
		}

	PanItem *pi = pan_item_new(PAN_ITEM_TEXT, x, y, size.width, size.height);

	pi->color = color;
	pi->border = border_size;
	pi->data = data;

	pw->list.push_front(pi);

	return pi;
}

static bool pan_item_text_draw(const PanItem *pi, GdkPixbuf *pixbuf, GdkRectangle request_rect,
                               PixbufRenderer *pr)
{
	auto *data = static_cast<PanItemTextData *>(pi->data);
	g_autoptr(PangoLayout) layout = pan_item_text_layout(data, GTK_WIDGET(pr));

	pixbuf_draw_layout(pixbuf, layout,
	                   pi->x - request_rect.x + pi->border,
	                   pi->y - request_rect.y + pi->border,
	                   pi->color);

	return false;
}


/*
 *-----------------------------------------------------------------------------
 * item thumbnail type
 *-----------------------------------------------------------------------------
 */

PanItem *pan_item_thumb_new(PanWindow *pw, FileData *fd, gint x, gint y)
{
	const gint size = pw->thumb_size + (PAN_SHADOW_OFFSET * 2);

	PanItem *pi = pan_item_new(PAN_ITEM_THUMB, x, y, size, size);

	pi->fd = fd;

	pw->list.push_front(pi);

	return pi;
}

static bool pan_item_thumb_draw(const PanItem *pi, GdkPixbuf *pixbuf, GdkRectangle request_rect,
                                PanImageSize size)
{
	GdkRectangle thumb_rect;

	const auto draw_rect_if_intersect = [pixbuf, &request_rect](GdkRectangle thumb_rect, GqColor color)
	{
		GdkRectangle r;
		if (!gdk_rectangle_intersect(&request_rect, &thumb_rect, &r)) return;

		r.x -= request_rect.x;
		r.y -= request_rect.y;
		pixbuf_draw_rect_fill(pixbuf, r, color);
	};

	if (pi->pixbuf)
		{
		gint tw = gdk_pixbuf_get_width(pi->pixbuf);
		gint th = gdk_pixbuf_get_height(pi->pixbuf);

		gint tx = pi->x + ((pi->width - tw) / 2);
		gint ty = pi->y + ((pi->height - th) / 2);

		const auto draw_shadow_if_intersect = [pixbuf, &request_rect, tx, ty, tw, th](GdkRectangle thumb_rect)
		{
			GdkRectangle r;
			if (!gdk_rectangle_intersect(&request_rect, &thumb_rect, &r)) return;

			r.x -= request_rect.x;
			r.y -= request_rect.y;
			pixbuf_draw_shadow(pixbuf, r,
			                   tx + PAN_SHADOW_OFFSET - request_rect.x, ty + PAN_SHADOW_OFFSET - request_rect.y, tw, th,
			                   PAN_SHADOW_FADE, PAN_SHADOW_COLOR);
		};

		if (gdk_pixbuf_get_has_alpha(pi->pixbuf))
			{
			draw_shadow_if_intersect({tx + PAN_SHADOW_OFFSET, ty + PAN_SHADOW_OFFSET, tw, th});
			}
		else
			{
			draw_shadow_if_intersect({tx + tw, ty + PAN_SHADOW_OFFSET, PAN_SHADOW_OFFSET, th - PAN_SHADOW_OFFSET});
			draw_shadow_if_intersect({tx + PAN_SHADOW_OFFSET, ty + th, tw, PAN_SHADOW_OFFSET});
			}

		thumb_rect = {tx, ty, tw, th};
		if (GdkRectangle r; gdk_rectangle_intersect(&request_rect, &thumb_rect, &r))
			{
			r.x -= request_rect.x;
			r.y -= request_rect.y;
			gdk_pixbuf_composite(pi->pixbuf, pixbuf, r.x, r.y, r.width, r.height,
			                     static_cast<gdouble>(tx) - request_rect.x,
			                     static_cast<gdouble>(ty) - request_rect.y,
			                     1.0, 1.0, GDK_INTERP_NEAREST, 255);
			}

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
		const guint8 a = PAN_SHADOW_ALPHA / ((size <= PAN_IMAGE_SIZE_THUMB_NONE) ? 2 : 8);
		draw_rect_if_intersect(thumb_rect, {PAN_SHADOW_RGB, a});
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
	if (auto size = pan_cache_get_image_size(pw, fd); size.has_value())
		{
		w = std::max(1, size->width * pw->image_size / 100);
		h = std::max(1, size->height * pw->image_size / 100);
		}

	PanItem *pi = pan_item_new(PAN_ITEM_IMAGE, x, y, w, h);

	pi->fd = fd;

	pw->list.push_front(pi);

	return pi;
}

static bool pan_item_image_draw(const PanItem *pi, GdkPixbuf *pixbuf, GdkRectangle request_rect)
{
	const GdkRectangle pi_rect{pi->x, pi->y, pi->width, pi->height};

	if (GdkRectangle r; gdk_rectangle_intersect(&request_rect, &pi_rect, &r))
		{
		r.x -= request_rect.x;
		r.y -= request_rect.y;

		if (pi->pixbuf)
			{
			gdk_pixbuf_composite(pi->pixbuf, pixbuf, r.x, r.y, r.width, r.height,
			                     static_cast<gdouble>(pi->x) - request_rect.x,
			                     static_cast<gdouble>(pi->y) - request_rect.y,
			                     1.0, 1.0, GDK_INTERP_NEAREST, 255);
			}
		else
			{
			pixbuf_draw_rect_fill(pixbuf, r, {PAN_SHADOW_RGB, PAN_SHADOW_ALPHA / 2});
			}
		}

	return (pi->pixbuf == nullptr);
}


/*
 *-----------------------------------------------------------------------------
 * item draw
 *-----------------------------------------------------------------------------
 */

bool PanItem::draw(GdkPixbuf *pixbuf, GdkRectangle request_rect,
                   PanImageSize size, PixbufRenderer *pr) const
{
	switch (type)
		{
		case PAN_ITEM_BOX:
			return pan_item_box_draw(this, pixbuf, request_rect);
		case PAN_ITEM_TRIANGLE:
			return pan_item_tri_draw(this, pixbuf, request_rect);
		case PAN_ITEM_TEXT:
			return pan_item_text_draw(this, pixbuf, request_rect, pr);
		case PAN_ITEM_THUMB:
			return pan_item_thumb_draw(this, pixbuf, request_rect, size);
		case PAN_ITEM_IMAGE:
			return pan_item_image_draw(this, pixbuf, request_rect);
		default:
			return false;
		}
}


/*
 *-----------------------------------------------------------------------------
 * item lookup/search
 *-----------------------------------------------------------------------------
 */

void pan_item_remove_by_key(PanWindow *pw, PanKey key)
{
	g_return_if_fail(key != PanKey::None);

	const auto pan_item_find_by_key_l = [key](PanItemList &list) -> PanItem *
	{
		for (auto work = list.crbegin(); work != list.crend(); ++work)
			{
			PanItem *pi = *work;

			if (pi->key == key) return pi;
			}

		return nullptr;
	};

	PanItem *pi;
	while ((pi = pan_item_find_by_key_l(pw->list))) pan_item_remove(pw, pi);
	while ((pi = pan_item_find_by_key_l(pw->list_static))) pan_item_remove(pw, pi);
}

/* when ignore_case and partial are TRUE, path should be converted to lower case */
static bool pan_item_match_path(const PanItem *pi, const gchar *path,
                                gboolean ignore_case, gboolean partial)
{
	if (path[0] == G_DIR_SEPARATOR)
		{
		return g_strcmp0(path, pi->fd->path) == 0;
		}

	if (!pi->fd->name) return false;

	if (partial)
		{
		if (ignore_case)
			{
			g_autofree gchar *haystack = g_utf8_strdown(pi->fd->name, -1);
			return strstr(haystack, path) != nullptr;
			}

		return strstr(pi->fd->name, path) != nullptr;
		}

	if (ignore_case)
		{
		return g_ascii_strcasecmp(path, pi->fd->name) == 0;
		}

	return strcmp(path, pi->fd->name) == 0;
}

/**
 * @returns Suitable items from pw->list and pw->list_static in following order:
 *          items from pw->list_static in reverse order,
 *          items from pw->list in reverse order
 *
 * When ignore_case and partial are TRUE, path should be converted to lower case
 */
PanItemList pan_item_find_by_path(PanWindow *pw, PanItemType type, const gchar *path,
                                  gboolean ignore_case, gboolean partial)
{
	if (!path) return {};
	if (partial && path[0] == G_DIR_SEPARATOR) return {};

	// Prepend items from search_list to list in reverse order
	const auto pan_item_find_by_path_l = [type, path, ignore_case, partial](PanItemList &list, const PanItemList &search_list)
	{
		for (PanItem *pi : search_list)
			{
			if (pi->is_type(type) && pi->fd &&
			    pan_item_match_path(pi, path, ignore_case, partial))
				{
				list.push_front(pi);
				}
			}
	};

	PanItemList list;
	pan_item_find_by_path_l(list, pw->list); // prepend items from pw->list in reverse order
	pan_item_find_by_path_l(list, pw->list_static); // prepend items from pw->list_static in reverse order

	return list;
}

PanItem *pan_item_find_by_fd(PanWindow *pw, PanItemType type, FileData *fd,
                             gboolean ignore_case, gboolean partial)
{
	if (!fd) return nullptr;

	PanItemList list = pan_item_find_by_path(pw, type, fd->path, ignore_case, partial);
	return list.empty() ? nullptr : list.front();
}


PanItem *pan_item_find_by_coord(PanWindow *pw, PanItemType type,
                                gint x, gint y, PanKey key)
{
	const auto has_coord = [type, x, y, key](const PanItem *pi)
	{
		return pi->is_type(type)
		    && x >= pi->x && x < pi->x + pi->width
		    && y >= pi->y && y < pi->y + pi->height
		    && (key == PanKey::None || pi->key == key);
	};

	auto it = std::find_if(pw->list.cbegin(), pw->list.cend(), has_coord);
	if (it != pw->list.cend()) return *it;

	it = std::find_if(pw->list_static.cbegin(), pw->list_static.cend(), has_coord);
	if (it != pw->list_static.cend()) return *it;

	return nullptr;
}


/*
 *-----------------------------------------------------------------------------
 * text alignments
 *-----------------------------------------------------------------------------
 */

PanTextAlignment::PanTextAlignment(PanWindow *pw, gint x, gint y, PanKey key)
	: pw(pw)
	, x(x)
	, y(y)
	, key(key)
{
}

void PanTextAlignment::add(const gchar *label, const gchar *text)
{
	Items items;

	if (label)
		{
		items.label = pan_item_text_new(pw, x, y, label, PAN_TEXT_ATTR_BOLD,
		                                0, PAN_POPUP_TEXT_COLOR);
		items.label->set_key(key);
		label_width_max = std::max(label_width_max, items.label->width);
		}

	if (text)
		{
		items.text = pan_item_text_new(pw, x, y, text, PAN_TEXT_ATTR_NONE,
		                               0, PAN_POPUP_TEXT_COLOR);
		items.text->set_key(key);
		}

	columns.push_back(items);
}

void PanTextAlignment::calc(PanItem *box)
{
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
			box->set_size_by_item(pi_label, PREF_PAD_BORDER);
			height = pi_label->height;
			}

		if (pi_text)
			{
			pi_text->x = x + label_width_max + PREF_PAD_SPACE;
			pi_text->y = y;
			box->set_size_by_item(pi_text, PREF_PAD_BORDER);
			height = std::max(height, pi_text->height);
			}

		if (!pi_label && !pi_text) height = PREF_PAD_GROUP;

		y += height;
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
