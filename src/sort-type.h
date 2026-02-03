/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SORT_TYPE_H
#define SORT_TYPE_H

#include <glib.h>

enum SortType : gint {
	SORT_NONE,
	SORT_NAME,
	SORT_SIZE,
	SORT_TIME,
	SORT_CTIME,
	SORT_PATH,
	SORT_NUMBER,
	SORT_EXIFTIME,
	SORT_EXIFTIMEDIGITIZED,
	SORT_RATING,
	SORT_CLASS
};

const gchar *sort_type_get_text(SortType method);
bool sort_type_requires_metadata(SortType method);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
