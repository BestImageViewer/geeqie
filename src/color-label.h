/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef COLOR_LABEL_H
#define COLOR_LABEL_H

#include <gtk/gtk.h>

class FileData;
struct ViewFile;

constexpr int COLOR_LABEL_COUNT = 5;
constexpr int COLOR_LABEL_OTHER = 6;

const gchar *color_label_name(int index);
const gchar *color_label_title(int index);
const gchar *color_label_color(int index);
int color_label_index(const gchar *text);
const gchar *color_label_read(FileData *fd);
gboolean color_label_assign(FileData *fd, int index);
GList *color_label_filter(GList *files, guint mask);
void color_label_widget_update(GtkWidget *label, FileData *fd);
void color_label_refresh_visible(ViewFile *vf);
void color_label_cell_data(GtkTreeViewColumn *, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer column);

#endif
