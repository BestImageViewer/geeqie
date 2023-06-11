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
#include "image-load.h"
#include "image-load-cr3.h"
#include "image-load-gdk.h"
#include "image-load-jpeg.h"
#include "image-load-tiff.h"
#include "image-load-dds.h"
#include "image-load-djvu.h"
#include "image-load-external.h"
#include "image-load-pdf.h"
#include "image-load-psd.h"
#include "image-load-heif.h"
#include "image-load-ffmpegthumbnailer.h"
#include "image-load-collection.h"
#include "image-load-zxscr.h"
#include "image-load-j2k.h"
#include "image-load-jpegxl.h"
#include "image-load-libraw.h"
#include "image-load-svgz.h"
#include "misc.h"

#include "exif.h"
#include "filedata.h"
#include "ui-fileops.h"
#include "gq-marshal.h"

#include <fcntl.h>
#include <sys/mman.h>

#define IMAGE_LOADER_READ_BUFFER_SIZE_DEFAULT 	4096
#define IMAGE_LOADER_IDLE_READ_LOOP_COUNT_DEFAULT 	1

/* image loader class */


enum {
	SIGNAL_AREA_READY = 0,
	SIGNAL_ERROR,
	SIGNAL_DONE,
	SIGNAL_PERCENT,
	SIGNAL_SIZE,
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
	static GType type = 0;
	if (type == 0)
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
		type = g_type_register_static(G_TYPE_OBJECT, "ImageLoaderType", &info, GTypeFlags(0));
		}
	return type;
}

