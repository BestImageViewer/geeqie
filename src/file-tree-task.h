/* SPDX-License-Identifier: GPL-2.0-or-later */

/** @file file-tree-task.h
 * @ingroup file_operations
 */

#ifndef FILE_TREE_TASK_H
#define FILE_TREE_TASK_H

#include <functional>

#include <gtk/gtk.h>

#include "ui-fileops.h"

/** @addtogroup file_operations
 * @{
 */

/** Worker-thread function. Borrow operation/error-out arguments only for this call.
 * Do not access GTK or unprotected mutable FileData/view state. Return TRUE for
 * success; on failure, return FALSE and optionally set an owned GError through
 * the error-out pointer. Observe the operation's GCancellable cooperatively.
 */
using FileTreeWork = std::function<gboolean(FileTreeOperation *, GError **)>;
/** Main-context completion after the worker returns and its progress dialog closes.
 * The error is borrowed for the invocation only (nullptr on success); copy it
 * with g_error_copy() to retain it. Completion still runs after cancellation.
 */
using FileTreeDone = std::function<void(gboolean, const GError *)>;

/**
 * @brief Run filesystem work with a progress dialog and cooperative cancellation
 * @param title Borrowed dialog title, consumed by dialog creation during this call
 * @param parent Borrowed presentation parent; its closure does not cancel this task
 * @param work Worker function retained by value until task teardown
 * @param done Completion function retained by value until task teardown
 * @param read_only Allow GTask cancellation to override the result for a read-only scan
 *
 * Call from the GTK main thread with nonempty work/done functions and a running
 * main loop. Closing/cancelling the progress dialog requests cancellation but
 * keeps it alive until the worker returns. done runs in the creating thread's
 * thread-default main context. Captured state must survive until completion;
 * a reference to a widget does not preserve its separately allocated view data.
 *
 * Mutating work reports its actual result, so cancellation may still complete
 * successfully (for example after an atomic move). Failure/cancellation does
 * not imply rollback: consult the specific filesystem helper's contract.
 * @see ownership_and_async
 */
void file_tree_task_run(const gchar *title, GtkWidget *parent, FileTreeWork work, FileTreeDone done, gboolean read_only = FALSE);

/** @} */

#endif
