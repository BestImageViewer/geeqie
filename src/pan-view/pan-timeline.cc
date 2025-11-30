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

#include "pan-timeline.h"

#include <algorithm>
#include <ctime>

#include "filedata.h"
#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view-filter.h"
#include "pan-view.h"

namespace
{

constexpr gint PAN_GROUP_MAX = 16;

} // namespace

void pan_timeline_compute(PanWindow *pw, FileData *dir_fd, gint &width, gint &height)
{
	GList *list;
	GList *work;
	gint x;
	gint y;
	time_t group_start_date;
	gint total;
	gint count;
	PanItem *pi_month = nullptr;
	PanItem *pi_day = nullptr;
	gint month_start;
	gint day_start;
	gint x_width;
	gint y_height;

	list = pan_list_tree(dir_fd, {SORT_NONE, TRUE, TRUE}, pw->ignore_symlinks);
	pan_filter_fd_list(&list, pw->filter_ui->filter_elements, pw->filter_ui->filter_classes);

	if (pw->cache_list && pw->exif_date_enable)
		{
		pw->cache_list = pan_cache_sort(pw->cache_list, {SORT_NAME, TRUE, TRUE});
		list = filelist_sort(list, {SORT_NAME, TRUE, TRUE});
		pan_cache_sync_date(pw, list);
		}

	pw->cache_list = pan_cache_sort(pw->cache_list, {SORT_TIME, TRUE, TRUE});
	list = filelist_sort(list, {SORT_TIME, TRUE, TRUE});

	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;

	x = 0;
	y = 0;
	month_start = y;
	day_start = month_start;
	x_width = 0;
	y_height = 0;
	group_start_date = 0;
	// total and count are used to enforce a stride of PAN_GROUP_MAX thumbs.
	total = 0;
	count = 0;
	work = list;
	while (work)
		{
		FileData *fd;
		PanItem *pi;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (!pan_date_compare(fd->date, group_start_date, PAN_DATE_LENGTH_DAY))
			{
			// FD starts a new day group.
			GList *needle;

			if (!pan_date_compare(fd->date, group_start_date, PAN_DATE_LENGTH_MONTH))
				{
				// FD starts a new month group.
				pi_day = nullptr;

				if (pi_month)
					{
					x = pi_month->x + pi_month->width + PAN_BOX_BORDER;
					}
				else
					{
					x = PAN_BOX_BORDER;
					}

				y = PAN_BOX_BORDER;

				g_autofree gchar *month_buf = pan_date_value_string(fd->date, PAN_DATE_LENGTH_MONTH);
				pi = pan_item_text_new(pw, x, y, month_buf,
						       static_cast<PanTextAttrType>(PAN_TEXT_ATTR_BOLD | PAN_TEXT_ATTR_HEADING),
						       PAN_BORDER_3,
						       {PAN_TEXT_COLOR, 255});
				y += pi->height;

				pi_month = pan_item_box_new(pw, file_data_ref(fd),
							    x, y, 0, 0,
							    PAN_BOX_OUTLINE_THICKNESS,
							    {PAN_BOX_COLOR, PAN_BOX_ALPHA},
							    {PAN_BOX_OUTLINE_COLOR, PAN_BOX_OUTLINE_ALPHA});

				x += PAN_BOX_BORDER;
				y += PAN_BOX_BORDER;
				month_start = y;
				}

			if (pi_day) x = pi_day->x + pi_day->width + PAN_BOX_BORDER;

			group_start_date = fd->date;
			total = 1;
			count = 0;

			needle = work;
			while (needle)
				{
				FileData *nfd;

				nfd = static_cast<FileData *>(needle->data);
				if (pan_date_compare(nfd->date, group_start_date, PAN_DATE_LENGTH_DAY))
					{
					needle = needle->next;
					total++;
					}
				else
					{
					needle = nullptr;
					}
				}

			g_autofree gchar *week_buf = pan_date_value_string(fd->date, PAN_DATE_LENGTH_WEEK);
			pi = pan_item_text_new(pw, x, y, week_buf, PAN_TEXT_ATTR_NONE,
					       PAN_BORDER_3,
					       {PAN_TEXT_COLOR, 255});

			y += pi->height;

			pi_day = pan_item_box_new(pw, file_data_ref(fd), x, y, 0, 0,
						  PAN_BOX_OUTLINE_THICKNESS,
						  {PAN_BOX_COLOR, PAN_BOX_ALPHA},
						  {PAN_BOX_OUTLINE_COLOR, PAN_BOX_OUTLINE_ALPHA});

			x += PAN_BOX_BORDER;
			y += PAN_BOX_BORDER;
			day_start = y;
			}

		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			pi = pan_item_image_new(pw, fd, x, y, 10, 10);
			x_width = std::max(pi->width, x_width);
			y_height = pi->height;
			}
		else
			{
			pi = pan_item_thumb_new(pw, fd, x, y);
			x_width = pw->thumb_size;
			y_height = pw->thumb_size;
			}

		pan_item_size_by_item(pi_day, pi, PAN_BOX_BORDER);
		pan_item_size_by_item(pi_month, pi_day, PAN_BOX_BORDER);

		total--;
		count++;

		if (total > 0 && count < PAN_GROUP_MAX)
			{
			y += y_height + pw->thumb_gap;
			}
		else
			{
			x += x_width + pw->thumb_gap;
			x_width = 0;
			count = 0;

			if (total > 0)
				y = day_start;
			else
				y = month_start;
			}

		pan_item_size_coordinates(pi_month, PAN_BOX_BORDER, width, height);
		}

	g_list_free(list);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
