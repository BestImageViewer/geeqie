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

void pan_grid_compute(PanWindow *pw, gint &width, gint &height)
{
	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;

	g_autoptr(GList) list = pan_list_tree_filtered(pw, SORT_NAME);

	auto grid_size = static_cast<gint>(sqrt(g_list_length(list)));

	gint x = pw->thumb_gap;
	gint y = pw->thumb_gap;

	if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
		{
		grid_size = grid_size * (512 + pw->thumb_gap) * pw->image_size / 100;

		gint next_y = 0;

		for (GList *work = list; work; work = work->next)
			{
			auto *fd = static_cast<FileData *>(work->data);

			PanItem *pi = pan_item_image_new(pw, fd, x, y, 10, 10);

			x += pi->width + pw->thumb_gap;
			next_y = std::max(y + pi->height + pw->thumb_gap, next_y);
			if (x > grid_size)
				{
				x = pw->thumb_gap;
				y = next_y;
				}

			pi->adjust_size(pw->thumb_gap, width, height);
			}
		}
	else
		{
		grid_size = grid_size * (pw->thumb_size + pw->thumb_gap);

		for (GList *work = list; work; work = work->next)
			{
			auto *fd = static_cast<FileData *>(work->data);

			PanItem *pi = pan_item_thumb_new(pw, fd, x, y);

			x += pw->thumb_size + pw->thumb_gap;
			if (x > grid_size)
				{
				x = pw->thumb_gap;
				y += pw->thumb_size + pw->thumb_gap;
				}

			pi->adjust_size(pw->thumb_gap, width, height);
			}
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
