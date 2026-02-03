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

gboolean is_image_file(const std::string& path)
{
	g_autoptr(GFile) file = g_file_new_for_path(path.c_str());
	g_autoptr(GError) error = nullptr;
	g_autoptr(GFileInfo) info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, nullptr, &error);

	if (!info)
		{
		return false;
		}

	const char *content_type = g_file_info_get_content_type(info);
	return (content_type && g_str_has_prefix(content_type, "image/"));
}

gboolean is_text_file(const gchar *filename)
{
	gboolean result_uncertain = TRUE;

	g_autofree gchar *content_type = g_content_type_guess(filename, nullptr, 0, &result_uncertain);

	if (!result_uncertain)
		{
		if (g_str_has_prefix(content_type, "text/plain"))
			{
			return TRUE;
			}
		}

	return FALSE;
}

#if HAVE_ARCHIVE
gboolean is_archive_file(const gchar *filename)
{
	gboolean result_uncertain = TRUE;

	g_autofree gchar *content_type = g_content_type_guess(filename, nullptr, 0, &result_uncertain);

	if (!result_uncertain)
		{
		if (g_str_has_prefix(content_type, "application/zip") ||
		        g_str_has_prefix(content_type, "application/x-tar") ||
		        g_str_has_prefix(content_type, "application/x-7z-compressed") ||
		        g_str_has_prefix(content_type, "application/x-bzip2") ||
		        g_str_has_prefix(content_type, "application/gzip") ||
		        g_str_has_prefix(content_type, "application/vnd.rar"))
			{
			return TRUE;
			}
		}

	return FALSE;
}

GtkWidget *create_archive_preview(const char *filename)
{
	struct archive *a;
	struct archive_entry *entry;
	int r;

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	r = archive_read_open_filename(a, filename, 10240);
	if (r != ARCHIVE_OK)
		{
		archive_read_close(a);
		archive_read_free(a);

		return nullptr;
		}

	g_autoptr(GString) output = g_string_new(nullptr);

	while ((archive_read_next_header(a, &entry)) == ARCHIVE_OK)
		{
		const char *pathname = archive_entry_pathname(entry);
		if (pathname)
			{
			output = g_string_append(output, pathname);
			output = g_string_append(output, "\n");
			}

		r = archive_read_data_skip(a);

		if (r != ARCHIVE_OK)
			{
			fprintf(stderr, "Error skipping data: %s\n", archive_error_string(a));
			break;
			}
		}

	if (r != ARCHIVE_EOF && r != ARCHIVE_OK)
		{
		g_printerr("Error reading archive: %s\n", archive_error_string(a));
		}

	archive_read_close(a);
	archive_read_free(a);

	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	gtk_text_buffer_set_text(buffer, output->str, -1);

	return textview;
}
#endif

void append_dir_entry(GString *output, const char *base_path, const char *entry)
{
	g_autofree gchar *fullpath = g_build_filename(base_path, entry, nullptr);

	if (isdir(fullpath))
		{
		g_string_append_printf(output, "📁 %s\n", entry);
		}
	else if (is_image_file(fullpath))
		{
		g_string_append_printf(output, "📷 %s\n", entry);
		}
#if HAVE_ARCHIVE
	else if (is_archive_file(fullpath))
		{
		g_string_append_printf(output, "🗜️ %s\n", entry);
		}
#endif
	else if (g_str_has_suffix(fullpath, ".pdf"))
		{
		g_string_append_printf(output, "📑 %s\n", entry);
		}
	else if (g_str_has_suffix(fullpath, ".icc"))
		{
		g_string_append_printf(output, "🌈 %s\n", entry);
		}
	else if (g_str_has_suffix(fullpath, GQ_COLLECTION_EXT))
		{
		g_string_append_printf(output, "⠿ %s\n", entry);
		}
	else
		{
		g_string_append_printf(output, "📄 %s\n", entry);
		}
}

