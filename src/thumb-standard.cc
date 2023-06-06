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

#include "main.h"
#include "thumb-standard.h"

#include "cache.h"
#include "image-load.h"
#include "md5-util.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "filedata.h"
#include "exif.h"
#include "metadata.h"
#include "color-man.h"


/**
 * @file
 *
 * This thumbnail caching implementation attempts to conform
 * to the Thumbnail Managing Standard proposed on freedesktop.org
 * The standard is documented here: \n
 *   https://www.freedesktop.org/wiki/Specifications/thumbnails/ \n
 *
 * This code attempts to conform to version 0.7.0 of the standard.
 *
 * Notes:
 *   > Validation of the thumb's embedded uri is a simple strcmp between our
 *   > version of the escaped uri and the thumb's escaped uri. But not all uri
 *   > escape functions escape the same set of chars, comparing the unescaped
 *   > versions may be more accurate. \n
 *   > Only Thumb::URI and Thumb::MTime are stored in a thumb at this time.
 *     Storing the Size, Width, Height should probably be implemented.
 */


#define THUMB_SIZE_NORMAL 128
#define THUMB_SIZE_LARGE  256

#define THUMB_MARKER_URI    "tEXt::Thumb::URI"
#define THUMB_MARKER_MTIME  "tEXt::Thumb::MTime"
#define THUMB_MARKER_SIZE   "tEXt::Thumb::Size"
#define THUMB_MARKER_WIDTH  "tEXt::Thumb::Image::Width"
#define THUMB_MARKER_HEIGHT "tEXt::Thumb::Image::Height"
#define THUMB_MARKER_APP    "tEXt::Software"

#define THUMB_PERMS_FOLDER 0700
#define THUMB_PERMS_THUMB  0600



/*
 *-----------------------------------------------------------------------------
 * thumbnail loader
 *-----------------------------------------------------------------------------
 */


static void thumb_loader_std_error_cb(ImageLoader *il, gpointer data);
static gint thumb_loader_std_setup(ThumbLoaderStd *tl, FileData *fd);


ThumbLoaderStd *thumb_loader_std_new(gint width, gint height)
{
	ThumbLoaderStd *tl;

	tl = g_new0(ThumbLoaderStd, 1);

	tl->standard_loader = TRUE;
	tl->requested_width = width;
	tl->requested_height = height;
	tl->cache_enable = options->thumbnails.enable_caching;

	return tl;
}

void thumb_loader_std_set_callbacks(ThumbLoaderStd *tl,
				    ThumbLoaderStdFunc func_done,
				    ThumbLoaderStdFunc func_error,
				    ThumbLoaderStdFunc func_progress,
				    gpointer data)
{
	if (!tl) return;

	tl->func_done = func_done;
	tl->func_error = func_error;
	tl->func_progress = func_progress;
	tl->data = data;
}

static void thumb_loader_std_reset(ThumbLoaderStd *tl)
{
	image_loader_free(tl->il);
	tl->il = nullptr;

	file_data_unref(tl->fd);
	tl->fd = nullptr;

	g_free(tl->thumb_path);
	tl->thumb_path = nullptr;

	g_free(tl->thumb_uri);
	tl->thumb_uri = nullptr;
	tl->local_uri = nullptr;

	tl->thumb_path_local = FALSE;

	tl->cache_hit = FALSE;

	tl->source_mtime = 0;
	tl->source_size = 0;
	tl->source_mode = 0;

	tl->progress = 0.0;
}

static gchar *thumb_std_cache_path(const gchar *path, const gchar *uri, gboolean local,
				   const gchar *cache_subfolder)
{
	gchar *result = nullptr;
	gchar *md5_text;
	guchar digest[16];
	gchar *name;

	if (!path || !uri || !cache_subfolder) return nullptr;

	md5_get_digest(reinterpret_cast<const guchar *>(uri), strlen(uri), digest);
	md5_text = md5_digest_to_text(digest);

	if (!md5_text) return nullptr;

	name = g_strconcat(md5_text, THUMB_NAME_EXTENSION, NULL);

	if (local)
		{
		gchar *base = remove_level_from_path(path);

		result = g_build_filename(base, THUMB_FOLDER_LOCAL, cache_subfolder, name, NULL);
		g_free(base);
		}
	else
		{
		result = g_build_filename(get_thumbnails_standard_cache_dir(),
													cache_subfolder, name, NULL);
		}

	g_free(name);
	g_free(md5_text);

	return result;
}

