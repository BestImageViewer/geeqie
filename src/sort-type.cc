/*
 * Copyright (C) 2004 John Ellis
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

#include "sort-type.h"

#include "intl.h"

const gchar *sort_type_get_text(SortType method)
{
	switch (method)
		{
		case SORT_SIZE:
			return _("Sort by size");
		case SORT_TIME:
			return _("Sort by date");
		case SORT_CTIME:
			return _("Sort by file creation date");
		case SORT_EXIFTIME:
			return _("Sort by Exif date original");
		case SORT_EXIFTIMEDIGITIZED:
			return _("Sort by Exif date digitized");
		case SORT_NONE:
			return _("Unsorted");
		case SORT_PATH:
			return _("Sort by path");
		case SORT_NUMBER:
			return _("Sort by number");
		case SORT_RATING:
			return _("Sort by rating");
		case SORT_CLASS:
			return _("Sort by class");
		case SORT_NAME:
		default:
			return _("Sort by name");
		}

	return nullptr;
}

bool sort_type_requires_metadata(SortType method)
{
	return method == SORT_EXIFTIME
	    || method == SORT_EXIFTIMEDIGITIZED
	    || method == SORT_RATING;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
