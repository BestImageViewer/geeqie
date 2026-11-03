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

#include "pan-util.h"

// IWYU pragma: no_include <features.h>

#include "date-time.h"


/*
 *-----------------------------------------------------------------------------
 * date functions
 *-----------------------------------------------------------------------------
 */

gboolean pan_date_compare(time_t a, time_t b, PanDateLengthType length)
{
	struct tm ta;
	struct tm tb;

	if (length == PAN_DATE_LENGTH_EXACT) return (a == b);

	if (!localtime_r(&a, &ta) ||
	    !localtime_r(&b, &tb)) return FALSE;

	if (ta.tm_year != tb.tm_year) return FALSE;
	if (length == PAN_DATE_LENGTH_YEAR) return TRUE;

	if (ta.tm_mon != tb.tm_mon) return FALSE;
	if (length == PAN_DATE_LENGTH_MONTH) return TRUE;

	if (length == PAN_DATE_LENGTH_WEEK) return (ta.tm_yday / 7 == tb.tm_yday / 7);

	if (ta.tm_mday != tb.tm_mday) return FALSE;
	if (length == PAN_DATE_LENGTH_DAY) return TRUE;

	return (ta.tm_hour == tb.tm_hour);
}

gint pan_date_value(time_t d, PanDateLengthType length)
{
	struct tm td;

	if (!localtime_r(&d, &td)) return -1;

	switch (length)
		{
		case PAN_DATE_LENGTH_DAY:
			return td.tm_mday;
			break;
		case PAN_DATE_LENGTH_WEEK:
			return td.tm_wday;
			break;
		case PAN_DATE_LENGTH_MONTH:
			return td.tm_mon + 1;
			break;
		case PAN_DATE_LENGTH_YEAR:
			return td.tm_year + 1900;
			break;
		case PAN_DATE_LENGTH_EXACT:
		default:
			break;
		}

	return -1;
}

#ifdef __GLIBC_PREREQ
# if __GLIBC_PREREQ(2, 27)
#  define HAS_GLIBC_STRFTIME_EXTENSIONS
# endif
#endif

gchar *pan_date_value_string(time_t d, PanDateLengthType length)
{
	struct tm td;
	if (!localtime_r(&d, &td)) return g_strdup("");

	const auto format_date = [&td](const gchar *format) -> gchar *
	{
		gchar buf[128];
		if (!strftime(buf, sizeof(buf), format, &td)) return nullptr;

		return g_locale_to_utf8(buf, -1, nullptr, nullptr, nullptr);
	};

	switch (length)
		{
		case PAN_DATE_LENGTH_DAY:
			return g_strdup_printf("%d", td.tm_mday);
			break;
		case PAN_DATE_LENGTH_WEEK:
			{
			gchar *ret = format_date("%A %e");
			if (ret) return ret;
			}
			break;
		case PAN_DATE_LENGTH_MONTH:
			{
#if defined(HAS_GLIBC_STRFTIME_EXTENSIONS) || defined(__FreeBSD__)
			gchar *ret = format_date("%OB %Y");
#else
			gchar *ret = format_date("%B %Y");
#endif
			if (ret) return ret;
			}
			break;
		case PAN_DATE_LENGTH_YEAR:
			return g_strdup_printf("%d", td.tm_year + 1900);
			break;
		case PAN_DATE_LENGTH_EXACT:
		default:
			return date_time_format(d);
			break;
		}

	return g_strdup("");
}

time_t pan_date_to_time(gint year, gint month, gint day)
{
	struct tm lt;

	lt.tm_sec = 0;
	lt.tm_min = 0;
	lt.tm_hour = 0;
	lt.tm_mday = (day >= 1 && day <= 31) ? day : 1;
	lt.tm_mon = (month >= 1 && month <= 12) ? month - 1 : 0;
	lt.tm_year = year - 1900;
	lt.tm_isdst = 0;

	return mktime(&lt);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