static gchar *thumb_loader_std_cache_path(ThumbLoaderStd *tl, gboolean local, GdkPixbuf *pixbuf, gboolean fail)
{
	const gchar *folder;
	gint w, h;

	if (!tl->fd || !tl->thumb_uri) return nullptr;

	if (pixbuf)
		{
		w = gdk_pixbuf_get_width(pixbuf);
		h = gdk_pixbuf_get_height(pixbuf);
		}
	else
		{
		w = tl->requested_width;
		h = tl->requested_height;
		}

	if (fail)
		{
		folder = THUMB_FOLDER_FAIL;
		}
	else if (w > THUMB_SIZE_NORMAL || h > THUMB_SIZE_NORMAL)
		{
		folder = THUMB_FOLDER_LARGE;
		}
	else
		{
		folder = THUMB_FOLDER_NORMAL;
		}

	return thumb_std_cache_path(tl->fd->path,
				    (local) ?  tl->local_uri : tl->thumb_uri,
				    local, folder);
}

static gboolean thumb_loader_std_fail_check(ThumbLoaderStd *tl)
{
	gchar *fail_path;
	gboolean result = FALSE;

	fail_path = thumb_loader_std_cache_path(tl, FALSE, nullptr, TRUE);
	if (isfile(fail_path))
		{
		GdkPixbuf *pixbuf;

		if (tl->cache_retry)
			{
			pixbuf = nullptr;
			}
		else
			{
			gchar *pathl;

			pathl = path_from_utf8(fail_path);
			pixbuf = gdk_pixbuf_new_from_file(pathl, nullptr);
			g_free(pathl);
			}

		if (pixbuf)
			{
			const gchar *mtime_str;

			mtime_str = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME);
			if (mtime_str && strtol(mtime_str, nullptr, 10) == tl->source_mtime)
				{
				result = TRUE;
				DEBUG_1("thumb fail valid: %s", tl->fd->path);
				DEBUG_1("           thumb: %s", fail_path);
				}

			g_object_unref(G_OBJECT(pixbuf));
			}

		if (!result) unlink_file(fail_path);
		}
	g_free(fail_path);

	return result;
}

static gboolean thumb_loader_std_validate(ThumbLoaderStd *tl, GdkPixbuf *pixbuf)
{
	const gchar *valid_uri;
	const gchar *uri;
	const gchar *mtime_str;
	time_t mtime;
	gint w, h;

	if (!pixbuf) return FALSE;

	w = gdk_pixbuf_get_width(pixbuf);
	h = gdk_pixbuf_get_height(pixbuf);

	if (w != THUMB_SIZE_NORMAL && w != THUMB_SIZE_LARGE &&
	    h != THUMB_SIZE_NORMAL && h != THUMB_SIZE_LARGE) return FALSE;

	valid_uri = (tl->thumb_path_local) ? tl->local_uri : tl->thumb_uri;

	uri = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_URI);
	mtime_str = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME);

	if (!mtime_str || !uri || !valid_uri) return FALSE;
	if (strcmp(uri, valid_uri) != 0) return FALSE;

	mtime = strtol(mtime_str, nullptr, 10);
	if (tl->source_mtime != mtime) return FALSE;

	return TRUE;
}

static void thumb_loader_std_save(ThumbLoaderStd *tl, GdkPixbuf *pixbuf)
{
	gchar *base_path;
	gchar *tmp_path;
	gboolean fail;

	if (!tl->cache_enable || tl->cache_hit) return;
	if (tl->thumb_path) return;

	if (!pixbuf)
		{
		/* local failures are not stored */
		if (tl->cache_local) return;

		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
		fail = TRUE;
		}
	else
		{
		g_object_ref(G_OBJECT(pixbuf));
		fail = FALSE;
		}

	tl->thumb_path = thumb_loader_std_cache_path(tl, tl->cache_local, pixbuf, fail);
	if (!tl->thumb_path)
		{
		g_object_unref(G_OBJECT(pixbuf));
		return;
		}
	tl->thumb_path_local = tl->cache_local;

	/* create thumbnail dir if needed */
	base_path = remove_level_from_path(tl->thumb_path);
	if (tl->cache_local)
		{
		if (!isdir(base_path))
			{
			struct stat st;
			gchar *source_base;

			source_base = remove_level_from_path(tl->fd->path);
			if (stat_utf8(source_base, &st))
				{
				recursive_mkdir_if_not_exists(base_path, st.st_mode);
				}
			g_free(source_base);
			}
		}
	else
		{
		recursive_mkdir_if_not_exists(base_path, THUMB_PERMS_FOLDER);
		}
	g_free(base_path);

	DEBUG_1("thumb saving: %s", tl->fd->path);
	DEBUG_1("       saved: %s", tl->thumb_path);

	/* save thumb, using a temp file then renaming into place */
	tmp_path = unique_filename(tl->thumb_path, ".tmp", "_", 2);
	if (tmp_path)
		{
		const gchar *mark_uri;
		gchar *mark_app;
		gchar *mark_mtime;
		gchar *pathl;
		gboolean success;

		mark_uri = (tl->cache_local) ? tl->local_uri :tl->thumb_uri;

		mark_app = g_strdup_printf("%s %s", GQ_APPNAME, VERSION);
		mark_mtime = g_strdup_printf("%llu", static_cast<unsigned long long>(tl->source_mtime));
		pathl = path_from_utf8(tmp_path);
		success = gdk_pixbuf_save(pixbuf, pathl, "png", nullptr,
					  THUMB_MARKER_URI, mark_uri,
					  THUMB_MARKER_MTIME, mark_mtime,
					  THUMB_MARKER_APP, mark_app,
					  NULL);
		if (success)
			{
			chmod(pathl, (tl->cache_local) ? tl->source_mode : THUMB_PERMS_THUMB);
			success = rename_file(tmp_path, tl->thumb_path);
			}

		g_free(pathl);

		g_free(mark_mtime);
		g_free(mark_app);

		g_free(tmp_path);
		if (!success)
			{
			DEBUG_1("thumb save failed: %s", tl->fd->path);
			DEBUG_1("            thumb: %s", tl->thumb_path);
			}

		}

	g_object_unref(G_OBJECT(pixbuf));
}

