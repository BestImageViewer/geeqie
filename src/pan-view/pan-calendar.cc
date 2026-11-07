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

#include <config.h>

#include <algorithm>
#if !HAVE__NL_TIME_FIRST_WEEKDAY
#  include <clocale>
#endif
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>

#include <gdk/gdk.h>
#include <langinfo.h>

#include "filedata.h"
#include "geometry.h"
#include <sys/stat.h>

#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view.h"

namespace
{

/**
 * @brief Returns integer representing first_day_of_week
 * @returns Integer in range 1 to 7
 *
 * Uses current locale to get first day of week.
 * If _NL_TIME_FIRST_WEEKDAY is not available, ISO 8601
 * states first day of week is Monday.
 * USA, Mexico and Canada (and others) use Sunday as first day of week.
 *
 * Sunday == 1
 */
int date_get_first_day_of_week()
{
#if HAVE__NL_TIME_FIRST_WEEKDAY
	return nl_langinfo(_NL_TIME_FIRST_WEEKDAY)[0];
#else
	gchar *current_locale = setlocale(LC_ALL, nullptr);
	gchar *dot = strstr(current_locale, ".");

	return ((strncmp(dot - 2, "CA", 2) == 0) || (strncmp(dot - 2, "MX", 2) == 0) || (strncmp(dot - 2, "US", 2) == 0)) ? 1 : 2;
#endif
}

/**
 * @brief Get an abbreviated day name from locale
 * @param day Integer in range 1 to 7, representing day of week
 * @returns String containing abbreviated day name
 *
 *  Uses current locale to get day name
 *
 * Sunday == 1
 */
const gchar *date_get_abbreviated_day_name(int day)
{
	switch (day)
		{
		case 1: return nl_langinfo(ABDAY_1);
		case 2: return nl_langinfo(ABDAY_2);
		case 3: return nl_langinfo(ABDAY_3);
		case 4: return nl_langinfo(ABDAY_4);
		case 5: return nl_langinfo(ABDAY_5);
		case 6: return nl_langinfo(ABDAY_6);
		case 7: return nl_langinfo(ABDAY_7);
		default: return nullptr;
		}
}

constexpr gint PAN_CAL_POPUP_BORDER = 1;
constexpr guint8 PAN_CAL_POPUP_ALPHA = 255;
constexpr GqColor PAN_CAL_POPUP_COLOR{220, 220, 220, PAN_CAL_POPUP_ALPHA};
constexpr GqColor PAN_CAL_POPUP_BORDER_COLOR{0, 0, 0, PAN_CAL_POPUP_ALPHA};
constexpr GqColor PAN_CAL_POPUP_TEXT_COLOR{0, 0, 0, 255};

constexpr gint PAN_CAL_DAY_WIDTH = 100;
constexpr gint PAN_CAL_DAY_HEIGHT = 80;
constexpr gint PAN_CAL_DAY_BORDER = 2;
constexpr guint8 PAN_CAL_DAY_ALPHA = 220;
constexpr GqColor PAN_CAL_DAY_COLOR{255, 255, 255, PAN_CAL_DAY_ALPHA};
constexpr GqColor PAN_CAL_DAY_BORDER_COLOR{0, 0, 0, PAN_CAL_DAY_ALPHA};
constexpr GqColor PAN_CAL_DAY_TEXT_COLOR{0, 0, 0, 255};

constexpr gint PAN_CAL_MONTH_BORDER = 4;
constexpr guint8 PAN_CAL_MONTH_ALPHA = 200;
constexpr GqColor PAN_CAL_MONTH_COLOR{255, 255, 255, PAN_CAL_MONTH_ALPHA};
constexpr GqColor PAN_CAL_MONTH_BORDER_COLOR{0, 0, 0, PAN_CAL_MONTH_ALPHA};
constexpr GqColor PAN_CAL_MONTH_TEXT_COLOR{0, 0, 0, 255};

constexpr gint PAN_CAL_DOT_SIZE = 3;
constexpr gint PAN_CAL_DOT_GAP = 2;
constexpr guint8 PAN_CAL_DOT_ALPHA = 128;
constexpr GqColor PAN_CAL_DOT_COLOR{128, 128, 128, PAN_CAL_DOT_ALPHA};

constexpr GqColor PAN_CAL_DAY_OF_WEEK_COLOR{128, 128, 128, 255};

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
	gint x;
	gint y;