static void image_loader_init(GTypeInstance *instance, gpointer UNUSED(g_class))
{
	auto il = reinterpret_cast<ImageLoader *>(instance);

	il->pixbuf = nullptr;
	il->idle_id = 0;
	il->idle_priority = G_PRIORITY_DEFAULT_IDLE;
	il->done = FALSE;
	il->loader = nullptr;

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

static void image_loader_class_init_wrapper(void *data, void *UNUSED(user_data))
{
	image_loader_class_init(static_cast<ImageLoaderClass *>(data));
}

static void image_loader_class_init(ImageLoaderClass *loader_class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (loader_class);

//	gobject_class->set_property = image_loader_set_property;
//	gobject_class->get_property = image_loader_get_property;

	gobject_class->finalize = image_loader_finalize;


	signals[SIGNAL_AREA_READY] =
		g_signal_new("area_ready",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(ImageLoaderClass, area_ready),
			     nullptr, nullptr,
			     gq_marshal_VOID__INT_INT_INT_INT,
			     G_TYPE_NONE, 4,
			     G_TYPE_INT,
			     G_TYPE_INT,
			     G_TYPE_INT,
			     G_TYPE_INT);

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

	signals[SIGNAL_SIZE] =
		g_signal_new("size_prepared",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(ImageLoaderClass, area_ready),
			     nullptr, nullptr,
			     gq_marshal_VOID__INT_INT,
			     G_TYPE_NONE, 2,
			     G_TYPE_INT,
			     G_TYPE_INT);

}

static void image_loader_finalize(GObject *object)
{
	auto il = reinterpret_cast<ImageLoader *>(object);

	image_loader_stop(il);

	if (il->error) DEBUG_1("%s", image_loader_get_error(il));

	DEBUG_1("freeing image loader %p bytes_read=%" G_GSIZE_FORMAT, (void *)il, il->bytes_read);

	if (il->idle_done_id)
		{
		g_source_remove(il->idle_done_id);
		il->idle_done_id = 0;
		}

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
	guint x;
	guint y;
	guint w;
	guint h;
};


static gboolean image_loader_emit_area_ready_cb(gpointer data)
{
	auto par = static_cast<ImageLoaderAreaParam *>(data);
	ImageLoader *il = par->il;
	guint x, y, w, h;
	g_mutex_lock(il->data_mutex);
	il->area_param_list = g_list_remove(il->area_param_list, par);
	x = par->x;
	y = par->y;
	w = par->w;
	h = par->h;
	g_free(par);
	g_mutex_unlock(il->data_mutex);

	g_signal_emit(il, signals[SIGNAL_AREA_READY], 0, x, y, w, h);

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

static gboolean image_loader_emit_size_cb(gpointer data)
{
	gint width, height;
	auto il = static_cast<ImageLoader *>(data);
	g_mutex_lock(il->data_mutex);
	width = il->actual_width;
	height = il->actual_height;
	g_mutex_unlock(il->data_mutex);
	g_signal_emit(il, signals[SIGNAL_SIZE], 0, width, height);
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

static void image_loader_emit_size(ImageLoader *il)
{
	g_idle_add_full(G_PRIORITY_HIGH, image_loader_emit_size_cb, il, nullptr);
}

static ImageLoaderAreaParam *image_loader_queue_area_ready(ImageLoader *il, GList **list, guint x, guint y, guint w, guint h)
{
	if (*list)
		{
		auto prev_par = static_cast<ImageLoaderAreaParam *>((*list)->data);
		if (prev_par->x == x && prev_par->w == w &&
		    prev_par->y + prev_par->h == y)
			{
			/* we can merge the notifications */
			prev_par->h += h;
			return nullptr;
			}
		if (prev_par->x == x && prev_par->w == w &&
		    y + h == prev_par->y)
			{
			/* we can merge the notifications */
			prev_par->h += h;
			prev_par->y = y;
			return nullptr;
			}
		if (prev_par->y == y && prev_par->h == h &&
		    prev_par->x + prev_par->w == x)
			{
			/* we can merge the notifications */
			prev_par->w += w;
			return nullptr;
			}
		if (prev_par->y == y && prev_par->h == h &&
		    x + w == prev_par->x)
			{
			/* we can merge the notifications */
			prev_par->w += w;
			prev_par->x = x;
			return nullptr;
			}
		}

	auto par = g_new0(ImageLoaderAreaParam, 1);
	par->il = il;
	par->x = x;
	par->y = y;
	par->w = w;
	par->h = h;

	*list = g_list_prepend(*list, par);
	return par;
}

/* this function expects that il->data_mutex is locked by caller */
static void image_loader_emit_area_ready(ImageLoader *il, guint x, guint y, guint w, guint h)
{
	ImageLoaderAreaParam *par = image_loader_queue_area_ready(il, &il->area_param_list, x, y, w, h);

	if (par)
		{
		g_idle_add_full(G_PRIORITY_HIGH, image_loader_emit_area_ready_cb, par, nullptr);
		}
}

/**************************************************************************************/
/* the following functions may be executed in separate thread */

/* this function expects that il->data_mutex is locked by caller */
static void image_loader_queue_delayed_area_ready(ImageLoader *il, guint x, guint y, guint w, guint h)
{
	image_loader_queue_area_ready(il, &il->area_param_delayed_list, x, y, w, h);
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

	if (!il->loader)
		{
		g_mutex_unlock(il->data_mutex);
		return;
		}

	pb = il->backend.get_pixbuf(il->loader);

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

static void image_loader_area_updated_cb(gpointer UNUSED(loader),
				 guint x, guint y, guint w, guint h,
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
		image_loader_queue_delayed_area_ready(il, x, y, w, h);
	else
		image_loader_emit_area_ready(il, x, y, w, h);

	if (il->stopping) il->backend.abort(il->loader);

	g_mutex_unlock(il->data_mutex);
}

static void image_loader_area_prepared_cb(gpointer loader, gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	GdkPixbuf *pb;
	guchar *pix;
	size_t h, rs;

	/* a workaround for
	   https://bugzilla.gnome.org/show_bug.cgi?id=547669
	   https://bugzilla.gnome.org/show_bug.cgi?id=589334
	*/
	gchar *format = il->backend.get_format_name(loader);
	if (strcmp(format, "svg") == 0 ||
	    strcmp(format, "xpm") == 0)
		{
		g_free(format);
		return;
		}

	g_free(format);

	pb = il->backend.get_pixbuf(loader);

	h = gdk_pixbuf_get_height(pb);
	rs = gdk_pixbuf_get_rowstride(pb);
	pix = gdk_pixbuf_get_pixels(pb);

	memset(pix, 0, rs * h); /*this should be faster than pixbuf_fill */

}

static void image_loader_size_cb(gpointer loader,
				 gint width, gint height, gpointer data)
{
	auto il = static_cast<ImageLoader *>(data);
	gchar **mime_types;
	gboolean scale = FALSE;
	gint n;

	g_mutex_lock(il->data_mutex);
	il->actual_width = width;
	il->actual_height = height;
	if (il->requested_width < 1 || il->requested_height < 1)
		{
		g_mutex_unlock(il->data_mutex);
		image_loader_emit_size(il);
		return;
		}
	g_mutex_unlock(il->data_mutex);

#ifdef HAVE_FFMPEGTHUMBNAILER
	if (il->fd->format_class == FORMAT_CLASS_VIDEO)
		scale = TRUE;
#endif
	mime_types = il->backend.get_format_mime_types(loader);
	n = 0;
	while (mime_types[n] && !scale)
		{
		if (strstr(mime_types[n], "jpeg")) scale = TRUE;
		n++;
		}
	g_strfreev(mime_types);

	if (!scale)
		{
		image_loader_emit_size(il);
		return;
		}

	g_mutex_lock(il->data_mutex);

	gint nw, nh;
	if (width > il->requested_width || height > il->requested_height)
		{

		if ((static_cast<gdouble>(il->requested_width) / width) < (static_cast<gdouble>(il->requested_height) / height))
			{
			nw = il->requested_width;
			nh = static_cast<gdouble>(nw) / width * height;
			if (nh < 1) nh = 1;
			}
		else
			{
			nh = il->requested_height;
			nw = static_cast<gdouble>(nh) / height * width;
			if (nw < 1) nw = 1;
			}

		il->actual_width = nw;
		il->actual_height = nh;
		il->backend.set_size(loader, nw, nh);
		il->shrunk = TRUE;
		}

	g_mutex_unlock(il->data_mutex);
	image_loader_emit_size(il);
}

static void image_loader_stop_loader(ImageLoader *il)
{
	if (!il) return;

	if (il->loader)
		{
		/* some loaders do not have a pixbuf till close, order is important here */
		il->backend.close(il->loader, il->error ? nullptr : &il->error); /* we are interested in the first error only */
		image_loader_sync_pixbuf(il);
		il->backend.free(il->loader);
		il->loader = nullptr;
		}
	g_mutex_lock(il->data_mutex);
	il->done = TRUE;
	g_mutex_unlock(il->data_mutex);
}

static void image_loader_setup_loader(ImageLoader *il)
{
#if defined HAVE_TIFF || defined HAVE_PDF || defined HAVE_HEIF || defined HAVE_DJVU
	gchar *format;
#endif

	gint external_preview = 1;

	g_mutex_lock(il->data_mutex);

	if (options->external_preview.enable)
		{
		gchar *cmd_line;
		gchar *tilde_filename;

		tilde_filename = expand_tilde(options->external_preview.select);

		cmd_line = g_strdup_printf("\"%s\" \"%s\"" , tilde_filename, il->fd->path);

		external_preview = runcmd(cmd_line);
		g_free(cmd_line);
		g_free(tilde_filename);
		}

	if (external_preview == 0)
		{
		DEBUG_1("Using custom external loader");
		image_loader_backend_set_external(&il->backend);
		}
	else

#ifdef HAVE_FFMPEGTHUMBNAILER
	if (il->fd->format_class == FORMAT_CLASS_VIDEO)
		{
		DEBUG_1("Using custom ffmpegthumbnailer loader");
		image_loader_backend_set_ft(&il->backend);
		}
	else
#endif
#ifdef HAVE_PDF
	if (il->bytes_total >= 4 &&
	    (memcmp(il->mapped_file + 0, "%PDF", 4) == 0))
		{
		DEBUG_1("Using custom pdf loader");
		image_loader_backend_set_pdf(&il->backend);
		}
	else
#endif
#ifdef HAVE_HEIF
	if (il->bytes_total >= 12 &&
		((memcmp(il->mapped_file + 4, "ftypheic", 8) == 0) ||
		(memcmp(il->mapped_file + 4, "ftypheix", 8) == 0) ||
		(memcmp(il->mapped_file + 4, "ftypmsf1", 8) == 0) ||
		(memcmp(il->mapped_file + 4, "ftypmif1", 8) == 0) ||
		(memcmp(il->mapped_file + 4, "ftypavif", 8) == 0)))
		{
		DEBUG_1("Using custom heif loader");
		image_loader_backend_set_heif(&il->backend);
		}
	else
#endif
#ifdef HAVE_DJVU
	if (il->bytes_total >= 16 &&
		(memcmp(il->mapped_file, "AT&TFORM", 8) == 0) &&
		(memcmp(il->mapped_file + 12, "DJV", 3) == 0))
		{
		DEBUG_1("Using custom djvu loader");
		image_loader_backend_set_djvu(&il->backend);
		}
	else
#endif
#ifdef HAVE_JPEG
	if (il->bytes_total >= 2 && il->mapped_file[0] == 0xff && il->mapped_file[1] == 0xd8)
		{
		DEBUG_1("Using custom jpeg loader");
		image_loader_backend_set_jpeg(&il->backend);
		}
#ifndef HAVE_RAW
	else
	if (il->bytes_total >= 11 &&
		(memcmp(il->mapped_file + 4, "ftypcrx", 7) == 0) &&
		(memcmp(il->mapped_file + 64, "CanonCR3", 8) == 0))
		{
		DEBUG_1("Using custom cr3 loader");
		image_loader_backend_set_cr3(&il->backend);
		}
#endif
	else
#endif
#ifdef HAVE_TIFF
	if (il->bytes_total >= 10 &&
	    (memcmp(il->mapped_file, "MM\0*", 4) == 0 ||
	     memcmp(il->mapped_file, "MM\0+\0\x08\0\0", 8) == 0 ||
	     memcmp(il->mapped_file, "II+\0\x08\0\0\0", 8) == 0 ||
	     memcmp(il->mapped_file, "II*\0", 4) == 0))
	     	{
		DEBUG_1("Using custom tiff loader");
		image_loader_backend_set_tiff(&il->backend);
		}
	else
#endif
	if (il->bytes_total >= 3 && il->mapped_file[0] == 0x44 && il->mapped_file[1] == 0x44 && il->mapped_file[2] == 0x53)
		{
		DEBUG_1("Using dds loader");
		image_loader_backend_set_dds(&il->backend);
		}
	else
	if (il->bytes_total >= 6 &&
		(memcmp(il->mapped_file, "8BPS\0\x01", 6) == 0))
		{
		DEBUG_1("Using custom psd loader");
		image_loader_backend_set_psd(&il->backend);
		}
	else
#ifdef HAVE_J2K
	if (il->bytes_total >= 12 &&
		(memcmp(il->mapped_file, "\0\0\0\x0CjP\x20\x20\x0D\x0A\x87\x0A", 12) == 0))
		{
		DEBUG_1("Using custom j2k loader");
		image_loader_backend_set_j2k(&il->backend);
		}
	else
#endif
#ifdef HAVE_JPEGXL
	if (il->bytes_total >= 12 &&
		(memcmp(il->mapped_file, "\0\0\0\x0C\x4A\x58\x4C\x20\x0D\x0A\x87\x0A", 12) == 0))
		{
		DEBUG_1("Using custom jpeg xl loader");
		image_loader_backend_set_jpegxl(&il->backend);
		}
	else
	if (il->bytes_total >= 2 &&
		(memcmp(il->mapped_file, "\xFF\x0A", 2) == 0))
		{
		DEBUG_1("Using custom jpeg xl loader");
		image_loader_backend_set_jpegxl(&il->backend);
		}
	else
#endif
	if ((il->bytes_total == 6144 || il->bytes_total == 6912) &&
		(file_extension_match(il->fd->path, ".scr")))
		{
		DEBUG_1("Using custom zxscr loader");
		image_loader_backend_set_zxscr(&il->backend);
		}
	else
	if (il->fd->format_class == FORMAT_CLASS_COLLECTION)
		{
		DEBUG_1("Using custom collection loader");
		image_loader_backend_set_collection(&il->backend);
		}
	else
	if (g_strcmp0(strrchr(il->fd->path, '.'), ".svgz") == 0)
		{
		DEBUG_1("Using custom svgz loader");
		image_loader_backend_set_svgz(&il->backend);
		}
	else
		image_loader_backend_set_default(&il->backend);

	il->loader = static_cast<void **>(il->backend.loader_new(image_loader_area_updated_cb, image_loader_size_cb, image_loader_area_prepared_cb, il));

#ifdef HAVE_TIFF
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "tiff") == 0)
		{
		il->backend.set_page_num(il->loader, il->fd->page_num);
		}
	g_free(format);
#endif

#ifdef HAVE_PDF
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "pdf") == 0)
		{
		il->backend.set_page_num(il->loader, il->fd->page_num);
		}
	g_free(format);
#endif

#ifdef HAVE_HEIF
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "heif") == 0)
		{
		il->backend.set_page_num(il->loader, il->fd->page_num);
		}
	g_free(format);
