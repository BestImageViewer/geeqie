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

#include <cmath>

#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view-filter.h"
#include "typedefs.h"

void pan_grid_compute(PanWindow *pw, FileData *dir_fd, gint *width, gint *height)
{
	GList *list;
	GList *work;
	gint x;
	gint y;
	gint grid_size;
	gint next_y;

	list = pan_list_tree(dir_fd, SORT_NAME, TRUE, TRUE, pw->ignore_symlinks);
	pan_filter_fd_list(&list, pw->filter_ui->filter_elements, pw->filter_ui->filter_classes);

	grid_size = static_cast<gint>(sqrt(static_cast<gdouble>(g_list_length(list))));
	if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
		{
		grid_size = grid_size * (512 + PAN_THUMB_GAP) * pw->image_size / 100;
		}
	else
		{
		grid_size = grid_size * (PAN_THUMB_SIZE + PAN_THUMB_GAP);
		}

	next_y = 0;

	*width = PAN_BOX_BORDER * 2;
	*height = PAN_BOX_BORDER * 2;

	x = PAN_THUMB_GAP;
	y = PAN_THUMB_GAP;
	work = list;
	while (work)
		{
		FileData *fd;
		PanItem *pi;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			pi = pan_item_image_new(pw, fd, x, y, 10, 10);

			x += pi->width + PAN_THUMB_GAP;
			if (y + pi->height + PAN_THUMB_GAP > next_y) next_y = y + pi->height + PAN_THUMB_GAP;
			if (x > grid_size)
				{
				x = PAN_THUMB_GAP;
				y = next_y;
				}
			}
		else
			{
			pi = pan_item_thumb_new(pw, fd, x, y);

			x += PAN_THUMB_SIZE + PAN_THUMB_GAP;
			if (x > grid_size)
				{
				x = PAN_THUMB_GAP;
				y += PAN_THUMB_SIZE + PAN_THUMB_GAP;
				}
			}
		pan_item_size_coordinates(pi, PAN_THUMB_GAP, width, height);
		}

	g_list_free(list);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
