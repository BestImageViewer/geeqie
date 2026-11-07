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
#include <unordered_map>

#include "filedata.h"
#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view.h"

namespace
{

constexpr gint PAN_GROUP_MAX = 16;

gint64 timeline_day(time_t date)
{
	std::tm value{};
	if (!localtime_r(&date, &value)) return 0;
	return (static_cast<gint64>(value.tm_year) * 372) + (value.tm_mon * 31) + value.tm_mday;
}

}  // namespace

std::function<bool()> pan_timeline_compute(PanWindow *pw, gint &width, gint &height)
{
	struct State
	{
		PanLayoutFiles files;
		GList *counting;
		std::unordered_map<gint64, gint> day_counts;
		gint x = 0, y = 0;
		time_t group_start_date = 0;
		gint total = 0, count = 0;
		PanItem *pi_month = nullptr, *pi_day = nullptr;
		gint month_start = 0, day_start = 0, x_width = 0, y_height = 0;
		explicit State(GList *list) : files(list), counting(list) {}
	};
	auto state = std::make_shared<State>(pan_cache_sync_list(pw, pan_list_tree_filtered(pw, SORT_NONE)));
	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;
	return [pw, state, &width, &height]()
		{
		auto &s = *state;
		if (s.counting)
			{
			++s.day_counts[timeline_day(static_cast<FileData *>(s.counting->data)->date)];
			s.counting = s.counting->next;
			return false;
			}
		if (!s.files.list) return true;
		FileData *fd;
		PanItem *pi;

		fd = state->files.take();

		if (!pan_date_compare(fd->date, s.group_start_date, PanDateLength::DAY))
			{
			// FD starts a new day group.

			if (!pan_date_compare(fd->date, s.group_start_date, PanDateLength::MONTH))
				{
				// FD starts a new month group.
				s.pi_day = nullptr;

				if (s.pi_month)
					{
					s.x = s.pi_month->x + s.pi_month->width + PAN_BOX_BORDER;
					}
				else
					{
					s.x = PAN_BOX_BORDER;
					}

				s.y = PAN_BOX_BORDER;

				g_autofree gchar *month_buf = pan_date_value_string(fd->date, PanDateLength::MONTH);
				pi = pan_item_text_new(pw, s.x, s.y, month_buf, PAN_TEXT_ATTR_BOLD_HEADING, PAN_TEXT_BORDER,
									   PAN_TEXT_COLOR);
				s.y += pi->height;

				s.pi_month = pan_item_box_new(pw, file_data_ref(fd), s.x, s.y, 0, 0, PAN_BOX_COLOR,
											  PAN_BOX_OUTLINE_THICKNESS, PAN_BOX_OUTLINE_COLOR);

				s.x += PAN_BOX_BORDER;
				s.y += PAN_BOX_BORDER;
				s.month_start = s.y;
				}

			if (s.pi_day) s.x = s.pi_day->x + s.pi_day->width + PAN_BOX_BORDER;

			s.group_start_date = fd->date;
			s.total = s.day_counts[timeline_day(fd->date)];
			s.count = 0;

			g_autofree gchar *week_buf = pan_date_value_string(fd->date, PanDateLength::WEEK);
			pi = pan_item_text_new(pw, s.x, s.y, week_buf, PAN_TEXT_ATTR_NONE, PAN_TEXT_BORDER, PAN_TEXT_COLOR);

			s.y += pi->height;

			s.pi_day = pan_item_box_new(pw, file_data_ref(fd), s.x, s.y, 0, 0, PAN_BOX_COLOR, PAN_BOX_OUTLINE_THICKNESS,
										PAN_BOX_OUTLINE_COLOR);

			s.x += PAN_BOX_BORDER;
			s.y += PAN_BOX_BORDER;
			s.day_start = s.y;
			}

		if (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE)
			{
			pi = pan_item_image_new(pw, fd, s.x, s.y, 10, 10);
			s.x_width = std::max(pi->width, s.x_width);
			s.y_height = pi->height;
			}
		else
			{
			pi = pan_item_thumb_new(pw, fd, s.x, s.y);
			s.x_width = pw->thumb_size;
			s.y_height = pw->thumb_size;
			}

		if (s.pi_day) s.pi_day->set_size_by_item(pi, PAN_BOX_BORDER);
		if (s.pi_month) s.pi_month->set_size_by_item(s.pi_day, PAN_BOX_BORDER);

		s.total--;
		s.count++;

		if (s.total > 0 && s.count < PAN_GROUP_MAX)
			{
			s.y += s.y_height + pw->thumb_gap;
			}
		else
			{
			s.x += s.x_width + pw->thumb_gap;
			s.x_width = 0;
			s.count = 0;

			if (s.total > 0)
				s.y = s.day_start;
			else
				s.y = s.month_start;
			}

		if (s.pi_month) s.pi_month->adjust_size(PAN_BOX_BORDER, width, height);
		return false;
		};
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
