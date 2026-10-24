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

#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <cairo.h>
#include <glib-object.h>

#include "collect.h"
#include "color-man.h"
#include "display-profile.h"
#include "exif.h"
#include "filecache.h"
#include "filedata.h"
#include "geometry.h"
#include "history-list.h"
#include "image-load.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "renderer-tiles.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"

struct ExifData;
class FileCache;

namespace
{

constexpr gdouble aspect_ratios[5] {0.0, gdouble(1.0), gdouble(4.0) / 3, gdouble(3) / 2, gdouble(16) / 9};

gdouble rectangle_aspect_ratio(RectangleDrawAspectRatio mode)
{
	const auto ratio = aspect_ratios[static_cast<gint>(mode)];
	return ratio > 0 && options->rectangle_draw_portrait ? 1.0 / ratio : ratio;
}

/*
 * SelectionRectangle
 */

struct SelectionRectangle
{
	gint x = 0;
	gint y = 0;
	gint width = 0;
	gint height = 0;
	gdouble aspect_ratio = 0.0;

	SelectionRectangle(gint origin_x = 0, gint origin_y = 0,
	                   RectangleDrawAspectRatio rectangle_draw_aspect_ratio = RECTANGLE_DRAW_ASPECT_RATIO_NONE);
	void set_cursor(gint cursor_x, gint cursor_y);
	[[nodiscard]] guint hit_test(gint cursor_x, gint cursor_y) const;
	void resize(gint cursor_x, gint cursor_y, guint edges, const SelectionRectangle &original);

private:
	gint origin_x;
	gint origin_y;
	RectangleDrawAspectRatio rectangle_draw_aspect_ratio = RECTANGLE_DRAW_ASPECT_RATIO_NONE;
};

SelectionRectangle::SelectionRectangle(gint origin_x, gint origin_y, RectangleDrawAspectRatio rectangle_draw_aspect_ratio)
    : x(origin_x)
    , y(origin_y)
    , aspect_ratio(rectangle_aspect_ratio(rectangle_draw_aspect_ratio))
    , origin_x(origin_x)
    , origin_y(origin_y)
    , rectangle_draw_aspect_ratio(rectangle_draw_aspect_ratio)
{}

void SelectionRectangle::set_cursor(gint cursor_x, gint cursor_y)
{
	x = std::min(origin_x, cursor_x);
	y = std::min(origin_y, cursor_y);
	width = std::abs(cursor_x - origin_x);
	height = std::abs(cursor_y - origin_y);

	if (rectangle_draw_aspect_ratio == RECTANGLE_DRAW_ASPECT_RATIO_NONE) return;

	if (width < height * aspect_ratio)
		width = height * aspect_ratio;
	else
		height = width / aspect_ratio;

	// left side of the origin: move x to respect that origin
	if (cursor_x < origin_x)
		x = origin_x - width;

	// above the origin: move y
	if (cursor_y < origin_y)
		y = origin_y - height;
}

enum RectangleEdge : guint
{
	RECTANGLE_LEFT = 1,
	RECTANGLE_RIGHT = 2,
	RECTANGLE_TOP = 4,
	RECTANGLE_BOTTOM = 8
};

constexpr gint RECTANGLE_HANDLE_SIZE = 8;

guint SelectionRectangle::hit_test(gint cursor_x, gint cursor_y) const
{
	if (width < 1 || height < 1 || cursor_x < x - RECTANGLE_HANDLE_SIZE ||
	    cursor_x > x + width + RECTANGLE_HANDLE_SIZE || cursor_y < y - RECTANGLE_HANDLE_SIZE ||
	    cursor_y > y + height + RECTANGLE_HANDLE_SIZE) return 0;

	guint edges = 0;
	if (std::abs(cursor_x - x) <= RECTANGLE_HANDLE_SIZE) edges |= RECTANGLE_LEFT;
	else if (std::abs(cursor_x - x - width) <= RECTANGLE_HANDLE_SIZE) edges |= RECTANGLE_RIGHT;
	if (std::abs(cursor_y - y) <= RECTANGLE_HANDLE_SIZE) edges |= RECTANGLE_TOP;
	else if (std::abs(cursor_y - y - height) <= RECTANGLE_HANDLE_SIZE) edges |= RECTANGLE_BOTTOM;
	return edges;
}

void SelectionRectangle::resize(gint cursor_x, gint cursor_y, guint edges, const SelectionRectangle &original)
{
	gint left = original.x;
	gint right = original.x + original.width;
	gint top = original.y;
	gint bottom = original.y + original.height;
	if (edges & RECTANGLE_LEFT) left = std::min(cursor_x, right - 1);
	if (edges & RECTANGLE_RIGHT) right = std::max(cursor_x, left + 1);
	if (edges & RECTANGLE_TOP) top = std::min(cursor_y, bottom - 1);
	if (edges & RECTANGLE_BOTTOM) bottom = std::max(cursor_y, top + 1);

	width = right - left;
	height = bottom - top;
	if (aspect_ratio > 0)
		{
		const bool horizontal = edges & (RECTANGLE_LEFT | RECTANGLE_RIGHT);
		const bool vertical = edges & (RECTANGLE_TOP | RECTANGLE_BOTTOM);
		if (horizontal && (!vertical || width >= height * aspect_ratio))
			height = std::max(1, static_cast<gint>(std::round(width / aspect_ratio)));
		else
			width = std::max(1, static_cast<gint>(std::round(height * aspect_ratio)));

		if (edges & RECTANGLE_LEFT) left = right - width;
		else if (!horizontal) left = original.x + ((original.width - width) / 2);
		if (edges & RECTANGLE_TOP) top = bottom - height;
		else if (!vertical) top = original.y + ((original.height - height) / 2);
		}
	x = left;
	y = top;
}

const gchar *rectangle_cursor(guint edges)
{
	if (edges == (RECTANGLE_LEFT | RECTANGLE_TOP) || edges == (RECTANGLE_RIGHT | RECTANGLE_BOTTOM)) return "nwse-resize";
	if (edges == (RECTANGLE_RIGHT | RECTANGLE_TOP) || edges == (RECTANGLE_LEFT | RECTANGLE_BOTTOM)) return "nesw-resize";
	if (edges & (RECTANGLE_LEFT | RECTANGLE_RIGHT)) return "ew-resize";
	if (edges & (RECTANGLE_TOP | RECTANGLE_BOTTOM)) return "ns-resize";
	return "crosshair";
}

// For draw rectangle function
GqPoint image_start;
gint rect_x1, rect_x2, rect_y1, rect_y2;
gint rect_id = 0;
SelectionRectangle selection_rectangle;
SelectionRectangle rectangle_before_drag;
ImageWindow *rectangle_owner = nullptr;
guint rectangle_resize_edges = 0;
gboolean rectangle_dragging = FALSE;
gboolean rectangle_moving = FALSE;
gboolean rectangle_pending = FALSE;
GqPoint rectangle_press;

void image_rectangle_clear(ImageWindow *imd)
{
	if (rectangle_owner != imd) return;
	if (rect_id) pixbuf_renderer_overlay_remove(PIXBUF_RENDERER(imd->pr), rect_id);
	rect_id = 0;
	rectangle_owner = nullptr;
	rectangle_dragging = FALSE;
	selection_rectangle = SelectionRectangle();
	rect_x1 = rect_x2 = rect_y1 = rect_y2 = 0;
}

} // namespace

static GList *image_list = nullptr;

/**
 * @brief Return borrowed, complete displayed pixels without requiring the image cache
 */
GdkPixbuf *image_get_complete_pixbuf(FileData *fd)
{
	for (GList *work = image_list; work; work = work->next)
		{
		auto *imd = static_cast<ImageWindow *>(work->data);
		if (imd->image_fd == fd && !imd->il && !(imd->state & IMAGE_STATE_LOADING) && !imd->unknown)
			{
			if (auto *pixbuf = image_get_pixbuf(imd)) return pixbuf;
			}
		}
	return nullptr;
}

static void image_read_ahead_start(ImageWindow *imd);
static void image_cache_set(ImageWindow *imd, FileData *fd);

/*
 *-------------------------------------------------------------------
 * 'signals'
 *-------------------------------------------------------------------
 */

static void image_click_cb(PixbufRenderer *, GqMouseButtonEvent *event, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);
	if (options->draw_rectangle && event->button == GDK_BUTTON_PRIMARY) return;

	if (!options->image_lm_click_nav && event->button == GDK_BUTTON_MIDDLE)
		{
		imd->mouse_wheel_mode = !imd->mouse_wheel_mode;
		}

	if (imd->func_button)
		{
		imd->func_button(imd, event, imd->data_button);
		}
}

static void switch_coords_orientation(ImageWindow *imd, gint x, gint y, gint width, gint height)
{
	switch (imd->orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT: /* normal -- nothing to do */
		case EXIF_ORIENTATION_LEFT_TOP: /* left mirrored, swap later */
			rect_x1 = image_start.x;
			rect_y1 = image_start.y;
			rect_x2 = x;
			rect_y2 = y;
			break;
		case EXIF_ORIENTATION_TOP_RIGHT: /* mirrored */
		case EXIF_ORIENTATION_RIGHT_TOP: /* rotated -90 (270), swap later */
			rect_x1 = width - x;
			rect_y1 = image_start.y;
			rect_x2 = width - image_start.x;
			rect_y2 = y;
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT: /* upside down */
		case EXIF_ORIENTATION_RIGHT_BOTTOM: /* right mirrored, swap later */
			rect_x1 = width - x;
			rect_y1 = height - y;
			rect_x2 = width - image_start.x;
			rect_y2 = height - image_start.y;
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT: /* flipped */
		case EXIF_ORIENTATION_LEFT_BOTTOM: /* rotated 90, swap later */
			rect_x1 = image_start.x;
			rect_y1 = height - y;
			rect_x2 = x;
			rect_y2 = height - image_start.y;
			break;
		default:
			/* The other values are out of range */
			break;
		}

	switch (imd->orientation)
		{
		case EXIF_ORIENTATION_LEFT_TOP: /* left mirrored, swap EXIF_ORIENTATION_TOP_LEFT */
		case EXIF_ORIENTATION_RIGHT_TOP: /* rotated -90 (270), swap EXIF_ORIENTATION_TOP_RIGHT */
		case EXIF_ORIENTATION_RIGHT_BOTTOM: /* right mirrored, swap EXIF_ORIENTATION_BOTTOM_RIGHT */
		case EXIF_ORIENTATION_LEFT_BOTTOM: /* rotated 90, swap EXIF_ORIENTATION_BOTTOM_LEFT */
			std::swap(rect_x1, rect_y1);
			std::swap(rect_x2, rect_y2);
			break;
		default:
			/* The other values are out of range */
			break;
		}
}

