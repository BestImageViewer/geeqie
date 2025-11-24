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

#include <memory>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>

class FileData;
struct GqSize;

#define TYPE_IMAGE_LOADER		(image_loader_get_type())

struct ImageLoaderBackend
{
public:
	virtual ~ImageLoaderBackend() = default;

	using AreaUpdatedCb = void (*)(gpointer, gint, gint, gint, gint, gpointer);
	using SizePreparedCb = void (*)(gpointer, gint, gint, gpointer);

	virtual void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, gpointer data) = 0;
	virtual void set_size(int /*width*/, int /*height*/) {};
	virtual gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) = 0;
	virtual GdkPixbuf *get_pixbuf() = 0;
	virtual gboolean close(GError **/*error*/) { return TRUE; };
	virtual void abort() {};
	virtual gchar *get_format_name() = 0;
	virtual gchar **get_format_mime_types() = 0;
	virtual void set_page_num(gint /*page_num*/) {};
	virtual gint get_page_total() { return 0; };
};

enum ImageLoaderPreview {
	IMAGE_LOADER_PREVIEW_NONE = 0,
	IMAGE_LOADER_PREVIEW_EXIF = 1,
	IMAGE_LOADER_PREVIEW_LIBRAW = 2
};


struct ImageLoader
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

	GError *error;
	std::unique_ptr<ImageLoaderBackend> backend;

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

struct ImageLoaderClass {
	GObjectClass parent;

	/* class members */
	void (*area_ready)(ImageLoader *, const GdkRectangle *area, gpointer);
	void (*error)(ImageLoader *, gpointer);
	void (*done)(ImageLoader *, gpointer);
	void (*percent)(ImageLoader *, gdouble, gpointer);
	void (*size_prepared)(ImageLoader *, const GqSize *size, gpointer);
};

GType image_loader_get_type();

ImageLoader *image_loader_new(FileData *fd);

void image_loader_free(ImageLoader *il);

void image_loader_delay_area_ready(ImageLoader *il, gboolean enable);

void image_loader_set_requested_size(ImageLoader *il, gint width, gint height);

void image_loader_set_buffer_size(ImageLoader *il, guint count);

void image_loader_set_priority(ImageLoader *il, gint priority);

gboolean image_loader_start(ImageLoader *il);


GdkPixbuf *image_loader_get_pixbuf(ImageLoader *il);
gdouble image_loader_get_percent(ImageLoader *il);
gboolean image_loader_get_is_done(ImageLoader *il);
FileData *image_loader_get_fd(ImageLoader *il);
gboolean image_loader_get_shrunk(ImageLoader *il);

gboolean image_load_dimensions(FileData *fd, gint *width, gint *height);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
