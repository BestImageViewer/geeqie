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

#include "image-load.h"

#include <sys/mman.h>

#include <cstring>

#include <config.h>

#include "exif.h"
#include "filedata.h"
#include "gq-size.h"
#include "image-load-collection.h"
#include "image-load-dds.h"
#if HAVE_DJVU
#  include "image-load-djvu.h"
#endif
#if HAVE_EXR
#  include "image-load-exr.h"
#endif
#include "image-load-external.h"
#if HAVE_FFMPEGTHUMBNAILER
#  include "image-load-ffmpegthumbnailer.h"
#endif
#if HAVE_FITS
#  include "image-load-fits.h"
#endif
#include "image-load-gdk.h"
#if HAVE_HEIF
#  include "image-load-heif.h"
#endif
#if HAVE_J2K
#  include "image-load-j2k.h"
#endif
#if HAVE_JPEG
#  if !HAVE_RAW
#    include "image-load-cr3.h"
#  endif
#  include "image-load-jpeg.h"
#endif
#if HAVE_JPEGXL
#  include "image-load-jpegxl.h"
#endif
#include "image-load-libraw.h"
#if HAVE_NPY
#  include "image-load-npy.h"
#endif
#if HAVE_PDF
#  include "image-load-pdf.h"
#endif
#include "image-load-psd.h"
#include "image-load-svgz.h"
#if HAVE_TIFF
#  include "image-load-tiff.h"
#endif
#include "image-load-webp.h"
#include "image-load-zxscr.h"
#include "jpeg-parser.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"

struct ExifData;

enum {
	IMAGE_LOADER_READ_BUFFER_SIZE_DEFAULT = 	4096,
	IMAGE_LOADER_IDLE_READ_LOOP_COUNT_DEFAULT = 	1
};

/* image loader class */


enum {
	SIGNAL_AREA_READY = 0,
	SIGNAL_ERROR,
	SIGNAL_DONE,
	SIGNAL_PERCENT,
	SIGNAL_SIZE_PREPARED,
	SIGNAL_COUNT
};

static guint signals[SIGNAL_COUNT] = { 0 };

static void image_loader_init(GTypeInstance *instance, gpointer g_class);
static void image_loader_class_init_wrapper(void *data, void *user_data);
static void image_loader_class_init(ImageLoaderClass *loader_class);
static void image_loader_finalize(GObject *object);
static void image_loader_stop(ImageLoader *il);

GType image_loader_get_type()
{
	static const GTypeInfo info = {
	    sizeof(ImageLoaderClass),
	    nullptr,   /* base_init */
	    nullptr,   /* base_finalize */
	    static_cast<GClassInitFunc>(image_loader_class_init_wrapper), /* class_init */
	    nullptr,   /* class_finalize */
	    nullptr,   /* class_data */
	    sizeof(ImageLoader),
	    0,      /* n_preallocs */
	    static_cast<GInstanceInitFunc>(image_loader_init), /* instance_init */
	    nullptr	/* value_table */
	};
	static GType type = g_type_register_static(G_TYPE_OBJECT, "ImageLoaderType", &info, static_cast<GTypeFlags>(0));

	return type;
}

static void image_loader_init(GTypeInstance *instance, gpointer)
{
	auto il = reinterpret_cast<ImageLoader *>(instance);

	il->pixbuf = nullptr;
	il->idle_id = 0;
	il->idle_priority = G_PRIORITY_DEFAULT_IDLE;
	il->done = FALSE;
	il->backend.reset(nullptr);

	il->bytes_read = 0;
	il->bytes_total = 0;

	il->idle_done_id = 0;

	il->idle_read_loop_count = IMAGE_LOADER_IDLE_READ_LOOP_COUNT_DEFAULT;
	il->read_buffer_size = IMAGE_LOADER_READ_BUFFER_SIZE_DEFAULT;
	il->mapped_file = nullptr;
	il->preview = IMAGE_LOADER_PREVIEW_NONE;

	il->requested_width = 0;
	il->requested_height = 0;
	il->actual_width = 0;
	il->actual_height = 0;
	il->shrunk = FALSE;

	il->can_destroy = TRUE;

	il->data_mutex = g_new(GMutex, 1);
	g_mutex_init(il->data_mutex);
	il->can_destroy_cond = g_new(GCond, 1);
	g_cond_init(il->can_destroy_cond);

	DEBUG_1("new image loader %p, bufsize=%" G_GSIZE_FORMAT " idle_loop=%u", (void *)il, il->read_buffer_size, il->idle_read_loop_count);
}

static void image_loader_class_init_wrapper(void *data, void *)
{
	image_loader_class_init(static_cast<ImageLoaderClass *>(data));
}