static void image_rectangle_draw(ImageWindow *imd)
{
	auto *pr = PIXBUF_RENDERER(imd->pr);
	gint width;
	gint height;
	if (!pixbuf_renderer_get_image_size(pr, width, height) || pr->scale <= 0) return;
	const gint left = pr->x_offset - pr->x_scroll;
	const gint top = pr->y_offset - pr->y_scroll;
	const gint right = left + static_cast<gint>(width * pr->scale);
	const gint bottom = top + static_cast<gint>(height * pr->scale * pr->aspect_ratio);
	image_start.x = std::clamp(static_cast<gint>((selection_rectangle.x - left) / pr->scale), 0, width);
	image_start.y = std::clamp(static_cast<gint>((selection_rectangle.y - top) / (pr->scale * pr->aspect_ratio)), 0, height);
	const gint end_x = std::clamp(static_cast<gint>((selection_rectangle.x + selection_rectangle.width - left) / pr->scale), 0, width);
	const gint end_y = std::clamp(static_cast<gint>((selection_rectangle.y + selection_rectangle.height - top) / (pr->scale * pr->aspect_ratio)), 0, height);
	switch_coords_orientation(imd, end_x, end_y, width, height);

	if (rect_id) pixbuf_renderer_overlay_remove(pr, rect_id);
	const gint overlay_width = gtk_widget_get_width(GTK_WIDGET(pr));
	const gint overlay_height = gtk_widget_get_height(GTK_WIDGET(pr));
	if (overlay_width < 1 || overlay_height < 1) return;
	g_autoptr(GdkPixbuf) rect_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, overlay_width, overlay_height);
	gdk_pixbuf_fill(rect_pixbuf, 0);
	pixbuf_set_rect_fill(rect_pixbuf, left, top, right - left, bottom - top, {0, 0, 0, 48});
	const auto &rect = selection_rectangle;
	pixbuf_set_rect_fill(rect_pixbuf, rect.x, rect.y, rect.width, rect.height, {0, 0, 0, 0});
	// A one-pixel white outline and black inner line remain visible on any image.
	pixbuf_set_rect(rect_pixbuf, rect.x, rect.y, rect.width, rect.height, {255, 255, 255, 255}, 1, 1, 1, 1);
	if (rect.width > 2 && rect.height > 2)
		{
		pixbuf_set_rect(rect_pixbuf, rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, {0, 0, 0, 255}, 1, 1, 1, 1);
		}
	constexpr gint handle_size = 7;
	for (gint row = 0; row < 3; ++row)
		{
		for (gint column = 0; column < 3; ++column)
			{
			if (row == 1 && column == 1) continue;
			const gint handle_x = rect.x + ((column * (rect.width - 1)) / 2) - (handle_size / 2);
			const gint handle_y = rect.y + ((row * (rect.height - 1)) / 2) - (handle_size / 2);
			pixbuf_set_rect_fill(rect_pixbuf, handle_x, handle_y, handle_size, handle_size, {255, 255, 255, 255});
			pixbuf_set_rect(rect_pixbuf, handle_x, handle_y, handle_size, handle_size, {0, 0, 0, 255}, 1, 1, 1, 1);
			}
		}
	rect_id = pixbuf_renderer_overlay_add(pr, rect_pixbuf, 0, 0, OVL_NORMAL);
}

void image_select_rectangle(ImageWindow *imd, gboolean active)
{
	if (rectangle_owner) image_rectangle_clear(rectangle_owner);
	if (!active || !imd) return;
	auto *pr = PIXBUF_RENDERER(imd->pr);
	if (!pr->pixbuf || pr->scale <= 0) return;
	rectangle_owner = imd;
	selection_rectangle = SelectionRectangle(pr->x_offset - pr->x_scroll, pr->y_offset - pr->y_scroll,
	                                         options->rectangle_draw_aspect_ratio);
	selection_rectangle.width = std::max(1, static_cast<gint>(pr->image_width * pr->scale));
	selection_rectangle.height = std::max(1, static_cast<gint>(pr->image_height * pr->scale * pr->aspect_ratio));
	if (selection_rectangle.aspect_ratio > 0)
		{
		const gint old_width = selection_rectangle.width;
		const gint old_height = selection_rectangle.height;
		selection_rectangle.width = std::min(old_width, static_cast<gint>(old_height * selection_rectangle.aspect_ratio));
		selection_rectangle.height = std::max(1, static_cast<gint>(selection_rectangle.width / selection_rectangle.aspect_ratio));
		selection_rectangle.x += (old_width - selection_rectangle.width) / 2;
		selection_rectangle.y += (old_height - selection_rectangle.height) / 2;
		}
	image_rectangle_draw(imd);
}

void image_rectangle_update_aspect_ratio()
{
	if (!rectangle_owner || !rect_id) return;
	auto *pr = PIXBUF_RENDERER(rectangle_owner->pr);
	const double center_x = selection_rectangle.x + (selection_rectangle.width / 2.0);
	const double center_y = selection_rectangle.y + (selection_rectangle.height / 2.0);
	double width = selection_rectangle.width;
	double height = selection_rectangle.height;
	const double ratio = rectangle_aspect_ratio(options->rectangle_draw_aspect_ratio);
	if (ratio > 0)
		{
		const double area = width * height;
		width = std::sqrt(area * ratio);
		height = width / ratio;
		const double left = pr->x_offset - pr->x_scroll;
		const double top = pr->y_offset - pr->y_scroll;
		const double right = left + (pr->image_width * pr->scale);
		const double bottom = top + (pr->image_height * pr->scale * pr->aspect_ratio);
		const double factor = std::min({1.0, (2 * std::min(center_x - left, right - center_x)) / width,
		                              (2 * std::min(center_y - top, bottom - center_y)) / height});
		width *= factor;
		height *= factor;
		}
	selection_rectangle = SelectionRectangle(static_cast<int>(center_x - (width / 2.0)),
	                                         static_cast<int>(center_y - (height / 2.0)),
	                                         options->rectangle_draw_aspect_ratio);
	selection_rectangle.width = std::max(1, static_cast<int>(width));
	selection_rectangle.height = std::max(1, static_cast<int>(height));
	image_rectangle_draw(rectangle_owner);
}

static void image_press_cb(PixbufRenderer *, GqMouseButtonEvent *event, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);
	LayoutWindow *lw;

	if (options->draw_rectangle)
		{
		if (event->button != GDK_BUTTON_PRIMARY) return;
		if (rectangle_owner && rectangle_owner != imd) image_rectangle_clear(rectangle_owner);
		rectangle_resize_edges = rect_id ? selection_rectangle.hit_test(event->x, event->y) : 0;
		rectangle_before_drag = selection_rectangle;
		rectangle_dragging = TRUE;
		rectangle_owner = imd;
		rectangle_press = {static_cast<gint>(event->x), static_cast<gint>(event->y)};
		rectangle_moving = rect_id && !rectangle_resize_edges &&
		    event->x >= selection_rectangle.x && event->x < selection_rectangle.x + selection_rectangle.width &&
		    event->y >= selection_rectangle.y && event->y < selection_rectangle.y + selection_rectangle.height;
		rectangle_pending = !rectangle_resize_edges && !rectangle_moving;
		selection_rectangle.aspect_ratio = rectangle_aspect_ratio(options->rectangle_draw_aspect_ratio);
		return;
		}
	image_rectangle_clear(imd);

	lw = layout_find_by_image(imd);
	if (!lw)
		{
		lw = get_current_layout();
		}

	if (lw && event->button == GDK_BUTTON_PRIMARY && event->press_count == 2
												&& !options->image_lm_click_nav)
		{
		layout_image_full_screen_toggle(lw);
		}
}

static void image_release_cb(PixbufRenderer *, GqMouseButtonEvent *event, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);
	LayoutWindow *lw;

	if (options->draw_rectangle && event->button == GDK_BUTTON_PRIMARY)
		{
		rectangle_dragging = FALSE;
		return;
		}

	lw = layout_find_by_image(imd);
	if (!lw)
		{
		lw = get_current_layout();
		}

	layout_handle_user_defined_mouse_buttons(lw, event->button);
}

static void image_drag_cb(PixbufRenderer *pr, GqPointerMotionEvent *event, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	if (options->draw_rectangle && rectangle_owner == imd && rectangle_dragging)
		{
		gint width;
		gint height;
		if (!pixbuf_renderer_get_image_size(pr, width, height) || pr->scale <= 0) return;
		const gint left = pr->x_offset - pr->x_scroll;
		const gint top = pr->y_offset - pr->y_scroll;
		const gint right = left + static_cast<gint>(width * pr->scale);
		const gint bottom = top + static_cast<gint>(height * pr->scale * pr->aspect_ratio);
		if (right <= left || bottom <= top) return;
		const gint cursor_x = std::clamp(static_cast<gint>(event->x), left, right);
		const gint cursor_y = std::clamp(static_cast<gint>(event->y), top, bottom);
		if (rectangle_pending)
			{
			constexpr gint drag_threshold = 4;
			if (std::abs(event->x - rectangle_press.x) < drag_threshold &&
			    std::abs(event->y - rectangle_press.y) < drag_threshold) return;
			selection_rectangle = SelectionRectangle(std::clamp(rectangle_press.x, left, right),
			                                         std::clamp(rectangle_press.y, top, bottom),
			                                         options->rectangle_draw_aspect_ratio);
			rectangle_before_drag = selection_rectangle;
			rectangle_pending = FALSE;
			}
		if (rectangle_moving)
			{
			selection_rectangle.x = std::clamp(rectangle_before_drag.x + static_cast<gint>(event->x - rectangle_press.x),
			                                   left, right - selection_rectangle.width);
			selection_rectangle.y = std::clamp(rectangle_before_drag.y + static_cast<gint>(event->y - rectangle_press.y),
			                                   top, bottom - selection_rectangle.height);
			}
		else if (rectangle_resize_edges)
			selection_rectangle.resize(cursor_x, cursor_y, rectangle_resize_edges, rectangle_before_drag);
		else
			selection_rectangle.set_cursor(cursor_x, cursor_y);
		// Fit fixed-ratio rectangles to the image without changing the fixed edge or corner.
		if (selection_rectangle.aspect_ratio > 0 && !rectangle_moving)
			{
			guint edges = rectangle_resize_edges;
			if (!edges)
				{
				edges = cursor_x < rectangle_before_drag.x ? RECTANGLE_LEFT : RECTANGLE_RIGHT;
				edges |= cursor_y < rectangle_before_drag.y ? RECTANGLE_TOP : RECTANGLE_BOTTOM;
				}
			const gdouble anchor_x = (edges & RECTANGLE_LEFT) ? rectangle_before_drag.x + rectangle_before_drag.width
			    : (edges & RECTANGLE_RIGHT) ? rectangle_before_drag.x
			    : rectangle_before_drag.x + (rectangle_before_drag.width / 2.0);
			const gdouble anchor_y = (edges & RECTANGLE_TOP) ? rectangle_before_drag.y + rectangle_before_drag.height
			    : (edges & RECTANGLE_BOTTOM) ? rectangle_before_drag.y
			    : rectangle_before_drag.y + (rectangle_before_drag.height / 2.0);
			const gdouble max_width = (edges & RECTANGLE_LEFT) ? anchor_x - left
			    : (edges & RECTANGLE_RIGHT) ? right - anchor_x : 2 * std::min(anchor_x - left, right - anchor_x);
			const gdouble max_height = (edges & RECTANGLE_TOP) ? anchor_y - top
			    : (edges & RECTANGLE_BOTTOM) ? bottom - anchor_y : 2 * std::min(anchor_y - top, bottom - anchor_y);
			const gdouble ratio = selection_rectangle.aspect_ratio;
			selection_rectangle.width = std::max(1, static_cast<gint>(std::min({static_cast<gdouble>(selection_rectangle.width), max_width, max_height * ratio})));
			selection_rectangle.height = std::max(1, static_cast<gint>(std::round(selection_rectangle.width / ratio)));
			selection_rectangle.x = anchor_x - ((edges & RECTANGLE_LEFT) ? selection_rectangle.width
			    : (edges & RECTANGLE_RIGHT) ? 0 : selection_rectangle.width / 2.0);
			selection_rectangle.y = anchor_y - ((edges & RECTANGLE_TOP) ? selection_rectangle.height
			    : (edges & RECTANGLE_BOTTOM) ? 0 : selection_rectangle.height / 2.0);
			}
		selection_rectangle.x = std::clamp(selection_rectangle.x, left, right - 1);
		selection_rectangle.y = std::clamp(selection_rectangle.y, top, bottom - 1);
		selection_rectangle.width = std::clamp(selection_rectangle.width, 1, right - selection_rectangle.x);
		selection_rectangle.height = std::clamp(selection_rectangle.height, 1, bottom - selection_rectangle.y);

		image_rectangle_draw(imd);
		gtk_widget_set_cursor_from_name(GTK_WIDGET(pr), rectangle_moving ? "move" : rectangle_cursor(rectangle_resize_edges));
		return;
		}

	if (imd->func_drag)
		{
		imd->func_drag(imd,
		               event,
		               imd->data_drag);
		}
}

