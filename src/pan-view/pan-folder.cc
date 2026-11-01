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
#include <memory>
#include <vector>

#include <gdk/gdk.h>

#include "filedata.h"
#include "geometry.h"
#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view-filter.h"
#include "pan-view.h"

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
	std::list<FlowerGroup *> children;
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

static gdouble pan_flower_position(const FlowerGroup *group, const FlowerGroup *parent,
                                   gint &x, gint &y)
{
	const auto radius = std::max<gint>(parent->circumference / (2*G_PI),
	                                   (parent->diameter / 2) + (group->diameter / 2));
	const gdouble a = 2*G_PI * group->diameter / parent->circumference;

	x = static_cast<gint>(static_cast<gdouble>(radius) * cos(parent->angle + (a / 2)));
	y = static_cast<gint>(static_cast<gdouble>(radius) * sin(parent->angle + (a / 2)));

	x += parent->x + (parent->width / 2) - (group->width / 2);
	y += parent->y + (parent->height / 2) - (group->height / 2);

	return a;
}

static void pan_flower_build(PanWindow *pw, FlowerGroup *group, FlowerGroup *parent)
{
	if (parent)
		{
		gint x;
		gint y;
		parent->angle += pan_flower_position(group, parent, x, y);

		pan_flower_move(group, x, y);

		GqPoint cp{parent->x + (parent->width / 2), parent->y + (parent->height / 2)};
		GqPoint cg{group->x + (group->width / 2), group->y + (group->height / 2)};

		pan_item_tri_new(pw, cp, cg, {cg.x + 5, cg.y + 5},
		                 {255, 40, 40, 128},
		                 PAN_BORDER_1_3, {255, 0, 0, 128});
		}

	pw->list.splice(pw->list.cbegin(), group->items);

	group->circumference = 0;
	for (const FlowerGroup *child : group->children)
		{
		group->circumference += child->diameter;
		}

	for (FlowerGroup *child : group->children)
		{
		pan_flower_build(pw, child, group);
		}

	delete group;
}

namespace
{

void flower_group_free(FlowerGroup *group)
{
	if (!group) return;
	std::vector<FlowerGroup *> pending{group};
	while (!pending.empty())
		{
		auto *current = pending.back();
		pending.pop_back();
		for (auto *child : current->children) pending.push_back(child);
		pan_item_list_clear(current->items);
		delete current;
		}
}

struct FolderFrame
{
	FileDataRef folder;
	PanLayoutFiles files;
	PanLayoutFiles dirs;
	PanItem *box = nullptr;
	FlowerGroup *group = nullptr;
	gint level;
	gint x = 0, y = 0, start_x = 0, row_height = 0, columns = 0, column = 0;
	bool initialized = false, has_files = false, files_done = false;
	FolderFrame(FileData *fd, gint level) : folder(fd), level(level) {}
	~FolderFrame()
	{
		flower_group_free(group);
	}

	bool initialize(PanWindow *pw, bool flower, gint top)
	{
		initialized = true;
		if (!pan_read_directory(pw, folder, &files.list, &dirs.list) || (!files.list && !dirs.list)) return false;
		files.list = pan_filter_fd_list(files.list, pw->filter_ui);
		has_files = files.list != nullptr;
		x = flower ? 0 : PAN_BOX_BORDER + (level * std::max(PAN_BOX_BORDER, pw->thumb_gap));
		y = flower ? 0 : top;
		auto *text = pan_item_text_new(pw, x, y, folder->path, PAN_TEXT_ATTR_NONE, PAN_TEXT_BORDER, PAN_TEXT_COLOR);
		y += text->height;
		box = pan_item_box_new(pw, file_data_ref(folder), x, y, PAN_BOX_BORDER * (flower ? 2 : 1),
							   PAN_BOX_BORDER * (flower ? 2 : 1), PAN_BOX_COLOR, PAN_BOX_OUTLINE_THICKNESS,
							   PAN_BOX_OUTLINE_COLOR);
		x += PAN_BOX_BORDER;
		y += PAN_BOX_BORDER;
		start_x = x;
		columns = static_cast<gint>(sqrt(g_list_length(files.list)) + 0.9);
		return true;
	}

