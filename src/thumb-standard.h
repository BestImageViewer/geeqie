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

#ifndef THUMB_STANDARD_H
#define THUMB_STANDARD_H

struct FileData;
struct ImageLoader;

#if GLIB_CHECK_VERSION (2, 34, 0)
#define THUMB_FOLDER_GLOBAL "thumbnails"
#else
#define THUMB_FOLDER_GLOBAL ".thumbnails"
#endif
#define THUMB_FOLDER_LOCAL  ".thumblocal"
#define THUMB_FOLDER_NORMAL "normal"
#define THUMB_FOLDER_LARGE  "large"
#define THUMB_FOLDER_FAIL   "fail" G_DIR_SEPARATOR_S GQ_APPNAME_LC "-" VERSION
#define THUMB_NAME_EXTENSION ".png"


struct ThumbLoaderStd
{
	gboolean standard_loader;

	ImageLoader *il;
	FileData *fd;

	time_t source_mtime;
	off_t source_size;
	mode_t source_mode;

	gchar *thumb_path;
	gchar *thumb_uri;
	const gchar *local_uri;

	gboolean thumb_path_local;

	gint requested_width;
	gint requested_height;

	gboolean cache_enable;
	gboolean cache_local;
	gboolean cache_hit;
	gboolean cache_retry;

	gdouble progress;

	using Func = void (*)(ThumbLoaderStd *, gpointer);
	Func func_done;
	Func func_error;
	Func func_progress;

	gpointer data;
};


ThumbLoaderStd *thumb_loader_std_new(gint width, gint height);
void thumb_loader_std_set_callbacks(ThumbLoaderStd *tl,
				    ThumbLoaderStd::Func func_done,
				    ThumbLoaderStd::Func func_error,
				    ThumbLoaderStd::Func func_progress,
				    gpointer data);
void thumb_loader_std_set_cache(ThumbLoaderStd *tl, gboolean enable_cache, gboolean local, gboolean retry_failed);
gboolean thumb_loader_std_start(ThumbLoaderStd *tl, FileData *fd);
void thumb_loader_std_free(ThumbLoaderStd *tl);

GdkPixbuf *thumb_loader_std_get_pixbuf(ThumbLoaderStd *tl);

void thumb_loader_std_calibrate_pixbuf(FileData *fd, GdkPixbuf *pixbuf);

/**
 * @headerfile thumb_loader_std_thumb_file_validate
 * validates a non local thumbnail file,
 * calling func_valid with the information when app is idle
 * for thumbnail's without a file: uri, validates against allowed_age in days
 */
ThumbLoaderStd *thumb_loader_std_thumb_file_validate(const gchar *thumb_path, gint allowed_days,
						     void (*func_valid)(const gchar *path, gboolean valid, gpointer data),
						     gpointer data);
void thumb_loader_std_thumb_file_validate_cancel(ThumbLoaderStd *tl);


void thumb_std_maint_removed(const gchar *source);
void thumb_std_maint_moved(const gchar *source, const gchar *dest);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