GtkWidget *create_text_preview(const char *filename)
{
	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

	GError *error = nullptr;
	GIOChannel *channel = g_io_channel_new_file(filename, "r", &error);
	if (!channel)
		{
		log_printf("Error opening file %s: %s\n", filename, error->message);
		g_error_free(error);
		return textview;
		}

	g_autoptr(GString) content = g_string_new(nullptr);
	gchar *line = nullptr;
	gsize len;
	GIOStatus status;
	int line_count = 0;

	/* Preview only up to 100 lines
	 */
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
		g_error_free(error);
		}

	g_io_channel_shutdown(channel, TRUE, nullptr);
	g_io_channel_unref(channel);

	gtk_text_buffer_set_text(buffer, content->str, -1);

	return textview;
}

GtkWidget *create_icc_preview(const char *filename)
{
	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);

	g_autofree gchar *stdout_data = nullptr;
	g_autofree gchar *stderr_data = nullptr;
	gint exit_status = 0;
	GError *error = nullptr;

	g_autofree gchar *cmd_line = g_strdup_printf("iccdump %s", filename);

	gboolean success = g_spawn_command_line_sync(cmd_line, &stdout_data, &stderr_data, &exit_status, &error);

	if (!success)
		{
		log_printf("iccdump is not installed. Install argyll package: %s\n", error->message);
		g_clear_error(&error);
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
		gtk_text_buffer_set_text(buffer, g_strdup("iccdump is not installed. \nInstall argyll package"), -1);
		}
	else
		{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
		gtk_text_buffer_set_text(buffer, stdout_data, -1);
		}

	return textview;
}

#if HAVE_PDF
GtkWidget *create_pdf_preview(const char *filename)
{
	g_autofree gchar *uri = g_strconcat("file://", filename, nullptr);

	g_autoptr(GError) error = nullptr;
	PopplerDocument *doc = poppler_document_new_from_file(uri, nullptr, &error);
	if (!doc)
		{
		log_printf(_("Error loading PDF: %s\n"), error->message);
		return nullptr;
		}

	/* Get first page
	 */
	PopplerPage *page = poppler_document_get_page(doc, 0);
	if (!page)
		{
		log_printf(_("Failed to get page 0\n"));
		g_object_unref(doc);
		return nullptr;
		}

	/* Get original page size
	 */
	double page_width;
	double page_height;
	poppler_page_get_size(page, &page_width, &page_height);

	const double scale_x = options->thumbnails.max_width / page_width;
	const double scale_y = options->thumbnails.max_height / page_height;
	const double scale = std::min(scale_x, scale_y);

	const auto target_width = static_cast<int>(page_width * scale);
	const auto target_height = static_cast<int>(page_height * scale);

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_width, target_height);
	cairo_t* cr = cairo_create(surface);

	cairo_scale(cr, scale, scale);
	poppler_page_render(page, cr);

	cairo_destroy(cr);

	g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, target_width, target_height);

	cairo_surface_destroy(surface);
	g_object_unref(page);
	g_object_unref(doc);

	return gtk_image_new_from_pixbuf(pixbuf); // GtkImage holds its own ref of pixbuf
}
#endif

gboolean is_dir(const char *dir_path, const std::string &entry)
{
	g_autofree gchar *full_path = g_build_filename(dir_path, entry.c_str(), NULL);
	gboolean result = g_file_test(full_path, G_FILE_TEST_IS_DIR);

	return result;
}