static void image_loader_class_init(ImageLoaderClass *loader_class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (loader_class);

	gobject_class->finalize = image_loader_finalize;


	signals[SIGNAL_AREA_READY] =
	    g_signal_new("area-ready",
	                 G_OBJECT_CLASS_TYPE(gobject_class),
	                 G_SIGNAL_RUN_LAST,
	                 G_STRUCT_OFFSET(ImageLoaderClass, area_ready),
	                 nullptr, nullptr,
	                 g_cclosure_marshal_VOID__BOXED,
	                 G_TYPE_NONE, 1,
	                 GDK_TYPE_RECTANGLE);

	signals[SIGNAL_ERROR] =
		g_signal_new("error",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(ImageLoaderClass, error),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);

	signals[SIGNAL_DONE] =
		g_signal_new("done",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(ImageLoaderClass, done),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);

	signals[SIGNAL_PERCENT] =
		g_signal_new("percent",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(ImageLoaderClass, percent),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__DOUBLE,
			     G_TYPE_NONE, 1,
			     G_TYPE_DOUBLE);

	signals[SIGNAL_SIZE_PREPARED] =
	    g_signal_new("size-prepared",
	                 G_OBJECT_CLASS_TYPE(gobject_class),
	                 G_SIGNAL_RUN_LAST,
	                 G_STRUCT_OFFSET(ImageLoaderClass, size_prepared),
	                 nullptr, nullptr,
	                 g_cclosure_marshal_VOID__BOXED,
	                 G_TYPE_NONE, 1,
	                 GQ_TYPE_SIZE);

}

#ifdef DEBUG
static const gchar *image_loader_get_error(ImageLoader *il)
{
	if (!il) return nullptr;

	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(il->data_mutex);

	return il->error ? il->error->message : nullptr;
}
#endif

static void image_loader_finalize(GObject *object)
{
	auto il = reinterpret_cast<ImageLoader *>(object);

	image_loader_stop(il);

	if (il->error) DEBUG_1("%s", image_loader_get_error(il));

	DEBUG_1("freeing image loader %p bytes_read=%" G_GSIZE_FORMAT, (void *)il, il->bytes_read);

	g_clear_handle_id(&il->idle_done_id, g_source_remove);

	while (g_source_remove_by_user_data(il))
		{
		DEBUG_2("pending signals detected");
		}

	while (il->area_param_list)
		{
		DEBUG_1("pending area_ready signals detected");
		while (g_source_remove_by_user_data(il->area_param_list->data)) {}
		g_free(il->area_param_list->data);
		il->area_param_list = g_list_delete_link(il->area_param_list, il->area_param_list);
		}

	while (il->area_param_delayed_list)
		{
		g_free(il->area_param_delayed_list->data);
		il->area_param_delayed_list = g_list_delete_link(il->area_param_delayed_list, il->area_param_delayed_list);
		}

	if (il->pixbuf) g_object_unref(il->pixbuf);

	if (il->error) g_error_free(il->error);

	file_data_unref(il->fd);

	g_mutex_clear(il->data_mutex);
	g_free(il->data_mutex);
	g_cond_clear(il->can_destroy_cond);
	g_free(il->can_destroy_cond);
}

void image_loader_free(ImageLoader *il)
{
	if (!il) return;
	g_object_unref(G_OBJECT(il));
}


ImageLoader *image_loader_new(FileData *fd)
{
	ImageLoader *il;

	if (!fd) return nullptr;

	il = static_cast<ImageLoader *>(g_object_new(TYPE_IMAGE_LOADER, nullptr));

	il->fd = file_data_ref(fd);

	return il;
}

/**************************************************************************************/
/* send signals via idle calbacks - the callback are executed in the main thread */

struct ImageLoaderAreaParam {
	ImageLoader *il;
	GdkRectangle area;
};


static gboolean image_loader_emit_area_ready_cb(gpointer data)
{
	auto par = static_cast<ImageLoaderAreaParam *>(data);
	ImageLoader *il = par->il;
	g_mutex_lock(il->data_mutex);
	il->area_param_list = g_list_remove(il->area_param_list, par);
	GdkRectangle area = par->area;
	g_free(par);
	g_mutex_unlock(il->data_mutex);

	g_signal_emit(il, signals[SIGNAL_AREA_READY], 0, &area);

	return G_SOURCE_REMOVE;
}

static gboolean image_loader_emit_done_cb(gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	g_signal_emit(il, signals[SIGNAL_DONE], 0);
	return G_SOURCE_REMOVE;
}

static gboolean image_loader_emit_error_cb(gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	g_signal_emit(il, signals[SIGNAL_ERROR], 0);
	return G_SOURCE_REMOVE;
}

static gboolean image_loader_emit_percent_cb(gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	g_signal_emit(il, signals[SIGNAL_PERCENT], 0, image_loader_get_percent(il));
	return G_SOURCE_REMOVE;
}

