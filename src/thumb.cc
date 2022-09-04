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

#include "main.h"
#include "thumb.h"

#include "cache.h"
#include "image-load.h"
#include "filedata.h"
#include "pixbuf-util.h"
#include "thumb-standard.h"
#include "ui-fileops.h"
#include "exif.h"
#include "metadata.h"

#include <utime.h>


static void thumb_loader_error_cb(ImageLoader *il, gpointer data);
static void thumb_loader_setup(ThumbLoader *tl, FileData *fd);

static GdkPixbuf *get_xv_thumbnail(gchar *thumb_filename, gint max_w, gint max_h);


/*
 *-----------------------------------------------------------------------------
 * thumbnail routines: creation, caching, and maintenance (public)
 *-----------------------------------------------------------------------------
 */

/* Save thumbnail to disk
 * or just mark failed thumbnail with 0 byte file (mark_failure = TRUE) */
static gboolean thumb_loader_save_thumbnail(ThumbLoader *tl, gboolean mark_failure)
{
	gchar *cache_dir;
	gboolean success = FALSE;
	mode_t mode = 0755;

	if (!tl || !tl->fd) return FALSE;
	if (!mark_failure && !tl->fd->thumb_pixbuf) return FALSE;

	cache_dir = cache_get_location(CACHE_TYPE_THUMB, tl->fd->path, FALSE, &mode);

	if (recursive_mkdir_if_not_exists(cache_dir, mode))
		{
		gchar *cache_path;
		gchar *pathl;
		gchar *name = g_strconcat(filename_from_path(tl->fd->path), GQ_CACHE_EXT_THUMB, NULL);

		cache_path = g_build_filename(cache_dir, name, NULL);
		g_free(name);

		pathl = path_from_utf8(cache_path);

		if (mark_failure)
			{
			FILE *f = fopen(pathl, "w"); ;

			DEBUG_1("Marking thumb failure: %s", cache_path);
			if (f)
				{
				fclose(f);
				success = TRUE;
				}
			}
		else
			{
			DEBUG_1("Saving thumb: %s", cache_path);
			success = pixbuf_to_file_as_png(tl->fd->thumb_pixbuf, pathl);
			}

		if (success)
			{
			struct utimbuf ut;
			/* set thumb time to that of source file */

			ut.actime = ut.modtime = filetime(tl->fd->path);
			if (ut.modtime > 0)
				{
				utime(pathl, &ut);
				}
			}
		else
			{
			DEBUG_1("Saving failed: %s", pathl);
			}

		g_free(pathl);
		g_free(cache_path);
		}

	g_free(cache_dir);

	return success;
}

static void thumb_loader_percent_cb(ImageLoader *UNUSED(il), gdouble percent, gpointer data)
{
	ThumbLoader *tl = data;

	tl->percent_done = percent;

	if (tl->func_progress) tl->func_progress(tl, tl->data);
}

static void thumb_loader_set_fallback(ThumbLoader *tl)
{
	if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
	tl->fd->thumb_pixbuf = pixbuf_fallback(tl->fd, tl->max_w, tl->max_h);
}