static void image_scroll_notify_cb(PixbufRenderer *pr, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	if (imd->scroll_notify_func && pr->scale)
		{
		imd->scroll_notify_func(imd,
		                        static_cast<gdouble>(pr->x_scroll) / pr->scale,
		                        static_cast<gdouble>(pr->y_scroll) / pr->scale,
		                        static_cast<gdouble>(pr->image_width) - (pr->vis_width / pr->scale),
		                        static_cast<gdouble>(pr->image_height) - (pr->vis_height / pr->scale));
		}
}

static void image_update_util(ImageWindow *imd)
{
	if (imd->func_update) imd->func_update(imd, imd->data_update);
}


static void image_complete_util(ImageWindow *imd, gboolean preload)
{
	if (imd->il && image_get_pixbuf(imd) != image_loader_get_pixbuf(imd->il)) return;

	DEBUG_1("%s image load completed \"%s\" (%s)", get_exec_time(),
			  (preload) ? (imd->read_ahead_fd ? imd->read_ahead_fd->path : "null") :
				      (imd->image_fd ? imd->image_fd->path : "null"),
			  (preload) ? "preload" : "current");

	if (!preload) imd->completed = TRUE;
	if (imd->func_complete) imd->func_complete(imd, preload, imd->data_complete);
}

static void image_render_complete_cb(PixbufRenderer *, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	image_complete_util(imd, FALSE);
}

static void image_state_set(ImageWindow *imd, ImageState state)
{
	if (state == IMAGE_STATE_NONE)
		{
		imd->state = state;
		}
	else
		{
		imd->state = static_cast<ImageState>(imd->state | state);
		}
	if (imd->func_state) imd->func_state(imd, state, imd->data_state);
}

static void image_state_unset(ImageWindow *imd, ImageState state)
{
	imd->state = static_cast<ImageState>(imd->state & ~state);
	if (imd->func_state) imd->func_state(imd, state, imd->data_state);
}

static void image_zoom_cb(PixbufRenderer *, gdouble, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	if (imd->title_show_zoom) image_update_title(imd);
	image_state_set(imd, IMAGE_STATE_IMAGE);
	image_update_util(imd);
}

/*
 *-------------------------------------------------------------------
 * misc
 *-------------------------------------------------------------------
 */

void image_update_title(ImageWindow *imd)
{
	if (!imd->top_window) return;

	g_autoptr(GString) title = g_string_new(imd->title);

	if (imd->image_fd) title = g_string_append(title, imd->image_fd->name);

	if (imd->title_show_zoom)
		{
		g_autofree gchar *buf = image_zoom_get_as_text(imd);
		g_string_append_printf(title, " [%s]", buf);
		}

	if (imd->collection && collection_valid(imd->collection))
		{
		const gchar *name = imd->collection->name;
		if (!name) name = _("Untitled");
		g_string_append_printf(title, _(" (Collection %s)"), name);
		}

	if (imd->image_fd) title = g_string_append(title, " - ");
	if (imd->title_right) title = g_string_append(title, imd->title_right);

	if (options->show_window_ids)
		{
		LayoutWindow *lw = layout_find_by_image(imd);
		if (lw)
			{
			g_string_append_printf(title, " (%s)", lw->options.id);
			}
		}

	gtk_window_set_title(GTK_WINDOW(imd->top_window), title->str);
}

/*
 *-------------------------------------------------------------------
 * rotation, flip, etc.
 *-------------------------------------------------------------------
 */
static GdkMonitor *image_get_monitor(const ImageWindow *imd)
{
	GtkNative *native = gtk_widget_get_native(imd->widget);
	GdkSurface *surface = native ? gtk_native_get_surface(native) : nullptr;
	GdkDisplay *display = gtk_widget_get_display(imd->widget);
	return surface ? gdk_display_get_monitor_at_surface(display, surface) : nullptr;
}

static bool image_get_system_screen_profile(const ImageWindow *imd, ColorManMemData &screen_data)
{
	screen_data.ptr.reset();
	screen_data.len = 0;

	auto *monitor = image_get_monitor(imd);
	if (!monitor || !gdk_monitor_is_valid(monitor)) return false;

	constexpr auto CACHE_KEY = "geeqie-display-profile-cache";
	auto *cache = static_cast<DisplayProfileCache *>(g_object_get_data(G_OBJECT(monitor), CACHE_KEY));
	if (!cache)
		{
		cache = new DisplayProfileCache;
		g_object_set_data_full(G_OBJECT(monitor), CACHE_KEY, cache,
		                      [](gpointer data) { delete static_cast<DisplayProfileCache *>(data); });
		g_signal_connect(monitor, "invalidate", G_CALLBACK(+[](GdkMonitor *monitor, gpointer)
			{
			g_object_set_data(G_OBJECT(monitor), "geeqie-display-profile-cache", nullptr);
			}), nullptr);
		}

	g_autoptr(GBytes) bytes = cache->get(gdk_monitor_get_connector(monitor), [monitor]()
		{
		// Never retain ImageWindow pointers across a lookup. Reload only current users.
		for (GList *work = image_list; work; work = work->next)
			{
			auto *image = static_cast<ImageWindow *>(work->data);
			auto *layout = layout_find_by_image(image);
			if (!layout || layout->animation) continue;
			if (image->image_fd && !image->il && image->color_profile_enable && options->color_profile.use_x11_screen_profile &&
			    image_get_monitor(image) == monitor)
				image_reload(image);
			}
		});
	if (!bytes) return false;

	gsize length = 0;
	const auto *contents = g_bytes_get_data(bytes, &length);
	screen_data.ptr.reset(static_cast<guchar *>(g_memdup2(contents, length)));
	screen_data.len = length;
	return true;
}

static gboolean image_post_process_color(ImageWindow *imd, gboolean run_in_bg)
{
	ColorMan *cm;
	ColorManProfileType input_type;
	ColorManProfileType screen_type;
	const gchar *input_file = nullptr;
	const gchar *screen_file = nullptr;

	if (imd->cm) return FALSE;

	ColorManMemData screen_profile;
	if (options->color_profile.use_x11_screen_profile &&
	    image_get_system_screen_profile(imd, screen_profile))
		{
		screen_type = COLOR_PROFILE_MEM;
		DEBUG_1("Using system screen profile, length: %u", screen_profile.len);
		}
	else if (options->color_profile.screen_file &&
	    is_readable_file(options->color_profile.screen_file))
		{
		screen_type = COLOR_PROFILE_FILE;
		screen_file = options->color_profile.screen_file;
		}
	else
		{
		screen_type = COLOR_PROFILE_SRGB;
		screen_file = nullptr;
		}


	ColorManProfileType color_profile_from_image = COLOR_PROFILE_NONE;
	ColorManMemData profile = exif_get_color_profile(imd->image_fd, color_profile_from_image);

	if (!imd->color_profile_use_image)
		{
		profile.ptr.reset();
		color_profile_from_image = COLOR_PROFILE_NONE;
		}

	if (color_profile_from_image != COLOR_PROFILE_NONE)
		{
		input_type = color_profile_from_image;
		}
	else if (imd->color_profile_input >= COLOR_PROFILE_FILE &&
	         imd->color_profile_input < COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS)
		{
		const gchar *file = options->color_profile.input_file[imd->color_profile_input - COLOR_PROFILE_FILE];

		if (!is_readable_file(file)) return FALSE;

		input_type = COLOR_PROFILE_FILE;
		input_file = file;
		}
	else if (imd->color_profile_input >= COLOR_PROFILE_SRGB &&
	         imd->color_profile_input < COLOR_PROFILE_FILE)
		{
		input_type = static_cast<ColorManProfileType>(imd->color_profile_input);
		}
	else
		{
		return FALSE;
		}

	const GdkPixbuf *pixbuf = run_in_bg ? image_get_pixbuf(imd) : nullptr;

	if (profile.ptr)
		{
		cm = color_man_new_embedded(pixbuf, profile,
		                            screen_type, screen_file, screen_profile);
		}
	else
		{
		cm = color_man_new(pixbuf, input_type, input_file,
		                   screen_type, screen_file, screen_profile);
		}

	if (cm)
		{
		imd->cm = cm;
		}

	image_update_util(imd);

	return !!cm;
}


void image_alter_orientation(ImageWindow *imd, FileData *fd_n, AlterType type)
{
	static const gint rotate_90[]    = {1,   6, 7, 8, 5, 2, 3, 4, 1};
	static const gint rotate_90_cc[] = {1,   8, 5, 6, 7, 4, 1, 2, 3};
	static const gint rotate_180[]   = {1,   3, 4, 1, 2, 7, 8, 5, 6};
	static const gint mirror[]       = {1,   2, 1, 4, 3, 6, 5, 8, 7};
	static const gint flip[]         = {1,   4, 3, 2, 1, 8, 7, 6, 5};

	if (!imd || !imd->pr || !imd->image_fd || !fd_n) return;

	gint orientation;

	if (fd_n->user_orientation)
		{
		orientation = fd_n->user_orientation;
		}
	else if (options->metadata.write_orientation)
		{
		if (imd->image_fd->supports_exif_orientation())
			{
			orientation = metadata_read_int(fd_n, ORIENTATION_KEY, EXIF_ORIENTATION_TOP_LEFT);
			}
		else
			{
			orientation = EXIF_ORIENTATION_TOP_LEFT;
			}
		}
	else
		{
		orientation = EXIF_ORIENTATION_TOP_LEFT;
		}

	switch (type)
		{
		case ALTER_ROTATE_90:
			orientation = rotate_90[orientation];
			break;
		case ALTER_ROTATE_90_CC:
			orientation = rotate_90_cc[orientation];
			break;
		case ALTER_ROTATE_180:
			orientation = rotate_180[orientation];
			break;
		case ALTER_MIRROR:
			orientation = mirror[orientation];
			break;
		case ALTER_FLIP:
			orientation = flip[orientation];
			break;
		case ALTER_NONE:
			orientation = fd_n->exif_orientation ? fd_n->exif_orientation : 1;
			break;
		default:
			return;
			break;
		}

	if (orientation != (fd_n->exif_orientation ? fd_n->exif_orientation : 1))
		{
		if (!options->metadata.write_orientation || !fd_n->supports_exif_orientation())
			{
			/* user_orientation does not work together with options->metadata.write_orientation,
			   use either one or the other.
			   we must however handle switching metadata.write_orientation on and off, therefore
			   we just disable referencing new fd's, not unreferencing the old ones
			*/
			if (fd_n->user_orientation == 0) file_data_ref(fd_n);
			fd_n->user_orientation = orientation;
			}
		}
	else
		{
		if (fd_n->user_orientation != 0) file_data_unref(fd_n);
		fd_n->user_orientation = 0;
		}

	if (options->metadata.write_orientation && fd_n->supports_exif_orientation())
		{
		if (type == ALTER_NONE)
			{
			metadata_write_revert(fd_n, ORIENTATION_KEY);
			}
		else
			{
			metadata_write_int(fd_n, ORIENTATION_KEY, orientation);
			}
		}

	if (imd->image_fd == fd_n && (!options->metadata.write_orientation || options->image.exif_rotate_enable))
		{
		imd->orientation = orientation;
		pixbuf_renderer_set_orientation(PIXBUF_RENDERER(imd->pr), orientation);
		}
}