static gboolean image_loader_emit_size_prepared_cb(gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	g_mutex_lock(il->data_mutex);
	GqSize size{ il->actual_width, il->actual_height };
	g_mutex_unlock(il->data_mutex);
	g_signal_emit(il, signals[SIGNAL_SIZE_PREPARED], 0, &size);
	return G_SOURCE_REMOVE;
}


/* DONE and ERROR are emitted only once, thus they can have normal priority
   PERCENT and AREA_READY should be processed ASAP
*/

static void image_loader_emit_done(ImageLoader *il)
{
	g_idle_add_full(il->idle_priority, image_loader_emit_done_cb, il, nullptr);
}

static void image_loader_emit_error(ImageLoader *il)
{
	g_idle_add_full(il->idle_priority, image_loader_emit_error_cb, il, nullptr);
}

static void image_loader_emit_percent(ImageLoader *il)
{
	g_idle_add_full(G_PRIORITY_HIGH, image_loader_emit_percent_cb, il, nullptr);
}

static void image_loader_emit_size_prepared(ImageLoader *il)
{
	g_idle_add_full(G_PRIORITY_HIGH, image_loader_emit_size_prepared_cb, il, nullptr);
}

static ImageLoaderAreaParam *image_loader_queue_area_ready(ImageLoader *il, GList **list, GdkRectangle area)
{
	if (*list)
		{
		auto prev_par = static_cast<ImageLoaderAreaParam *>((*list)->data);
		GdkRectangle &prev_area = prev_par->area;

		if (prev_area.x == area.x && prev_area.width == area.width)
			{
			if (prev_area.y + prev_area.height == area.y)
				{
				/* we can merge the notifications */
				prev_area.height += area.height;
				return nullptr;
				}

			if (prev_area.y == area.y + area.height)
				{
				/* we can merge the notifications */
				prev_area.y = area.y;
				prev_area.height += area.height;
				return nullptr;
				}
			}

		if (prev_area.y == area.y && prev_area.height == area.height)
			{
			if (prev_area.x + prev_area.width == area.x)
				{
				/* we can merge the notifications */
				prev_area.width += area.width;
				return nullptr;
				}

			if (prev_area.x == area.x + area.width)
				{
				/* we can merge the notifications */
				prev_area.x = area.x;
				prev_area.width += area.width;
				return nullptr;
				}
			}
		}

	auto par = g_new0(ImageLoaderAreaParam, 1);
	par->il = il;
	par->area = area;

	*list = g_list_prepend(*list, par);
	return par;
}

/* this function expects that il->data_mutex is locked by caller */
static void image_loader_emit_area_ready(ImageLoader *il, GdkRectangle area)
{
	ImageLoaderAreaParam *par = image_loader_queue_area_ready(il, &il->area_param_list, area);

	if (par)
		{
		g_idle_add_full(G_PRIORITY_HIGH, image_loader_emit_area_ready_cb, par, nullptr);
		}
}

/**************************************************************************************/
/* the following functions may be executed in separate thread */

/* this function expects that il->data_mutex is locked by caller */
static void image_loader_queue_delayed_area_ready(ImageLoader *il, GdkRectangle area)
{
	image_loader_queue_area_ready(il, &il->area_param_delayed_list, area);
}



static gboolean image_loader_get_stopping(ImageLoader *il)
{
	gboolean ret;
	if (!il) return FALSE;

	g_mutex_lock(il->data_mutex);
	ret = il->stopping;
	g_mutex_unlock(il->data_mutex);

	return ret;
}


static void image_loader_sync_pixbuf(ImageLoader *il)
{
	GdkPixbuf *pb;

	g_mutex_lock(il->data_mutex);

	if (!il->backend)
		{
		g_mutex_unlock(il->data_mutex);
		return;
		}

	pb = il->backend->get_pixbuf();

	if (pb == il->pixbuf)
		{
		g_mutex_unlock(il->data_mutex);
		return;
		}

	if (g_ascii_strcasecmp(".jps", il->fd->extension) == 0)
		{
		g_object_set_data(G_OBJECT(pb), "stereo_data", GINT_TO_POINTER(STEREO_PIXBUF_CROSS));
		}

	if (il->pixbuf) g_object_unref(il->pixbuf);

	il->pixbuf = pb;
	if (il->pixbuf) g_object_ref(il->pixbuf);

	g_mutex_unlock(il->data_mutex);
}

static void image_loader_area_updated_cb(gpointer,
                                         gint x, gint y, gint w, gint h,
                                         gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);

	if (!image_loader_get_pixbuf(il))
		{
		image_loader_sync_pixbuf(il);
		if (!image_loader_get_pixbuf(il))
			{
			log_printf("critical: area_ready signal with NULL pixbuf (out of mem?)\n");
			}
		}

	g_mutex_lock(il->data_mutex);
	if (il->delay_area_ready)
		image_loader_queue_delayed_area_ready(il, {x, y, w, h});
	else
		image_loader_emit_area_ready(il, {x, y, w, h});

	if (il->stopping) il->backend->abort();

	g_mutex_unlock(il->data_mutex);
}

