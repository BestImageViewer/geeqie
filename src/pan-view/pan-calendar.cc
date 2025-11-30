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

#include "pan-calendar.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>

#include <gdk/gdk.h>

#include "filedata.h"
#include "misc.h"
#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view-filter.h"
#include "pan-view.h"

namespace
{

constexpr gint PAN_CAL_POPUP_BORDER = 1;
constexpr guint8 PAN_CAL_POPUP_ALPHA = 255;
constexpr PanColor PAN_CAL_POPUP_COLOR{220, 220, 220, PAN_CAL_POPUP_ALPHA};
constexpr PanColor PAN_CAL_POPUP_BORDER_COLOR{0, 0, 0, PAN_CAL_POPUP_ALPHA};
constexpr PanColor PAN_CAL_POPUP_TEXT_COLOR{0, 0, 0, 255};

constexpr gint PAN_CAL_DAY_WIDTH = 100;
constexpr gint PAN_CAL_DAY_HEIGHT = 80;
constexpr gint PAN_CAL_DAY_BORDER = 2;
constexpr guint8 PAN_CAL_DAY_ALPHA = 220;
constexpr PanColor PAN_CAL_DAY_COLOR{255, 255, 255, PAN_CAL_DAY_ALPHA};
constexpr PanColor PAN_CAL_DAY_BORDER_COLOR{0, 0, 0, PAN_CAL_DAY_ALPHA};
constexpr PanColor PAN_CAL_DAY_TEXT_COLOR{0, 0, 0, 255};

constexpr gint PAN_CAL_MONTH_BORDER = 4;
constexpr guint8 PAN_CAL_MONTH_ALPHA = 200;
constexpr PanColor PAN_CAL_MONTH_COLOR{255, 255, 255, PAN_CAL_MONTH_ALPHA};
constexpr PanColor PAN_CAL_MONTH_BORDER_COLOR{0, 0, 0, PAN_CAL_MONTH_ALPHA};
constexpr PanColor PAN_CAL_MONTH_TEXT_COLOR{0, 0, 0, 255};

constexpr gint PAN_CAL_DOT_SIZE = 3;
constexpr gint PAN_CAL_DOT_GAP = 2;
constexpr guint8 PAN_CAL_DOT_ALPHA = 128;
constexpr PanColor PAN_CAL_DOT_COLOR{128, 128, 128, PAN_CAL_DOT_ALPHA};

constexpr PanColor PAN_CAL_DAY_OF_WEEK_COLOR{128, 128, 128, 255};

} // namespace

/*
 *-----------------------------------------------------------------------------
 * calendar
 *-----------------------------------------------------------------------------
 */

void pan_calendar_update(PanWindow *pw, PanItem *pi_day)
{
	PanItem *pbox;
	PanItem *pi;
	GList *list;
	GList *work;
	gint x;
	gint y;
	gint grid;
	gint column;

	while ((pi = pan_item_find_by_key(pw, PAN_ITEM_NONE, "day_bubble"))) pan_item_remove(pw, pi);

	if (!pi_day || pi_day->type != PAN_ITEM_BOX ||
	    !pi_day->key || strcmp(pi_day->key, "day") != 0) return;

	list = pan_layout_intersect(pw, pi_day->x, pi_day->y, pi_day->width, pi_day->height);

	work = list;
	while (work)
		{
		PanItem *dot;
		GList *node;

		dot = static_cast<PanItem *>(work->data);
		node = work;
		work = work->next;

		if (dot->type != PAN_ITEM_BOX || !dot->fd ||
		    !dot->key || strcmp(dot->key, "dot") != 0)
			{
			list = g_list_delete_link(list, node);
			}
		}

	grid = static_cast<gint>(sqrt(g_list_length(list)) + 0.5);

	x = pi_day->x + pi_day->width + 4;
	y = pi_day->y;

	pbox = pan_item_box_new(pw, nullptr, x, y, PAN_BOX_BORDER, PAN_BOX_BORDER,
				PAN_CAL_POPUP_BORDER, PAN_CAL_POPUP_COLOR, PAN_CAL_POPUP_BORDER_COLOR);
	pan_item_set_key(pbox, "day_bubble");

	if (pi_day->fd)
		{
		PanItem *plabel;

		g_autofree gchar *buf = pan_date_value_string(pi_day->fd->date, PAN_DATE_LENGTH_WEEK);
		plabel = pan_item_text_new(pw, x, y, buf, static_cast<PanTextAttrType>(PAN_TEXT_ATTR_BOLD | PAN_TEXT_ATTR_HEADING),
					   PAN_BORDER_3, PAN_CAL_POPUP_TEXT_COLOR);
		pan_item_set_key(plabel, "day_bubble");

		pan_item_size_by_item(pbox, plabel, 0);

		y += plabel->height;
		}

	if (list)
		{
		column = 0;

		x += PAN_BOX_BORDER;
		y += PAN_BOX_BORDER;

		work = list;
		while (work)
			{
			PanItem *dot;

			dot = static_cast<PanItem *>(work->data);
			work = work->next;

			if (dot->fd)
				{
				PanItem *pimg;

				pimg = pan_item_thumb_new(pw, file_data_ref(dot->fd), x, y);
				pan_item_set_key(pimg, "day_bubble");

				pan_item_size_by_item(pbox, pimg, PAN_BOX_BORDER);

				column++;
				if (column < grid)
					{
					x += pw->thumb_size + pw->thumb_gap;
					}
				else
					{
					column = 0;
					x = pbox->x + PAN_BOX_BORDER;
					y += pw->thumb_size + pw->thumb_gap;
					}
				}
			}
		}

	GdkPoint c1{pi_day->x + pi_day->width - 8, pi_day->y + 8};
	GdkPoint c2{pbox->x + 1, pbox->y + std::min(42, pbox->height)};
	GdkPoint c3{pbox->x + 1, std::max(pbox->y, c2.y - 30)};

	pi = pan_item_tri_new(pw,
	                      c1, c2, c3,
	                      PAN_CAL_POPUP_COLOR,
	                      PAN_BORDER_1 | PAN_BORDER_3, PAN_CAL_POPUP_BORDER_COLOR);
	pan_item_set_key(pi, "day_bubble");
	pan_item_added(pw, pi);

	pan_item_box_shadow(pbox, PAN_SHADOW_OFFSET * 2, PAN_SHADOW_FADE * 2);
	pan_item_added(pw, pbox);

	pan_layout_resize(pw);
}

