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

enum ZoomMode {
	ZOOM_RESET_ORIGINAL	= 0,
	ZOOM_RESET_FIT_WINDOW	= 1,
	ZOOM_RESET_NONE		= 2
};

enum ClipboardDestination {
	CLIPBOARD_TEXT_PLAIN	= 0,
	CLIPBOARD_TEXT_URI_LIST	= 1,
	CLIPBOARD_X_SPECIAL_GNOME_COPIED_FILES	= 2,
	CLIPBOARD_UTF8_STRING	= 3
};

enum ClipboardSelection {
	CLIPBOARD_PRIMARY	= 0,
	CLIPBOARD_CLIPBOARD = 1,
	CLIPBOARD_BOTH = 2
};

enum MouseButton {
	MOUSE_BUTTON_LEFT	= 1,
	MOUSE_BUTTON_MIDDLE	= 2,
	MOUSE_BUTTON_RIGHT	= 3,
	MOUSE_BUTTON_WHEEL_UP	= 4,
	MOUSE_BUTTON_WHEEL_DOWN	= 5,
	MOUSE_BUTTON_8	= 8,
	MOUSE_BUTTON_9	= 9
};

enum DirViewType {
	DIRVIEW_LIST,
	DIRVIEW_TREE,

	DIRVIEW_LAST = DIRVIEW_TREE /**< Keep this up to date! */
};

enum FileViewType {
	FILEVIEW_LIST,
	FILEVIEW_ICON,

	FILEVIEW_LAST = FILEVIEW_ICON /**< Keep this up to date! */
};

#define	CMD_COPY     "geeqie-copy-command.desktop"
#define	CMD_MOVE     "geeqie-move-command.desktop"
#define	CMD_RENAME   "geeqie-rename-command.desktop"
#define	CMD_DELETE   "geeqie-delete-command.desktop"
#define	CMD_FOLDER   "geeqie-folder-command.desktop"

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

enum AlterType {
	ALTER_NONE,		/**< do nothing */
	ALTER_ROTATE_90,
	ALTER_ROTATE_90_CC,	/**< counterclockwise */
	ALTER_ROTATE_180,
	ALTER_MIRROR,
	ALTER_FLIP,
};


enum ImageSplitMode {
	SPLIT_NONE = 0,
	SPLIT_VERT,
	SPLIT_HOR,
	SPLIT_TRIPLE,
	SPLIT_QUAD,
};

enum MarkToSelectionMode {
	MTS_MODE_MINUS,
	MTS_MODE_SET,
	MTS_MODE_OR,
	MTS_MODE_AND
};

enum SelectionToMarkMode {
	STM_MODE_RESET,
	STM_MODE_SET,
	STM_MODE_TOGGLE
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

enum NotifyType {
	NOTIFY_MARKS		= 1 << 1, /**< changed marks */
	NOTIFY_PIXBUF		= 1 << 2, /**< image was read into fd->pixbuf */
	NOTIFY_HISTMAP		= 1 << 3, /**< histmap was read into fd->histmap */
	NOTIFY_ORIENTATION	= 1 << 4, /**< image was rotated */
	NOTIFY_METADATA		= 1 << 5, /**< changed image metadata, not yet written */
	NOTIFY_GROUPING		= 1 << 6, /**< change in fd->sidecar_files or fd->parent */
	NOTIFY_REREAD		= 1 << 7, /**< changed file size, date, etc., file name remains unchanged */
	NOTIFY_CHANGE		= 1 << 8  /**< generic change described by fd->change */
};

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

enum ToolbarType {
	TOOLBAR_MAIN,
	TOOLBAR_STATUS,
	TOOLBAR_COUNT
};

enum PixbufRendererStereoMode {
	PR_STEREO_NONE             = 0,	  /**< do nothing */
	PR_STEREO_DUAL             = 1 << 0, /**< independent stereo buffers, for example nvidia opengl */
	PR_STEREO_FIXED            = 1 << 1,  /**< custom position */
	PR_STEREO_HORIZ            = 1 << 2,  /**< side by side */
	PR_STEREO_VERT             = 1 << 3,  /**< above below */
	PR_STEREO_RIGHT            = 1 << 4,  /**< render right buffer */
	PR_STEREO_ANAGLYPH_RC      = 1 << 5,  /**< anaglyph red-cyan */
	PR_STEREO_ANAGLYPH_GM      = 1 << 6,  /**< anaglyph green-magenta */
	PR_STEREO_ANAGLYPH_YB      = 1 << 7,  /**< anaglyph yellow-blue */
	PR_STEREO_ANAGLYPH_GRAY_RC = 1 << 8,  /**< anaglyph gray red-cyan*/
	PR_STEREO_ANAGLYPH_GRAY_GM = 1 << 9,  /**< anaglyph gray green-magenta */
	PR_STEREO_ANAGLYPH_GRAY_YB = 1 << 10, /**< anaglyph gray yellow-blue */
	PR_STEREO_ANAGLYPH_DB_RC   = 1 << 11, /**< anaglyph dubois red-cyan */
	PR_STEREO_ANAGLYPH_DB_GM   = 1 << 12, /**< anaglyph dubois green-magenta */
	PR_STEREO_ANAGLYPH_DB_YB   = 1 << 13, /**< anaglyph dubois yellow-blue */
	PR_STEREO_ANAGLYPH         = PR_STEREO_ANAGLYPH_RC |
	                             PR_STEREO_ANAGLYPH_GM |
	                             PR_STEREO_ANAGLYPH_YB |
	                             PR_STEREO_ANAGLYPH_GRAY_RC |
	                             PR_STEREO_ANAGLYPH_GRAY_GM |
	                             PR_STEREO_ANAGLYPH_GRAY_YB |
	                             PR_STEREO_ANAGLYPH_DB_RC |
	                             PR_STEREO_ANAGLYPH_DB_GM |
	                             PR_STEREO_ANAGLYPH_DB_YB, /**< anaglyph mask */

	PR_STEREO_MIRROR_LEFT      = 1 << 14, /**< mirror */
	PR_STEREO_FLIP_LEFT        = 1 << 15, /**< flip */

	PR_STEREO_MIRROR_RIGHT     = 1 << 16, /**< mirror */
	PR_STEREO_FLIP_RIGHT       = 1 << 17, /**< flip */

	PR_STEREO_MIRROR           = PR_STEREO_MIRROR_LEFT | PR_STEREO_MIRROR_RIGHT, /**< mirror mask*/
	PR_STEREO_FLIP             = PR_STEREO_FLIP_LEFT | PR_STEREO_FLIP_RIGHT, /**< flip mask*/
	PR_STEREO_SWAP             = 1 << 18,  /**< swap left and right buffers */
	PR_STEREO_TEMP_DISABLE     = 1 << 19,  /**< temporarily disable stereo mode if source image is not stereo */
	PR_STEREO_HALF             = 1 << 20
};

enum StereoPixbufData {
	STEREO_PIXBUF_DEFAULT  = 0,
	STEREO_PIXBUF_SBS      = 1,
	STEREO_PIXBUF_CROSS    = 2,
	STEREO_PIXBUF_NONE     = 3
};

using FileUtilDoneFunc = void (*)(gboolean, const gchar *, gpointer);

#define FILEDATA_MARKS_SIZE 10

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