static void image_loader_size_prepared_cb(gpointer, gint width, gint height, gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	gboolean scale = FALSE;

	g_mutex_lock(il->data_mutex);
	il->actual_width = width;
	il->actual_height = height;
	if (il->requested_width < 1 || il->requested_height < 1)
		{
		g_mutex_unlock(il->data_mutex);
		image_loader_emit_size_prepared(il);
		return;
		}
	g_mutex_unlock(il->data_mutex);

#if HAVE_FFMPEGTHUMBNAILER
	if (il->fd->format_class == FORMAT_CLASS_VIDEO)
		scale = TRUE;
#endif

	if (!scale)
		{
		g_auto(GStrv) mime_types = il->backend->get_format_mime_types();
		gint n = 0;
		while (mime_types[n] && !scale)
			{
			if (strstr(mime_types[n], "jpeg")) scale = TRUE;
			n++;
			}
		}

	if (!scale)
		{
		image_loader_emit_size_prepared(il);
		return;
		}

	g_mutex_lock(il->data_mutex);

	if (width > il->requested_width || height > il->requested_height)
		{
		pixbuf_scale_aspect(il->requested_width, il->requested_height, width, height, il->actual_width, il->actual_height);

		il->backend->set_size(il->actual_width, il->actual_height);
		il->shrunk = TRUE;
		}

	g_mutex_unlock(il->data_mutex);
	image_loader_emit_size_prepared(il);
}

static void image_loader_stop_loader(ImageLoader *il)
{
	if (!il) return;

	if (il->backend)
		{
		/* some loaders do not have a pixbuf till close, order is important here */
		il->backend->close(il->error ? nullptr : &il->error); /* we are interested in the first error only */
		image_loader_sync_pixbuf(il);
		il->backend.reset(nullptr);
		}
	g_mutex_lock(il->data_mutex);
	il->done = TRUE;
	g_mutex_unlock(il->data_mutex);
}

