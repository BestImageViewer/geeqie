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

#include "pan-folder.h"

#include <algorithm>
#include <cmath>

#include <gdk/gdk.h>

#include "filedata.h"
#include "geometry.h"
#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view-filter.h"

static void pan_flower_size(PanWindow *pw, gint &width, gint &height)
{
	gint x1;
	gint y1;
	gint x2;
	gint y2;

	x1 = 0;
	y1 = 0;
	x2 = 0;
	y2 = 0;

	for (const PanItem *pi : pw->list)
		{
		x1 = std::min(x1, pi->x);
		y1 = std::min(y1, pi->y);
		x2 = std::max(x2, pi->x + pi->width);
		y2 = std::max(y2, pi->y + pi->height);
		}

	x1 -= PAN_BOX_BORDER;
	y1 -= PAN_BOX_BORDER;
	x2 += PAN_BOX_BORDER;
	y2 += PAN_BOX_BORDER;

	for (PanItem *pi : pw->list)
		{
		pi->x -= x1;
		pi->y -= y1;

		if (pi->is_type(PAN_ITEM_TRIANGLE))
			{
			pan_item_tri_shift(pi, x1, y1);
			}
		}

	width = x2 - x1;
	height = y2 - y1;
}

struct FlowerGroup {
	PanItemList items;
	GList *children;
	gint x;
	gint y;
	gint width;
	gint height;

	gdouble angle;
	gint circumference;
	gint diameter;
};

static void pan_flower_move(FlowerGroup *group, gint x, gint y)
{
	for (PanItem *pi : group->items)
		{
		pi->x += x;
		pi->y += y;
		}

	group->x += x;
	group->y += y;
}

static void pan_flower_position(FlowerGroup *group, FlowerGroup *parent,
							     gint *result_x, gint *result_y)
{
	gint x;
	gint y;
	gint radius;
	gdouble a;

	radius = parent->circumference / (2*G_PI);
	radius = std::max(radius, (parent->diameter / 2) + (group->diameter / 2));

	a = 2*G_PI * group->diameter / parent->circumference;

	x = static_cast<gint>(static_cast<gdouble>(radius) * cos(parent->angle + (a / 2)));
	y = static_cast<gint>(static_cast<gdouble>(radius) * sin(parent->angle + (a / 2)));

	parent->angle += a;

	x += parent->x;
	y += parent->y;

	x += parent->width / 2;
	y += parent->height / 2;

	x -= group->width / 2;
	y -= group->height / 2;

	*result_x = x;
	*result_y = y;
}

static void pan_flower_build(PanWindow *pw, FlowerGroup *group, FlowerGroup *parent)
{
	GList *work;
	gint x;
	gint y;

	if (!group) return;

	if (parent && parent->children)
		{
		pan_flower_position(group, parent, &x, &y);
		}
	else
		{
		x = 0;
		y = 0;
		}

	pan_flower_move(group, x, y);

	if (parent)
		{
		GqPoint cp{parent->x + (parent->width / 2), parent->y + (parent->height / 2)};
		GqPoint cg{group->x + (group->width / 2), group->y + (group->height / 2)};

		pan_item_tri_new(pw, cp, cg, {cg.x + 5, cg.y + 5},
		                 {255, 40, 40, 128},
		                 PAN_BORDER_1_3, {255, 0, 0, 128});
		}

	pw->list.splice(pw->list.cbegin(), group->items);

	group->circumference = 0;
	work = group->children;
	while (work)
		{
		FlowerGroup *child;

		child = static_cast<FlowerGroup *>(work->data);
		work = work->next;

		group->circumference += child->diameter;
		}

	work = g_list_last(group->children);
	while (work)
		{
		FlowerGroup *child;

		child = static_cast<FlowerGroup *>(work->data);
		work = work->prev;

		pan_flower_build(pw, child, group);
		}

	g_list_free(group->children);
	delete group;
}