static void thumb_loader_std_set_fallback(ThumbLoaderStd *tl)
{
	if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
	tl->fd->thumb_pixbuf = pixbuf_fallback(tl->fd, tl->requested_width, tl->requested_height);
}


void thumb_loader_std_calibrate_pixbuf(FileData *fd, GdkPixbuf *pixbuf) {
	ColorMan *cm = nullptr;
	ExifData *exif = nullptr;
	gint color_profile_from_image = COLOR_PROFILE_NONE;
	ColorManProfileType input_type = COLOR_PROFILE_MEM;
	ColorManProfileType screen_type;
	const gchar *input_file = nullptr;
	guchar *profile = nullptr;
	guint profile_len;
	gint sw, sh;

	if (!options->thumbnails.use_color_management)
		{
		return;
		}

	sw = gdk_pixbuf_get_width(pixbuf);
	sh = gdk_pixbuf_get_height(pixbuf);

	exif = exif_read_fd(fd);

	if (exif)
		{
		if (g_strcmp0(fd->format_name, "heif") == 0)
			{
			profile = heif_color_profile(fd, &profile_len);
			}

		if (!profile)
			{
			profile = exif_get_color_profile(exif, &profile_len);
			}

		if (profile)
			{
			DEBUG_1("Found embedded color profile");
			color_profile_from_image = COLOR_PROFILE_MEM;
			}
		else
			{
			gchar *interop_index = exif_get_data_as_text(exif, "Exif.Iop.InteroperabilityIndex");

			if (interop_index)
				{
				/* Exif 2.21 specification */
				if (!strcmp(interop_index, "R98"))
					{
					color_profile_from_image = COLOR_PROFILE_SRGB;
					DEBUG_1("Found EXIF 2.21 ColorSpace of sRGB");
					}
				else if (!strcmp(interop_index, "R03"))
					{
					color_profile_from_image = COLOR_PROFILE_ADOBERGB;
					DEBUG_1("Found EXIF 2.21 ColorSpace of AdobeRGB");
					}
				g_free(interop_index);
				}
			else
				{
				gint cs;

				/* ColorSpace == 1 specifies sRGB per EXIF 2.2 */
				if (!exif_get_integer(exif, "Exif.Photo.ColorSpace", &cs)) cs = 0;
				if (cs == 1)
					{
					color_profile_from_image = COLOR_PROFILE_SRGB;
					DEBUG_1("Found EXIF 2.2 ColorSpace of sRGB");
					}
				else if (cs == 2)
					{
					/* non-standard way of specifying AdobeRGB (used by some software) */
					color_profile_from_image = COLOR_PROFILE_ADOBERGB;
					DEBUG_1("Found EXIF 2.2 ColorSpace of AdobeRGB");
					}
				}
			}

		if(color_profile_from_image != COLOR_PROFILE_NONE)
			{
				// transform image, we always use sRGB as target for thumbnails
				screen_type = COLOR_PROFILE_SRGB;

				if (profile)
					{
					cm = color_man_new_embedded(nullptr, pixbuf,
									profile, profile_len,
									screen_type, nullptr, nullptr, 0);
					g_free(profile);
					}
				else
					{
					cm = color_man_new(nullptr, pixbuf,
							input_type, input_file,
							screen_type, nullptr, nullptr, 0);
					}

				if(cm) {
					color_man_correct_region(cm, cm->pixbuf, 0, 0, sw, sh);
					g_free(cm);
				}

			}
		exif_free_fd(fd, exif);
		}
}