static void image_loader_setup_loader(ImageLoader *il)
{
	gint external_preview = 1;

	g_mutex_lock(il->data_mutex);

	if (options->external_preview.enable)
		{
		g_autofree gchar *tilde_filename = expand_tilde(options->external_preview.select);
		g_autofree gchar *cmd_line = g_strdup_printf("\"%s\" \"%s\"", tilde_filename, il->fd->path);

		external_preview = runcmd(cmd_line);
		}

	if (external_preview == 0)
		{
		DEBUG_1("Using custom external loader");
		il->backend = get_image_loader_backend_external();
		}
	else
		{
#if HAVE_FFMPEGTHUMBNAILER
		if (il->fd->format_class == FORMAT_CLASS_VIDEO)
			{
			DEBUG_1("Using custom ffmpegthumbnailer loader");
			il->backend = get_image_loader_backend_ft();
			}
		else
#endif
#if HAVE_FITS
		if (il->bytes_total >= 6 &&
			(memcmp(il->mapped_file, "SIMPLE", 6) == 0))
			{
			DEBUG_1("Using custom fits loader");
			il->backend = get_image_loader_backend_fits();
			}
		else
#endif
#if HAVE_PDF
		if (il->bytes_total >= 4 &&
		    (memcmp(il->mapped_file + 0, "%PDF", 4) == 0))
			{
			DEBUG_1("Using custom pdf loader");
			il->backend = get_image_loader_backend_pdf();
			}
		else
#endif
#if HAVE_HEIF
		if (il->bytes_total >= 12 &&
		    ((memcmp(il->mapped_file + 4, "ftypheic", 8) == 0) ||
		     (memcmp(il->mapped_file + 4, "ftypheix", 8) == 0) ||
		     (memcmp(il->mapped_file + 4, "ftypmsf1", 8) == 0) ||
		     (memcmp(il->mapped_file + 4, "ftypmif1", 8) == 0) ||
		     (memcmp(il->mapped_file + 4, "ftypavif", 8) == 0)))
			{
			DEBUG_1("Using custom heif loader");
			il->backend = get_image_loader_backend_heif();
			}
		else
	#endif
	#if HAVE_WEBP
		if (il->bytes_total >= 12 &&
			(memcmp(il->mapped_file, "RIFF", 4) == 0) &&
			(memcmp(il->mapped_file + 8, "WEBP", 4) == 0))
			{
			DEBUG_1("Using custom webp loader");
			il->backend = get_image_loader_backend_webp();
			}
		else
#endif
#if HAVE_DJVU
		if (il->bytes_total >= 16 &&
			(memcmp(il->mapped_file, "AT&TFORM", 8) == 0) &&
			(memcmp(il->mapped_file + 12, "DJV", 3) == 0))
			{
			DEBUG_1("Using custom djvu loader");
			il->backend = get_image_loader_backend_djvu();
			}
		else
#endif
#if HAVE_EXR
		if (il->bytes_total >= 4 &&
			(memcmp(il->mapped_file, "\x76\x2F\x31\x01", 4) == 0))
			{
			DEBUG_1("Using custom exr loader");
			il->backend = get_image_loader_backend_exr();
			}
		else
#endif
#if HAVE_JPEG
		if (il->bytes_total >= 2 && il->mapped_file[0] == 0xff && il->mapped_file[1] == 0xd8)
			{
			DEBUG_1("Using custom jpeg loader");
			il->backend = get_image_loader_backend_jpeg();
			}
#if !HAVE_RAW
		else
		if (il->bytes_total >= 11 &&
		    (memcmp(il->mapped_file + 4, "ftypcrx", 7) == 0) &&
		    (memcmp(il->mapped_file + 64, "CanonCR3", 8) == 0))
			{
			DEBUG_1("Using custom cr3 loader");
			il->backend = get_image_loader_backend_cr3();
			}
#endif
		else
#endif
#if HAVE_TIFF
		if (il->bytes_total >= 10 &&
		    (memcmp(il->mapped_file, "MM\0*", 4) == 0 ||
		     memcmp(il->mapped_file, "MM\0+\0\x08\0\0", 8) == 0 ||
		     memcmp(il->mapped_file, "II+\0\x08\0\0\0", 8) == 0 ||
		     memcmp(il->mapped_file, "II*\0", 4) == 0))
		     	{
			DEBUG_1("Using custom tiff loader");
			il->backend = get_image_loader_backend_tiff();
			}
		else
#endif
#if HAVE_NPY
		if (il->bytes_total >= 6 &&
			(memcmp(il->mapped_file, "\x93NUMPY", 6) == 0))
			{
			DEBUG_1("Using custom npy loader");
			il->backend = get_image_loader_backend_npy();
			}
		else
#endif
		if (il->bytes_total >= 3 && il->mapped_file[0] == 0x44 && il->mapped_file[1] == 0x44 && il->mapped_file[2] == 0x53)
			{
			DEBUG_1("Using dds loader");
			il->backend = get_image_loader_backend_dds();
			}
		else
		if (il->bytes_total >= 6 &&
			(memcmp(il->mapped_file, "8BPS\0\x01", 6) == 0))
			{
			DEBUG_1("Using custom psd loader");
			il->backend = get_image_loader_backend_psd();
			}
		else
#if HAVE_J2K
		if (il->bytes_total >= 12 &&
			(memcmp(il->mapped_file, "\0\0\0\x0CjP\x20\x20\x0D\x0A\x87\x0A", 12) == 0))
			{
			DEBUG_1("Using custom j2k loader");
			il->backend = get_image_loader_backend_j2k();
			}
		else
#endif
#if HAVE_JPEGXL
		if ((il->bytes_total >= 12 &&
		     (memcmp(il->mapped_file, "\0\0\0\x0C\x4A\x58\x4C\x20\x0D\x0A\x87\x0A", 12) == 0)) ||
		    (il->bytes_total >= 2 &&
		     (memcmp(il->mapped_file, "\xFF\x0A", 2) == 0)))
			{
			DEBUG_1("Using custom jpeg xl loader");
			il->backend = get_image_loader_backend_jpegxl();
			}
		else
#endif
		if ((il->bytes_total == 6144 || il->bytes_total == 6912) &&
			(file_extension_match(il->fd->path, ".scr")))
			{
			DEBUG_1("Using custom zxscr loader");
			il->backend = get_image_loader_backend_zxscr();
			}
		else
		if (il->fd->format_class == FORMAT_CLASS_COLLECTION)
			{
			DEBUG_1("Using custom collection loader");
			il->backend = get_image_loader_backend_collection();
			}
		else
		if (g_strcmp0(strrchr(il->fd->path, '.'), ".svgz") == 0)
			{
			DEBUG_1("Using custom svgz loader");
			il->backend = get_image_loader_backend_svgz();
			}
		else
			il->backend = get_image_loader_backend_default();
		}

	il->backend->init(image_loader_area_updated_cb, image_loader_size_prepared_cb, il);
	il->backend->set_page_num(il->fd->page_num);

	il->fd->format_name = il->backend->get_format_name();

	g_mutex_unlock(il->data_mutex);
}


static void image_loader_done(ImageLoader *il)
{
	image_loader_stop_loader(il);

	image_loader_emit_done(il);
}

static void image_loader_error(ImageLoader *il)
{
	image_loader_stop_loader(il);

	DEBUG_1("pixbuf_loader reported load error for: %s", il->fd->path);

	image_loader_emit_error(il);
}

