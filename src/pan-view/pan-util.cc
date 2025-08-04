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
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "filedata.h"
#include "main-defines.h"
#include "misc.h"
#include "ui-fileops.h"


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

#if defined(__GLIBC_PREREQ)
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
			return g_strdup(text_from_time(d));
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


/*
 *-----------------------------------------------------------------------------
 * folder validation
 *-----------------------------------------------------------------------------
 */

gboolean pan_is_link_loop(const gchar *s)
{
	g_autofree gchar *buf = get_symbolic_link(s);
	if (!buf || *buf == '\0') return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);
	parse_out_relatives(sl);

	parse_out_relatives(buf);
	const gint l = strlen(buf); // @todo Calculate after correction below?

	if (buf[0] != G_DIR_SEPARATOR)
		{
		g_autofree gchar *link_path = g_build_filename(sl, buf, NULL);
		parse_out_relatives(link_path);
		std::swap(buf, link_path);
		}

	return strncmp(sl, buf, l) == 0 &&
	       (sl[l] == '\0' || sl[l] == G_DIR_SEPARATOR || l == 1);
}

gboolean pan_is_ignored(const gchar *s, gboolean ignore_symlinks)
{
	struct stat st;
	const gchar *n;

	if (!lstat_utf8(s, &st)) return TRUE;

#if 0
	/* normal filesystems have directories with some size or block allocation,
	 * special filesystems (like linux /proc) set both to zero.
	 * enable this check if you enable listing the root "/" folder
	 */
	if (st.st_size == 0 && st.st_blocks == 0) return TRUE;
#endif

	if (S_ISLNK(st.st_mode) && (ignore_symlinks || pan_is_link_loop(s))) return TRUE;

	n = filename_from_path(s);
	if (n && strcmp(n, GQ_RC_DIR) == 0) return TRUE;

	return FALSE;
}

GList *pan_list_tree(FileData *dir_fd, FileData::FileList::SortSettings settings, gboolean ignore_symlinks)
{
	GList *flist;
	GList *dlist;
	GList *result;
	GList *folders;

	filelist_read(dir_fd, &flist, &dlist);
	if (settings.method != SORT_NONE)
		{
		flist = filelist_sort(flist, settings);
		dlist = filelist_sort(dlist, settings);
		}

	result = flist;
	folders = dlist;
	while (folders)
		{
		FileData *fd;

		fd = static_cast<FileData *>(folders->data);
		folders = g_list_remove(folders, fd);

		if (!pan_is_ignored(fd->path, ignore_symlinks) &&
		    filelist_read(fd, &flist, &dlist))
			{
			if (settings.method != SORT_NONE)
				{
				flist = filelist_sort(flist, settings);
				dlist = filelist_sort(dlist, settings);
				}

			result = g_list_concat(result, flist);
			folders = g_list_concat(dlist, folders);
			}

		file_data_unref(fd);
		}

	return result;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
