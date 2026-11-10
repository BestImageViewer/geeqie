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

/** @file image-load.h
 * @ingroup image_loading
 */

#ifndef IMAGE_LOAD_H
#define IMAGE_LOAD_H

#include <memory>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

class FileData;
struct GqSize;

/** @addtogroup image_loading
 * @{
 */

#define TYPE_IMAGE_LOADER		(image_loader_get_type())

/** Decoder interface called by ImageLoader, normally from its worker thread.
 * Backend callbacks report decoder progress to the loader, not directly to GTK.
 * get_pixbuf() returns borrowed backend pixels; the loader takes its own reference
 * when synchronizing them. get_format_name() and get_format_mime_types() return
 * allocated values for g_free() and g_strfreev(), respectively.
 * Implementations must support the loader's cooperative stopping checks; a
 * blocking decoder call can delay cancellation and finalization.
 */
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


/**
 * @brief GObject owning a FileData reference and an asynchronous decoding job
 *
 * Create, start, connect signals, and release the loader on the GTK main thread.
 * Decoding normally uses a worker. area_ready, size_prepared, percent, done, and
 * error are delivered through the default main context. Rectangle/size pointers
 * in signal arguments are borrowed for that signal emission only.
 * image_loader_delay_area_ready() can also emit area_ready synchronously.
 *
 * The caller retains a reference while work is pending. Releasing the last
 * reference stops the job, cancels a deferred start and external extraction,
 * waits for active decoder work, and removes queued signal sources. Cancellation
 * by finalization does not promise a final done/error signal. A retained extra
 * reference can keep the loader and its signals alive after a view closes:
 * disconnect handlers using that view's data before destroying the view.
 * @see ownership_and_async
 */
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
	GCancellable *cancellable;
	gchar *external_select;
	gchar *external_extract;
	gboolean stopping;
	gboolean can_destroy;
	GCond *can_destroy_cond;
	gboolean thread;

	guchar *input_buffer; /**< Owned encoded data, independent of source-file changes. */
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

/** @brief Create a loader without starting it.
 * @param fd Borrowed source; the loader adds its own reference
 * @return Owned loader reference for g_object_unref(), or nullptr for a null source
 */
ImageLoader *image_loader_new(FileData *fd);

/** @brief Queue area updates, or emit all delayed updates before returning.
 * @param il Borrowed loader, kept alive by the caller throughout this call
 * @param enable TRUE to delay; FALSE to synchronously flush queued area_ready signals
 */
void image_loader_delay_area_ready(ImageLoader *il, gboolean enable);

void image_loader_set_requested_size(ImageLoader *il, gint width, gint height);

void image_loader_set_buffer_size(ImageLoader *il, guint count);

void image_loader_set_priority(ImageLoader *il, gint priority);

/** Start a new loader's worker; TRUE means work was submitted, not decoded.
 * Startup or decoding failures in the worker are reported by the error signal.
 * Connect handlers before calling this function. Do not start a loader twice.
 */
gboolean image_loader_start(ImageLoader *il);

/** Schedule a main-context start, allowing later selections to supersede it.
 * Returns FALSE if no start can be scheduled. A failure after scheduling emits
 * error. Finalization removes the pending start; retain the loader until then.
 */
gboolean image_loader_start_deferred(ImageLoader *il);


/** Borrow the current pixels, possibly nullptr or still being decoded.
 * Take a g_object_ref() to retain the pixbuf beyond the loader's lifetime. A
 * reference preserves lifetime, not immutability: pixels may change while the
 * worker decodes. Do not treat a nonnull pixbuf as proof of successful completion.
 */
GdkPixbuf *image_loader_get_pixbuf(ImageLoader *il);
gdouble image_loader_get_percent(ImageLoader *il);
gboolean image_loader_get_is_done(ImageLoader *il);
/** Borrow the source FileData; add a reference if it must outlive the loader. */
FileData *image_loader_get_fd(ImageLoader *il);
gboolean image_loader_get_shrunk(ImageLoader *il);
/** Return an owned error copy for g_error_free(), or nullptr if no error is set. */
GError *image_loader_dup_error(ImageLoader *il);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(ImageLoader, g_object_unref)

/** Synchronously load enough of a borrowed file to determine its dimensions.
 * This can block the caller; it is not the asynchronous ImageLoader interface.
 */
gboolean image_load_dimensions(FileData *fd, GqSize &dimensions);

void free_pixels(guchar *pixels, gpointer data);

/** @} */

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