static gboolean image_loader_continue(ImageLoader *il)
{
	gint c;

	if (!il) return G_SOURCE_REMOVE;

	c = il->idle_read_loop_count ? il->idle_read_loop_count : 1;
	while (c > 0 && !image_loader_get_stopping(il))
		{
		if (il->bytes_total == il->bytes_read)
			{
			image_loader_done(il);
			return G_SOURCE_REMOVE;
			}

		if (il->bytes_total < il->bytes_read)
			{
			image_loader_error(il);
			return G_SOURCE_REMOVE;
			}

		gsize b = std::min(il->read_buffer_size, il->bytes_total - il->bytes_read);

		if (!il->backend->write(il->mapped_file + il->bytes_read, b, il->bytes_total, &il->error))
			{
			image_loader_error(il);
			return G_SOURCE_REMOVE;
			}

		il->bytes_read += b;

		c--;
		}

	if (il->bytes_total > 0)
		{
		image_loader_emit_percent(il);
		}

	return G_SOURCE_CONTINUE;
}

static gboolean image_loader_begin(ImageLoader *il)
{
	if (il->pixbuf) return FALSE;

	if (il->bytes_total <= il->bytes_read) return FALSE;

	gsize b = std::min(il->read_buffer_size, il->bytes_total - il->bytes_read);

	image_loader_setup_loader(il);

	g_assert(il->bytes_read == 0);
	if (!il->backend->write(il->mapped_file, b, il->bytes_total, &il->error))
		{
		image_loader_stop_loader(il);
		return FALSE;
		}

	file_data_set_page_total(il->fd, il->backend->get_page_total());

	il->bytes_read += b;

	/* read until size is known */
	while (il->backend && !il->backend->get_pixbuf() && b > 0 && !image_loader_get_stopping(il))
		{
		if (il->bytes_total < il->bytes_read)
			{
			image_loader_stop_loader(il);
			return FALSE;
			}

		b = std::min(il->read_buffer_size, il->bytes_total - il->bytes_read);
		if (b > 0 && !il->backend->write(il->mapped_file + il->bytes_read, b, il->bytes_total, &il->error))
			{
			image_loader_stop_loader(il);
			return FALSE;
			}
		il->bytes_read += b;
		}
	if (!il->pixbuf) image_loader_sync_pixbuf(il);

	if (il->bytes_read == il->bytes_total || b < 1)
		{
		/* done, handle (broken) loaders that do not have pixbuf till close */
		image_loader_stop_loader(il);

		if (!il->pixbuf) return FALSE;

		image_loader_done(il);
		return TRUE;
		}

	if (!il->pixbuf)
		{
		image_loader_stop_loader(il);
		return FALSE;
		}

	return TRUE;
}

/**************************************************************************************/
/* the following functions are always executed in the main thread */


static gboolean image_loader_setup_source(ImageLoader *il)
{
	if (!il || il->backend || il->mapped_file) return FALSE;

	il->mapped_file = nullptr;

	if (il->fd)
		{
		ExifData *exif = exif_read_fd(il->fd);

		if (options->thumbnails.use_exif)
			{
			il->mapped_file = exif_get_preview(exif, reinterpret_cast<guint *>(&il->bytes_total), il->requested_width, il->requested_height);

			if (il->mapped_file)
				{
				il->preview = IMAGE_LOADER_PREVIEW_EXIF;
				}
			}
		else
			{
			il->mapped_file = libraw_get_preview(il->fd->path, il->bytes_total);

			if (il->mapped_file)
				{
				il->preview = IMAGE_LOADER_PREVIEW_LIBRAW;
				}
			}

		/* If libraw does not find a thumbnail, try exiv2 */
		if (!il->mapped_file)
			{
			il->mapped_file = exif_get_preview(exif, reinterpret_cast<guint *>(&il->bytes_total), 0, 0); /* get the largest available preview image or NULL for normal images*/

			if (il->mapped_file)
				{
				/* exiv2 sometimes returns a pointer to a file section that is not a jpeg */
				if (!is_jpeg_container(il->mapped_file, il->bytes_total))
					{
					exif_free_preview(il->mapped_file);
					il->mapped_file = nullptr;
					}
				else
					{
					il->preview = IMAGE_LOADER_PREVIEW_EXIF;
					}
				}
			}

		if (il->mapped_file)
			{
			DEBUG_1("Usable reduced size (preview) image loaded from file %s", il->fd->path);
			}
		exif_free_fd(il->fd, exif);
		}

	if (!il->mapped_file)
		{
		/* normal file */
		g_autofree gchar *pathl = path_from_utf8(il->fd->path);

		il->mapped_file = map_file(pathl, il->bytes_total); // bytes_total written from fs.stsize
		if (!il->mapped_file)
			{
			return FALSE;
			}
		il->preview = IMAGE_LOADER_PREVIEW_NONE;
		}

	return TRUE;
}

