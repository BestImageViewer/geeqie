/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef FILE_TREE_TASK_H
#define FILE_TREE_TASK_H

#include <functional>

#include <gtk/gtk.h>

#include "ui-fileops.h"

using FileTreeWork = std::function<gboolean(FileTreeOperation *, GError **)>;
using FileTreeDone = std::function<void(gboolean, const GError *)>;

/** Runs GIO work on a worker thread, with progress and cancellation on the main thread. */
void file_tree_task_run(const gchar *title, GtkWidget *parent, FileTreeWork work, FileTreeDone done, gboolean read_only = FALSE);

#endif