static FlowerGroup *pan_flower_group(PanWindow *pw, FileData *dir_fd, gint x, gint y)
{
	g_autoptr(GList) f = nullptr;
	g_autoptr(FileDataList) d = nullptr;
	GList *work;
	PanItem *pi_box;
	gint x_start;
	gint y_height;
	gint grid_size;
	gint grid_count;

	if (!filelist_read(dir_fd, &f, &d)) return nullptr;
	if (!f && !d) return nullptr;

	f = filelist_sort(f, {SORT_NAME, TRUE, TRUE});
	d = filelist_sort(d, {SORT_NAME, TRUE, TRUE});

	f = pan_filter_fd_list(f, pw->filter_ui);

	pi_box = pan_item_text_new(pw, x, y, dir_fd->path, PAN_TEXT_ATTR_NONE,
	                           PAN_TEXT_BORDER, PAN_TEXT_COLOR);

	y += pi_box->height;

	pi_box = pan_item_box_new(pw, file_data_ref(dir_fd),
	                          x, y, PAN_BOX_BORDER * 2, PAN_BOX_BORDER * 2, PAN_BOX_COLOR,
	                          PAN_BOX_OUTLINE_THICKNESS, PAN_BOX_OUTLINE_COLOR);

	x += PAN_BOX_BORDER;
	y += PAN_BOX_BORDER;

	grid_size = static_cast<gint>(sqrt(g_list_length(f)) + 0.9);
	grid_count = 0;
	x_start = x;
	y_height = y;

	work = f;
	while (work)
		{
		FileData *fd;
		PanItem *pi;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			pi = pan_item_image_new(pw, fd, x, y, 10, 10);
			x += pi->width + pw->thumb_gap;
			y_height = std::max(pi->height, y_height);
			}
		else
			{
			pi = pan_item_thumb_new(pw, fd, x, y);
			x += pw->thumb_size + pw->thumb_gap;
			y_height = pw->thumb_size;
			}

		grid_count++;
		if (grid_count >= grid_size)
			{
			grid_count = 0;
			x = x_start;
			y += y_height + pw->thumb_gap;
			y_height = 0;
			}

		pi_box->set_size_by_item(pi, PAN_BOX_BORDER);
		}

	auto group = std::make_unique<FlowerGroup>();
	group->items.splice(group->items.cend(), pw->list);

	group->width = pi_box->width;
	group->height = pi_box->y + pi_box->height;
	group->diameter = static_cast<gint>(hypot(group->width, group->height));

	work = d;
	while (work)
		{
		FileData *fd;
		FlowerGroup *child;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (!pan_is_ignored(fd->path, pw->ignore_symlinks))
			{
			child = pan_flower_group(pw, fd, 0, 0);
			if (child) group->children = g_list_prepend(group->children, child);
			}
		}

	if (!f && !group->children)
		{
		pan_item_list_clear(group->items);
		return nullptr;
		}

	return group.release();
}

void pan_flower_compute(PanWindow *pw, gint &width, gint &height,
                        gint &scroll_x, gint &scroll_y)
{
	FlowerGroup *group;

	group = pan_flower_group(pw, pw->dir_fd, 0, 0);
	pan_flower_build(pw, group, nullptr);

	pan_flower_size(pw, width, height);

	PanItem *pi = pan_item_find_by_fd(pw, PAN_ITEM_BOX, pw->dir_fd, FALSE, FALSE);
	if (pi)
		{
		scroll_x = pi->x + pi->width / 2;
		scroll_y = pi->y + pi->height / 2;
		}
}

static void pan_folder_tree_path(PanWindow *pw, FileData *dir_fd,
                                 gint &x, gint &y, gint level,
                                 PanItem *parent,
                                 gint &width, gint &height)
{
	GList *f;
	GList *d;
	GList *work;
	PanItem *pi_box;
	gint y_height = 0;

	if (!filelist_read(dir_fd, &f, &d)) return;
	if (!f && !d) return;

	f = filelist_sort(f, {SORT_NAME, TRUE, TRUE});
	d = filelist_sort(d, {SORT_NAME, TRUE, TRUE});

	f = pan_filter_fd_list(f, pw->filter_ui);

	x = PAN_BOX_BORDER + (level * std::max(PAN_BOX_BORDER, pw->thumb_gap));

	pi_box = pan_item_text_new(pw, x, y, dir_fd->path, PAN_TEXT_ATTR_NONE,
	                           PAN_TEXT_BORDER, PAN_TEXT_COLOR);

	y += pi_box->height;

	pi_box = pan_item_box_new(pw, file_data_ref(dir_fd),
	                          x, y, PAN_BOX_BORDER, PAN_BOX_BORDER, PAN_BOX_COLOR,
	                          PAN_BOX_OUTLINE_THICKNESS, PAN_BOX_OUTLINE_COLOR);

	x += PAN_BOX_BORDER;
	y += PAN_BOX_BORDER;

	work = f;
	while (work)
		{
		FileData *fd;
		PanItem *pi;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			pi = pan_item_image_new(pw, fd, x, y, 10, 10);
			x += pi->width + pw->thumb_gap;
			y_height = std::max(pi->height, y_height);
			}
		else
			{
			pi = pan_item_thumb_new(pw, fd, x, y);
			x += pw->thumb_size + pw->thumb_gap;
			y_height = pw->thumb_size;
			}

		pi_box->set_size_by_item(pi, PAN_BOX_BORDER);
		}

	if (f) y = pi_box->y + pi_box->height;

	g_list_free(f);

	work = d;
	while (work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (!pan_is_ignored(fd->path, pw->ignore_symlinks))
			{
			pan_folder_tree_path(pw, fd, x, y, level + 1, pi_box, width, height);
			}
		}

	file_data_list_free(d);

	if (parent) parent->set_size_by_item(pi_box, PAN_BOX_BORDER);

	y = std::max(y, pi_box->y + pi_box->height + PAN_BOX_BORDER);

	pi_box->adjust_size(PAN_BOX_BORDER, width, height);
}

void pan_folder_tree_compute(PanWindow *pw, gint &width, gint &height)
{
	gint x;
	gint y;

	x = PAN_BOX_BORDER;
	y = PAN_BOX_BORDER;
	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;

	pan_folder_tree_path(pw, pw->dir_fd, x, y, 0, nullptr, width, height);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