static void image_set_pixbuf_renderer_post_process_func(ImageWindow *imd)
{
	if (imd->cm || imd->desaturate || imd->overunderexposed)
		{
		const auto image_post_process_tile_color_cb = [imd](PixbufRenderer *, GdkPixbuf **pixbuf, gint x, gint y, gint w, gint h)
		{
			if (imd->cm) imd->cm->correct_region(*pixbuf, {x, y, w, h});
			if (imd->desaturate) pixbuf_desaturate_rect(*pixbuf, x, y, w, h);
			if (imd->overunderexposed) pixbuf_highlight_overunderexposed(*pixbuf, x, y, w, h);
		};
		pixbuf_renderer_set_post_process_func(PIXBUF_RENDERER(imd->pr), image_post_process_tile_color_cb, (imd->cm != nullptr) );
		}
	else
		{
		pixbuf_renderer_set_post_process_func(PIXBUF_RENDERER(imd->pr), nullptr, TRUE);
		}
}

void image_set_desaturate(ImageWindow *imd, gboolean desaturate)
{
	imd->desaturate = desaturate;
	image_set_pixbuf_renderer_post_process_func(imd);
	pixbuf_renderer_set_orientation(PIXBUF_RENDERER(imd->pr), imd->orientation);
}

gboolean image_get_desaturate(ImageWindow *imd)
{
	return imd->desaturate;
}

void image_set_overunderexposed(ImageWindow *imd, gboolean overunderexposed)
{
	imd->overunderexposed = overunderexposed;
	image_set_pixbuf_renderer_post_process_func(imd);
	pixbuf_renderer_set_orientation(PIXBUF_RENDERER(imd->pr), imd->orientation);
}

void image_set_ignore_alpha(ImageWindow *imd, gboolean ignore_alpha)
{
   pixbuf_renderer_set_ignore_alpha(PIXBUF_RENDERER(imd->pr), ignore_alpha);
}

/*
 *-------------------------------------------------------------------
 * read ahead (prebuffer)
 *-------------------------------------------------------------------
 */

static void image_read_ahead_cancel(ImageWindow *imd)
{
	DEBUG_1("%s read ahead cancelled for :%s", get_exec_time(), imd->read_ahead_fd ? imd->read_ahead_fd->path : "null");

	image_loader_free(imd->read_ahead_il);
	imd->read_ahead_il = nullptr;

	file_data_unref(imd->read_ahead_fd);
	imd->read_ahead_fd = nullptr;
}

static void image_read_ahead_done_cb(ImageLoader *, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	if (!imd->read_ahead_fd || !imd->read_ahead_il) return;

	DEBUG_1("%s read ahead done for :%s", get_exec_time(), imd->read_ahead_fd->path);

	if (!imd->read_ahead_fd->pixbuf)
		{
		imd->read_ahead_fd->pixbuf = image_loader_get_pixbuf(imd->read_ahead_il);
		if (imd->read_ahead_fd->pixbuf)
			{
			g_object_ref(imd->read_ahead_fd->pixbuf);
			image_cache_set(imd, imd->read_ahead_fd);
			}
		}
	image_loader_free(imd->read_ahead_il);
	imd->read_ahead_il = nullptr;

	image_complete_util(imd, TRUE);
}

static void image_read_ahead_error_cb(ImageLoader *il, gpointer data)
{
	/* we even treat errors as success, maybe at least some of the file was ok */
	image_read_ahead_done_cb(il, data);
}

static void image_read_ahead_start(ImageWindow *imd)
{
	/* already started ? */
	if (!imd->read_ahead_fd || imd->read_ahead_il || imd->read_ahead_fd->pixbuf) return;

	/* still loading ?, do later */
	if (imd->il /*|| imd->cm*/) return;

	DEBUG_1("%s read ahead started for :%s", get_exec_time(), imd->read_ahead_fd->path);

	imd->read_ahead_il = image_loader_new(imd->read_ahead_fd);

	image_loader_delay_area_ready(imd->read_ahead_il, TRUE); /* we will need the area_ready signals later */

	g_signal_connect(G_OBJECT(imd->read_ahead_il), "error", (GCallback)image_read_ahead_error_cb, imd);
	g_signal_connect(G_OBJECT(imd->read_ahead_il), "done", (GCallback)image_read_ahead_done_cb, imd);

	if (!image_loader_start_deferred(imd->read_ahead_il))
		{
		image_read_ahead_cancel(imd);
		image_complete_util(imd, TRUE);
		}
}

static void image_read_ahead_set(ImageWindow *imd, FileData *fd)
{
	if (imd->read_ahead_fd && fd && imd->read_ahead_fd == fd) return;

	image_read_ahead_cancel(imd);

	imd->read_ahead_fd = file_data_ref(fd);

	DEBUG_1("read ahead set to :%s", imd->read_ahead_fd->path);

	image_read_ahead_start(imd);
}

/*
 *-------------------------------------------------------------------
 * post buffering
 *-------------------------------------------------------------------
 */

static void image_cache_release_cb(FileData *fd)
{
	g_object_unref(fd->pixbuf);
	fd->pixbuf = nullptr;
}

static FileCache *image_get_cache()
{
	static FileCache *cache = file_cache_new(image_cache_release_cb, 1);
	file_cache_set_max_size(cache, static_cast<gulong>(options->image.image_cache_max) * 1048576); /* update from options */
	return cache;
}

static void image_cache_set(ImageWindow *, FileData *fd)
{
	g_assert(fd->pixbuf);

	file_cache_put(image_get_cache(), fd, static_cast<gulong>(gdk_pixbuf_get_rowstride(fd->pixbuf)) * static_cast<gulong>(gdk_pixbuf_get_height(fd->pixbuf)));
	file_data_send_notification(fd, NOTIFY_PIXBUF); /* to update histogram */
}

static gint image_cache_get(ImageWindow *imd)
{
	gint success;

	success = file_cache_get(image_get_cache(), imd->image_fd);
	if (success)
		{
		g_assert(imd->image_fd->pixbuf);
		image_change_pixbuf(imd, imd->image_fd->pixbuf, image_zoom_get(imd), FALSE);
		}

	return success;
}

/*
 *-------------------------------------------------------------------
 * loading
 *-------------------------------------------------------------------
 */

static void image_load_pixbuf_ready(ImageWindow *imd)
{
	if (image_get_pixbuf(imd) || !imd->il) return;

	image_change_pixbuf(imd, image_loader_get_pixbuf(imd->il), image_zoom_get(imd), FALSE);
}

static void image_load_area_ready_cb(ImageLoader *il, const GdkRectangle *area, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);
	PixbufRenderer *pr = PIXBUF_RENDERER(imd->pr);

	if (imd->delay_flip &&
	    pr->pixbuf != image_loader_get_pixbuf(il))
		{
		return;
		}

	if (!pr->pixbuf) image_change_pixbuf(imd, image_loader_get_pixbuf(imd->il), image_zoom_get(imd), TRUE);

	pixbuf_renderer_area_changed(pr, *area);
}

static void image_load_done_cb(ImageLoader *, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	DEBUG_1("%s image done", get_exec_time());

	if (options->image.enable_read_ahead && imd->image_fd && !imd->image_fd->pixbuf && image_loader_get_pixbuf(imd->il))
		{
		imd->image_fd->pixbuf = g_object_ref(image_loader_get_pixbuf(imd->il));
		image_cache_set(imd, imd->image_fd);
		}
	/* call the callback triggered by image_state after fd->pixbuf is set */
	g_object_set(imd->pr, "loading", FALSE, NULL);
	image_state_unset(imd, IMAGE_STATE_LOADING);

	if (!image_loader_get_pixbuf(imd->il))
		{
		GdkPixbuf *pixbuf = pixbuf_fallback(imd->image_fd, 0, 0);

		image_change_pixbuf(imd, pixbuf, image_zoom_get(imd), FALSE);
		g_object_unref(pixbuf);

		imd->unknown = TRUE;
		}
	else if (imd->delay_flip &&
	    image_get_pixbuf(imd) != image_loader_get_pixbuf(imd->il))
		{
		g_object_set(imd->pr, "complete", FALSE, NULL);
		image_change_pixbuf(imd, image_loader_get_pixbuf(imd->il), image_zoom_get(imd), FALSE);
		}

	image_loader_free(imd->il);
	imd->il = nullptr;
	if (imd->image_fd && !imd->unknown && image_get_pixbuf(imd))
		file_data_send_notification(imd->image_fd, NOTIFY_PIXBUF);

	image_read_ahead_start(imd);
}

static void image_load_size_prepared_cb(ImageLoader *, const GqSize *size, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	DEBUG_1("image_load_size_cb: %dx%d", size->width, size->height);
	pixbuf_renderer_set_size_early(PIXBUF_RENDERER(imd->pr), size->width, size->height);
}

static void image_load_error_cb(ImageLoader *il, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);
	g_autoptr(GError) error = image_loader_dup_error(il);

	DEBUG_1("%s image error", get_exec_time());
	image_state_set(imd, IMAGE_STATE_ERROR);

	if (error)
		{
		log_printf("%s: %s\n", _("Image load failed"), error->message);
		if (error->domain == GDK_PIXBUF_ERROR && error->code == GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY)
			{
			warning_dialog(_("Image is too large"), error->message,
			               GQ_ICON_DIALOG_WARNING, imd->top_window);
			}
		}

	/* even on error handle it like it was done,
	 * since we have a pixbuf with _something_ */

	image_load_done_cb(il, data);
}

static void image_load_set_signals(ImageWindow *imd, gboolean override_old_signals)
{
	g_assert(imd->il);
	if (override_old_signals)
		{
		/* override the old signals */
		g_signal_handlers_disconnect_matched(G_OBJECT(imd->il), G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, imd);
		}

	g_signal_connect(G_OBJECT(imd->il), "area-ready", G_CALLBACK(image_load_area_ready_cb), imd);
	g_signal_connect(G_OBJECT(imd->il), "error", (GCallback)image_load_error_cb, imd);
	g_signal_connect(G_OBJECT(imd->il), "done", (GCallback)image_load_done_cb, imd);
	g_signal_connect(G_OBJECT(imd->il), "size-prepared", G_CALLBACK(image_load_size_prepared_cb), imd);
}

/* this read ahead is located here merely for the callbacks, above */