static void thumb_loader_done_cb(ImageLoader *il, gpointer data)
{
	ThumbLoader *tl = data;
	GdkPixbuf *pixbuf;
	gint pw, ph;
	gint save;
	GdkPixbuf *rotated = NULL;

	DEBUG_1("thumb done: %s", tl->fd->path);

	pixbuf = image_loader_get_pixbuf(tl->il);
	if (!pixbuf)
		{
		DEBUG_1("...but no pixbuf: %s", tl->fd->path);
		thumb_loader_error_cb(tl->il, tl);
		return;
		}

	if(!tl->cache_hit)
		{
			// apply color correction, if required
			thumb_loader_std_calibrate_pixbuf(tl->fd, pixbuf);
		}

	if (!tl->cache_hit && options->image.exif_rotate_enable)
		{
		if (!tl->fd->exif_orientation)
			{
			if (g_strcmp0(il->fd->format_name, "heif") != 0)
				{
				tl->fd->exif_orientation = metadata_read_int(tl->fd, ORIENTATION_KEY, EXIF_ORIENTATION_TOP_LEFT);
				}
			else
				{
				tl->fd->exif_orientation = EXIF_ORIENTATION_TOP_LEFT;
				}
			}

		if (tl->fd->exif_orientation != EXIF_ORIENTATION_TOP_LEFT)
			{
			rotated = pixbuf_apply_orientation(pixbuf, tl->fd->exif_orientation);
			pixbuf = rotated;
			}
		}

	pw = gdk_pixbuf_get_width(pixbuf);
	ph = gdk_pixbuf_get_height(pixbuf);

	if (tl->cache_hit && pw != tl->max_w && ph != tl->max_h)
		{
		/* requested thumbnail size may have changed, load original */
		DEBUG_1("thumbnail size mismatch, regenerating: %s", tl->fd->path);
		tl->cache_hit = FALSE;

		thumb_loader_setup(tl, tl->fd);

		g_signal_connect(G_OBJECT(tl->il), "done", (GCallback)thumb_loader_done_cb, tl);

		if (!image_loader_start(tl->il))
			{
			image_loader_free(tl->il);
			tl->il = NULL;

			DEBUG_1("regeneration failure: %s", tl->fd->path);
			thumb_loader_error_cb(tl->il, tl);
			}
		return;
		}

	/* scale ?? */

	if (pw > tl->max_w || ph > tl->max_h)
		{
		gint w, h;

		if (((gdouble)tl->max_w / pw) < ((gdouble)tl->max_h / ph))
			{
			w = tl->max_w;
			h = (gdouble)w / pw * ph;
			if (h < 1) h = 1;
			}
		else
			{
			h = tl->max_h;
			w = (gdouble)h / ph * pw;
			if (w < 1) w = 1;
			}

		if (tl->fd)
			{
			if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
			tl->fd->thumb_pixbuf = gdk_pixbuf_scale_simple(pixbuf, w, h, (GdkInterpType)options->thumbnails.quality);
			}
		save = TRUE;
		}
	else
		{
		if (tl->fd)
			{
			if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
			tl->fd->thumb_pixbuf = pixbuf;

			g_object_ref(tl->fd->thumb_pixbuf);
			}
		save = image_loader_get_shrunk(il);
		}

	if (rotated) g_object_unref(rotated);

	/* save it ? */
	if (tl->cache_enable && save)
		{
		thumb_loader_save_thumbnail(tl, FALSE);
		}

	if (tl->func_done) tl->func_done(tl, tl->data);
}

static void thumb_loader_error_cb(ImageLoader *il, gpointer data)
{
	ThumbLoader *tl = data;

	/* if at least some of the image is available, go to done_cb */
	if (image_loader_get_pixbuf(tl->il) != NULL)
		{
		thumb_loader_done_cb(il, data);
		return;
		}

	DEBUG_1("thumb error: %s", tl->fd->path);

	image_loader_free(tl->il);
	tl->il = NULL;

	thumb_loader_set_fallback(tl);

	if (tl->func_error) tl->func_error(tl, tl->data);
}

static gboolean thumb_loader_done_delay_cb(gpointer data)
{
	ThumbLoader *tl = data;

	tl->idle_done_id = 0;

	if (tl->func_done) tl->func_done(tl, tl->data);

	return FALSE;
}

static void thumb_loader_delay_done(ThumbLoader *tl)
{
	if (!tl->idle_done_id) tl->idle_done_id = g_idle_add(thumb_loader_done_delay_cb, tl);
}

static void thumb_loader_setup(ThumbLoader *tl, FileData *fd)
{
	image_loader_free(tl->il);
	tl->il = image_loader_new(fd);
	image_loader_set_priority(tl->il, G_PRIORITY_LOW);

	/* this will speed up jpegs by up to 3x in some cases */
	image_loader_set_requested_size(tl->il, tl->max_w, tl->max_h);

	g_signal_connect(G_OBJECT(tl->il), "error", (GCallback)thumb_loader_error_cb, tl);
	if (tl->func_progress) g_signal_connect(G_OBJECT(tl->il), "percent", (GCallback)thumb_loader_percent_cb, tl);
}

void thumb_loader_set_callbacks(ThumbLoader *tl,
				ThumbLoaderFunc func_done,
				ThumbLoaderFunc func_error,
				ThumbLoaderFunc func_progress,
				gpointer data)
{
	if (!tl) return;

	if (tl->standard_loader)
		{
		thumb_loader_std_set_callbacks((ThumbLoaderStd *)tl,
					       (ThumbLoaderStdFunc) func_done,
					       (ThumbLoaderStdFunc) func_error,
					       (ThumbLoaderStdFunc) func_progress,
					       data);
		return;
		}

	tl->func_done = func_done;
	tl->func_error = func_error;
	tl->func_progress = func_progress;

	tl->data = data;
}