void pan_calendar_compute(PanWindow *pw, FileData *dir_fd, gint &width, gint &height)
{
	GList *list;
	GList *work;
	gint x;
	gint y;
	time_t tc;
	gint count;
	gint day_max;
	gint grid;
	gint year = 0;
	gint month = 0;
	gint end_year = 0;
	gint end_month = 0;
	gint day_of_week;

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

	day_max = 0;
	count = 0;
	tc = 0;
	work = list;
	while (work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (!pan_date_compare(fd->date, tc, PAN_DATE_LENGTH_DAY))
			{
			count = 0;
			tc = fd->date;
			}
		else
			{
			count++;
			day_max = std::max(day_max, count);
			}
		}

	DEBUG_1("biggest day contains %d images", day_max);

	grid = static_cast<gint>(sqrt(static_cast<gdouble>(day_max)) + 0.5) * (pw->thumb_size + PAN_SHADOW_OFFSET * 2 + pw->thumb_gap);

	if (list)
		{
		auto fd = static_cast<FileData *>(list->data);

		year = pan_date_value(fd->date, PAN_DATE_LENGTH_YEAR);
		month = pan_date_value(fd->date, PAN_DATE_LENGTH_MONTH);
		}

	work = g_list_last(list);
	if (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		end_year = pan_date_value(fd->date, PAN_DATE_LENGTH_YEAR);
		end_month = pan_date_value(fd->date, PAN_DATE_LENGTH_MONTH);
		}

	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;

	x = PAN_BOX_BORDER;
	y = PAN_BOX_BORDER;

	work = list;
	while (work && (year < end_year || (year == end_year && month <= end_month)))
		{
		PanItem *pi_month;
		PanItem *pi_text;
		PanItem *pi_day_number;
		gint day;
		gint days;
		gint col;
		time_t dt;

		/* figure last second of this month */
		dt = pan_date_to_time((month == 12) ? year + 1 : year, (month == 12) ? 1 : month + 1, 1);
		dt -= 60 * 60 * 24;

		/* anything to show this month? */
		if (!pan_date_compare((static_cast<FileData *>(work->data))->date, dt, PAN_DATE_LENGTH_MONTH))
			{
			month ++;
			if (month > 12)
				{
				year++;
				month = 1;
				}
			continue;
			}

		days = pan_date_value(dt, PAN_DATE_LENGTH_DAY);
		dt = pan_date_to_time(year, month, 1);
		col = pan_date_value(dt, PAN_DATE_LENGTH_WEEK);
		col = col - (date_get_first_day_of_week() - 1);
		if (col < 0) col = col + 7;

		x = PAN_BOX_BORDER;

		pi_month = pan_item_box_new(pw, nullptr, x, y, PAN_CAL_DAY_WIDTH * 7, PAN_CAL_DAY_HEIGHT / 4,
					    PAN_CAL_MONTH_BORDER, PAN_CAL_MONTH_COLOR, PAN_CAL_MONTH_BORDER_COLOR);
		g_autofree gchar *month_buf = pan_date_value_string(dt, PAN_DATE_LENGTH_MONTH);
		pi_text = pan_item_text_new(pw, x, y, month_buf,
		                            static_cast<PanTextAttrType>(PAN_TEXT_ATTR_BOLD | PAN_TEXT_ATTR_HEADING),
		                            PAN_BORDER_3, PAN_CAL_MONTH_TEXT_COLOR);
		pi_text->x = pi_month->x + (pi_month->width - pi_text->width) / 2;

		pi_month->height = pi_text->y + pi_text->height - pi_month->y;

		x = PAN_BOX_BORDER + col * PAN_CAL_DAY_WIDTH;
		y = pi_month->y + pi_month->height + PAN_BOX_BORDER;

		for (day = 1; day <= days; day++)
			{
			FileData *fd;
			PanItem *pi_day;
			gint dx;
			gint dy;
			gint n = 0;
			gchar fake_path[20];

			dt = pan_date_to_time(year, month, day);

			/*
			 * Create a FileData entry that represents the given day.
			 * It does not correspond to any real file
			 */

			g_snprintf(fake_path, sizeof(fake_path), "//%04d-%02d-%02d", year, month, day);
			fd = file_data_new_no_grouping(fake_path);
			fd->date = dt;
			pi_day = pan_item_box_new(pw, fd, x, y, PAN_CAL_DAY_WIDTH, PAN_CAL_DAY_HEIGHT,
						  PAN_CAL_DAY_BORDER, PAN_CAL_DAY_COLOR, PAN_CAL_DAY_BORDER_COLOR);
			pan_item_set_key(pi_day, "day");

			dx = x + PAN_CAL_DOT_GAP * 2;
			dy = y + PAN_CAL_DOT_GAP * 2;

			fd = static_cast<FileData *>((work) ? work->data : nullptr);
			while (fd && pan_date_compare(fd->date, dt, PAN_DATE_LENGTH_DAY))
				{
				PanItem *pi;

				pi = pan_item_box_new(pw, fd, dx, dy, PAN_CAL_DOT_SIZE, PAN_CAL_DOT_SIZE,
						      0,
						      PAN_CAL_DOT_COLOR,
						      {0, 0, 0, 0});
				pan_item_set_key(pi, "dot");

				dx += PAN_CAL_DOT_SIZE + PAN_CAL_DOT_GAP;
				if (dx + PAN_CAL_DOT_SIZE > pi_day->x + pi_day->width - PAN_CAL_DOT_GAP * 2)
					{
					dx = x + PAN_CAL_DOT_GAP * 2;
					dy += PAN_CAL_DOT_SIZE + PAN_CAL_DOT_GAP;
					}
				if (dy + PAN_CAL_DOT_SIZE > pi_day->y + pi_day->height - PAN_CAL_DOT_GAP * 2)
					{
					/* must keep all dots within respective day even if it gets ugly */
					dy = y + PAN_CAL_DOT_GAP * 2;
					}

				n++;

				work = work->next;
				fd = static_cast<FileData *>((work) ? work->data : nullptr);
				}

			if (n > 0)
				{
				PanItem *pi;

				pi_day->color.r = std::max(pi_day->color.r - 61 - (n * 3), 80);
				pi_day->color.g = pi_day->color.r;

				g_autofree gchar *day_buf = g_strdup_printf("( %d )", n);
				pi = pan_item_text_new(pw, x, y, day_buf, PAN_TEXT_ATTR_NONE,
				                       PAN_BORDER_3, PAN_CAL_DAY_TEXT_COLOR);

				pi->x = pi_day->x + (pi_day->width - pi->width) / 2;
				pi->y = pi_day->y + (pi_day->height - pi->height) / 2;
				}

			pi_day_number = pan_item_text_new(pw, x + 4, y + 4, std::to_string(day).c_str(),
			                                  static_cast<PanTextAttrType>(PAN_TEXT_ATTR_BOLD | PAN_TEXT_ATTR_HEADING),
			                                  PAN_BORDER_3, PAN_CAL_DAY_TEXT_COLOR);

			day_of_week = date_get_first_day_of_week() + col;
			if (day_of_week > 7) day_of_week = day_of_week - 7;

			g_autofree gchar *day_of_week_buf = date_get_abbreviated_day_name(day_of_week);
			pan_item_text_new(pw, x + 4 + pi_day_number->width + 4, y + 4, day_of_week_buf,
			                  PAN_TEXT_ATTR_NONE, PAN_BORDER_3, PAN_CAL_DAY_OF_WEEK_COLOR);

			pan_item_size_coordinates(pi_day, PAN_BOX_BORDER, width, height);

			col++;
			if (col > 6)
				{
				col = 0;
				x = PAN_BOX_BORDER;
				y += PAN_CAL_DAY_HEIGHT;
				}
			else
				{
				x += PAN_CAL_DAY_WIDTH;
				}
			}

		if (col > 0) y += PAN_CAL_DAY_HEIGHT;
		y += PAN_BOX_BORDER * 2;

		month ++;
		if (month > 12)
			{
			year++;
			month = 1;
			}
		}

	width += grid;
	height = std::max(height, grid + (PAN_BOX_BORDER * 2 * 2));

	g_list_free(list);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
