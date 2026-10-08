/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef METADATA_TEMPLATE_H
#define METADATA_TEMPLATE_H

#include <glib.h>
#include <gtk/gtk.h>

// Takes ownership of the referenced file list.
void metadata_template_dialog(GtkWidget *parent, GList *files);

#endif