void thumb_loader_set_cache(ThumbLoader *tl, gboolean enable_cache, gboolean local, gboolean retry_failed)
{
	if (!tl) return;

	if (tl->standard_loader)
		{
		thumb_loader_std_set_cache((ThumbLoaderStd *)tl, enable_cache, local, retry_failed);
		return;
		}

	tl->cache_enable = enable_cache;
}


gboolean thumb_loader_start(ThumbLoader *tl, FileData *fd)
{
	gchar *cache_path = NULL;

	if (!tl) return FALSE;

	if (tl->standard_loader)
		{
		return thumb_loader_std_start((ThumbLoaderStd *)tl, fd);
		}

	if (!tl->fd && !fd) return FALSE;

	if (!tl->fd) tl->fd = file_data_ref(fd);

	if (tl->fd->format_class != FORMAT_CLASS_IMAGE && tl->fd->format_class != FORMAT_CLASS_RAWIMAGE && tl->fd->format_class != FORMAT_CLASS_COLLECTION && tl->fd->format_class != FORMAT_CLASS_VIDEO && tl->fd->format_class != FORMAT_CLASS_DOCUMENT && !options->file_filter.disable)
		{
		thumb_loader_set_fallback(tl);
		return FALSE;
		}

	if (tl->cache_enable)
		{
		cache_path = cache_find_location(CACHE_TYPE_THUMB, tl->fd->path);

		if (cache_path)
			{
			if (cache_time_valid(cache_path, tl->fd->path))
				{
				DEBUG_1("Found in cache:%s", tl->fd->path);

				if (filesize(cache_path) == 0)
					{
					DEBUG_1("Broken image mark found:%s", cache_path);
					g_free(cache_path);
					thumb_loader_set_fallback(tl);
					return FALSE;
					}

				DEBUG_1("Cache location:%s", cache_path);
				}
			else
				{
				g_free(cache_path);
				cache_path = NULL;
				}
			}
		}

	if (!cache_path && options->thumbnails.use_xvpics)
		{
		if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
		tl->fd->thumb_pixbuf = get_xv_thumbnail(tl->fd->path, tl->max_w, tl->max_h);
		if (tl->fd->thumb_pixbuf)
			{
			thumb_loader_delay_done(tl);
			return TRUE;
			}
		}

	if (cache_path)
		{
		FileData *fd = file_data_new_no_grouping(cache_path);
		thumb_loader_setup(tl, fd);
		file_data_unref(fd);
		g_free(cache_path);
		tl->cache_hit = TRUE;
		}
	else
		{
		thumb_loader_setup(tl, tl->fd);
		}

	g_signal_connect(G_OBJECT(tl->il), "done", (GCallback)thumb_loader_done_cb, tl);
	if (!image_loader_start(tl->il))
		{
		/* try from original if cache attempt */
		if (tl->cache_hit)
			{
			tl->cache_hit = FALSE;
			log_printf("%s", _("Thumbnail image in cache failed to load, trying to recreate.\n"));

			thumb_loader_setup(tl, tl->fd);
			g_signal_connect(G_OBJECT(tl->il), "done", (GCallback)thumb_loader_done_cb, tl);
			if (image_loader_start(tl->il)) return TRUE;
			}
		/* mark failed thumbnail in cache with 0 byte file */
		if (tl->cache_enable)
			{
			thumb_loader_save_thumbnail(tl, TRUE);
			}

		image_loader_free(tl->il);
		tl->il = NULL;
		thumb_loader_set_fallback(tl);
		return FALSE;
		}

	return TRUE;
}

GdkPixbuf *thumb_loader_get_pixbuf(ThumbLoader *tl)
{
	GdkPixbuf *pixbuf;

	if (tl && tl->standard_loader)
		{
		return thumb_loader_std_get_pixbuf((ThumbLoaderStd *)tl);
		}

	if (tl && tl->fd && tl->fd->thumb_pixbuf)
		{
		pixbuf = tl->fd->thumb_pixbuf;
		g_object_ref(pixbuf);
		}
	else
		{
		pixbuf = pixbuf_fallback(NULL, tl->max_w, tl->max_h);
		}

	return pixbuf;
}

