/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ui-file-chooser.h"

#include <algorithm>
#include <config.h>
#include <string>
#include <vector>

#if HAVE_ARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif
#if HAVE_PDF
#include <poppler.h>
#endif

#include "cache.h"
#include "compat.h"
#include "history-list.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "options.h"
#include "ui-fileops.h"

namespace {

struct PendingFileDialog
{
	FileDialogAction action;
	FileDialogCallback callback;
	gpointer data;
	GtkWidget *dialog;
	GtkWidget *chooser;
	GtkWidget *preview_scroller;
	gchar *preview_path;
	guint preview_timer_id;
	gboolean finished;
};

void finish_file_dialog(PendingFileDialog *pending, gint response_id);

gboolean is_image_file(const gchar *path)
{
	g_autoptr(GFile) file = g_file_new_for_path(path);
	g_autoptr(GError) error = nullptr;
	g_autoptr(GFileInfo) info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, nullptr, &error);

	if (!info)
		{
		return FALSE;
		}

	const gchar *content_type = g_file_info_get_content_type(info);
	return content_type && g_str_has_prefix(content_type, "image/");
}

gboolean is_text_file(const gchar *filename)
{
	gboolean result_uncertain = TRUE;

	g_autofree gchar *content_type = g_content_type_guess(filename, nullptr, 0, &result_uncertain);

	return !result_uncertain && content_type && g_str_has_prefix(content_type, "text/plain");
}

#if HAVE_ARCHIVE
gboolean is_archive_file(const gchar *filename)
{
	gboolean result_uncertain = TRUE;

	g_autofree gchar *content_type = g_content_type_guess(filename, nullptr, 0, &result_uncertain);

	return !result_uncertain && content_type &&
	       (g_str_has_prefix(content_type, "application/zip") ||
	        g_str_has_prefix(content_type, "application/x-tar") ||
	        g_str_has_prefix(content_type, "application/x-7z-compressed") ||
	        g_str_has_prefix(content_type, "application/x-bzip2") ||
	        g_str_has_prefix(content_type, "application/gzip") ||
	        g_str_has_prefix(content_type, "application/vnd.rar"));
}

GtkWidget *create_archive_preview(const gchar *filename)
{
	struct archive *archive = archive_read_new();
	archive_read_support_filter_all(archive);
	archive_read_support_format_all(archive);

	if (archive_read_open_filename(archive, filename, 10240) != ARCHIVE_OK)
		{
		archive_read_close(archive);
		archive_read_free(archive);
		return nullptr;
		}

	g_autoptr(GString) output = g_string_new(nullptr);
	struct archive_entry *entry;
	gint result;

	while ((result = archive_read_next_header(archive, &entry)) == ARCHIVE_OK)
		{
		const gchar *pathname = archive_entry_pathname(entry);
		if (pathname)
			{
			g_string_append(output, pathname);
			g_string_append_c(output, '\n');
			}

		result = archive_read_data_skip(archive);
		if (result != ARCHIVE_OK)
			{
			log_printf("Error skipping archive data: %s\n", archive_error_string(archive));
			break;
			}
		}

	if (result != ARCHIVE_EOF && result != ARCHIVE_OK)
		{
		log_printf("Error reading archive: %s\n", archive_error_string(archive));
		}

	archive_read_close(archive);
	archive_read_free(archive);

	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)), output->str, -1);

	return textview;
}
#endif

const gchar *dir_entry_icon_name(const gchar *base_path, const gchar *entry)
{
	g_autofree gchar *fullpath = g_build_filename(base_path, entry, nullptr);

	if (isdir(fullpath))
		{
		return "folder";
		}
	if (is_image_file(fullpath))
		{
		return "image-x-generic";
		}
#if HAVE_ARCHIVE
	if (is_archive_file(fullpath))
		{
		return "package-x-generic";
		}
#endif
	if (g_str_has_suffix(fullpath, ".pdf"))
		{
		return "application-pdf";
		}
	if (g_str_has_suffix(fullpath, ".icc"))
		{
		return "applications-graphics";
		}
	if (g_str_has_suffix(fullpath, GQ_COLLECTION_EXT))
		{
		return GQ_ICON_FILE_FILTER;
		}

	return "text-x-generic";
}

GtkWidget *create_dir_preview_row(const gchar *base_path, const gchar *entry)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_top(box, 2);
	gtk_widget_set_margin_bottom(box, 2);
	gtk_widget_set_margin_start(box, 4);
	gtk_widget_set_margin_end(box, 4);

	GtkWidget *icon = gtk_image_new_from_icon_name(dir_entry_icon_name(base_path, entry));
	gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
	gtk_box_append(GTK_BOX(box), icon);

	GtkWidget *label = gtk_label_new(entry);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_box_append(GTK_BOX(box), label);

	return box;
}