#endif

#ifdef HAVE_DJVU
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "djvu") == 0)
		{
		il->backend.set_page_num(il->loader, il->fd->page_num);
		}
	g_free(format);
#endif

	il->fd->format_name = il->backend.get_format_name(il->loader);

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
	gint b;
	gint c;

	if (!il) return G_SOURCE_REMOVE;

	c = il->idle_read_loop_count ? il->idle_read_loop_count : 1;
	while (c > 0 && !image_loader_get_stopping(il))
		{
		b = MIN(il->read_buffer_size, il->bytes_total - il->bytes_read);

		if (b == 0)
			{
			image_loader_done(il);
			return G_SOURCE_REMOVE;
			}

		if (b < 0 || (b > 0 && !il->backend.write(il->loader, il->mapped_file + il->bytes_read, b, &il->error)))
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
#if defined HAVE_TIFF || defined HAVE_PDF || defined HAVE_HEIF || defined HAVE_DJVU
	gchar *format;
#endif
	gssize b;

	if (il->pixbuf) return FALSE;

	b = MIN(il->read_buffer_size, il->bytes_total - il->bytes_read);
	if (b < 1) return FALSE;

	image_loader_setup_loader(il);

	g_assert(il->bytes_read == 0);
	if (il->backend.load) {
		b = il->bytes_total;
		if (!il->backend.load(il->loader, il->mapped_file, b, &il->error))
			{
			image_loader_stop_loader(il);
			return FALSE;
			}
	}
	else if (!il->backend.write(il->loader, il->mapped_file, b, &il->error))
		{
		image_loader_stop_loader(il);
		return FALSE;
		}

#ifdef HAVE_PDF
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "pdf") == 0)
		{
		gint i = il->backend.get_page_total(il->loader);
		file_data_set_page_total(il->fd, i);
		}
	g_free(format);