static gboolean image_read_ahead_check(ImageWindow *imd)
{
	if (!imd->read_ahead_fd) return FALSE;
	if (imd->il) return FALSE;

	if (!imd->image_fd || imd->read_ahead_fd != imd->image_fd)
		{
		image_read_ahead_cancel(imd);
		return FALSE;
		}

	if (imd->read_ahead_il)
		{
		imd->il = imd->read_ahead_il;
		imd->read_ahead_il = nullptr;

		image_load_set_signals(imd, TRUE);

		g_object_set(imd->pr, "loading", TRUE, NULL);
		image_state_set(imd, IMAGE_STATE_LOADING);

		if (!imd->delay_flip)
			{
			image_change_pixbuf(imd, image_loader_get_pixbuf(imd->il), image_zoom_get(imd), TRUE);
			}

		image_loader_delay_area_ready(imd->il, FALSE); /* send the delayed area_ready signals */

		file_data_unref(imd->read_ahead_fd);
		imd->read_ahead_fd = nullptr;
		return TRUE;
		}
	if (imd->read_ahead_fd->pixbuf)
		{
		image_change_pixbuf(imd, imd->read_ahead_fd->pixbuf, image_zoom_get(imd), FALSE);

		file_data_unref(imd->read_ahead_fd);
		imd->read_ahead_fd = nullptr;

		return TRUE;
		}

	image_read_ahead_cancel(imd);
	return FALSE;
}

static gboolean image_load_begin(ImageWindow *imd, FileData *fd)
{
	DEBUG_1("%s image begin", get_exec_time());

	if (imd->il) return FALSE;

	imd->completed = FALSE;
	g_object_set(imd->pr, "complete", FALSE, NULL);

	if (image_cache_get(imd))
		{
		DEBUG_1("from cache: %s", imd->image_fd->path);
		return TRUE;
		}

	if (image_read_ahead_check(imd))
		{
		DEBUG_1("from read ahead buffer: %s", imd->image_fd->path);
		return TRUE;
		}

	if (!imd->delay_flip && image_get_pixbuf(imd))
		{
		PixbufRenderer *pr;

		pr = PIXBUF_RENDERER(imd->pr);
		if (pr->pixbuf) g_object_unref(pr->pixbuf);
		pr->pixbuf = nullptr;
		}

	g_object_set(imd->pr, "loading", TRUE, NULL);

	imd->il = image_loader_new(fd);

	image_load_set_signals(imd, FALSE);

	if (!image_loader_start_deferred(imd->il))
		{
		DEBUG_1("image start error");

		g_object_set(imd->pr, "loading", FALSE, NULL);

		image_loader_free(imd->il);
		imd->il = nullptr;

		image_complete_util(imd, FALSE);

		return FALSE;
		}

	image_state_set(imd, IMAGE_STATE_LOADING);

/*
	if (!imd->delay_flip && !image_get_pixbuf(imd) && image_loader_get_pixbuf(imd->il))
		{
		image_change_pixbuf(imd, image_loader_get_pixbuf(imd->il), image_zoom_get(imd), TRUE);
		}
*/
	return TRUE;
}

static void image_reset(ImageWindow *imd)
{
	/* stops anything currently being done */

	DEBUG_1("%s image reset", get_exec_time());

	g_object_set(imd->pr, "loading", FALSE, NULL);

	image_loader_free(imd->il);
	imd->il = nullptr;

	g_clear_pointer(&imd->cm, delete_cb<ColorMan>);

	image_state_set(imd, IMAGE_STATE_NONE);
}

/*
 *-------------------------------------------------------------------
 * image changer
 *-------------------------------------------------------------------
 */

static void image_change_complete(ImageWindow *imd, gdouble zoom)
{
	image_reset(imd);
	imd->unknown = TRUE;

	/** @FIXME Might be improved when the wepb animation changes happen */
	g_object_set(imd->pr,
	             "zoom_2pass", options->image.zoom_2pass,
	             "zoom_quality", options->image.zoom_quality,
	             NULL);

	if (!imd->image_fd)
		{
		image_change_pixbuf(imd, nullptr, zoom, FALSE);
		}
	else
		{

		if (is_readable_file(imd->image_fd->path))
			{
			PixbufRenderer *pr;

			pr = PIXBUF_RENDERER(imd->pr);
			pr->zoom = zoom;	/* store the zoom, needed by the loader */

			LayoutWindow *lw = get_current_layout();
			if (lw->animation)
				{
				/* Disable 2-pass for GIFs. Animated GIFs can flicker when enabled
				 * Reduce quality to worst but fastest to avoid dropped frames */
				if (g_ascii_strcasecmp(imd->image_fd->extension, ".GIF") == 0)
					{
					g_object_set(imd->pr,
					             "zoom_2pass", FALSE,
					             "zoom_quality", GDK_INTERP_NEAREST,
					             NULL);
					}
				}


			if (image_load_begin(imd, imd->image_fd))
				{
				imd->unknown = FALSE;
				}
			}

		if (imd->unknown)
			{
			GdkPixbuf *pixbuf;

			pixbuf = pixbuf_inline(PIXBUF_INLINE_BROKEN);
			image_change_pixbuf(imd, pixbuf, zoom, FALSE);
			g_object_unref(pixbuf);
			}
		}

	image_update_util(imd);
}

static void image_change_real(ImageWindow *imd, FileData *fd,
			      CollectionData *cd, CollectInfo *info, gdouble zoom)
{

	image_transition_cancel(imd);
	image_rectangle_clear(imd);
	imd->collection = cd;
	imd->collection_info = info;

	if (imd->auto_refresh && imd->image_fd)
		file_data_unregister_real_time_monitor(imd->image_fd);

	if (imd->image_fd != fd && imd->func_file_changed)
		imd->func_file_changed(imd, imd->data_file_changed);

	file_data_unref(imd->image_fd);
	imd->image_fd = file_data_ref(fd);


	image_change_complete(imd, zoom);

	image_update_title(imd);
	image_state_set(imd, IMAGE_STATE_IMAGE);

	if (imd->auto_refresh && imd->image_fd)
		file_data_register_real_time_monitor(imd->image_fd);
}

/*
 *-------------------------------------------------------------------
 * focus stuff
 *-------------------------------------------------------------------
 */
static void image_focus_in_cb(GtkEventControllerFocus *, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	if (imd->func_focus_in)
		{
		imd->func_focus_in(imd, imd->data_focus_in);
		}
}

static void image_motion_cb(GtkEventControllerMotion *, gdouble x, gdouble y, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	imd->pointer_x = x;
	imd->pointer_y = y;
	imd->pointer_position_valid = TRUE;
	if (options->draw_rectangle)
		{
		const guint edges = rectangle_owner == imd && rect_id ? selection_rectangle.hit_test(x, y) : 0;
		const bool inside = rectangle_owner == imd && rect_id && x >= selection_rectangle.x &&
		    x < selection_rectangle.x + selection_rectangle.width && y >= selection_rectangle.y &&
		    y < selection_rectangle.y + selection_rectangle.height;
		gtk_widget_set_cursor_from_name(imd->pr, inside && !edges ? "move" : rectangle_cursor(edges));
		}
}

static gboolean image_scroll_cb(GtkEventControllerScroll *controller, gdouble dx, gdouble dy, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);
	GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));

	if (!imd->func_scroll || !event)
		{
		return FALSE;
		}

	gdouble event_x;
	gdouble event_y;
	if (gdk_event_get_position(event, &event_x, &event_y) &&
	    std::isfinite(event_x) && std::isfinite(event_y))
		{
		imd->pointer_x = event_x;
		imd->pointer_y = event_y;
		imd->pointer_position_valid = TRUE;
		}
	else if (!imd->pointer_position_valid)
		{
		imd->pointer_x = gtk_widget_get_width(imd->pr) / 2.0;
		imd->pointer_y = gtk_widget_get_height(imd->pr) / 2.0;
		}

	const GdkScrollDirection direction = gdk_scroll_event_get_direction(event);
	const GqScrollEvent scroll_event{
		imd->pointer_x,
		imd->pointer_y,
		dx,
		dy,
		gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller)),
		direction,
		gdk_scroll_event_get_unit(event),
		gdk_event_get_time(event)
	};

	LayoutWindow *lw = get_current_layout();
	gboolean in_lw = FALSE;

	if (lw)
		{
		for (auto & split_image : lw->split_images)
			{
			if (imd == split_image)
				{
				in_lw = TRUE;
				break;
				}
			}
		}

	if (in_lw)
		{
		if (lw->options.split_pane_sync)
			{
			for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
				{
				if (lw->split_images[i])
					{
					layout_image_activate(lw, i, FALSE);
					imd->func_scroll(lw->split_images[i], &scroll_event, lw->split_images[i]->data_scroll);
					}
				}
			}
		else
			{
			imd->func_scroll(imd, &scroll_event, imd->data_scroll);
			}

		return TRUE;
		}

	imd->func_scroll(imd, &scroll_event, imd->data_scroll);
	return TRUE;
}

/*
 *-------------------------------------------------------------------
 * public interface
 *-------------------------------------------------------------------
 */

void image_attach_window(ImageWindow *imd, GtkWidget *window,
			 const gchar *title, const gchar *title_right, gboolean show_zoom)
{
	LayoutWindow *lw;

	imd->top_window = window;
	g_free(imd->title);
	imd->title = g_strdup(title);
	g_free(imd->title_right);
	imd->title_right = g_strdup(title_right);
	imd->title_show_zoom = show_zoom;

	lw = layout_find_by_image(imd);

	if (!options->image.fit_window_to_image || !lw || (!lw->options.tools_float && !lw->options.tools_hidden)) window = nullptr;

	pixbuf_renderer_set_parent(PIXBUF_RENDERER(imd->pr), GTK_WINDOW(window));

	image_update_title(imd);
}

void image_set_update_func(ImageWindow *imd,
			   void (*func)(ImageWindow *imd, gpointer data),
			   gpointer data)
{
	imd->func_update = func;
	imd->data_update = data;
}

void image_set_complete_func(ImageWindow *imd,
			     void (*func)(ImageWindow *imd, gboolean preload, gpointer data),
			     gpointer data)
{
	imd->func_complete = func;
	imd->data_complete = data;
}

void image_set_state_func(ImageWindow *imd,
			void (*func)(ImageWindow *imd, ImageState state, gpointer data),
			gpointer data)
{
	imd->func_state = func;
	imd->data_state = data;
}

void image_set_button_func(ImageWindow *imd,
			   void (*func)(ImageWindow *, GqMouseButtonEvent *event, gpointer),
			   gpointer data)
{
	imd->func_button = func;
	imd->data_button = data;
}

void image_set_drag_func(ImageWindow *imd,
                         void (*func)(ImageWindow *, const GqPointerMotionEvent *event, gpointer),
                         gpointer data)
{
	imd->func_drag = func;
	imd->data_drag = data;
}

void image_set_scroll_func(ImageWindow *imd,
                           void (*func)(ImageWindow *, const GqScrollEvent *event, gpointer),
                           gpointer data)
{
	imd->func_scroll = func;
	imd->data_scroll = data;
}

void image_set_focus_in_func(ImageWindow *imd,
			   void (*func)(ImageWindow *, gpointer),
			   gpointer data)
{
	imd->func_focus_in = func;
	imd->data_focus_in = data;
}

/* path, name */

const gchar *image_get_path(ImageWindow *imd)
{
	if (imd->image_fd == nullptr) return nullptr;
	return imd->image_fd->path;
}

const gchar *image_get_name(ImageWindow *imd)
{
	if (imd->image_fd == nullptr) return nullptr;
	return imd->image_fd->name;
}

FileData *image_get_fd(ImageWindow *imd)
{
	return imd->image_fd;
}

/**
 * @brief Merely changes path string, does not change the image!
 */
void image_set_fd(ImageWindow *imd, FileData *fd)
{
	if (imd->auto_refresh && imd->image_fd)
		file_data_unregister_real_time_monitor(imd->image_fd);

	if (imd->image_fd != fd && imd->func_file_changed)
		imd->func_file_changed(imd, imd->data_file_changed);

	file_data_unref(imd->image_fd);
	imd->image_fd = file_data_ref(fd);

	image_update_title(imd);
	image_state_set(imd, IMAGE_STATE_IMAGE);

	if (imd->auto_refresh && imd->image_fd)
		file_data_register_real_time_monitor(imd->image_fd);
}