GtkWidget *create_dir_preview(const char *dir_path)
{
	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);

	GError *error = nullptr;
	GDir *dir = g_dir_open(dir_path, 0, &error);

	if (!dir)
		{
		if (error)
			{
			log_printf("Dir preview failed: %s", error->message);
			g_error_free(error);
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

	/* Sort: directories first, then files, alphabetically within groups
	 */
	std::sort(entries.begin(), entries.end(), [&](const std::string &a, const std::string &b)
		{
		bool dir_a = is_dir(dir_path, a);
		bool dir_b = is_dir(dir_path, b);

		if (dir_a != dir_b)
			{
			return dir_a; // dirs first
			}

		return a < b; // then alphabetical
		});

	GString *output = g_string_new("");

	for (const auto &e : entries)
		{
		append_dir_entry(output, dir_path, e.c_str());
		}

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	gtk_text_buffer_set_text(buffer, output->str, -1);

	return textview;
}

GtkWidget *create_image_preview(const char *file_path)
{
	GtkWidget *image_widget = nullptr;

	g_autofree gchar *thumb_file = cache_find_location(CACHE_TYPE_THUMB, file_path);
	GdkPixbuf *pixbuf = nullptr;

	if (thumb_file)
		{
		pixbuf = gdk_pixbuf_new_from_file(thumb_file, nullptr);
		}
	else
		{
		g_autoptr(GdkPixbuf) orig_pixbuf = gdk_pixbuf_new_from_file(file_path, nullptr);
		if (orig_pixbuf)
			{
			pixbuf = gdk_pixbuf_scale_simple(orig_pixbuf, options->thumbnails.max_width, options->thumbnails.max_height, GDK_INTERP_BILINEAR);
			}
		}

	if (pixbuf)
		{
		image_widget = gtk_image_new();
		gtk_image_set_from_pixbuf(GTK_IMAGE(image_widget), pixbuf);
		g_object_unref(pixbuf);
		}

	return image_widget;
}

void preview_file_cb(GtkFileChooser *chooser, gpointer)
{
	g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
	if (!file)
		{
		return;
		}

	g_autofree gchar *file_name = g_file_get_path(file);
	if (!file_name)
		{
		return;
		}

	GtkWidget *preview_box = gtk_file_chooser_get_preview_widget(chooser);

	if (GtkWidget *child = gq_gtk_bin_get_child(GTK_WIDGET(preview_box)))
		{
		gtk_container_remove(GTK_CONTAINER(preview_box), child);
		}

	GtkWidget *new_preview = nullptr;

	if (isdir(file_name))
		{
		new_preview = create_dir_preview(file_name);
		}
	else if (isfile(file_name) && g_str_has_suffix(file_name, "icc"))
		{
		new_preview = create_icc_preview(file_name);
		}
#if HAVE_PDF
	else if (isfile(file_name) && g_str_has_suffix(file_name, "pdf"))
		{
		new_preview = create_pdf_preview(file_name);
		}
#endif
#if HAVE_ARCHIVE
	else if (is_archive_file(file_name))
		{
		new_preview = create_archive_preview(file_name);
		}
#endif
	else if (is_text_file(file_name))
		{
		new_preview = create_text_preview(file_name);
		}
	else if (is_image_file(file_name))
		{
		new_preview = create_image_preview(file_name);
		}

	if (new_preview)
		{
		gq_gtk_container_add(preview_box, new_preview);
		gq_gtk_widget_show_all(new_preview);
		}
}

void update_file_chooser_preview_cb(GtkFileChooser *chooser, gpointer)
{
	preview_file_cb(chooser, nullptr);
}

GtkWidget* create_history_combo_box(const gchar *history_key)
{
	GList *work = history_list_get_by_key(history_key);

	if (work)
		{
		GtkWidget *history_combo = gtk_combo_box_text_new();

		for (GList *history_list = work; history_list != nullptr; history_list = history_list->next)
			{
			const auto path = static_cast<const gchar*>(history_list->data);
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(history_combo), path);
			}

		gtk_combo_box_set_active(GTK_COMBO_BOX(history_combo), 0);

		return history_combo;
		}

	return nullptr;
}

void history_combo_changed_cb(GtkComboBoxText *history_combo, gpointer data)
{
	g_autofree gchar *text = gtk_combo_box_text_get_active_text(history_combo);

	if (text)
		{
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(data), text);
		}
}

} // namespace