	pan_item_remove_by_key(pw, PanKey::DayBubble);

	g_return_if_fail(pi_day && pi_day->is_type(PAN_ITEM_BOX) && pi_day->key == PanKey::Day);

	PanItemList list = pan_layout_intersect(pw, pi_day->x, pi_day->y, pi_day->width, pi_day->height);
	list.remove_if([](const PanItem *dot){ return !dot->is_type(PAN_ITEM_BOX) || !dot->fd || dot->key != PanKey::Dot; });

	const auto grid = static_cast<gint>(sqrt(list.size()) + 0.5);

	x = pi_day->x + pi_day->width + 4;
	y = pi_day->y;

	pbox = pan_item_box_new(pw, nullptr, x, y, PAN_BOX_BORDER, PAN_BOX_BORDER,
	                        PAN_CAL_POPUP_COLOR, PAN_CAL_POPUP_BORDER, PAN_CAL_POPUP_BORDER_COLOR);
	pbox->set_key(PanKey::DayBubble);

	if (pi_day->fd)
		{
		PanItem *plabel;

		g_autofree gchar *buf = pan_date_value_string(pi_day->fd->date, PanDateLength::WEEK);
		plabel = pan_item_text_new(pw, x, y, buf, PAN_TEXT_ATTR_BOLD_HEADING,
		                           PAN_TEXT_BORDER, PAN_CAL_POPUP_TEXT_COLOR);
		plabel->set_key(PanKey::DayBubble);

		pbox->set_size_by_item(plabel, 0);

		y += plabel->height;
		}