GtkWidget *create_text_preview(const gchar *filename)
{
	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(textview), TRUE);

	g_autoptr(GError) error = nullptr;
	GIOChannel *channel = g_io_channel_new_file(filename, "r", &error);
	if (!channel)
		{
		log_printf("Error opening file %s: %s\n", filename, error->message);
		return textview;
		}

	g_autoptr(GString) content = g_string_new(nullptr);
	gchar *line = nullptr;
	gsize len;
	GIOStatus status = G_IO_STATUS_NORMAL;
	gint line_count = 0;

	while (line_count < 100 &&
	       (status = g_io_channel_read_line(channel, &line, &len, nullptr, &error)) == G_IO_STATUS_NORMAL)
		{
		g_string_append(content, line);
		g_free(line);
		line = nullptr;
		line_count++;
		}

	if (status == G_IO_STATUS_ERROR && error)
		{
		log_printf("Error reading file: %s\n", error->message);
		}

	g_io_channel_shutdown(channel, TRUE, nullptr);
	g_io_channel_unref(channel);

	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)), content->str, -1);

	return textview;
}

GtkWidget *create_icc_preview(const gchar *filename)
{
	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(textview), TRUE);

	g_autofree gchar *stdout_data = nullptr;
	g_autofree gchar *stderr_data = nullptr;
	gint exit_status = 0;
	g_autoptr(GError) error = nullptr;

	g_autofree gchar *quoted_filename = g_shell_quote(filename);
	g_autofree gchar *cmd_line = g_strdup_printf("iccdump %s", quoted_filename);

	gboolean success = g_spawn_command_line_sync(cmd_line, &stdout_data, &stderr_data, &exit_status, &error);
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

	if (!success)
		{
		log_printf("iccdump is not installed. Install argyll package: %s\n", error->message);
		gtk_text_buffer_set_text(buffer, _("iccdump is not installed.\nInstall argyll package"), -1);
		}
	else
		{
		gtk_text_buffer_set_text(buffer, stdout_data, -1);
		}

	return textview;
}

#if HAVE_PDF
GtkWidget *create_pdf_preview(const gchar *filename)
{
	g_autofree gchar *uri = g_filename_to_uri(filename, nullptr, nullptr);
	if (!uri)
		{
		return nullptr;
		}

	g_autoptr(GError) error = nullptr;
	PopplerDocument *doc = poppler_document_new_from_file(uri, nullptr, &error);
	if (!doc)
		{
		log_printf(_("Error loading PDF: %s\n"), error->message);
		return nullptr;
		}

	PopplerPage *page = poppler_document_get_page(doc, 0);
	if (!page)
		{
		log_printf(_("Failed to get page 0\n"));
		g_object_unref(doc);
		return nullptr;
		}

	double page_width;
	double page_height;
	poppler_page_get_size(page, &page_width, &page_height);

	const double scale_x = options->thumbnails.max_width / page_width;
	const double scale_y = options->thumbnails.max_height / page_height;
	const double scale = std::min(scale_x, scale_y);
	const auto target_width = static_cast<gint>(page_width * scale);
	const auto target_height = static_cast<gint>(page_height * scale);

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_width, target_height);
	cairo_t *cr = cairo_create(surface);

	cairo_scale(cr, scale, scale);
	poppler_page_render(page, cr);

	cairo_destroy(cr);

	g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, target_width, target_height);
	cairo_surface_destroy(surface);
	g_object_unref(page);
	g_object_unref(doc);

	if (!pixbuf)
		{
		return nullptr;
		}

	g_autoptr(GdkTexture) texture = gdk_texture_new_for_pixbuf(pixbuf);
	GtkWidget *picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
	gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
	gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
	gtk_widget_set_size_request(picture, options->thumbnails.max_width, options->thumbnails.max_height);

	return picture;
}
#endif

gboolean entry_is_dir(const gchar *dir_path, const std::string &entry)
{
	g_autofree gchar *full_path = g_build_filename(dir_path, entry.c_str(), nullptr);
	return g_file_test(full_path, G_FILE_TEST_IS_DIR);
}