static GdkPixbuf *thumb_loader_std_finish(ThumbLoaderStd *tl, GdkPixbuf *pixbuf, gboolean shrunk)
{
	GdkPixbuf *pixbuf_thumb = nullptr;
	GdkPixbuf *result;
	GdkPixbuf *rotated = nullptr;
	gint sw, sh;


	if (!tl->cache_hit && options->image.exif_rotate_enable)
		{
		if (!tl->fd->exif_orientation)
			{
			if (g_strcmp0(tl->fd->format_name, "heif") != 0)
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

	sw = gdk_pixbuf_get_width(pixbuf);
	sh = gdk_pixbuf_get_height(pixbuf);

	if (tl->cache_enable)
		{
		if (!tl->cache_hit)
			{
			gint cache_w, cache_h;

			if (tl->requested_width > THUMB_SIZE_NORMAL || tl->requested_height > THUMB_SIZE_NORMAL)
				{
				cache_w = cache_h = THUMB_SIZE_LARGE;
				}
			else
				{
				cache_w = cache_h = THUMB_SIZE_NORMAL;
				}

			if (sw > cache_w || sh > cache_h || shrunk)
				{
				gint thumb_w, thumb_h;
				struct stat st;

				if (pixbuf_scale_aspect(cache_w, cache_h, sw, sh,
								  &thumb_w, &thumb_h))
					{
					pixbuf_thumb = gdk_pixbuf_scale_simple(pixbuf, thumb_w, thumb_h,
									       static_cast<GdkInterpType>(options->thumbnails.quality));
					}
				else
					{
					pixbuf_thumb = pixbuf;
					g_object_ref(G_OBJECT(pixbuf_thumb));
					}

				/* do not save the thumbnail if the source file has changed meanwhile -
				   the thumbnail is most probably broken */
				if (stat_utf8(tl->fd->path, &st) &&
				    tl->source_mtime == st.st_mtime &&
				    tl->source_size == st.st_size)
					{
					thumb_loader_std_save(tl, pixbuf_thumb);
					}
				}
			}
		else if (tl->cache_hit &&
			 tl->cache_local && !tl->thumb_path_local)
			{
			/* A local cache save was requested, but a valid thumb is in $HOME,
			 * so specifically save as a local thumbnail.
			 */
			g_free(tl->thumb_path);
			tl->thumb_path = nullptr;

			tl->cache_hit = FALSE;

			DEBUG_1("thumb copied: %s", tl->fd->path);

			thumb_loader_std_save(tl, pixbuf);
			}
		}

	if (sw <= tl->requested_width && sh <= tl->requested_height)
		{
		result = pixbuf;
		g_object_ref(result);
		}
	else
		{
		gint thumb_w, thumb_h;

		if (pixbuf_thumb)
			{
			pixbuf = pixbuf_thumb;
			sw = gdk_pixbuf_get_width(pixbuf);
			sh = gdk_pixbuf_get_height(pixbuf);
			}

		if (pixbuf_scale_aspect(tl->requested_width, tl->requested_height, sw, sh,
						  &thumb_w, &thumb_h))
			{
			result = gdk_pixbuf_scale_simple(pixbuf, thumb_w, thumb_h,
							 static_cast<GdkInterpType>(options->thumbnails.quality));
			}
		else
			{
			result = pixbuf;
			g_object_ref(result);
			}
		}

	// apply color correction, if required
	thumb_loader_std_calibrate_pixbuf(tl->fd, result);

	if (pixbuf_thumb) g_object_unref(pixbuf_thumb);
	if (rotated) g_object_unref(rotated);

	return result;
}

static gboolean thumb_loader_std_next_source(ThumbLoaderStd *tl, gboolean remove_broken)
{
	image_loader_free(tl->il);
	tl->il = nullptr;

	if (tl->thumb_path)
		{
		if (!tl->thumb_path_local && remove_broken)
			{
			DEBUG_1("thumb broken, unlinking: %s", tl->thumb_path);
			unlink_file(tl->thumb_path);
			}

		g_free(tl->thumb_path);
		tl->thumb_path = nullptr;

		if (!tl->thumb_path_local)
			{
			tl->thumb_path = thumb_loader_std_cache_path(tl, TRUE, nullptr, FALSE);
			if (isfile(tl->thumb_path))
				{
				FileData *fd = file_data_new_no_grouping(tl->thumb_path);
				if (thumb_loader_std_setup(tl, fd))
					{
					file_data_unref(fd);
					tl->thumb_path_local = TRUE;
					return TRUE;
					}
				file_data_unref(fd);
				}

			g_free(tl->thumb_path);
			tl->thumb_path = nullptr;
			}

		if (thumb_loader_std_setup(tl, tl->fd)) return TRUE;
		}

	thumb_loader_std_save(tl, nullptr);
	return FALSE;
}

static void thumb_loader_std_done_cb(ImageLoader *il, gpointer data)
{
	auto tl = static_cast<ThumbLoaderStd *>(data);
	GdkPixbuf *pixbuf;

	DEBUG_1("thumb image done: %s", tl->fd ? tl->fd->path : "???");
	DEBUG_1("            from: %s", image_loader_get_fd(tl->il)->path);

	pixbuf = image_loader_get_pixbuf(tl->il);
	if (!pixbuf)
		{
		DEBUG_1("...but no pixbuf");
		thumb_loader_std_error_cb(il, data);
		return;
		}

	if (tl->thumb_path && !thumb_loader_std_validate(tl, pixbuf))
		{
		if (thumb_loader_std_next_source(tl, TRUE)) return;

		if (tl->func_error) tl->func_error(tl, tl->data);
		return;
		}

	tl->cache_hit = (tl->thumb_path != nullptr);

	if (tl->fd)
		{
		if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
		tl->fd->thumb_pixbuf = thumb_loader_std_finish(tl, pixbuf, image_loader_get_shrunk(il));
		}

	if (tl->func_done) tl->func_done(tl, tl->data);
}

static void thumb_loader_std_error_cb(ImageLoader *il, gpointer data)
{
	auto tl = static_cast<ThumbLoaderStd *>(data);

	/* if at least some of the image is available, go to done */
	if (image_loader_get_pixbuf(tl->il) != nullptr)
		{
		thumb_loader_std_done_cb(il, data);
		return;
		}

	DEBUG_1("thumb image error: %s", tl->fd->path);
	DEBUG_1("             from: %s", image_loader_get_fd(tl->il)->path);

	if (thumb_loader_std_next_source(tl, TRUE)) return;

	thumb_loader_std_set_fallback(tl);

	if (tl->func_error) tl->func_error(tl, tl->data);
}

static void thumb_loader_std_progress_cb(ImageLoader *UNUSED(il), gdouble percent, gpointer data)
{
	auto tl = static_cast<ThumbLoaderStd *>(data);

	tl->progress = percent;

	if (tl->func_progress) tl->func_progress(tl, tl->data);
}

static gboolean thumb_loader_std_setup(ThumbLoaderStd *tl, FileData *fd)
{
	tl->il = image_loader_new(fd);
	image_loader_set_priority(tl->il, G_PRIORITY_LOW);

	/* this will speed up jpegs by up to 3x in some cases */
	if (tl->requested_width <= THUMB_SIZE_NORMAL &&
	    tl->requested_height <= THUMB_SIZE_NORMAL)
		{
		image_loader_set_requested_size(tl->il, THUMB_SIZE_NORMAL, THUMB_SIZE_NORMAL);
		}
	else
		{
		image_loader_set_requested_size(tl->il, THUMB_SIZE_LARGE, THUMB_SIZE_LARGE);
		}

	g_signal_connect(G_OBJECT(tl->il), "error", (GCallback)thumb_loader_std_error_cb, tl);
	if (tl->func_progress)
		{
		g_signal_connect(G_OBJECT(tl->il), "percent", (GCallback)thumb_loader_std_progress_cb, tl);
		}
	g_signal_connect(G_OBJECT(tl->il), "done", (GCallback)thumb_loader_std_done_cb, tl);

	if (image_loader_start(tl->il))
		{
		return TRUE;
		}

	image_loader_free(tl->il);
	tl->il = nullptr;
	return FALSE;
}

/*
 * Note: Currently local_cache only specifies where to save a _new_ thumb, if
 *       a valid existing thumb is found anywhere the local thumb will not be created.
 */
void thumb_loader_std_set_cache(ThumbLoaderStd *tl, gboolean enable_cache, gboolean local, gboolean retry_failed)
{
	if (!tl) return;

	tl->cache_enable = enable_cache;
	tl->cache_local = local;
	tl->cache_retry = retry_failed;
}

gboolean thumb_loader_std_start(ThumbLoaderStd *tl, FileData *fd)
{
	static gchar *thumb_cache = nullptr;
	struct stat st;

	if (!tl || !fd) return FALSE;

	thumb_loader_std_reset(tl);


	tl->fd = file_data_ref(fd);
	if (!stat_utf8(fd->path, &st) || (tl->fd->format_class != FORMAT_CLASS_IMAGE && tl->fd->format_class != FORMAT_CLASS_RAWIMAGE && tl->fd->format_class != FORMAT_CLASS_VIDEO && tl->fd->format_class != FORMAT_CLASS_COLLECTION && tl->fd->format_class != FORMAT_CLASS_DOCUMENT && !options->file_filter.disable))
		{
		thumb_loader_std_set_fallback(tl);
		return FALSE;
		}
	tl->source_mtime = st.st_mtime;
	tl->source_size = st.st_size;
	tl->source_mode = st.st_mode;

	if (!thumb_cache)
		{
		thumb_cache = g_strdup(get_thumbnails_standard_cache_dir());
		}

	if (strncmp(tl->fd->path, thumb_cache, strlen(thumb_cache)) != 0)
		{
		gchar *pathl;

		pathl = path_from_utf8(fd->path);
		tl->thumb_uri = g_filename_to_uri(pathl, nullptr, nullptr);
		tl->local_uri = filename_from_path(tl->thumb_uri);
		g_free(pathl);
		}

	if (tl->cache_enable)
		{
		gint found;

		tl->thumb_path = thumb_loader_std_cache_path(tl, FALSE, nullptr, FALSE);
		tl->thumb_path_local = FALSE;

		found = isfile(tl->thumb_path);
		if (found)
			{
			FileData *fd = file_data_new_no_grouping(tl->thumb_path);
			if (thumb_loader_std_setup(tl, fd))
				{
				file_data_unref(fd);
				return TRUE;
				}
			file_data_unref(fd);
			}

		if (thumb_loader_std_fail_check(tl) ||
		    !thumb_loader_std_next_source(tl, found))
			{
			thumb_loader_std_set_fallback(tl);
			return FALSE;
			}
		return TRUE;
		}

	if (!thumb_loader_std_setup(tl, tl->fd))
		{
		thumb_loader_std_save(tl, nullptr);
		thumb_loader_std_set_fallback(tl);
		return FALSE;
		}

	return TRUE;
}

void thumb_loader_std_free(ThumbLoaderStd *tl)
{
	if (!tl) return;

	thumb_loader_std_reset(tl);
	g_free(tl);
}

GdkPixbuf *thumb_loader_std_get_pixbuf(ThumbLoaderStd *tl)
{
	GdkPixbuf *pixbuf;

	if (tl && tl->fd && tl->fd->thumb_pixbuf)
		{
		pixbuf = tl->fd->thumb_pixbuf;
		g_object_ref(pixbuf);
		}
	else
		{
		pixbuf = pixbuf_fallback(nullptr, tl->requested_width, tl->requested_height);
		}

	return pixbuf;
}


typedef struct _ThumbValidate ThumbValidate;
struct _ThumbValidate
{
	ThumbLoaderStd *tl;
	gchar *path;
	gint days;

	void (*func_valid)(const gchar *path, gboolean valid, gpointer data);
	gpointer data;

	guint idle_id; /* event source id */
};

static void thumb_loader_std_thumb_file_validate_free(ThumbValidate *tv)
{
	thumb_loader_std_free(tv->tl);
	g_free(tv->path);
	g_free(tv);
}

void thumb_loader_std_thumb_file_validate_cancel(ThumbLoaderStd *tl)
{
	ThumbValidate *tv;

	if (!tl) return;

	tv = static_cast<ThumbValidate *>(tl->data);

	if (tv->idle_id)
		{
		g_source_remove(tv->idle_id);
		tv->idle_id = 0;
		}

	thumb_loader_std_thumb_file_validate_free(tv);
}

static void thumb_loader_std_thumb_file_validate_finish(ThumbValidate *tv, gboolean valid)
{
	if (tv->func_valid) tv->func_valid(tv->path, valid, tv->data);

	thumb_loader_std_thumb_file_validate_free(tv);
}

static void thumb_loader_std_thumb_file_validate_done_cb(ThumbLoaderStd *UNUSED(tl), gpointer data)
{
	auto tv = static_cast<ThumbValidate *>(data);
	GdkPixbuf *pixbuf;
	gboolean valid = FALSE;

	/* get the original thumbnail pixbuf (unrotated, with original options)
	   this is called from image_loader done callback, so tv->tl->il must exist*/
	pixbuf = image_loader_get_pixbuf(tv->tl->il);
	if (pixbuf)
		{
		const gchar *uri;
		const gchar *mtime_str;

		uri = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_URI);
		mtime_str = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME);
		if (uri && mtime_str)
			{
			if (strncmp(uri, "file:", strlen("file:")) == 0)
				{
				struct stat st;
				gchar *target;

				target = g_filename_from_uri(uri, nullptr, nullptr);
				if (stat(target, &st) == 0 &&
				    st.st_mtime == strtol(mtime_str, nullptr, 10))
					{
					valid = TRUE;
					}
				g_free(target);
				}
			else
				{
				struct stat st;

				DEBUG_1("thumb uri foreign, doing day check: %s", uri);

				if (stat_utf8(tv->path, &st))
					{
					time_t now;

					now = time(nullptr);
					if (st.st_atime >= now - static_cast<time_t>(tv->days) * 24 * 60 * 60)
						{
						valid = TRUE;
						}
					}
				}
			}
		else
			{
			DEBUG_1("invalid image found in std cache: %s", tv->path);
			}
		}

	thumb_loader_std_thumb_file_validate_finish(tv, valid);
}