static void image_loader_stop_source(ImageLoader *il)
{
	if (!il) return;

	if (il->mapped_file)
		{
		if (il->preview == IMAGE_LOADER_PREVIEW_EXIF)
			{
			exif_free_preview(il->mapped_file);
			}
		else if (il->preview == IMAGE_LOADER_PREVIEW_LIBRAW)
			{
			libraw_free_preview(il->mapped_file);
			}
		else
			{
			munmap(il->mapped_file, il->bytes_total);
			}
		il->mapped_file = nullptr;
		}
}

static void image_loader_stop(ImageLoader *il)
{
	if (!il) return;

	g_clear_handle_id(&il->idle_id, g_source_remove);

	if (il->thread)
		{
		/* stop loader in the other thread */
		g_mutex_lock(il->data_mutex);
		il->stopping = TRUE;
		while (!il->can_destroy) g_cond_wait(il->can_destroy_cond, il->data_mutex);
		g_mutex_unlock(il->data_mutex);
		}

	image_loader_stop_loader(il);
	image_loader_stop_source(il);

}

/**
 * @brief Delay area_ready signals
 */
void image_loader_delay_area_ready(ImageLoader *il, gboolean enable)
{
	g_mutex_lock(il->data_mutex);
	il->delay_area_ready = enable;
	if (!enable)
		{
		/* send delayed */
		GList *list;
		GList *work;
		list = g_list_reverse(il->area_param_delayed_list);
		il->area_param_delayed_list = nullptr;
		g_mutex_unlock(il->data_mutex);

		work = list;

		while (work)
			{
			auto par = static_cast<ImageLoaderAreaParam *>(work->data);
			work = work->next;

			g_signal_emit(il, signals[SIGNAL_AREA_READY], 0, &par->area);
			}
		g_list_free_full(list, g_free);
		}
	else
		{
		/* just unlock */
		g_mutex_unlock(il->data_mutex);
		}
}


/**************************************************************************************/
/* execution via idle calls */

static gboolean image_loader_idle_cb(gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);

	gboolean ret = G_SOURCE_REMOVE;
	if (il->idle_id)
		{
		ret = image_loader_continue(il);
		}

	if (ret == G_SOURCE_REMOVE)
		{
		image_loader_stop_source(il);
		}

	return ret;
}

static gboolean image_loader_start_idle(ImageLoader *il)
{
	gboolean ret;

	if (!il) return FALSE;

	if (!il->fd) return FALSE;

	if (!image_loader_setup_source(il)) return FALSE;

	ret = image_loader_begin(il);

	if (ret && !il->done) il->idle_id = g_idle_add_full(il->idle_priority, image_loader_idle_cb, il, nullptr);
	return ret;
}

/**************************************************************************************/
/* execution via thread */

static GThreadPool *image_loader_thread_pool = nullptr;

static GCond *image_loader_prio_cond = nullptr;
static GMutex *image_loader_prio_mutex = nullptr;
static gint image_loader_prio_num = 0;


static void image_loader_thread_enter_high()
{
	g_mutex_lock(image_loader_prio_mutex);
	image_loader_prio_num++;
	g_mutex_unlock(image_loader_prio_mutex);
}

static void image_loader_thread_leave_high()
{
	g_mutex_lock(image_loader_prio_mutex);
	image_loader_prio_num--;
	if (image_loader_prio_num == 0) g_cond_broadcast(image_loader_prio_cond); /* wake up all low prio threads */
	g_mutex_unlock(image_loader_prio_mutex);
}

static void image_loader_thread_wait_high()
{
	g_mutex_lock(image_loader_prio_mutex);
	while (image_loader_prio_num)
		{
		g_cond_wait(image_loader_prio_cond, image_loader_prio_mutex);
		}

	g_mutex_unlock(image_loader_prio_mutex);
}


static void image_loader_thread_run(gpointer data, gpointer)
{
	auto il = static_cast<ImageLoader *>(data);
	gboolean cont;
	gboolean err;

	if (il->idle_priority > G_PRIORITY_DEFAULT_IDLE)
		{
		/* low prio, wait until high prio tasks finishes */
		image_loader_thread_wait_high();
		}
	else
		{
		/* high prio */
		image_loader_thread_enter_high();
		}

	err = !image_loader_begin(il);

	if (err)
		{
		/*
		loader failed, we have to send signal
		(idle mode returns the image_loader_begin return value directly)
		(success is always reported indirectly from image_loader_begin)
		*/
		image_loader_emit_error(il);
		}

	cont = !err;

	while (cont && !image_loader_get_is_done(il) && !image_loader_get_stopping(il))
		{
		if (il->idle_priority > G_PRIORITY_DEFAULT_IDLE)
			{
			/* low prio, wait until high prio tasks finishes */
			image_loader_thread_wait_high();
			}
		cont = image_loader_continue(il);
		}
	image_loader_stop_loader(il);

	if (il->idle_priority <= G_PRIORITY_DEFAULT_IDLE)
		{
		/* high prio */
		image_loader_thread_leave_high();
		}

	g_mutex_lock(il->data_mutex);
	il->can_destroy = TRUE;
	g_cond_signal(il->can_destroy_cond);
	g_mutex_unlock(il->data_mutex);

}