	if (!list.empty())
		{
		gint column = 0;

		x += PAN_BOX_BORDER;
		y += PAN_BOX_BORDER;

		for (PanItem *dot : list)
			{
			PanItem *pimg = pan_item_thumb_new(pw, file_data_ref(dot->fd), x, y);
			pimg->set_key(PanKey::DayBubble);

			pbox->set_size_by_item(pimg, PAN_BOX_BORDER);

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

	GqPoint c1{pi_day->x + pi_day->width - 8, pi_day->y + 8};
	GqPoint c2{pbox->x + 1, pbox->y + std::min(42, pbox->height)};
	GqPoint c3{pbox->x + 1, std::max(pbox->y, c2.y - 30)};

	pi = pan_item_tri_new(pw, c1, c2, c3, PAN_CAL_POPUP_COLOR,
	                      PAN_BORDER_1_3, PAN_CAL_POPUP_BORDER_COLOR);
	pi->set_key(PanKey::DayBubble);
	pan_item_added(pw, pi);

	pan_item_box_shadow(pbox, PAN_SHADOW_OFFSET * 2, PAN_SHADOW_FADE * 2);
	pan_item_added(pw, pbox);

	pan_layout_resize(pw);
}

std::function<bool()> pan_calendar_compute(PanWindow *pw, gint &width, gint &height)
{
	struct State
	{
		PanLayoutFiles files;
		GList *counting;
		time_t previous = 0;
		gint count = 0, day_max = 0, grid = 0;
		gint year = 0, month = 0, end_year = 0, end_month = 0;
		gint x = PAN_BOX_BORDER, y = PAN_BOX_BORDER;
		gint day = 1, days = 0, col = 0, dx = 0, dy = 0, n = 0;
		time_t date = 0;
		PanItem *pi_day = nullptr;
		gint phase = 0;
		explicit State(GList *list) : files(list), counting(list) {}
	};
	auto state = std::make_shared<State>(pan_cache_sync_list(pw, pan_list_tree_filtered(pw, SORT_NONE)));
	width = PAN_BOX_BORDER * 2;
	height = PAN_BOX_BORDER * 2;
	return [pw, state, &width, &height]()
		{
		auto &s = *state;
		if (s.phase == 0)
			{
			if (s.counting)
				{
				auto *fd = static_cast<FileData *>(s.counting->data);
				s.counting = s.counting->next;
				if (!pan_date_compare(fd->date, s.previous, PanDateLength::DAY))
					{
					s.count = 0;
					s.previous = fd->date;
					}
				else
					s.day_max = std::max(s.day_max, ++s.count);
				return false;
				}
			s.grid = static_cast<gint>(sqrt(static_cast<gdouble>(s.day_max)) + 0.5) *
					 (pw->thumb_size + PAN_SHADOW_OFFSET * 2 + pw->thumb_gap);
			if (s.files.list)
				{
				auto *first = static_cast<FileData *>(s.files.list->data);
				auto *last = static_cast<FileData *>(g_list_last(s.files.list)->data);
				s.year = pan_date_value(first->date, PanDateLength::YEAR);
				s.month = pan_date_value(first->date, PanDateLength::MONTH);
				s.end_year = pan_date_value(last->date, PanDateLength::YEAR);
				s.end_month = pan_date_value(last->date, PanDateLength::MONTH);
				}
			s.phase = 1;
			}
		if (s.phase == 1)
			{
			if (!s.files.list || s.year > s.end_year || (s.year == s.end_year && s.month > s.end_month))
				{
				width += s.grid;
				height = std::max(height, s.grid + (PAN_BOX_BORDER * 4));
				return true;
				}
			time_t last_day =
				pan_date_to_time(s.month == 12 ? s.year + 1 : s.year, s.month == 12 ? 1 : s.month + 1, 1) - 86400;
			if (!pan_date_compare(static_cast<FileData *>(s.files.list->data)->date, last_day, PanDateLength::MONTH))
				{
				if (++s.month > 12)
					{
					++s.year;
					s.month = 1;
					}
				return false;
				}
			s.days = pan_date_value(last_day, PanDateLength::DAY);
			s.date = pan_date_to_time(s.year, s.month, 1);
			s.col = pan_date_value(s.date, PanDateLength::WEEK) - (date_get_first_day_of_week() - 1);
			if (s.col < 0) s.col += 7;
			auto *month =
				pan_item_box_new(pw, nullptr, PAN_BOX_BORDER, s.y, PAN_CAL_DAY_WIDTH * 7, PAN_CAL_DAY_HEIGHT / 4,
								 PAN_CAL_MONTH_COLOR, PAN_CAL_MONTH_BORDER, PAN_CAL_MONTH_BORDER_COLOR);
			g_autofree gchar *label = pan_date_value_string(s.date, PanDateLength::MONTH);
			auto *text = pan_item_text_new(pw, PAN_BOX_BORDER, s.y, label, PAN_TEXT_ATTR_BOLD_HEADING, PAN_TEXT_BORDER,
										   PAN_CAL_MONTH_TEXT_COLOR);
			text->x = month->x + ((month->width - text->width) / 2);
			month->height = text->y + text->height - month->y;
			s.x = PAN_BOX_BORDER + (s.col * PAN_CAL_DAY_WIDTH);
			s.y = month->y + month->height + PAN_BOX_BORDER;
			s.day = 1;
			s.phase = 2;
			return false;
			}
		if (s.phase == 2)
			{
			s.date = pan_date_to_time(s.year, s.month, s.day);
			g_autofree gchar *path = g_strdup_printf("//%04d-%02d-%02d", s.year, s.month, s.day);
			// These calendar entries are synthetic and never require filesystem access.
			struct stat attributes{};
			auto fd = FileData::new_from_stat(path, attributes);
			fd->date = s.date;
			s.pi_day = pan_item_box_new(pw, fd.release(), s.x, s.y, PAN_CAL_DAY_WIDTH, PAN_CAL_DAY_HEIGHT,
										PAN_CAL_DAY_COLOR, PAN_CAL_DAY_BORDER, PAN_CAL_DAY_BORDER_COLOR);
			s.pi_day->set_key(PanKey::Day);
			s.dx = s.x + (PAN_CAL_DOT_GAP * 2);
			s.dy = s.y + (PAN_CAL_DOT_GAP * 2);
			s.n = 0;
			s.phase = 3;
			return false;
			}
		if (s.files.list &&
			pan_date_compare(static_cast<FileData *>(s.files.list->data)->date, s.date, PanDateLength::DAY))
			{
			auto *dot = pan_item_box_new(pw, s.files.take(), s.dx, s.dy, PAN_CAL_DOT_SIZE, PAN_CAL_DOT_SIZE,
										 PAN_CAL_DOT_COLOR, 0, {0, 0, 0, 0});
			dot->set_key(PanKey::Dot);
			s.dx += PAN_CAL_DOT_SIZE + PAN_CAL_DOT_GAP;
			if (s.dx + PAN_CAL_DOT_SIZE > s.pi_day->x + s.pi_day->width - (PAN_CAL_DOT_GAP * 2))
				{
				s.dx = s.x + (PAN_CAL_DOT_GAP * 2);
				s.dy += PAN_CAL_DOT_SIZE + PAN_CAL_DOT_GAP;
				}
			if (s.dy + PAN_CAL_DOT_SIZE > s.pi_day->y + s.pi_day->height - (PAN_CAL_DOT_GAP * 2))
				s.dy = s.y + (PAN_CAL_DOT_GAP * 2);
			++s.n;
			return false;
			}
		if (s.n)
			{
			s.pi_day->color.r = std::max(s.pi_day->color.r - 61 - (s.n * 3), 80);
			s.pi_day->color.g = s.pi_day->color.r;
			g_autofree gchar *label = g_strdup_printf("( %d )", s.n);
			auto *text =
				pan_item_text_new(pw, s.x, s.y, label, PAN_TEXT_ATTR_NONE, PAN_TEXT_BORDER, PAN_CAL_DAY_TEXT_COLOR);
			text->x = s.pi_day->x + ((s.pi_day->width - text->width) / 2);
			text->y = s.pi_day->y + ((s.pi_day->height - text->height) / 2);
			}
		auto *number = pan_item_text_new(pw, s.x + 4, s.y + 4, std::to_string(s.day).c_str(),
										 PAN_TEXT_ATTR_BOLD_HEADING, PAN_TEXT_BORDER, PAN_CAL_DAY_TEXT_COLOR);
		gint weekday = date_get_first_day_of_week() + s.col;
		if (weekday > 7) weekday -= 7;
		pan_item_text_new(pw, s.x + 4 + number->width + 4, s.y + 4, date_get_abbreviated_day_name(weekday),
						  PAN_TEXT_ATTR_NONE, PAN_TEXT_BORDER, PAN_CAL_DAY_OF_WEEK_COLOR);
		s.pi_day->adjust_size(PAN_BOX_BORDER, width, height);
		if (++s.col > 6)
			{
			s.col = 0;
			s.x = PAN_BOX_BORDER;
			s.y += PAN_CAL_DAY_HEIGHT;
			}
		else
			s.x += PAN_CAL_DAY_WIDTH;
		s.phase = 2;
		if (++s.day > s.days)
			{
			if (s.col > 0) s.y += PAN_CAL_DAY_HEIGHT;
			s.y += PAN_BOX_BORDER * 2;
			if (++s.month > 12)
				{
				++s.year;
				s.month = 1;
				}
			s.phase = 1;
			}
		return false;
		};
}