static void thumb_loader_std_thumb_file_validate_error_cb(ThumbLoaderStd *UNUSED(tl), gpointer data)
{
	auto tv = static_cast<ThumbValidate *>(data);

	thumb_loader_std_thumb_file_validate_finish(tv, FALSE);
}

static gboolean thumb_loader_std_thumb_file_validate_idle_cb(gpointer data)
{
	auto tv = static_cast<ThumbValidate *>(data);

	tv->idle_id = 0;
	thumb_loader_std_thumb_file_validate_finish(tv, FALSE);

	return G_SOURCE_REMOVE;
}

ThumbLoaderStd *thumb_loader_std_thumb_file_validate(const gchar *thumb_path, gint allowed_days,
						     void (*func_valid)(const gchar *path, gboolean valid, gpointer data),
						     gpointer data)
{
	ThumbValidate *tv;

	tv = g_new0(ThumbValidate, 1);

	tv->tl = thumb_loader_std_new(THUMB_SIZE_LARGE, THUMB_SIZE_LARGE);
	thumb_loader_std_set_callbacks(tv->tl,
				       thumb_loader_std_thumb_file_validate_done_cb,
				       thumb_loader_std_thumb_file_validate_error_cb,
				       nullptr,
				       tv);
	thumb_loader_std_reset(tv->tl);

	tv->path = g_strdup(thumb_path);
	tv->days = allowed_days;
	tv->func_valid = func_valid;
	tv->data = data;

	FileData *fd = file_data_new_no_grouping(thumb_path);
	if (!thumb_loader_std_setup(tv->tl, fd))
		{
		tv->idle_id = g_idle_add(thumb_loader_std_thumb_file_validate_idle_cb, tv);
		}
	else
		{
		tv->idle_id = 0;
		}

	file_data_unref(fd);
	return tv->tl;
}