GtkWidget *create_dir_preview(const gchar *dir_path)
{
	GtkWidget *listbox = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_NONE);

	g_autoptr(GError) error = nullptr;
	GDir *dir = g_dir_open(dir_path, 0, &error);
	if (!dir)
		{
		if (error)
			{
			log_printf("Dir preview failed: %s\n", error->message);
			}
		return nullptr;
		}

	std::vector<std::string> entries;
	const gchar *entry;

	while ((entry = g_dir_read_name(dir)) != nullptr)
		{
		entries.emplace_back(entry);
		}

	g_dir_close(dir);

	std::sort(entries.begin(), entries.end(), [&](const std::string &a, const std::string &b)
		{
		const bool dir_a = entry_is_dir(dir_path, a);
		const bool dir_b = entry_is_dir(dir_path, b);

		if (dir_a != dir_b)
			{
			return dir_a;
			}

		return a < b;
		});

	for (const auto &dir_entry : entries)
		{
		gtk_list_box_append(GTK_LIST_BOX(listbox), create_dir_preview_row(dir_path, dir_entry.c_str()));
		}

	return listbox;
}

GtkWidget *create_image_preview(const gchar *file_path)
{
	g_autofree gchar *thumb_file = cache_find_location(CacheType::THUMB, file_path);
	g_autoptr(GdkPixbuf) pixbuf = nullptr;

	if (thumb_file)
		{
		pixbuf = gdk_pixbuf_new_from_file(thumb_file, nullptr);
		}

	if (!pixbuf)
		{
		g_autoptr(GdkPixbuf) orig_pixbuf = gdk_pixbuf_new_from_file(file_path, nullptr);
		if (orig_pixbuf)
			{
			pixbuf = gdk_pixbuf_scale_simple(orig_pixbuf, options->thumbnails.max_width, options->thumbnails.max_height, GDK_INTERP_BILINEAR);
			}
		}

	if (!pixbuf)
		{
		return nullptr;
		}

	g_autoptr(GdkTexture) texture = gdk_texture_new_for_pixbuf(pixbuf);
	GtkWidget *picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
	gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
	gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
	gtk_widget_set_size_request(picture, options->thumbnails.max_width, options->thumbnails.max_height);

	return picture;
}

GtkWidget *create_placeholder_preview()
{
	GtkWidget *label = gtk_label_new(_("No preview"));
	gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_widget_set_vexpand(label, TRUE);

	return label;
}

GtkWidget *create_preview_for_file(const gchar *file_name)
{
	if (isdir(file_name))
		{
		return create_dir_preview(file_name);
		}
	if (isfile(file_name) && g_str_has_suffix(file_name, ".icc"))
		{
		return create_icc_preview(file_name);
		}
#if HAVE_PDF
	if (isfile(file_name) && g_str_has_suffix(file_name, ".pdf"))
		{
		return create_pdf_preview(file_name);
		}
#endif
#if HAVE_ARCHIVE
	if (is_archive_file(file_name))
		{
		return create_archive_preview(file_name);
		}
#endif
	if (is_text_file(file_name))
		{
		return create_text_preview(file_name);
		}
	if (is_image_file(file_name))
		{
		return create_image_preview(file_name);
		}

	return nullptr;
}

void update_preview(PendingFileDialog *pending)
{
	g_autoptr(GFile) file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(pending->chooser));
	g_autofree gchar *file_name = file ? g_file_get_path(file) : nullptr;

	if (g_strcmp0(file_name, pending->preview_path) == 0)
		{
		return;
		}

	g_free(pending->preview_path);
	pending->preview_path = g_strdup(file_name);

	GtkWidget *new_preview = file_name ? create_preview_for_file(file_name) : nullptr;
	if (!new_preview)
		{
		new_preview = create_placeholder_preview();
		}

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pending->preview_scroller), new_preview);
}

gboolean preview_timer_cb(gpointer data)
{
	auto *pending = static_cast<PendingFileDialog *>(data);

	if (pending->finished)
		{
		return G_SOURCE_REMOVE;
		}

	update_preview(pending);
	return G_SOURCE_CONTINUE;
}

GtkFileChooserAction to_gtk_file_chooser_action(FileDialogAction action)
{
	switch (action)
		{
		case FileDialogAction::OPEN:
			return GTK_FILE_CHOOSER_ACTION_OPEN;
		case FileDialogAction::SAVE:
			return GTK_FILE_CHOOSER_ACTION_SAVE;
		case FileDialogAction::SELECT_FOLDER:
			return GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
		}

	return GTK_FILE_CHOOSER_ACTION_OPEN;
}