/* load a new image */

void image_change_fd(ImageWindow *imd, FileData *fd, gdouble zoom)
{
	if (imd->image_fd == fd) return;

	image_change_real(imd, fd, nullptr, nullptr, zoom);
}

gboolean image_get_image_size(ImageWindow *imd, gint &width, gint &height)
{
	return pixbuf_renderer_get_image_size(PIXBUF_RENDERER(imd->pr), width, height);
}

GdkPixbuf *image_get_pixbuf(ImageWindow *imd)
{
	return pixbuf_renderer_get_pixbuf(PIXBUF_RENDERER(imd->pr));
}

void image_change_pixbuf(ImageWindow *imd, GdkPixbuf *pixbuf, gdouble zoom, gboolean lazy)
{
	LayoutWindow *lw;
	StereoPixbufData stereo_data = STEREO_PIXBUF_DEFAULT;
	/* read_exif and similar functions can actually notice that the file has changed and trigger
	   a notification that removes the pixbuf from cache and unrefs it. Therefore we must ref it
	   here before it is taken over by the renderer. */
	if (pixbuf) g_object_ref(pixbuf);

	imd->orientation = EXIF_ORIENTATION_TOP_LEFT;
	if (imd->image_fd)
		{
		if (imd->image_fd->user_orientation)
			{
			imd->orientation = imd->image_fd->user_orientation;
			}
		else if (options->image.exif_rotate_enable)
			{
			if (imd->image_fd->supports_exif_orientation())
				{
				imd->orientation = metadata_read_int(imd->image_fd, ORIENTATION_KEY, EXIF_ORIENTATION_TOP_LEFT);
				}
			else
				{
				imd->orientation = EXIF_ORIENTATION_TOP_LEFT;
				}

			imd->image_fd->exif_orientation = imd->orientation;
			}
		}

	if (pixbuf)
		{
		stereo_data = imd->user_stereo;
		if (stereo_data == STEREO_PIXBUF_DEFAULT)
			{
			stereo_data = static_cast<StereoPixbufData>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pixbuf), "stereo_data")));
			}
		}

	pixbuf_renderer_set_post_process_func(PIXBUF_RENDERER(imd->pr), nullptr, FALSE);
	if (imd->cm)
		{
		g_clear_pointer(&imd->cm, delete_cb<ColorMan>);
		}

	if (lazy)
		{
		pixbuf_renderer_set_pixbuf_lazy(PIXBUF_RENDERER(imd->pr), pixbuf, zoom, imd->orientation, stereo_data);
		}
	else
		{
		pixbuf_renderer_set_pixbuf(PIXBUF_RENDERER(imd->pr), pixbuf, zoom);
		pixbuf_renderer_set_orientation(PIXBUF_RENDERER(imd->pr), imd->orientation);
		pixbuf_renderer_set_stereo_data(PIXBUF_RENDERER(imd->pr), stereo_data);
		}

	if (pixbuf) g_object_unref(pixbuf);

	/* Color correction takes too much time for an animated gif */
	lw = layout_find_by_image(imd);
	if (imd->color_profile_enable && lw && !lw->animation)
		{
		image_post_process_color(imd, FALSE); /** @todo error handling */
		}

	image_set_pixbuf_renderer_post_process_func(imd);

	image_state_set(imd, IMAGE_STATE_IMAGE);
}

void image_change_from_collection(ImageWindow *imd, CollectionData *cd, CollectInfo *info, gdouble zoom)
{
	if (!cd || !info || !g_list_find(cd->list, info)) return;

	image_change_real(imd, info->fd, cd, info, zoom);

	if (info->fd)
		{
		image_chain_append_end(info->fd->path);
		}
}

CollectionData *image_get_collection(ImageWindow *imd, CollectInfo **info)
{
	if (info) *info = nullptr;

	if (!collection_valid(imd->collection)) return nullptr;

	if (info && g_list_find(imd->collection->list, imd->collection_info))
		{
		*info = imd->collection_info;
		}

	return imd->collection;
}

static void image_loader_sync_read_ahead_data(ImageLoader *il, gpointer old_data, gpointer data)
{
	if (g_signal_handlers_disconnect_by_func(G_OBJECT(il), (gpointer)image_read_ahead_error_cb, old_data))
		g_signal_connect(G_OBJECT(il), "error", G_CALLBACK(image_read_ahead_error_cb), data);

	if (g_signal_handlers_disconnect_by_func(G_OBJECT(il), (gpointer)image_read_ahead_done_cb, old_data))
		g_signal_connect(G_OBJECT(il), "done", G_CALLBACK(image_read_ahead_done_cb), data);
}

static void image_loader_sync_data(ImageLoader *il, gpointer old_data, gpointer data)
{
	if (g_signal_handlers_disconnect_by_func(G_OBJECT(il), (gpointer)image_load_area_ready_cb, old_data))
		g_signal_connect(G_OBJECT(il), "area-ready", G_CALLBACK(image_load_area_ready_cb), data);

	if (g_signal_handlers_disconnect_by_func(G_OBJECT(il), (gpointer)image_load_error_cb, old_data))
		g_signal_connect(G_OBJECT(il), "error", G_CALLBACK(image_load_error_cb), data);

	if (g_signal_handlers_disconnect_by_func(G_OBJECT(il), (gpointer)image_load_done_cb, old_data))
		g_signal_connect(G_OBJECT(il), "done", G_CALLBACK(image_load_done_cb), data);
}

/* this is more like a move function
 * it moves most data from source to imd
 */
void image_move_from_image(ImageWindow *imd, ImageWindow *source)
{
	if (imd == source) return;

	image_transition_cancel(imd);
	image_transition_cancel(source);

	imd->unknown = source->unknown;

	imd->collection = source->collection;
	imd->collection_info = source->collection_info;

	image_loader_free(imd->il);
	imd->il = nullptr;

	image_set_fd(imd, image_get_fd(source));


	if (source->il)
		{
		imd->il = source->il;
		source->il = nullptr;

		image_loader_sync_data(imd->il, source, imd);
		}

	imd->color_profile_enable = source->color_profile_enable;
	imd->color_profile_input = source->color_profile_input;
	imd->color_profile_use_image = source->color_profile_use_image;

	g_clear_pointer(&imd->cm, delete_cb<ColorMan>);
	if (source->cm)
		{
		std::swap(imd->cm, source->cm);
		}

	file_data_unref(imd->read_ahead_fd);
	source->read_ahead_fd = nullptr;

	imd->orientation = source->orientation;
	imd->desaturate = source->desaturate;

	imd->user_stereo = source->user_stereo;

	pixbuf_renderer_move(PIXBUF_RENDERER(imd->pr), PIXBUF_RENDERER(source->pr));

	image_set_pixbuf_renderer_post_process_func(imd);
}

/* this is  a copy function
 * source stays unchanged
 */
void image_copy_from_image(ImageWindow *imd, ImageWindow *source)
{
	if (imd == source) return;

	image_transition_cancel(imd);
	image_transition_cancel(source);

	imd->unknown = source->unknown;

	imd->collection = source->collection;
	imd->collection_info = source->collection_info;

	image_loader_free(imd->il);
	imd->il = nullptr;

	image_set_fd(imd, image_get_fd(source));


	imd->color_profile_enable = source->color_profile_enable;
	imd->color_profile_input = source->color_profile_input;
	imd->color_profile_use_image = source->color_profile_use_image;

	g_clear_pointer(&imd->cm, delete_cb<ColorMan>);
	if (source->cm)
		{
		std::swap(imd->cm, source->cm);
		}

	image_loader_free(imd->read_ahead_il);
	imd->read_ahead_il = source->read_ahead_il;
	source->read_ahead_il = nullptr;
	if (imd->read_ahead_il) image_loader_sync_read_ahead_data(imd->read_ahead_il, source, imd);

	file_data_unref(imd->read_ahead_fd);
	imd->read_ahead_fd = source->read_ahead_fd;
	source->read_ahead_fd = nullptr;

	imd->completed = source->completed;
	imd->state = source->state;
	source->state = IMAGE_STATE_NONE;

	imd->orientation = source->orientation;
	imd->desaturate = source->desaturate;

	imd->user_stereo = source->user_stereo;

	pixbuf_renderer_copy(PIXBUF_RENDERER(imd->pr), PIXBUF_RENDERER(source->pr));

	image_set_pixbuf_renderer_post_process_func(imd);

	/* A copied renderer has no loader to finish an in-progress source image. */
	if (source->il) image_reload(imd);
}


/* manipulation */

void image_area_changed(ImageWindow *imd, gint x, gint y, gint width, gint height)
{
	pixbuf_renderer_area_changed(PIXBUF_RENDERER(imd->pr), {x, y, width, height});
}

void image_reload(ImageWindow *imd)
{
	if (pixbuf_renderer_get_tiles(PIXBUF_RENDERER(imd->pr))) return;

	image_change_complete(imd, image_zoom_get(imd));
}

void image_mousewheel_scroll(ImageWindow *imd, GdkScrollDirection direction)
{
	constexpr gint MOUSEWHEEL_SCROLL_SIZE = 20;

	switch (direction)
		{
		case GDK_SCROLL_UP:
			image_scroll(imd, 0, -MOUSEWHEEL_SCROLL_SIZE);
			break;
		case GDK_SCROLL_DOWN:
			image_scroll(imd, 0, MOUSEWHEEL_SCROLL_SIZE);
			break;
		case GDK_SCROLL_LEFT:
			image_scroll(imd, -MOUSEWHEEL_SCROLL_SIZE, 0);
			break;
		case GDK_SCROLL_RIGHT:
			image_scroll(imd, MOUSEWHEEL_SCROLL_SIZE, 0);
			break;
		default:
			break;
		}
	}

gint image_scroll_navigation_steps(ImageWindow *imd, const GqScrollEvent *event)
{
	gdouble delta = event->dy;

	if (event->unit != GDK_SCROLL_UNIT_WHEEL || delta == 0.0)
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP: delta = -1.0; break;
			case GDK_SCROLL_DOWN: delta = 1.0; break;
			default: return 0;
			}
		}

	if ((delta < 0.0 && imd->wheel_navigation_accumulator > 0.0) ||
	    (delta > 0.0 && imd->wheel_navigation_accumulator < 0.0))
		{
		imd->wheel_navigation_accumulator = 0.0;
		}

	imd->wheel_navigation_accumulator += delta;
	const gint steps = static_cast<gint>(imd->wheel_navigation_accumulator);
	imd->wheel_navigation_accumulator -= steps;

	return steps;
}

gdouble image_smooth_scroll_zoom_delta(ImageWindow *imd, gdouble delta, gdouble increment)
{
	if (delta == 0.0) return 0.0;

	if ((imd->smooth_zoom_accumulator < 0.0) != (delta < 0.0))
		{
		imd->smooth_zoom_accumulator = 0.0;
		}

	imd->smooth_zoom_accumulator += delta;
	const gint steps = static_cast<gint>(imd->smooth_zoom_accumulator);
	if (steps == 0) return 0.0;

	imd->smooth_zoom_accumulator -= steps;
	return -steps * increment;
}