static void thumb_std_maint_remove_one(const gchar *source, const gchar *uri, gboolean local,
				       const gchar *subfolder)
{
	gchar *thumb_path;

	thumb_path = thumb_std_cache_path(source,
					  (local) ? filename_from_path(uri) : uri,
					  local, subfolder);
	if (isfile(thumb_path))
		{
		DEBUG_1("thumb removing: %s", thumb_path);
		unlink_file(thumb_path);
		}
	g_free(thumb_path);
}

/* this also removes local thumbnails (the source is gone so it makes sense) */
void thumb_std_maint_removed(const gchar *source)
{
	gchar *uri;
	gchar *sourcel;

	sourcel = path_from_utf8(source);
	uri = g_filename_to_uri(sourcel, nullptr, nullptr);
	g_free(sourcel);

	/* all this to remove a thumbnail? */

	thumb_std_maint_remove_one(source, uri, FALSE, THUMB_FOLDER_NORMAL);
	thumb_std_maint_remove_one(source, uri, FALSE, THUMB_FOLDER_LARGE);
	thumb_std_maint_remove_one(source, uri, FALSE, THUMB_FOLDER_FAIL);
	thumb_std_maint_remove_one(source, uri, TRUE, THUMB_FOLDER_NORMAL);
	thumb_std_maint_remove_one(source, uri, TRUE, THUMB_FOLDER_LARGE);

	g_free(uri);
}