ThumbLoader *thumb_loader_new(gint width, gint height)
{
	ThumbLoader *tl;

	/* non-std thumb loader is more effective for configurations with disabled caching
	   because it loads the thumbnails at the required size. loader_std loads
	   the thumbnails at the sizes appropriate for standard cache (typically 256x256 pixels)
	   and then performs one additional scaling */
	if (options->thumbnails.spec_standard && options->thumbnails.enable_caching)
		{
		return (ThumbLoader *)thumb_loader_std_new(width, height);
		}

	tl = g_new0(ThumbLoader, 1);

	tl->cache_enable = options->thumbnails.enable_caching;
	tl->percent_done = 0.0;
	tl->max_w = width;
	tl->max_h = height;

	return tl;
}

void thumb_loader_free(ThumbLoader *tl)
{
	if (!tl) return;

	if (tl->standard_loader)
		{
		thumb_loader_std_free((ThumbLoaderStd *)tl);
		return;
		}

	image_loader_free(tl->il);
	file_data_unref(tl->fd);

	if (tl->idle_done_id) g_source_remove(tl->idle_done_id);

	g_free(tl);
}

/* release thumb_pixbuf on file change - this forces reload. */
void thumb_notify_cb(FileData *fd, NotifyType type, gpointer UNUSED(data))
{
	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE)) && fd->thumb_pixbuf)
		{
		DEBUG_1("Notify thumb: %s %04x", fd->path, type);
		g_object_unref(fd->thumb_pixbuf);
		fd->thumb_pixbuf = NULL;
		}
}


/*
 *-----------------------------------------------------------------------------
 * xvpics thumbnail support, read-only (private)
 *-----------------------------------------------------------------------------
 */

/*
 * xvpics code originally supplied by:
 * "Diederen Damien" <D.Diederen@student.ulg.ac.be>
 *
 * Note: Code has been modified to fit the style of the other code, and to use
 *       a few more glib-isms.
 * 08-28-2000: Updated to return a gdk_pixbuf, Imlib is dying a death here.
 */

#define XV_BUFFER 2048
static guchar *load_xv_thumbnail(gchar *filename, gint *widthp, gint *heightp)
{
	FILE *file;
	gchar buffer[XV_BUFFER];
	guchar *data = NULL;

	file = fopen(filename, "rt");
	if (!file) return NULL;

	if (fgets(buffer, XV_BUFFER, file) != NULL
	    && strncmp(buffer, "P7 332", 6) == 0)
		{
		gint width, height, depth;

		while (fgets(buffer, XV_BUFFER, file) && buffer[0] == '#') /* do_nothing() */;

		if (sscanf(buffer, "%d %d %d", &width, &height, &depth) == 3)
			{
			gsize size = width * height;

			data = g_new(guchar, size);
			if (data && fread(data, 1, size, file) == size)
				{
				*widthp = width;
				*heightp = height;
				}
			}
		}

	fclose(file);
	return data;
}
#undef XV_BUFFER

static void free_rgb_buffer(guchar *pixels, gpointer UNUSED(data))
{
	g_free(pixels);
}

static GdkPixbuf *get_xv_thumbnail(gchar *thumb_filename, gint max_w, gint max_h)
{
	gint width, height;
	gchar *thumb_name;
	gchar *path;
	gchar *directory;
	gchar *name;
	guchar *packed_data;

	path = path_from_utf8(thumb_filename);
	directory = g_path_get_dirname(path);
	name = g_path_get_basename(path);

	thumb_name = g_build_filename(directory, ".xvpics", name, NULL);

	g_free(name);
	g_free(directory);
	g_free(path);

	packed_data = load_xv_thumbnail(thumb_name, &width, &height);
	g_free(thumb_name);

	if (packed_data)
		{
		guchar *rgb_data;
		GdkPixbuf *pixbuf;
		gint i;

		rgb_data = g_new(guchar, width * height * 3);
		for (i = 0; i < width * height; i++)
			{
			rgb_data[i * 3 + 0] = (packed_data[i] >> 5) * 36;
			rgb_data[i * 3 + 1] = ((packed_data[i] & 28) >> 2) * 36;
			rgb_data[i * 3 + 2] = (packed_data[i] & 3) * 85;
			}
		g_free(packed_data);

		pixbuf = gdk_pixbuf_new_from_data(rgb_data, GDK_COLORSPACE_RGB, FALSE, 8,
						  width, height, 3 * width, free_rgb_buffer, NULL);

		if (pixbuf_scale_aspect(width, height, max_w, max_h, &width, &height))
			{
			/* scale */
			GdkPixbuf *tmp;

			tmp = pixbuf;
			pixbuf = gdk_pixbuf_scale_simple(tmp, width, height, GDK_INTERP_NEAREST);
			g_object_unref(tmp);
			}

		return pixbuf;
		}

	return NULL;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
