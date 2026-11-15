/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef CLIPBOARD_IMAGE_H
#define CLIPBOARD_IMAGE_H

#include <gdk/gdk.h>
#include <gio/gio.h>

struct LayoutWindow;

/** Encode an immutable texture and atomically save a PNG on a worker thread. */
void clipboard_image_save_async(GdkTexture *texture, GFile *file, GCancellable *cancellable,
                                GAsyncReadyCallback callback, gpointer data);
/** Finish clipboard_image_save_async(), returning false and an error on failure. */
gboolean clipboard_image_save_finish(GdkTexture *texture, GAsyncResult *result, GError **error);
/** Read clipboard pixels, ask for a destination, and display the saved image. */
void clipboard_image_dialog_show(LayoutWindow *lw);

#endif