typedef struct _TMaintMove TMaintMove;
struct _TMaintMove
{
	gchar *source;
	gchar *dest;

	ThumbLoaderStd *tl;
	gchar *source_uri;
	gchar *thumb_path;

	gint pass;
};

static GList *thumb_std_maint_move_list = nullptr;
static GList *thumb_std_maint_move_tail = nullptr;


static void thumb_std_maint_move_step(TMaintMove *tm);
static gboolean thumb_std_maint_move_idle(gpointer data);


static void thumb_std_maint_move_validate_cb(const gchar *UNUSED(path), gboolean UNUSED(valid), gpointer data)
{
	auto tm = static_cast<TMaintMove *>(data);
	GdkPixbuf *pixbuf;

	/* get the original thumbnail pixbuf (unrotated, with original options)
	   this is called from image_loader done callback, so tm->tl->il must exist*/
	pixbuf = image_loader_get_pixbuf(tm->tl->il);
	if (pixbuf)
		{
		const gchar *uri;
		const gchar *mtime_str;

		uri = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_URI);
		mtime_str = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME);

		if (uri && mtime_str && strcmp(uri, tm->source_uri) == 0)
			{
			gchar *pathl;

			/* The validation utility abuses ThumbLoader, and we
			 * abuse the utility just to load the thumbnail,
			 * but the loader needs to look sane for the save to complete.
			 */

			tm->tl->cache_enable = TRUE;
			tm->tl->cache_hit = FALSE;
			tm->tl->cache_local = FALSE;
			file_data_unref(tm->tl->fd);
			tm->tl->fd = file_data_new_group(tm->dest);
			tm->tl->source_mtime = strtol(mtime_str, nullptr, 10);

			pathl = path_from_utf8(tm->tl->fd->path);
			g_free(tm->tl->thumb_uri);
			tm->tl->thumb_uri = g_filename_to_uri(pathl, nullptr, nullptr);
			tm->tl->local_uri = filename_from_path(tm->tl->thumb_uri);
			g_free(pathl);

			g_free(tm->tl->thumb_path);
			tm->tl->thumb_path = nullptr;
			tm->tl->thumb_path_local = FALSE;

			DEBUG_1("thumb move attempting save:");

			thumb_loader_std_save(tm->tl, pixbuf);
			}

		DEBUG_1("thumb move unlink: %s", tm->thumb_path);
		unlink_file(tm->thumb_path);
		}

	thumb_std_maint_move_step(tm);
}