	void add_file(PanWindow *pw, FileData *fd, bool flower)
	{
		PanItem *item;
		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			item = pan_item_image_new(pw, fd, x, y, 10, 10);
			x += item->width + pw->thumb_gap;
			row_height = std::max(item->height, row_height);
			}
		else
			{
			item = pan_item_thumb_new(pw, fd, x, y);
			x += pw->thumb_size + pw->thumb_gap;
			row_height = pw->thumb_size;
			}
		if (flower && ++column >= columns)
			{
			column = 0;
			x = start_x;
			y += row_height + pw->thumb_gap;
			row_height = 0;
			}
		box->set_size_by_item(item, PAN_BOX_BORDER);
	}
};

struct FolderState
{
	std::vector<std::unique_ptr<FolderFrame>> stack;
	gint y = PAN_BOX_BORDER;
	explicit FolderState(FileData *root)
	{
		stack.push_back(std::make_unique<FolderFrame>(root, 0));
	}
	void descend(FolderFrame &frame)
	{
		auto *fd = frame.dirs.take();
		stack.push_back(std::make_unique<FolderFrame>(fd, frame.level + 1));
		file_data_unref(fd);
	}
};

}  // namespace

std::function<bool()> pan_flower_compute(PanWindow *pw, gint &width, gint &height, gint &scroll_x, gint &scroll_y)
{
	auto state = std::make_shared<FolderState>(pw->dir_fd);
	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;
	return [pw, state, &width, &height, &scroll_x, &scroll_y]()
		{
		if (state->stack.empty()) return true;
		auto &frame = *state->stack.back();
		if (!frame.initialized)
			{
			if (!frame.initialize(pw, true, 0)) state->stack.pop_back();
			return false;
			}
		if (auto *fd = frame.files.take())
			{
			frame.add_file(pw, fd, true);
			return false;
			}
		if (!frame.group)
			{
			frame.group = new FlowerGroup();
			frame.group->items.splice(frame.group->items.cend(), pw->list);
			frame.group->width = frame.box->width;
			frame.group->height = frame.box->y + frame.box->height;
			frame.group->diameter = static_cast<gint>(hypot(frame.group->width, frame.group->height));
			}
		if (frame.dirs.list)
			{
			state->descend(frame);
			return false;
			}
		auto *group = frame.group;
		frame.group = nullptr;
		const bool empty = !frame.has_files && group->children.empty();
		state->stack.pop_back();
		if (empty)
			{
			flower_group_free(group);
			return false;
			}
		if (!state->stack.empty())
			{
			state->stack.back()->group->children.push_back(group);
			return false;
			}
		pan_flower_build(pw, group, nullptr);
		pan_flower_size(pw, width, height);
		if (auto *item = pan_item_find_by_fd(pw, PAN_ITEM_BOX, pw->dir_fd, FALSE, FALSE))
			{
			scroll_x = item->x + (item->width / 2);
			scroll_y = item->y + (item->height / 2);
			}
		return true;
		};
}

std::function<bool()> pan_folder_tree_compute(PanWindow *pw, gint &width, gint &height)
{
	auto state = std::make_shared<FolderState>(pw->dir_fd);
	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;
	return [pw, state, &width, &height]()
		{
		if (state->stack.empty()) return true;
		auto &frame = *state->stack.back();
		if (!frame.initialized)
			{
			if (!frame.initialize(pw, false, state->y)) state->stack.pop_back();
			return false;
			}
		if (auto *fd = frame.files.take())
			{
			frame.add_file(pw, fd, false);
			return false;
			}
		if (!frame.files_done)
			{
			frame.files_done = true;
			state->y = frame.has_files ? frame.box->y + frame.box->height : frame.y;
			}
		if (frame.dirs.list)
			{
			state->descend(frame);
			return false;
			}
		if (state->stack.size() > 1)
			state->stack[state->stack.size() - 2]->box->set_size_by_item(frame.box, PAN_BOX_BORDER);
		state->y = std::max(state->y, frame.box->y + frame.box->height + PAN_BOX_BORDER);
		frame.box->adjust_size(PAN_BOX_BORDER, width, height);
		state->stack.pop_back();
		return false;
		};
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