void image_smooth_scroll_get_deltas(ImageWindow *imd, gdouble dx, gdouble dy, gdouble scale,
				    gint &x, gint &y)
{
	imd->smooth_scroll_x += dx * scale;
	imd->smooth_scroll_y += dy * scale;

	x = static_cast<gint>(imd->smooth_scroll_x);
	y = static_cast<gint>(imd->smooth_scroll_y);

	imd->smooth_scroll_x -= x;
	imd->smooth_scroll_y -= y;
}

void image_scroll(ImageWindow *imd, gint x, gint y)
{
	pixbuf_renderer_scroll(PIXBUF_RENDERER(imd->pr), x, y);
}

void image_scroll_to_point(ImageWindow *imd, gint x, gint y,
			   gdouble x_align, gdouble y_align)
{
	pixbuf_renderer_scroll_to_point(PIXBUF_RENDERER(imd->pr), x, y, x_align, y_align);
}

void image_get_scroll_center(ImageWindow *imd, gdouble &x, gdouble &y)
{
	pixbuf_renderer_get_scroll_center(PIXBUF_RENDERER(imd->pr), x, y);
}

void image_set_scroll_center(ImageWindow *imd, gdouble x, gdouble y)
{
	pixbuf_renderer_set_scroll_center(PIXBUF_RENDERER(imd->pr), x, y);
}

void image_zoom_adjust(ImageWindow *imd, gdouble increment)
{
	pixbuf_renderer_zoom_adjust(PIXBUF_RENDERER(imd->pr), increment);
}

void image_zoom_adjust_at_point(ImageWindow *imd, gdouble increment, gint x, gint y)
{
	pixbuf_renderer_zoom_adjust_at_point(PIXBUF_RENDERER(imd->pr), increment, x, y);
}

void image_zoom_set_limits(ImageWindow *imd, gdouble min, gdouble max)
{
	pixbuf_renderer_zoom_set_limits(PIXBUF_RENDERER(imd->pr), min, max);
}

void image_zoom_set(ImageWindow *imd, gdouble zoom)
{
	pixbuf_renderer_zoom_set(PIXBUF_RENDERER(imd->pr), zoom);
}

void image_zoom_set_fill_geometry(ImageWindow *imd, gboolean vertical)
{
	PixbufRenderer *pr = PIXBUF_RENDERER(imd->pr);
	gdouble zoom;
	gint width;
	gint height;

	if (!pixbuf_renderer_get_pixbuf(pr) ||
	    !pixbuf_renderer_get_image_size(pr, width, height)) return;

	if (vertical)
		{
		zoom = static_cast<gdouble>(pr->viewport_height) / height;
		}
	else
		{
		zoom = static_cast<gdouble>(pr->viewport_width) / width;
		}

	if (zoom < 1.0)
		{
		zoom = 0.0 - (1.0 / zoom);
		}

	pixbuf_renderer_zoom_set(pr, zoom);
}

gdouble image_zoom_get(ImageWindow *imd)
{
	return pixbuf_renderer_zoom_get(PIXBUF_RENDERER(imd->pr));
}

gdouble image_zoom_get_real(ImageWindow *imd)
{
	return pixbuf_renderer_zoom_get_scale(PIXBUF_RENDERER(imd->pr));
}

gchar *image_zoom_get_as_text(ImageWindow *imd)
{
	gdouble zoom;
	gdouble scale;
	gdouble l = 1.0;
	gdouble r = 1.0;
	gint pl = 0;
	gint pr = 0;
	const gchar *approx = " ";

	zoom = image_zoom_get(imd);
	scale = image_zoom_get_real(imd);

	if (zoom > 0.0)
		{
		l = zoom;
		}
	else if (zoom < 0.0)
		{
		r = 0.0 - zoom;
		}
	else if (zoom == 0.0 && scale != 0.0)
		{
		if (scale >= 1.0)
			{
			l = scale;
			}
		else
			{
			r = 1.0 / scale;
			}
		approx = "~";
		}

	if (rint(l) != l) pl = 2;
	if (rint(r) != r) pr = 2;

	return g_strdup_printf("%.*f :%s%.*f", pl, l, approx, pr, r);
}

gdouble image_zoom_get_default(ImageWindow *imd)
{
	gdouble zoom = 1.0;

	switch (options->image.zoom_mode)
	{
	case ZOOM_RESET_ORIGINAL:
		break;
	case ZOOM_RESET_FIT_WINDOW:
		zoom = 0.0;
		break;
	case ZOOM_RESET_NONE:
		if (imd) zoom = image_zoom_get(imd);
		break;
	}

	return zoom;
}

/* stereo */

void image_stereo_set(ImageWindow *imd, gint stereo_mode)
{
	DEBUG_1("Setting stereo mode %04x for imd %p", stereo_mode, (void *)imd);
	pixbuf_renderer_stereo_set(PIXBUF_RENDERER(imd->pr), stereo_mode);
}

StereoPixbufData image_stereo_pixbuf_get(const ImageWindow *imd)
{
	return imd->user_stereo;
}

void image_stereo_pixbuf_set(ImageWindow *imd, StereoPixbufData stereo_mode)
{
	imd->user_stereo = stereo_mode;
	image_reload(imd);
}

/**
 * @brief Read ahead, pass NULL to cancel
 */
void image_prebuffer_set(ImageWindow *imd, FileData *fd)
{
	if (pixbuf_renderer_get_tiles(PIXBUF_RENDERER(imd->pr))) return;

	if (fd)
		{
		if (!file_cache_get(image_get_cache(), fd))
			{
			image_read_ahead_set(imd, fd);
			}
		}
	else
		{
		image_read_ahead_cancel(imd);
		}
}

static void image_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);

	if (!imd || !image_get_pixbuf(imd) ||
	    /* imd->il || */ /* loading in progress - do not check - it should start from the beginning anyway */
	    !imd->image_fd || /* nothing to reload */
	    imd->state == IMAGE_STATE_NONE /* loading not started, no need to reload */
	    ) return;

	if ((type & NOTIFY_REREAD) && fd == imd->image_fd)
		{
		/* there is no need to reload on NOTIFY_CHANGE,
		   modified files should be detacted anyway and NOTIFY_REREAD should be received
		   or they are removed from the filelist completely on "move" and "delete"
		*/
		DEBUG_1("Notify image: %s %04x", fd->path, type);
		image_reload(imd);
		}
}

/**
 * @brief Auto refresh
 */
void image_auto_refresh_enable(ImageWindow *imd, gboolean enable)
{
	if (!enable && imd->auto_refresh && imd->image_fd)
		file_data_unregister_real_time_monitor(imd->image_fd);

	if (enable && !imd->auto_refresh && imd->image_fd)
		file_data_register_real_time_monitor(imd->image_fd);

	imd->auto_refresh = enable;
}

/**
 * @brief Allow top window to be resized ?
 */
void image_top_window_set_sync(ImageWindow *imd, gboolean allow_sync)
{
	imd->top_window_sync = allow_sync;

	g_object_set(imd->pr, "window_fit", allow_sync, NULL);
}

void image_background_set_color(ImageWindow *imd, const GdkRGBA &color)
{
	pixbuf_renderer_set_color(PIXBUF_RENDERER(imd->pr), color);
}

void image_background_set_color_from_options(ImageWindow *imd, gboolean fullscreen)
{
	GdkRGBA *color = nullptr;
	GdkRGBA theme_color;

	if ((options->image.use_custom_border_color && !fullscreen) ||
	    (options->image.use_custom_border_color_in_fullscreen && fullscreen))
		{
		color = &options->image.border_color;
		}
	else
		{
		theme_color.red = 0.0;
		theme_color.green = 0.0;
		theme_color.blue = 0.0;
		theme_color.alpha = 1.0;

		color = &theme_color;
		}

	image_background_set_color(imd, *color);
}

void image_color_profile_set(ImageWindow *imd, gint input_type, gboolean use_image)
{
	if (!imd) return;

	if (input_type < 0 || input_type >= COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS)
		{
		return;
		}

	imd->color_profile_input = input_type;
	imd->color_profile_use_image = use_image;
}

gboolean image_color_profile_get(const ImageWindow *imd, gint &input_type, gboolean &use_image)
{
	if (!imd) return FALSE;

	input_type = imd->color_profile_input;
	use_image = imd->color_profile_use_image;

	return TRUE;
}

void image_color_profile_set_use(ImageWindow *imd, gboolean enable)
{
	if (!imd) return;

	if (imd->color_profile_enable == enable) return;

	imd->color_profile_enable = enable;
}

gboolean image_color_profile_get_use(ImageWindow *imd)
{
	if (!imd) return FALSE;

	return imd->color_profile_enable;
}

std::optional<ColorManStatus> image_color_profile_get_status(const ImageWindow *imd)
{
	if (!imd || !imd->cm) return {};

	return imd->cm->get_status();
}

/**
 * @brief Set delayed page flipping
 */
void image_set_delay_flip(ImageWindow *imd, gboolean delay)
{
	if (!imd ||
	    imd->delay_flip == delay) return;

	imd->delay_flip = delay;

	g_object_set(imd->pr, "delay_flip", delay, NULL);

	if (!imd->delay_flip && imd->il)
		{
		PixbufRenderer *pr;

		pr = PIXBUF_RENDERER(imd->pr);
		if (pr->pixbuf) g_object_unref(pr->pixbuf);
		pr->pixbuf = nullptr;

		image_load_pixbuf_ready(imd);
		}
}

void image_select(ImageWindow *imd, bool select)
{
	if (!imd->has_frame) return;

	if (select)
		{
		gtk_widget_set_state_flags(imd->widget, GTK_STATE_FLAG_SELECTED, TRUE);
		gtk_widget_set_state_flags(imd->pr, GTK_STATE_FLAG_NORMAL, TRUE); /* do not propagate */
		}
	else
		gtk_widget_set_state_flags(imd->widget, GTK_STATE_FLAG_NORMAL, TRUE);
}

void image_set_selectable(ImageWindow *imd, gboolean selectable)
{
	if (!imd->has_frame) return;

	gtk_widget_remove_css_class(imd->frame, "frame");
	gtk_widget_set_margin_top(imd->frame, selectable ? 4 : 0);
	gtk_widget_set_margin_bottom(imd->frame, selectable ? 4 : 0);
	gtk_widget_set_margin_start(imd->frame, selectable ? 4 : 0);
	gtk_widget_set_margin_end(imd->frame, selectable ? 4 : 0);
}

void image_grab_focus(ImageWindow *imd)
{
	if (imd->has_frame)
		{
		gtk_widget_grab_focus(imd->frame);
		}
	else
		{
		gtk_widget_grab_focus(imd->widget);
		}
}


/*
 *-------------------------------------------------------------------
 * prefs sync
 *-------------------------------------------------------------------
 */

static void image_options_set(ImageWindow *imd, ConfOptions *options)
{
	g_object_set(imd->pr,
	             "zoom_quality", options->image.zoom_quality,
	             "zoom_2pass", options->image.zoom_2pass,
	             "zoom_expand", options->image.zoom_to_fit_allow_expand,
	             "scroll_reset", options->image.scroll_reset_method,
	             "cache_display", options->image.tile_cache_max,
	             "window_fit", imd->top_window_sync && options->image.fit_window_to_image,
	             "window_limit", options->image.limit_window_size,
	             "window_limit_value", options->image.max_window_size,
	             "autofit_limit", options->image.limit_autofit_size,
	             "autofit_limit_value", options->image.max_autofit_size,
	             "enlargement_limit_value", options->image.max_enlargement_size,
	             NULL);

	pixbuf_renderer_set_nearest_above_200(PIXBUF_RENDERER(imd->pr), options->image.nearest_above_200);
	pixbuf_renderer_set_allow_overscroll(PIXBUF_RENDERER(imd->pr), options->image.allow_overscroll);
	pixbuf_renderer_set_parent(PIXBUF_RENDERER(imd->pr), GTK_WINDOW(imd->top_window));

	image_stereo_set(imd, options->stereo.mode);
	pixbuf_renderer_stereo_fixed_set(PIXBUF_RENDERER(imd->pr), options->stereo.fixed_size,
	                                 options->stereo.fixed_left, options->stereo.fixed_right);
}

