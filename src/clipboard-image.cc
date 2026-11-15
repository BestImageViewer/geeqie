/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "clipboard-image.h"

#include <cstring>
#include <memory>

#include <gtk/gtk.h>

#include "filedata.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"

namespace
{

void save_png_thread(GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
	if (g_task_return_error_if_cancelled(task)) return;
	auto *file = G_FILE(data);
	g_autofree gchar *name = g_file_get_basename(file);
	const gchar *extension = name ? std::strrchr(name, '.') : nullptr;
	if (!extension || g_ascii_strcasecmp(extension, ".png") != 0)
		{
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
		                        "%s", _("Use a filename ending in .png for the clipboard image."));
		return;
		}
	g_autoptr(GBytes) png = gdk_texture_save_to_png_bytes(GDK_TEXTURE(source));
	if (!png)
		{
		g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
		                        "%s", _("Unable to encode the clipboard image as PNG."));
		return;
		}
	gsize length = 0;
	const auto *bytes = static_cast<const gchar *>(g_bytes_get_data(png, &length));
	g_autoptr(GError) error = nullptr;
	if (!g_file_replace_contents(file, bytes, length, nullptr, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
	                             nullptr, cancellable, &error))
		{
		g_task_return_error(task, g_steal_pointer(&error));
		return;
		}
	g_task_return_boolean(task, TRUE);
}

struct ClipboardImageDialog
{
	LayoutWindow *layout;
	GWeakRef parent{};
	gchar *directory;
	GdkTexture *texture = nullptr;
	GFile *file = nullptr;
	GCancellable *read_cancel = g_cancellable_new();

	explicit ClipboardImageDialog(LayoutWindow *lw)
		: layout(lw), directory(g_strdup(layout_get_path(lw)))
	{
		g_weak_ref_init(&parent, lw->window);
		g_signal_connect_object(lw->window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data)
			{
			g_cancellable_cancel(G_CANCELLABLE(data));
			}), read_cancel, G_CONNECT_DEFAULT);
	}

	~ClipboardImageDialog()
	{
		g_weak_ref_clear(&parent);
		g_free(directory);
		g_clear_object(&texture);
		g_clear_object(&file);
		g_object_unref(read_cancel);
	}
};

bool dialog_layout_valid(ClipboardImageDialog *dialog, GtkWidget *parent)
{
	return parent && layout_valid(&dialog->layout) && dialog->layout->window == parent;
}

void save_finished_cb(GObject *source, GAsyncResult *result, gpointer data)
{
	std::unique_ptr<ClipboardImageDialog> dialog(static_cast<ClipboardImageDialog *>(data));
	g_autoptr(GError) error = nullptr;
	const bool saved = clipboard_image_save_finish(GDK_TEXTURE(source), result, &error);
	g_autoptr(GtkWidget) parent = GTK_WIDGET(g_weak_ref_get(&dialog->parent));
	if (!dialog_layout_valid(dialog.get(), parent)) return;
	if (!saved)
		{
		warning_dialog(_("Unable to save clipboard image"), error->message, GQ_ICON_DIALOG_ERROR, parent);
		return;
		}
	g_autofree gchar *path = g_file_get_path(dialog->file);
	g_autofree gchar *utf8_path = path_to_utf8(path);
	auto fd = FileData::new_simple(utf8_path);
	layout_refresh(dialog->layout);
	layout_set_fd(dialog->layout, fd);
}

void destination_selected_cb(GFile *file, gpointer data)
{
	std::unique_ptr<ClipboardImageDialog> dialog(static_cast<ClipboardImageDialog *>(data));
	g_autoptr(GtkWidget) parent = GTK_WIDGET(g_weak_ref_get(&dialog->parent));
	if (!file || !dialog_layout_valid(dialog.get(), parent)) return;
	if (!g_file_is_native(file))
		{
		warning_dialog(_("Unable to save clipboard image"), _("Choose a local file for the clipboard image."),
		               GQ_ICON_DIALOG_ERROR, parent);
		return;
		}
	dialog->file = G_FILE(g_object_ref(file));
	// An accepted save may finish after the originating window closes.
	GdkTexture *texture = dialog->texture;
	clipboard_image_save_async(texture, file, nullptr, save_finished_cb, dialog.release());
}

void clipboard_read_cb(GObject *source, GAsyncResult *result, gpointer data)
{
	std::unique_ptr<ClipboardImageDialog> dialog(static_cast<ClipboardImageDialog *>(data));
	g_autoptr(GError) error = nullptr;
	dialog->texture = gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source), result, &error);
	g_autoptr(GtkWidget) parent = GTK_WIDGET(g_weak_ref_get(&dialog->parent));
	if (!dialog_layout_valid(dialog.get(), parent)) return;
	if (!dialog->texture)
		{
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) return;
		const gchar *message = !error || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)
		                     ? _("The clipboard does not contain an image.") : error->message;
		warning_dialog(_("Unable to read clipboard image"), message, GQ_ICON_DIALOG_ERROR, parent);
		return;
		}
	g_autoptr(GDateTime) now = g_date_time_new_now_local();
	g_autofree gchar *name = g_date_time_format(now, "clipboard-%Y%m%d-%H%M%S.png"); // NOTRANSLATE: Default filename.
	FileDialogData chooser{};
	chooser.action = FileDialogAction::SAVE;
	chooser.callback = destination_selected_cb;
	chooser.parent = GTK_WINDOW(parent);
	chooser.destroy_with_parent = TRUE;
	chooser.title = _("Save Clipboard Image");
	chooser.accept_text = _("Save");
	chooser.filename = dialog->directory;
	chooser.suggested_name = name;
	chooser.filter = "*.png;*.PNG";
	chooser.filter_description = _("PNG images");
	chooser.data = dialog.release();
	file_dialog_show(chooser);
}

} // namespace

void clipboard_image_save_async(GdkTexture *texture, GFile *file, GCancellable *cancellable,
                                GAsyncReadyCallback callback, gpointer data)
{
	g_return_if_fail(GDK_IS_TEXTURE(texture));
	g_return_if_fail(G_IS_FILE(file));
	g_autoptr(GTask) task = g_task_new(texture, cancellable, callback, data);
	g_task_set_task_data(task, g_object_ref(file), g_object_unref);
	g_task_run_in_thread(task, save_png_thread);
}

gboolean clipboard_image_save_finish(GdkTexture *texture, GAsyncResult *result, GError **error)
{
	g_return_val_if_fail(g_task_is_valid(result, texture), FALSE);
	return g_task_propagate_boolean(G_TASK(result), error);
}

void clipboard_image_dialog_show(LayoutWindow *lw)
{
	if (!lw || !layout_valid(&lw)) return;
	auto *dialog = new ClipboardImageDialog(lw);
	GdkClipboard *clipboard = gtk_widget_get_clipboard(lw->window);
	gdk_clipboard_read_texture_async(clipboard, dialog->read_cancel, clipboard_read_cb, dialog);
}