GtkFileChooserDialog *file_chooser_dialog_new(const FileChooserDialogData &fcdd)
{
	const gchar *title;

	title = fcdd.title ? fcdd.title : _("Select path");

	GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));

	GtkFileChooserDialog *dialog = GTK_FILE_CHOOSER_DIALOG(gtk_file_chooser_dialog_new(title, window, fcdd.action, _("_Cancel"), GTK_RESPONSE_CANCEL, fcdd.accept_text, GTK_RESPONSE_ACCEPT, nullptr));

	if (window)
		{
		gtk_window_set_transient_for(GTK_WINDOW(dialog), window);
		}
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
	gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);

	/* Always include an "all files" filter - for consistency of displayed layout,
	 * with or without a history combo or text box
	 */
	GtkFileFilter *all_filter = gtk_file_filter_new();
	gtk_file_filter_set_name(all_filter, _("All files"));
	gtk_file_filter_add_pattern(all_filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), all_filter);

	/* Optional filter
	 */
	g_auto(GStrv) extension_list = nullptr;

	if (fcdd.filter)
		{
		extension_list = g_strsplit(fcdd.filter, ";", -1);
		}

	if (extension_list)
		{
		GtkFileFilter *sub_filter = gtk_file_filter_new();
		gtk_file_filter_set_name(sub_filter, fcdd.filter_description);

		for (gint i = 0; extension_list[i] != nullptr; i++)
			{
			gchar *ext_pattern = g_strconcat("*", extension_list[i], nullptr );

			gtk_file_filter_add_pattern(sub_filter, ext_pattern);
			}
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), sub_filter);
		gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), sub_filter);
		}

	g_signal_connect(dialog, "response", fcdd.response_callback, fcdd.data);

	/* It is expected that extra_widget contains only a single widget - i.e. that
	 * entry text and history combo are not used at the same time
	 *
	 * Optional entry box
	 */
	if (fcdd.entry_text)
		{
		GtkWidget *entry = gtk_entry_new();
		gtk_entry_set_placeholder_text(GTK_ENTRY(entry), fcdd.entry_text);
		gtk_widget_set_tooltip_text(entry, fcdd.entry_tooltip);
		gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), entry);
		}

	/* Optional history combo
	 */
	GtkWidget *history_combo = nullptr;
	if (fcdd.history_key)
		{
		history_combo = create_history_combo_box(fcdd.history_key);

		if (history_combo)
			{
			gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), history_combo);
			g_signal_connect(history_combo, "changed", G_CALLBACK(history_combo_changed_cb), dialog);
			}
		}

	GtkWidget *textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);

	GtkWidget *scroller = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_container_add(scroller, textview);
	gtk_widget_set_size_request(scroller, 200, -1);
	gq_gtk_widget_show_all(scroller);

	gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(dialog), scroller);
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

	g_signal_connect(dialog, "update-preview", G_CALLBACK(update_file_chooser_preview_cb), buffer);

	/* Add book mark shortcuts. Always include the current dir.
	 * Current layout folder
	 */
#if HAVE_GTK4
	g_autoptr(GFile) path = g_file_new_for_path(layout_get_path(get_current_layout()), nullptr);
	gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dialog), path, nullptr);
#else
	gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dialog), layout_get_path(get_current_layout()), nullptr);
#endif

	if (fcdd.shortcuts)
		{
		g_auto(GStrv) shortcut_list = g_strsplit(fcdd.shortcuts, ";", -1);

		for (gint i = 0; shortcut_list[i] != nullptr; i++)
			{
#if HAVE_GTK4
			g_autoptr(GFile) path = g_file_new_for_path(shortcut_list[i], nullptr);
			gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dialog), path, nullptr);
#else
			gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dialog), shortcut_list[i], nullptr);
#endif
			}
		}

	/* Set priority order for default directory
	 */
	if (history_combo)
		{
		gchar *first = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(history_combo));
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), first);
		}
	else if (isfile(fcdd.filename))
		{
		g_autoptr(GFile) file = g_file_new_for_path(fcdd.filename);
		g_autoptr(GFile) parent = g_file_get_parent(file);

		if (parent)
			{
			gtk_file_chooser_set_file(GTK_FILE_CHOOSER(dialog), file, nullptr);
			}
		}
	else if (isdir(fcdd.filename))
		{
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), fcdd.filename);
		}

	if (fcdd.action == GTK_FILE_CHOOSER_ACTION_SAVE && fcdd.suggested_name)
		{
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), fcdd.suggested_name);
		}
	gq_gtk_widget_show_all(GTK_WIDGET(dialog));

	return dialog;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
