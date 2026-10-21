/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "file-tree-task.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>

#include "intl.h"
#include "ui-utildlg.h"

namespace
{

struct ConflictRequest;

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
	ConflictRequest *conflict = nullptr; /**< Accessed only on the main thread */
};

struct ConflictRequest
{
	TreeTask *task;
	std::string source;
	std::string destination;
	FileTreeConflict choice;
	GenericDialog *dialog = nullptr;
	GtkWidget *entry = nullptr;
	GtkWidget *error = nullptr;
	GtkWidget *apply_to_all = nullptr;
	GtkWidget *rename_button = nullptr;
	bool folders = false;
	std::mutex mutex;
	std::condition_variable ready;
	bool resolved = false;
};

void conflict_finish(ConflictRequest *request, FileTreeConflict choice)
{
	request->task->conflict = nullptr;
	if (request->dialog) generic_dialog_close(request->dialog);
	const std::scoped_lock lock(request->mutex);
	request->choice = std::move(choice);
	request->resolved = true;
	request->ready.notify_one();
}

void conflict_cancel(GenericDialog *, gpointer data)
{
	conflict_finish(static_cast<ConflictRequest *>(data), {});
}

void conflict_skip(GenericDialog *, gpointer data)
{
	auto *request = static_cast<ConflictRequest *>(data);
	conflict_finish(request, {FileTreeConflictAction::SKIP, {},
	                         gtk_check_button_get_active(GTK_CHECK_BUTTON(request->apply_to_all)) != FALSE});
}

void conflict_rename(GenericDialog *, gpointer data)
{
	auto *request = static_cast<ConflictRequest *>(data);
	const std::string name = gtk_editable_get_text(GTK_EDITABLE(request->entry));
	if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos)
		{
		gtk_label_set_text(GTK_LABEL(request->error), _("Enter a file name without a folder path."));
		return;
		}
	conflict_finish(request, {FileTreeConflictAction::RENAME, name});
}

void conflict_duplicate(GenericDialog *, gpointer data)
{
	auto *request = static_cast<ConflictRequest *>(data);
	conflict_finish(request, {FileTreeConflictAction::DUPLICATE, {},
	                         gtk_check_button_get_active(GTK_CHECK_BUTTON(request->apply_to_all)) != FALSE});
}

void conflict_merge(GenericDialog *, gpointer data)
{
	auto *request = static_cast<ConflictRequest *>(data);
	conflict_finish(request, {FileTreeConflictAction::MERGE, {},
	                         gtk_check_button_get_active(GTK_CHECK_BUTTON(request->apply_to_all)) != FALSE});
}

gboolean conflict_show(gpointer data)
{
	auto *request = static_cast<ConflictRequest *>(data);
	if (g_cancellable_is_cancelled(request->task->cancellable))
		{
		conflict_finish(request, {});
		return G_SOURCE_REMOVE;
		}
	request->task->conflict = request;
	request->dialog = generic_dialog_new(_("Folder merge conflict"), "folder_merge_conflict",
	                                     request->task->dialog->dialog, FALSE, conflict_cancel, request);
	gtk_window_set_modal(GTK_WINDOW(request->dialog->dialog), TRUE);
	g_autofree gchar *message = g_strdup_printf(_("Source:\n%s\n\nThe destination already exists:\n%s"),
	                                           request->source.c_str(), request->destination.c_str());
	generic_dialog_add_message(request->dialog, nullptr, _("An item with this name already exists"), message, TRUE);
	request->entry = gtk_entry_new();
	g_autofree gchar *name = g_path_get_basename(request->destination.c_str());
	gtk_editable_set_text(GTK_EDITABLE(request->entry), name);
	g_signal_connect(request->entry, "activate", G_CALLBACK(+[](GtkEntry *, gpointer data)
		{
		conflict_rename(nullptr, data);
		}), request);
	gtk_box_append(GTK_BOX(request->dialog->vbox), request->entry);
	request->error = gtk_label_new("");
	gtk_box_append(GTK_BOX(request->dialog->vbox), request->error);
	request->apply_to_all = gtk_check_button_new_with_label(_("Apply this action to all files"));
	gtk_box_append(GTK_BOX(request->dialog->vbox), request->apply_to_all);
	GtkWidget *hint = gtk_label_new(request->folders ? _("Merge combines folder contents. Apply to all with Merge covers folder conflicts only; conflicting files still need a separate choice. Duplicate keeps the incoming folder separate using a name ending in (Copy).")
	                                              : _("Duplicate keeps both files using a name ending in (Copy), before the extension."));
	gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
	gtk_box_append(GTK_BOX(request->dialog->vbox), hint);
	g_signal_connect(request->apply_to_all, "toggled", G_CALLBACK(+[](GtkCheckButton *button, gpointer data)
		{
		auto *request = static_cast<ConflictRequest *>(data);
		gtk_widget_set_sensitive(request->entry, !gtk_check_button_get_active(button));
		gtk_widget_set_sensitive(request->rename_button, !gtk_check_button_get_active(button));
		gtk_label_set_text(GTK_LABEL(request->error), "");
		}), request);
	generic_dialog_add_button(request->dialog, nullptr, _("Skip"), conflict_skip, FALSE);
	request->rename_button = generic_dialog_add_button(request->dialog, nullptr, _("Rename"), conflict_rename, FALSE);
	generic_dialog_add_button(request->dialog, nullptr, _("Duplicate"), conflict_duplicate, !request->folders);
	if (request->folders) generic_dialog_add_button(request->dialog, nullptr, _("Merge"), conflict_merge, TRUE);
	gtk_window_present(GTK_WINDOW(request->dialog->dialog));
	gtk_widget_grab_focus(request->entry);
	return G_SOURCE_REMOVE;
}

FileTreeConflict task_conflict(TreeTask *task, GFile *source, GFile *destination)
{
	g_autofree gchar *source_name = g_file_get_parse_name(source);
	g_autofree gchar *destination_name = g_file_get_parse_name(destination);
	ConflictRequest request{};
	request.task = task;
	request.source = source_name;
	request.destination = destination_name;
	request.folders = g_file_query_file_type(source, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, task->cancellable) == G_FILE_TYPE_DIRECTORY &&
	                  g_file_query_file_type(destination, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, task->cancellable) == G_FILE_TYPE_DIRECTORY;
	std::unique_lock<std::mutex> lock(request.mutex);
	g_idle_add(conflict_show, &request);
	request.ready.wait(lock, [&request] { return request.resolved; });
	return request.choice;
}

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
	if (task->conflict && g_cancellable_is_cancelled(task->cancellable)) conflict_finish(task->conflict, {});
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
	operation.conflict = [task](GFile *source, GFile *destination) { return task_conflict(task, source, destination); };
	operation.confirm_merge = true;
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
