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

#ifndef TYPEDEFS_H
#define TYPEDEFS_H

#include <cstdio>

#include <glib.h>

enum SortType {
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

enum FileFormatClass {
	FORMAT_CLASS_UNKNOWN,
	FORMAT_CLASS_IMAGE,
	FORMAT_CLASS_RAWIMAGE,
	FORMAT_CLASS_META,
	FORMAT_CLASS_VIDEO,
	FORMAT_CLASS_COLLECTION,
	FORMAT_CLASS_DOCUMENT,
	FORMAT_CLASS_ARCHIVE,
	FILE_FORMAT_CLASSES
};

extern const gchar *format_class_list[]; /**< defined in preferences.cc */

enum ChangeError {
	CHANGE_OK                      = 0,
	CHANGE_WARN_DEST_EXISTS        = 1 << 0,
	CHANGE_WARN_NO_WRITE_PERM      = 1 << 1,
	CHANGE_WARN_SAME               = 1 << 2,
	CHANGE_WARN_CHANGED_EXT        = 1 << 3,
	CHANGE_WARN_UNSAVED_META       = 1 << 4,
	CHANGE_WARN_NO_WRITE_PERM_DEST_DIR  = 1 << 5,
	CHANGE_ERROR_MASK              = ~0xff, /**< the values below are fatal errors */
	CHANGE_NO_READ_PERM            = 1 << 8,
	CHANGE_NO_WRITE_PERM_DIR       = 1 << 9,
	CHANGE_NO_DEST_DIR             = 1 << 10,
	CHANGE_DUPLICATE_DEST          = 1 << 11,
	CHANGE_NO_WRITE_PERM_DEST      = 1 << 12,
	CHANGE_DEST_EXISTS             = 1 << 13,
	CHANGE_NO_SRC                  = 1 << 14,
	CHANGE_GENERIC_ERROR           = 1 << 16
};

enum MetadataFormat {
	METADATA_PLAIN		= 0, /**< format that can be edited and written back */
	METADATA_FORMATTED	= 1  /**< for display only */
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FILE, fclose)

#define FILEDATA_MARKS_SIZE 10

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
