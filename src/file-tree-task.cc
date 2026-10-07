/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "file-tree-task.h"

#include <mutex>
#include <string>
#include <utility>

#include "intl.h"
#include "ui-utildlg.h"

namespace
{

struct TreeTask
{
	FileTreeWork work;
	FileTreeDone done;
	GCancellable *cancellable;
	GenericDialog *dialog;
	GtkWidget *label;
	GtkWidget *bar;
	guint timer;
	std::mutex mutex;
	std::string current;
	FileTreeOperation *operation;
};

void task_progress(GFile *file, gpointer data)
{
	auto *task = static_cast<TreeTask *>(data);
	g_autofree gchar *path = g_file_get_parse_name(file);
	const std::scoped_lock lock(task->mutex);
	const gchar *format = _("Checking:\n%s");
	if (task->operation->phase == FileTreePhase::COPY) format = _("Copying:\n%s");
	if (task->operation->phase == FileTreePhase::REMOVE) format = _("Removing:\n%s");
	g_autofree gchar *message = g_strdup_printf(format, path);
	task->current = message;
}

gboolean task_update(gpointer data)
{
	auto *task = static_cast<TreeTask *>(data);
	const std::scoped_lock lock(task->mutex);
	gtk_label_set_text(GTK_LABEL(task->label), g_cancellable_is_cancelled(task->cancellable) ? _("Cancelling…") : task->current.c_str());
	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(task->bar));
	return G_SOURCE_CONTINUE;
}

void task_cancel(GenericDialog *dialog, gpointer data)
{
	auto *task = static_cast<TreeTask *>(data);
	g_cancellable_cancel(task->cancellable);
	gtk_widget_set_sensitive(dialog->hbox, FALSE);
}

void task_worker(GTask *result, gpointer, gpointer data, GCancellable *cancellable)
{
	auto *task = static_cast<TreeTask *>(data);
	FileTreeOperation operation{cancellable, task_progress, task};
	task->operation = &operation;
	GError *error = nullptr;
	if (task->work(&operation, &error)) g_task_return_boolean(result, TRUE);
	else if (error) g_task_return_error(result, error);
	else g_task_return_new_error(result, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", _("Folder operation failed"));
}

void task_done(GObject *, GAsyncResult *result, gpointer)
{
	auto *task = static_cast<TreeTask *>(g_task_get_task_data(G_TASK(result)));
	g_source_remove(task->timer);
	generic_dialog_close(task->dialog);
	g_autoptr(GError) error = nullptr;
	const gboolean success = g_task_propagate_boolean(G_TASK(result), &error);
	task->done(success, error);
}

} // namespace

void file_tree_task_run(const gchar *title, GtkWidget *parent, FileTreeWork work, FileTreeDone done, gboolean read_only)
{
	auto *task = new TreeTask{};
	task->work = std::move(work);
	task->done = std::move(done);
	task->cancellable = g_cancellable_new();
	task->dialog = generic_dialog_new(title, "folder_operation", parent, FALSE, task_cancel, task);
	task->label = gtk_label_new(_("Preparing…"));
	gtk_label_set_ellipsize(GTK_LABEL(task->label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_size_request(task->label, 400, -1);
	gtk_box_append(GTK_BOX(task->dialog->vbox), task->label);
	task->bar = gtk_progress_bar_new();
	gtk_box_append(GTK_BOX(task->dialog->vbox), task->bar);
	task->timer = g_timeout_add(150, task_update, task);
	gtk_window_present(GTK_WINDOW(task->dialog->dialog));
	g_autoptr(GTask) result = g_task_new(nullptr, task->cancellable, task_done, nullptr);
	// Mutating work must report its actual outcome, including a successful atomic move.
	// Read-only scans can safely discard their results after cancellation.
	g_task_set_check_cancellable(result, read_only);
	g_task_set_task_data(result, task, [](gpointer data)
		{
		auto *task = static_cast<TreeTask *>(data);
		g_object_unref(task->cancellable);
		delete task;
		});
	g_task_run_in_thread(result, task_worker);
}