static void thumb_std_maint_move_step(TMaintMove *tm)
{
	const gchar *folder;

	tm->pass++;
	if (tm->pass > 2)
		{
		g_free(tm->source);
		g_free(tm->dest);
		g_free(tm->source_uri);
		g_free(tm->thumb_path);
		g_free(tm);

		if (thumb_std_maint_move_list)
			{
			g_idle_add_full(G_PRIORITY_LOW, thumb_std_maint_move_idle, nullptr, nullptr);
			}

		return;
		}

	folder = (tm->pass == 1) ? THUMB_FOLDER_NORMAL : THUMB_FOLDER_LARGE;

	g_free(tm->thumb_path);
	tm->thumb_path = thumb_std_cache_path(tm->source, tm->source_uri, FALSE, folder);
	tm->tl = thumb_loader_std_thumb_file_validate(tm->thumb_path, 0,
						      thumb_std_maint_move_validate_cb, tm);
}

static gboolean thumb_std_maint_move_idle(gpointer UNUSED(data))
{
	TMaintMove *tm;
	gchar *pathl;

	if (!thumb_std_maint_move_list) return G_SOURCE_REMOVE;

	tm = static_cast<TMaintMove *>(thumb_std_maint_move_list->data);

	thumb_std_maint_move_list = g_list_remove(thumb_std_maint_move_list, tm);
	if (!thumb_std_maint_move_list) thumb_std_maint_move_tail = nullptr;

	pathl = path_from_utf8(tm->source);
	tm->source_uri = g_filename_to_uri(pathl, nullptr, nullptr);
	g_free(pathl);

	tm->pass = 0;

	thumb_std_maint_move_step(tm);

	return G_SOURCE_REMOVE;
}

/* This will schedule a move of the thumbnail for source image to dest when idle.
 * We do this so that file renaming or moving speed is not sacrificed by
 * moving the thumbnails at the same time because:
 *
 * This cache design requires the tedious task of loading the png thumbnails and saving them.
 *
 * The thumbnails are processed when the app is idle. If the app
 * exits early well too bad - they can simply be regenerated from scratch.
 */
/** @FIXME This does not manage local thumbnails (fixme ?)
 */
void thumb_std_maint_moved(const gchar *source, const gchar *dest)
{
	TMaintMove *tm;

	tm = g_new0(TMaintMove, 1);
	tm->source = g_strdup(source);
	tm->dest = g_strdup(dest);

	if (!thumb_std_maint_move_list)
		{
		g_idle_add_full(G_PRIORITY_LOW, thumb_std_maint_move_idle, nullptr, nullptr);
		}

	if (thumb_std_maint_move_tail)
		{
		thumb_std_maint_move_tail = g_list_append(thumb_std_maint_move_tail, tm);
		thumb_std_maint_move_tail = thumb_std_maint_move_tail->next;
		}
	else
		{
		thumb_std_maint_move_list = g_list_append(thumb_std_maint_move_list, tm);
		thumb_std_maint_move_tail = thumb_std_maint_move_list;
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