GFile *get_selected_file(PendingFileDialog *pending)
{
	GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(pending->chooser));
	if (file)
		{
		return file;
		}

	g_autoptr(GFile) folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(pending->chooser));
	g_autofree gchar *name = gtk_file_chooser_get_current_name(GTK_FILE_CHOOSER(pending->chooser));

	if (!name || name[0] == '\0')
		{
		return nullptr;
		}

	if (g_path_is_absolute(name))
		{
		return g_file_new_for_path(name);
		}

	if (!folder)
		{
		return nullptr;
		}

	return g_file_get_child(folder, name);
}

gboolean set_chooser_folder(PendingFileDialog *pending, GFile *folder)
{
	g_autoptr(GError) error = nullptr;
	if (!gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(pending->chooser), folder, &error))
		{
		if (error)
			{
			log_printf("Unable to change folder: %s\n", error->message);
			}
		return FALSE;
		}

	update_preview(pending);

	return TRUE;
}

void finish_file_dialog(PendingFileDialog *pending, gint response_id)
{
	if (pending->finished)
		{
		return;
		}

	pending->finished = TRUE;
	if (pending->preview_timer_id != 0)
		{
		g_source_remove(pending->preview_timer_id);
		pending->preview_timer_id = 0;
		}

	g_autoptr(GFile) file = nullptr;
	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		file = get_selected_file(pending);
		}

	pending->callback(file, pending->data);
	gtk_window_destroy(GTK_WINDOW(pending->dialog));
}

void overwrite_confirm_response_cb(GtkDialog *dialog, gint response_id, gpointer data)
{
	auto *pending = static_cast<PendingFileDialog *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		finish_file_dialog(pending, GTK_RESPONSE_ACCEPT);
		}

	gtk_window_destroy(GTK_WINDOW(dialog));
}

void show_overwrite_confirmation(PendingFileDialog *pending, const gchar *path)
{
	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(pending->dialog),
	                                           GTK_DIALOG_MODAL,
	                                           GTK_MESSAGE_QUESTION,
	                                           GTK_BUTTONS_NONE,
	                                           _("A file named \"%s\" already exists. Do you want to replace it?"),
	                                           path);

	gtk_dialog_add_buttons(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Replace"), GTK_RESPONSE_ACCEPT, nullptr);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	g_signal_connect(dialog, "response", G_CALLBACK(overwrite_confirm_response_cb), pending);
	gtk_widget_show(dialog);
}

void file_dialog_response_cb(GtkDialog *, gint response_id, gpointer data)
{
	auto *pending = static_cast<PendingFileDialog *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT && pending->action == FileDialogAction::OPEN)
		{
		g_autoptr(GFile) file = get_selected_file(pending);
		g_autofree gchar *path = file ? g_file_get_path(file) : nullptr;

		if (path && isdir(path))
			{
			set_chooser_folder(pending, file);
			return;
			}
		}

	if (response_id == GTK_RESPONSE_ACCEPT && pending->action == FileDialogAction::SAVE)
		{
		g_autoptr(GFile) file = get_selected_file(pending);
		g_autofree gchar *path = file ? g_file_get_path(file) : nullptr;

		if (path && isfile(path))
			{
			show_overwrite_confirmation(pending, path);
			return;
			}
		}

	finish_file_dialog(pending, response_id);
}

void file_dialog_destroy_cb(GtkWidget *, gpointer data)
{
	auto *pending = static_cast<PendingFileDialog *>(data);

	if (!pending->finished)
		{
		pending->finished = TRUE;
		pending->callback(nullptr, pending->data);
		}

	if (pending->preview_timer_id != 0)
		{
		g_source_remove(pending->preview_timer_id);
		}

	g_free(pending->preview_path);
	g_free(pending);
}

void add_filters(GtkFileChooser *chooser, const FileDialogData &fdd)
{
	GtkFileFilter *all_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(all_filter, _("All files"));
	gtk_file_filter_add_pattern(all_filter, "*");
	gtk_file_chooser_add_filter(chooser, all_filter);
	gtk_file_chooser_set_filter(chooser, all_filter);

	if (!fdd.filter)
		{
		return;
		}

	g_auto(GStrv) extension_list = g_strsplit(fdd.filter, ";", -1);
	GtkFileFilter *sub_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(sub_filter, fdd.filter_description ? fdd.filter_description : fdd.filter);

	for (gint i = 0; extension_list[i] != nullptr; i++)
		{
		g_autofree gchar *ext_pattern = g_strconcat("*", extension_list[i], nullptr);
		gtk_file_filter_add_pattern(sub_filter, ext_pattern);
		}

	gtk_file_chooser_add_filter(chooser, sub_filter);
	gtk_file_chooser_set_filter(chooser, sub_filter);
}

