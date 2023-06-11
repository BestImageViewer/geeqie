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

#ifndef IMAGE_LOAD_H
#define IMAGE_LOAD_H

#define TYPE_IMAGE_LOADER		(image_loader_get_type())

using ImageLoaderBackendCbAreaPrepared = void (*)(gpointer, gpointer);
using ImageLoaderBackendCbSize = void (*)(gpointer, gint, gint, gpointer);
using ImageLoaderBackendCbAreaUpdated = void (*)(gpointer, guint, guint, guint, guint, gpointer);

using ImageLoaderBackendFuncLoaderNew = gpointer (*)(ImageLoaderBackendCbAreaUpdated, ImageLoaderBackendCbSize, ImageLoaderBackendCbAreaPrepared, gpointer);
using ImageLoaderBackendFuncSetSize = void (*)(gpointer, int, int);
using ImageLoaderBackendFuncLoad = gboolean (*)(gpointer, const guchar *, gsize, GError **); /* optional, load whole image at once */
using ImageLoaderBackendFuncWrite = gboolean (*)(gpointer, const guchar *, gsize, GError **);
using ImageLoaderBackendFuncGetPixbuf = GdkPixbuf *(*)(gpointer);
using ImageLoaderBackendFuncClose = gboolean (*)(gpointer, GError **);
using ImageLoaderBackendFuncAbort = void (*)(gpointer);
using ImageLoaderBackendFuncFree = void (*)(gpointer);
using ImageLoaderBackendFuncGetFormatName = gchar *(*)(gpointer);
using ImageLoaderBackendFuncGetFormatMimeTypes = gchar **(*)(gpointer);
using ImageLoaderBackendFuncSetPageNum = void (*)(gpointer, gint);
using ImageLoaderBackendFuncGetPageTotal = gint (*)(gpointer);

using ImageLoaderBackend = struct _ImageLoaderBackend;
struct _ImageLoaderBackend
{
	ImageLoaderBackendFuncLoaderNew loader_new;
	ImageLoaderBackendFuncSetSize set_size;
	ImageLoaderBackendFuncLoad load;
	ImageLoaderBackendFuncWrite write;
	ImageLoaderBackendFuncGetPixbuf get_pixbuf;
	ImageLoaderBackendFuncClose close;
	ImageLoaderBackendFuncAbort abort;
	ImageLoaderBackendFuncFree free;
	ImageLoaderBackendFuncGetFormatName get_format_name;
	ImageLoaderBackendFuncGetFormatMimeTypes get_format_mime_types;
	ImageLoaderBackendFuncSetPageNum set_page_num;
	ImageLoaderBackendFuncGetPageTotal get_page_total;
};

enum ImageLoaderPreview {
	IMAGE_LOADER_PREVIEW_NONE = 0,
	IMAGE_LOADER_PREVIEW_EXIF = 1,
	IMAGE_LOADER_PREVIEW_LIBRAW = 2
};


//typedef struct _ImageLoader ImageLoader;
using ImageLoaderClass = struct _ImageLoaderClass;

struct _ImageLoader
{
	GObject parent;

	/*< private >*/
	GdkPixbuf *pixbuf;
	FileData *fd;
	gchar *path;

	gsize bytes_read;
	gsize bytes_total;

	ImageLoaderPreview preview;

	gint requested_width;
	gint requested_height;

	gint actual_width;
	gint actual_height;

	gboolean shrunk;

	gboolean done;
	guint idle_id; /**< event source id */
	gint idle_priority;

	gpointer *loader;
	GError *error;
	ImageLoaderBackend backend;

	guint idle_done_id; /**< event source id */
	GList *area_param_list;
	GList *area_param_delayed_list;

	gboolean delay_area_ready;

	GMutex *data_mutex;
	gboolean stopping;
	gboolean can_destroy;
	GCond *can_destroy_cond;
	gboolean thread;

	guchar *mapped_file;
	gsize read_buffer_size;
	guint idle_read_loop_count;
};

struct _ImageLoaderClass {
	GObjectClass parent;

	/* class members */
	void (*area_ready)(ImageLoader *, guint x, guint y, guint w, guint h, gpointer);
	void (*error)(ImageLoader *, gpointer);
	void (*done)(ImageLoader *, gpointer);
	void (*percent)(ImageLoader *, gdouble, gpointer);
};

GType image_loader_get_type();

ImageLoader *image_loader_new(FileData *fd);

void image_loader_free(ImageLoader *il);

/**
 * @headerfile image_loader_delay_area_ready
 * delay area_ready signals
 */
void image_loader_delay_area_ready(ImageLoader *il, gboolean enable);

/**
 * @headerfile image_loader_set_requested_size
 * Speed up loading when you only need at most width x height size image,
 * only the jpeg GdkPixbuf loader benefits from it - so there is no
 * guarantee that the image will scale down to the requested size..
 */
void image_loader_set_requested_size(ImageLoader *il, gint width, gint height);

void image_loader_set_buffer_size(ImageLoader *il, guint size);

/**
 * @headerfile image_loader_set_priority
 * this only has effect if used before image_loader_start()
 * default is G_PRIORITY_DEFAULT_IDLE
 */
void image_loader_set_priority(ImageLoader *il, gint priority);

gboolean image_loader_start(ImageLoader *il);


GdkPixbuf *image_loader_get_pixbuf(ImageLoader *il);
gdouble image_loader_get_percent(ImageLoader *il);
gboolean image_loader_get_is_done(ImageLoader *il);
FileData *image_loader_get_fd(ImageLoader *il);
gboolean image_loader_get_shrunk(ImageLoader *il);
const gchar *image_loader_get_error(ImageLoader *il);

gboolean image_load_dimensions(FileData *fd, gint *width, gint *height);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
