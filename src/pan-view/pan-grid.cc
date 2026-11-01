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

#include "pan-grid.h"

#include <algorithm>
#include <cmath>

#include "pan-item.h"
#include "pan-types.h"
#include "pan-view.h"

std::function<bool()> pan_grid_compute(PanWindow *pw, gint &width, gint &height)
{
	struct State
	{
		PanLayoutFiles files;
		gint x;
		gint y;
		gint next_y = 0;
		gint grid_size;
		State(PanWindow *pw)
			: files(pan_list_tree_filtered(pw, SORT_NAME)),
			  x(pw->thumb_gap),
			  y(pw->thumb_gap),
			  grid_size(static_cast<gint>(sqrt(g_list_length(files.list))))
		{
		}
	};
	auto state = std::make_shared<State>(pw);
	if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
		state->grid_size = state->grid_size * (512 + pw->thumb_gap) * pw->image_size / 100;
	else
		state->grid_size *= pw->thumb_size + pw->thumb_gap;
	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;
	return [pw, state, &width, &height]()
		{
		auto &s = *state;
		auto *fd = s.files.take();
		if (!fd) return true;
		PanItem *pi;
		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			pi = pan_item_image_new(pw, fd, s.x, s.y, 10, 10);
			s.x += pi->width + pw->thumb_gap;
			s.next_y = std::max(s.y + pi->height + pw->thumb_gap, s.next_y);
			if (s.x > s.grid_size)
				{
				s.x = pw->thumb_gap;
				s.y = s.next_y;
				}
			}
		else
			{
			pi = pan_item_thumb_new(pw, fd, s.x, s.y);
			s.x += pw->thumb_size + pw->thumb_gap;
			if (s.x > s.grid_size)
				{
				s.x = pw->thumb_gap;
				s.y += pw->thumb_size + pw->thumb_gap;
				}
			}
		pi->adjust_size(pw->thumb_gap, width, height);
		return false;
		};
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