#endif
#ifdef HAVE_HEIF
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "heif") == 0)
		{
		gint i = il->backend.get_page_total(il->loader);
		file_data_set_page_total(il->fd, i);
		}
	g_free(format);
#endif
#ifdef HAVE_DJVU
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "djvu") == 0)
		{
		gint i = il->backend.get_page_total(il->loader);
		file_data_set_page_total(il->fd, i);
		}
	g_free(format);
#endif
#ifdef HAVE_TIFF
	format = il->backend.get_format_name(il->loader);
	if (g_strcmp0(format, "tiff") == 0)
		{
		gint i = il->backend.get_page_total(il->loader);
		file_data_set_page_total(il->fd, i);
		}
	g_free(format);
#endif

	il->bytes_read += b;

	/* read until size is known */
	while (il->loader && !il->backend.get_pixbuf(il->loader) && b > 0 && !image_loader_get_stopping(il))
		{
		b = MIN(il->read_buffer_size, il->bytes_total - il->bytes_read);
		if (b < 0 || (b > 0 && !il->backend.write(il->loader, il->mapped_file + il->bytes_read, b, &il->error)))
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
	struct stat st;
	gchar *pathl;

	if (!il || il->loader || il->mapped_file) return FALSE;

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
			il->mapped_file = libraw_get_preview(il, reinterpret_cast<guint *>(&il->bytes_total));

			if (il->mapped_file)
				{
				/* Both exiv2 and libraw sometimes return a pointer to a file
				 * section that is not a jpeg */
				if (!(il->mapped_file[0] == 0xFF && il->mapped_file[1] == 0xD8))
					{
					il->mapped_file = nullptr;
					}
				else
					{
					il->preview = IMAGE_LOADER_PREVIEW_LIBRAW;
					}
				}
			}

		/* If libraw does not find a thumbnail, try exiv2 */
		if (!il->mapped_file)
			{
			il->mapped_file = exif_get_preview(exif, reinterpret_cast<guint *>(&il->bytes_total), 0, 0); /* get the largest available preview image or NULL for normal images*/

			if (il->mapped_file)
				{
				/* Both exiv2 and libraw sometimes return a pointer to a file
				 * section that is not a jpeg */
				if (!(il->mapped_file[0] == 0xFF && il->mapped_file[1] == 0xD8))
					{
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
		gint load_fd;

		pathl = path_from_utf8(il->fd->path);
		load_fd = open(pathl, O_RDONLY | O_NONBLOCK);
		g_free(pathl);
		if (load_fd == -1) return FALSE;

		if (fstat(load_fd, &st) == 0)
			{
			il->bytes_total = st.st_size;
			}
		else
			{
			close(load_fd);
			return FALSE;
			}

		il->mapped_file = static_cast<guchar *>(mmap(nullptr, il->bytes_total, PROT_READ|PROT_WRITE, MAP_PRIVATE, load_fd, 0));
		close(load_fd);
		if (il->mapped_file == MAP_FAILED)
			{
			il->mapped_file = nullptr;
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

	if (il->idle_id)
		{
		g_source_remove(il->idle_id);
		il->idle_id = 0;
		}

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

void image_loader_delay_area_ready(ImageLoader *il, gboolean enable)
{
	g_mutex_lock(il->data_mutex);
	il->delay_area_ready = enable;
	if (!enable)
		{
		/* send delayed */
		GList *list, *work;
		list = g_list_reverse(il->area_param_delayed_list);
		il->area_param_delayed_list = nullptr;
		g_mutex_unlock(il->data_mutex);

		work = list;

		while (work)
			{
			auto par = static_cast<ImageLoaderAreaParam *>(work->data);
			work = work->next;

			g_signal_emit(il, signals[SIGNAL_AREA_READY], 0, par->x, par->y, par->w, par->h);
			g_free(par);
			}
		g_list_free(list);
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
	gboolean ret = G_SOURCE_REMOVE;
	auto il = static_cast<ImageLoader *>(data);

	if (il->idle_id)
		{
		ret = image_loader_continue(il);
		}

	if (!ret)
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


static void image_loader_thread_run(gpointer data, gpointer UNUSED(user_data))
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
	DEBUG_1("Thread pool num threads: %d", g_thread_pool_get_num_threads(image_loader_thread_pool));

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

const gchar *image_loader_get_error(ImageLoader *il)
{
	const gchar *ret = nullptr;
	if (!il) return nullptr;
	g_mutex_lock(il->data_mutex);
	if (il->error) ret = il->error->message;
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
