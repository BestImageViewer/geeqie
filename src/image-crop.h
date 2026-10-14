/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IMAGE_CROP_H
#define IMAGE_CROP_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

class FileData;
struct ImageWindow;

/** @brief Copy a crop in source pixel coordinates and apply orientation. The caller owns the returned pixbuf. */
GdkPixbuf *image_crop_pixbuf(GdkPixbuf *source, const GdkRectangle &rectangle, gint orientation, GError **error);
/** @brief Atomically save a JPEG or PNG copy, refusing to replace the source or its links. */
gboolean image_crop_save(FileData *source, GdkPixbuf *crop, const gchar *destination,
                         gboolean png, gint quality, const gchar *profile, gboolean metadata, GError **error);
void image_crop_dialog_show(ImageWindow *imd, GtkWidget *parent);

#endif