static gboolean image_loader_start_thread(ImageLoader *il)
{
	if (!il) return FALSE;

	if (!il->fd) return FALSE;

	il->thread = TRUE;

	if (!image_loader_setup_source(il)) return FALSE;

        if (!image_loader_thread_pool)
		{
		image_loader_thread_pool = g_thread_pool_new(image_loader_thread_run, nullptr, -1, FALSE, nullptr);
		if (!image_loader_prio_cond) image_loader_prio_cond = g_new(GCond, 1);
		g_cond_init(image_loader_prio_cond);
		if (!image_loader_prio_mutex) image_loader_prio_mutex = g_new(GMutex, 1);
		g_mutex_init(image_loader_prio_mutex);
		}

	il->can_destroy = FALSE; /* ImageLoader can't be freed until image_loader_thread_run finishes */

	g_thread_pool_push(image_loader_thread_pool, il, nullptr);
	DEBUG_1("Thread pool num threads: %u", g_thread_pool_get_num_threads(image_loader_thread_pool));

	return TRUE;
}


/**************************************************************************************/
/* public interface */


gboolean image_loader_start(ImageLoader *il)
{
	if (!il) return FALSE;

	if (!il->fd) return FALSE;

	return image_loader_start_thread(il);
}


/* don't forget to gdk_pixbuf_ref() it if you want to use it after image_loader_free() */
GdkPixbuf *image_loader_get_pixbuf(ImageLoader *il)
{
	GdkPixbuf *ret;
	if (!il) return nullptr;

	g_mutex_lock(il->data_mutex);
	ret = il->pixbuf;
	g_mutex_unlock(il->data_mutex);
	return ret;
}

/**
 * @brief Speed up loading when you only need at most width x height size image,
 * only the jpeg GdkPixbuf loader benefits from it - so there is no
 * guarantee that the image will scale down to the requested size..
 */
void image_loader_set_requested_size(ImageLoader *il, gint width, gint height)
{
	if (!il) return;

	g_mutex_lock(il->data_mutex);
	il->requested_width = width;
	il->requested_height = height;
	g_mutex_unlock(il->data_mutex);
}

void image_loader_set_buffer_size(ImageLoader *il, guint count)
{
	if (!il) return;

	g_mutex_lock(il->data_mutex);
	il->idle_read_loop_count = count ? count : 1;
	g_mutex_unlock(il->data_mutex);
}

/**
 * @brief This only has effect if used before image_loader_start()
 * default is G_PRIORITY_DEFAULT_IDLE
 */
void image_loader_set_priority(ImageLoader *il, gint priority)
{
	if (!il) return;

	if (il->thread) return; /* can't change prio if the thread already runs */
	il->idle_priority = priority;
}


gdouble image_loader_get_percent(ImageLoader *il)
{
	gdouble ret;
	if (!il) return 0.0;

	g_mutex_lock(il->data_mutex);
	if (il->bytes_total == 0)
		{
		ret = 0.0;
		}
	else
		{
		ret = static_cast<gdouble>(il->bytes_read) / il->bytes_total;
		}
	g_mutex_unlock(il->data_mutex);
	return ret;
}

gboolean image_loader_get_is_done(ImageLoader *il)
{
	gboolean ret;
	if (!il) return FALSE;

	g_mutex_lock(il->data_mutex);
	ret = il->done;
	g_mutex_unlock(il->data_mutex);

	return ret;
}

FileData *image_loader_get_fd(ImageLoader *il)
{
	FileData *ret;
	if (!il) return nullptr;

	g_mutex_lock(il->data_mutex);
	ret = il->fd;
	g_mutex_unlock(il->data_mutex);

	return ret;
}

gboolean image_loader_get_shrunk(ImageLoader *il)
{
	gboolean ret;
	if (!il) return FALSE;

	g_mutex_lock(il->data_mutex);
	ret = il->shrunk;
	g_mutex_unlock(il->data_mutex);
	return ret;
}


/**
 *  @FIXME this can be rather slow and blocks until the size is known
 */
gboolean image_load_dimensions(FileData *fd, gint *width, gint *height)
{
	ImageLoader *il;
	gboolean success;

	il = image_loader_new(fd);

	success = image_loader_start_idle(il);

	if (success && il->pixbuf)
		{
		if (width) *width = gdk_pixbuf_get_width(il->pixbuf);
		if (height) *height = gdk_pixbuf_get_height(il->pixbuf);;
		}
	else
		{
		if (width) *width = -1;
		if (height) *height = -1;
		}

	image_loader_free(il);

	return success;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