/**
 * @brief Reset default options
 */
void image_options_sync()
{
	g_list_foreach(image_list, reinterpret_cast<GFunc>(image_options_set), options);
}

/*
 *-------------------------------------------------------------------
 * init / destroy
 *-------------------------------------------------------------------
 */

static gboolean image_transition_ready(ImageWindow *imd)
{
	auto *pr = PIXBUF_RENDERER(imd->pr);
	return !imd->il && !pr->loading && pr->pixbuf && pr->complete &&
	       renderer_tiles_is_ready(pr->renderer) &&
	       (!pr->renderer2 || renderer_tiles_is_ready(pr->renderer2));
}

GdkPaintable *image_transition_capture(ImageWindow *imd)
{
	if (!gtk_widget_get_mapped(imd->pr) || !image_transition_ready(imd)) return nullptr;
	auto *snapshot = gtk_snapshot_new();
	// Snapshot the completed tiles directly; the widget's cached frame can still
	// contain the last progressive update rather than the final image.
	GTK_WIDGET_GET_CLASS(imd->pr)->snapshot(imd->pr, snapshot);
	graphene_size_t size;
	graphene_size_init(&size, gtk_widget_get_width(imd->pr), gtk_widget_get_height(imd->pr));
	return gtk_snapshot_free_to_paintable(snapshot, &size);
}

void image_transition_cancel(ImageWindow *imd)
{
	g_clear_handle_id(&imd->transition_source, g_source_remove);
	imd->transition_pending = FALSE;
	gtk_stack_set_transition_type(GTK_STACK(imd->view), GTK_STACK_TRANSITION_TYPE_NONE);
	if (gtk_stack_get_transition_running(GTK_STACK(imd->view)))
		gtk_stack_set_visible_child(GTK_STACK(imd->view), imd->outgoing[imd->outgoing_index]);
	gtk_stack_set_visible_child(GTK_STACK(imd->view), imd->pr);
	gtk_picture_set_paintable(GTK_PICTURE(imd->outgoing[imd->outgoing_index]), nullptr);
}

gboolean image_transition_active(ImageWindow *imd)
{
	return imd->transition_pending || gtk_stack_get_transition_running(GTK_STACK(imd->view));
}

static gboolean image_transition_ready_cb(gpointer data)
{
	auto *imd = static_cast<ImageWindow *>(data);
	if (!image_transition_ready(imd)) return G_SOURCE_CONTINUE;
	imd->transition_source = 0;
	imd->transition_pending = FALSE;
	gtk_stack_set_transition_duration(GTK_STACK(imd->view), imd->transition_duration);
	gtk_stack_set_transition_type(GTK_STACK(imd->view), imd->transition_type);
	gtk_stack_set_visible_child(GTK_STACK(imd->view), imd->pr);
	if (!gtk_stack_get_transition_running(GTK_STACK(imd->view)))
		gtk_picture_set_paintable(GTK_PICTURE(imd->outgoing[imd->outgoing_index]), nullptr);
	return G_SOURCE_REMOVE;
}

void image_transition_from(ImageWindow *imd, GdkPaintable *outgoing, GtkStackTransitionType type, guint duration)
{
	image_transition_cancel(imd);
	if (!outgoing || type == GTK_STACK_TRANSITION_TYPE_NONE || !gtk_widget_get_mapped(imd->view)) return;
	imd->outgoing_index ^= 1;
	imd->transition_type = type;
	imd->transition_duration = duration;
	imd->transition_pending = TRUE;
	gtk_picture_set_paintable(GTK_PICTURE(imd->outgoing[imd->outgoing_index]), outgoing);
	gtk_stack_set_visible_child(GTK_STACK(imd->view), imd->outgoing[imd->outgoing_index]);
	imd->transition_source = g_timeout_add(25, image_transition_ready_cb, imd);
}

static void image_free(ImageWindow *imd)
{
	g_clear_handle_id(&imd->transition_source, g_source_remove);
	g_signal_handlers_disconnect_by_data(imd->view, imd);
	image_rectangle_clear(imd);
	image_list = g_list_remove(image_list, imd);

	if (imd->auto_refresh && imd->image_fd)
		file_data_unregister_real_time_monitor(imd->image_fd);

	file_data_unregister_notify_func(image_notify_cb, imd);

	image_reset(imd);

	image_read_ahead_cancel(imd);

	file_data_unref(imd->image_fd);
	g_free(imd->title);
	g_free(imd->title_right);
	g_free(imd);
}

static void image_destroy_cb(GtkWidget *, gpointer data)
{
	auto imd = static_cast<ImageWindow *>(data);
	image_free(imd);
}

void image_set_frame(ImageWindow *imd, gboolean frame)
{
	frame = !!frame;

	if (frame == imd->has_frame) return;

	if (frame)
		{
		imd->frame = gtk_frame_new(nullptr);
		DEBUG_NAME(imd->frame);
		g_object_ref(imd->view);
		if (imd->has_frame != -1) gtk_overlay_set_child(GTK_OVERLAY(imd->overlay), nullptr);
		gtk_frame_set_child(GTK_FRAME(imd->frame), imd->view);

		g_object_unref(imd->view);
		gtk_widget_set_can_focus(imd->frame, TRUE);

		GtkEventController *controller = gtk_event_controller_focus_new();
		g_signal_connect(controller, "enter", G_CALLBACK(image_focus_in_cb), imd);
		gtk_widget_add_controller(imd->frame, controller);

		gtk_widget_set_hexpand(imd->frame, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(imd->widget))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
		gtk_widget_set_vexpand(imd->frame, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(imd->widget))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
		gtk_overlay_set_child(GTK_OVERLAY(imd->overlay), imd->frame);
		}
	else
		{
		g_object_ref(imd->view);
		if (imd->frame)
			{
			gtk_frame_set_child(GTK_FRAME(imd->frame), nullptr);
			gtk_overlay_set_child(GTK_OVERLAY(imd->overlay), nullptr);
			imd->frame = nullptr;
			}
		gtk_widget_set_hexpand(imd->view, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(imd->widget))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
		gtk_widget_set_vexpand(imd->view, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(imd->widget))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
		gtk_overlay_set_child(GTK_OVERLAY(imd->overlay), imd->view);

		g_object_unref(imd->view);
		}

	imd->has_frame = frame;
}

ImageWindow *image_new(gboolean frame)
{
	ImageWindow *imd;

	imd = g_new0(ImageWindow, 1);

	imd->unknown = TRUE;
	imd->has_frame = -1; /* not initialized; for image_set_frame */
	imd->state = IMAGE_STATE_NONE;
	imd->orientation = 1;

	imd->pr = GTK_WIDGET(pixbuf_renderer_new());
	DEBUG_NAME(imd->pr);

	image_options_set(imd, options);

	imd->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(imd->widget);

	imd->overlay = gtk_overlay_new();
	gtk_widget_set_hexpand(imd->overlay, TRUE);
	gtk_widget_set_vexpand(imd->overlay, TRUE);
	gtk_box_append(GTK_BOX(imd->widget), imd->overlay);

	imd->view = gtk_stack_new();
	gtk_widget_set_hexpand(imd->view, TRUE);
	gtk_widget_set_vexpand(imd->view, TRUE);
	gtk_stack_set_transition_type(GTK_STACK(imd->view), GTK_STACK_TRANSITION_TYPE_NONE);
	for (guint i = 0; i < G_N_ELEMENTS(imd->outgoing); ++i)
		{
		imd->outgoing[i] = gtk_picture_new();
		gtk_picture_set_can_shrink(GTK_PICTURE(imd->outgoing[i]), TRUE);
		gtk_picture_set_content_fit(GTK_PICTURE(imd->outgoing[i]), GTK_CONTENT_FIT_FILL);
		gtk_stack_add_child(GTK_STACK(imd->view), imd->outgoing[i]);
		if (i == 0) gtk_stack_add_named(GTK_STACK(imd->view), imd->pr, "image");
		}
	gtk_stack_set_visible_child(GTK_STACK(imd->view), imd->pr);
	g_signal_connect(imd->view, "unmap", G_CALLBACK(+[](GtkWidget *, gpointer data)
		{
		image_transition_cancel(static_cast<ImageWindow *>(data));
		}), imd);
	g_signal_connect(imd->view, "notify::transition-running", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer data)
		{
		auto *image = static_cast<ImageWindow *>(data);
		if (!image->transition_pending && !gtk_stack_get_transition_running(GTK_STACK(image->view)))
			gtk_picture_set_paintable(GTK_PICTURE(image->outgoing[image->outgoing_index]), nullptr);
		}), imd);
	image_set_frame(imd, frame);

	image_set_selectable(imd, 0);

	g_signal_connect(G_OBJECT(imd->pr), "clicked",
			 G_CALLBACK(image_click_cb), imd);
	g_signal_connect(G_OBJECT(imd->pr), "button-press",
			 G_CALLBACK(image_press_cb), imd);
	g_signal_connect(G_OBJECT(imd->pr), "button-release",
			 G_CALLBACK(image_release_cb), imd);
	g_signal_connect(G_OBJECT(imd->pr), "scroll_notify",
			 G_CALLBACK(image_scroll_notify_cb), imd);

	GtkEventController *motion_controller = gtk_event_controller_motion_new();
	g_signal_connect(motion_controller, "enter", G_CALLBACK(image_motion_cb), imd);
	g_signal_connect(motion_controller, "motion", G_CALLBACK(image_motion_cb), imd);
	gtk_widget_add_controller(GTK_WIDGET(imd->pr), motion_controller);

	GtkEventController *controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
	g_signal_connect(controller, "scroll", G_CALLBACK(image_scroll_cb), imd);
	gtk_widget_add_controller(GTK_WIDGET(imd->pr), controller);

	g_signal_connect(G_OBJECT(imd->pr), "destroy",
			 G_CALLBACK(image_destroy_cb), imd);

	g_signal_connect(G_OBJECT(imd->pr), "zoom",
			 G_CALLBACK(image_zoom_cb), imd);
	g_signal_connect(G_OBJECT(imd->pr), "render_complete",
			 G_CALLBACK(image_render_complete_cb), imd);
	g_signal_connect(G_OBJECT(imd->pr), "drag",
			 G_CALLBACK(image_drag_cb), imd);

	file_data_register_notify_func(image_notify_cb, imd, NOTIFY_PRIORITY_LOW);

	image_list = g_list_append(image_list, imd);

	return imd;
}

std::tuple<int, int, int, int> image_get_rectangle()
{
	return { rect_x1, rect_y1, rect_x2, rect_y2 };
}

gboolean image_get_selected_rectangle(ImageWindow *imd, GdkRectangle &rectangle)
{
	if (!options->draw_rectangle || rectangle_owner != imd || !rect_id) return FALSE;
	rectangle = {std::min(rect_x1, rect_x2), std::min(rect_y1, rect_y2),
	             std::abs(rect_x2 - rect_x1), std::abs(rect_y2 - rect_y1)};
	return rectangle.width > 0 && rectangle.height > 0;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