void set_initial_location(GtkFileChooser *chooser, const FileDialogData &fdd)
{
	const gchar *initial_folder = nullptr;
	if (fdd.history_key)
		{
		initial_folder = history_list_find_last_path_by_key(fdd.history_key);
		}

	if (fdd.filename && isfile(fdd.filename))
		{
		g_autoptr(GFile) file = g_file_new_for_path(fdd.filename);
		gtk_file_chooser_set_file(chooser, file, nullptr);
		}
	else if (fdd.filename && isdir(fdd.filename))
		{
		g_autoptr(GFile) folder = g_file_new_for_path(fdd.filename);
		gtk_file_chooser_set_current_folder(chooser, folder, nullptr);
		}
	else if (initial_folder && isdir(initial_folder))
		{
		g_autoptr(GFile) folder = g_file_new_for_path(initial_folder);
		gtk_file_chooser_set_current_folder(chooser, folder, nullptr);
		}
	else
		{
		g_autoptr(GFile) folder = g_file_new_for_path(layout_get_path(get_current_layout()));
		gtk_file_chooser_set_current_folder(chooser, folder, nullptr);
		}

	if (fdd.action == FileDialogAction::SAVE && fdd.suggested_name)
		{
		gtk_file_chooser_set_current_name(chooser, fdd.suggested_name);
		}
}

void add_shortcut_folder(GtkFileChooser *chooser, const gchar *path)
{
	if (!path || !isdir(path))
		{
		return;
		}

	g_autoptr(GFile) folder = g_file_new_for_path(path);
	gtk_file_chooser_add_shortcut_folder(chooser, folder, nullptr);
}

GtkWidget *create_dialog_content(PendingFileDialog *pending)
{
	GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand(paned, TRUE);
	gtk_widget_set_vexpand(paned, TRUE);

	gtk_paned_set_start_child(GTK_PANED(paned), pending->chooser);
	gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
	gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

	GtkWidget *preview_frame = gtk_frame_new(_("Preview"));
	gtk_widget_set_size_request(preview_frame, 240, -1);

	pending->preview_scroller = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pending->preview_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(pending->preview_scroller, TRUE);
	gtk_widget_set_vexpand(pending->preview_scroller, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(pending->preview_scroller), create_placeholder_preview());
	gtk_frame_set_child(GTK_FRAME(preview_frame), pending->preview_scroller);

	gtk_paned_set_end_child(GTK_PANED(paned), preview_frame);
	gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);
	gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
	gtk_paned_set_position(GTK_PANED(paned), 760);

	return paned;
}

} // namespace

void file_dialog_show(const FileDialogData &fdd)
{
	GtkWindow *parent = fdd.parent;
	if (!parent)
		{
		parent = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
		}

	const gchar *title = fdd.title ? fdd.title : _("Select path");
	const gchar *accept_text = fdd.accept_text ? fdd.accept_text : _("Open");

	auto *pending = g_new0(PendingFileDialog, 1);
	pending->action = fdd.action;
	pending->callback = fdd.callback;
	pending->data = fdd.data;

	pending->dialog = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, _("_Cancel"), GTK_RESPONSE_CANCEL, accept_text, GTK_RESPONSE_ACCEPT, nullptr);
	gtk_window_set_default_size(GTK_WINDOW(pending->dialog), 1040, 640);

	pending->chooser = gtk_file_chooser_widget_new(to_gtk_file_chooser_action(fdd.action));
	gtk_widget_set_size_request(pending->chooser, 720, 520);
	gtk_widget_set_hexpand(pending->chooser, TRUE);
	gtk_widget_set_vexpand(pending->chooser, TRUE);

	GtkFileChooser *chooser = GTK_FILE_CHOOSER(pending->chooser);
	gtk_file_chooser_set_create_folders(chooser, TRUE);
	add_filters(chooser, fdd);
	add_shortcut_folder(chooser, layout_get_path(get_current_layout()));

	set_initial_location(chooser, fdd);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(pending->dialog));
	gtk_widget_set_margin_top(content, 6);
	gtk_widget_set_margin_bottom(content, 6);
	gtk_widget_set_margin_start(content, 6);
	gtk_widget_set_margin_end(content, 6);
	gtk_box_append(GTK_BOX(content), create_dialog_content(pending));

	g_signal_connect(pending->dialog, "response", G_CALLBACK(file_dialog_response_cb), pending);
	g_signal_connect(pending->dialog, "destroy", G_CALLBACK(file_dialog_destroy_cb), pending);

	update_preview(pending);
	pending->preview_timer_id = g_timeout_add(250, preview_timer_cb, pending);

	gtk_widget_show(pending->dialog);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
