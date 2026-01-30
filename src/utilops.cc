/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "utilops.h"

#include <unistd.h>

#include <array>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>

#include <config.h>

#include "cache.h"
#include "compat.h"
#include "editors.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "history-list.h"
#include "image.h"
#include "intl.h"
#include "main-defines.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "thumb-standard.h"
#include "trash.h"
#include "ui-bookmark.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-utildlg.h"

namespace
{

struct PixmapErrors
{
	GdkPixbuf *error;
	GdkPixbuf *warning;
	GdkPixbuf *apply;
};

struct ClipboardData
{
	GList *path_list; /**< g_strdup(fd->path) */
	gboolean quoted;
	ClipboardAction action;
};

enum ClipboardDestination {
	CLIPBOARD_TEXT_PLAIN	= 0,
	CLIPBOARD_TEXT_URI_LIST	= 1,
	CLIPBOARD_X_SPECIAL_GNOME_COPIED_FILES	= 2,
	CLIPBOARD_UTF8_STRING	= 3
};

constexpr std::array<GtkTargetEntry, 4> target_types
{{
	{const_cast<gchar *>("text/plain"), 0, CLIPBOARD_TEXT_PLAIN},
	{const_cast<gchar *>("text/uri-list"), 0, CLIPBOARD_TEXT_URI_LIST},
	{const_cast<gchar *>("x-special/gnome-copied-files"), 0, CLIPBOARD_X_SPECIAL_GNOME_COPIED_FILES},
	{const_cast<gchar *>("UTF8_STRING"), 0, CLIPBOARD_UTF8_STRING},
}};

constexpr gint DIALOG_DEF_IMAGE_DIM_X = 150;
constexpr gint DIALOG_DEF_IMAGE_DIM_Y = 100;

constexpr gint UTILITY_LIST_MIN_WIDTH = 250;
constexpr gint UTILITY_LIST_MIN_HEIGHT = 150;

constexpr gint DIALOG_WIDTH = 750;

/** @FIXME It would be better if the window size was auto-adjusted.
 */
constexpr gint RENAME_WINDOW_WIDTH = 625;
constexpr gint RENAME_WINDOW_HEIGHT = 635;

constexpr gint PROGRESS_WINDOW_WIDTH = 450;
constexpr gint PROGRESS_WINDOW_HEIGHT = 150;

/* thumbnail spec has a max depth of 4 (.thumb??/fail/appname/??.png) */
constexpr gint UTILITY_DELETE_MAX_DEPTH = 5;

GdkPixbuf *file_util_get_error_icon(FileData *fd, GList *list, GtkWidget *)
{
	static PixmapErrors pe = []() -> PixmapErrors
	{
		GtkIconTheme *icon_theme = gtk_icon_theme_get_default();

		gint size;
		if (!gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &size, &size))
			{
			size = 16;
			}

		GdkPixbuf *pb_error = gq_gtk_icon_theme_load_icon_copy(icon_theme, GQ_ICON_DIALOG_ERROR, size, GTK_ICON_LOOKUP_USE_BUILTIN);
		GdkPixbuf *pb_warning = gq_gtk_icon_theme_load_icon_copy(icon_theme, GQ_ICON_DIALOG_WARNING, size, GTK_ICON_LOOKUP_USE_BUILTIN);
		GdkPixbuf *pb_apply = gq_gtk_icon_theme_load_icon_copy(icon_theme, GQ_ICON_APPLY, size, GTK_ICON_LOOKUP_USE_BUILTIN);
		return {pb_error, pb_warning, pb_apply};
	}();

	gint error = file_data_sc_verify_ci(fd, list);

	if (error & CHANGE_ERROR_MASK)
		{
		return pe.error;
		}

	if (error)
		{
		return pe.warning;
		}

	return pe.apply;
}

} // namespace

/*
 *--------------------------------------------------------------------------
 * Adds 1 or 2 images (if 2, side by side) to a GenericDialog
 *--------------------------------------------------------------------------
 */

static void generic_dialog_add_image(GenericDialog *gd, GtkWidget *box,
				     FileData *fd1, const gchar *header1,
				     gboolean second_image,
				     FileData *fd2, const gchar *header2,
				     gboolean show_filename)
{
	ImageWindow *imd;
	GtkWidget *preview_box = nullptr;
	GtkWidget *vbox;
	GtkWidget *label = nullptr;

	if (!box) box = gd->vbox;

	if (second_image)
		{
		preview_box = pref_box_new(box, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
		}

	/* image 1 */

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	if (preview_box)
		{
		GtkWidget *sep;

		gq_gtk_box_pack_start(GTK_BOX(preview_box), vbox, FALSE, TRUE, 0);

		sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
		gq_gtk_box_pack_start(GTK_BOX(preview_box), sep, FALSE, FALSE, 0);
		gtk_widget_show(sep);
		}
	else
		{
		gq_gtk_box_pack_start(GTK_BOX(box), vbox, FALSE, TRUE, PREF_PAD_GAP);
		}
	gtk_widget_show(vbox);

	if (header1)
		{
		GtkWidget *head;

		head = pref_label_new(vbox, header1);
		pref_label_bold(head, TRUE, FALSE);
		gtk_label_set_xalign(GTK_LABEL(head), 0.0);
		gtk_label_set_yalign(GTK_LABEL(head), 0.5);
		}

	imd = image_new(FALSE);
	g_object_set(imd->pr, "zoom_expand", FALSE, NULL);
	gtk_widget_set_size_request(imd->widget, DIALOG_DEF_IMAGE_DIM_X, DIALOG_DEF_IMAGE_DIM_Y);
	gq_gtk_box_pack_start(GTK_BOX(vbox), imd->widget, TRUE, TRUE, 0);
	image_change_fd(imd, fd1, 0.0);
	gtk_widget_show(imd->widget);

	if (show_filename)
		{
		label = pref_label_new(vbox, (fd1 == nullptr) ? "" : fd1->name);
		}

	/* only the first image is stored (for use in gd_image_set) */
	g_object_set_data(G_OBJECT(gd->dialog), "img_image", imd);
	g_object_set_data(G_OBJECT(gd->dialog), "img_label", label);


	/* image 2 */

	if (preview_box)
		{
		vbox = pref_box_new(preview_box, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

		if (header2)
			{
			GtkWidget *head;

			head = pref_label_new(vbox, header2);
			pref_label_bold(head, TRUE, FALSE);
			gtk_label_set_xalign(GTK_LABEL(head), 0.0);
			gtk_label_set_yalign(GTK_LABEL(head), 0.5);
			}

		imd = image_new(FALSE);
		g_object_set(imd->pr, "zoom_expand", FALSE, NULL);
		gtk_widget_set_size_request(imd->widget, DIALOG_DEF_IMAGE_DIM_X, DIALOG_DEF_IMAGE_DIM_Y);
		gq_gtk_box_pack_start(GTK_BOX(vbox), imd->widget, TRUE, TRUE, 0);
		if (fd2) image_change_fd(imd, fd2, 0.0);
		gtk_widget_show(imd->widget);

		if (show_filename)
			{
			label = pref_label_new(vbox, (fd2 == nullptr) ? "" : fd2->name);
			}
		g_object_set_data(G_OBJECT(gd->dialog), "img_image2", imd);
		g_object_set_data(G_OBJECT(gd->dialog), "img_label2", label);
		}
}

/*
 *--------------------------------------------------------------------------
 * Wrappers to aid in setting additional dialog properties (unde mouse, etc.)
 *--------------------------------------------------------------------------
 */

/**
 * @brief
 * @param title
 * @param role
 * @param parent
 * @param auto_close
 * @param cancel_cb
 * @param data
 * @returns
 *
 * \image html file_util_gen_dlg.png 'Typical implementation' width=200
 */
GenericDialog *file_util_gen_dlg(const gchar *title,
				 const gchar *role,
				 GtkWidget *parent, gboolean auto_close,
				 void (*cancel_cb)(GenericDialog *, gpointer), gpointer data)
{
	GenericDialog *gd;

	gd = generic_dialog_new(title, role, parent, auto_close, cancel_cb, data);
	if (options->place_dialogs_under_mouse)
		{
		gq_gtk_window_set_position(GTK_WINDOW(gd->dialog), GTK_WIN_POS_MOUSE);
		}

	return gd;
}

/* this warning dialog is copied from SLIK's ui_utildg.c,
 * because it does not have a mouse center option,
 * and we must center it before show, implement it here.
 */
GenericDialog *file_util_warning_dialog(const gchar *heading, const gchar *message,
					const gchar *icon_name, GtkWidget *parent)
{
	GenericDialog *gd;

	gd = file_util_gen_dlg(heading, "warning", parent, TRUE, nullptr, nullptr);
	generic_dialog_add_message(gd, icon_name, heading, message, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", generic_dialog_dummy_cb, TRUE);
	if (options->place_dialogs_under_mouse)
		{
		gq_gtk_window_set_position(GTK_WINDOW(gd->dialog), GTK_WIN_POS_MOUSE);
		}
	gtk_widget_show(gd->dialog);

	return gd;
}

static gint filename_base_length(const gchar *name)
{
	gint n;

	if (!name) return 0;

	n = strlen(name);

	if (filter_name_exists(name))
		{
		const gchar *ext;

		ext = registered_extension_from_path(name);
		if (ext) n -= strlen(ext);
		}

	return n;
}




enum class UtilityType {
	COPY,
	MOVE,
	RENAME,
	RENAME_FOLDER,
	EDITOR,
	FILTER,
	DELETE,
	DELETE_LINK,
	DELETE_FOLDER,
	WRITE_METADATA
};

enum class UtilityPhase {
	START = 0,
	INTERMEDIATE,
	ENTERING,
	CHECKED,
	DONE,
	CANCEL,
	DISCARD
};

enum {
	UTILITY_RENAME = 0,
	UTILITY_RENAME_AUTO,
	UTILITY_RENAME_FORMATTED
};

struct UtilityDataMessages {
	const gchar *title;
	const gchar *question;
	const gchar *desc_flist;
	const gchar *desc_source_fd;
	const gchar *fail;
};

struct UtilityData {
	UtilityType type;
	UtilityPhase phase;

	FileData *dir_fd;
	GList *content_list;
	GList *flist;

	FileData *sel_fd;

	GtkWidget *parent;
	GenericDialog *gd;
	GtkFileChooserDialog *fdlg;

	guint update_idle_id; /* event source id */
	guint perform_idle_id; /* event source id */

	gboolean with_sidecars; /* operate on grouped or single files; TRUE = use file_data_sc_, FALSE = use file_data_ functions */

	/* alternative dialog parts */
	GtkWidget *notebook;

	UtilityDataMessages messages;

	/* helper entries for various modes */
	GtkWidget *rename_entry;
	GtkWidget *rename_label;
	GtkWidget *auto_entry_front;
	GtkWidget *auto_entry_end;
	GtkWidget *auto_spin_start;
	GtkWidget *auto_spin_pad;
	GtkWidget *format_entry;
	GtkWidget *format_spin;

	GtkWidget *listview;


	gchar *dest_path;

	/* data for the operation itself, internal or external */
	gboolean external; /* TRUE for external command, FALSE for internal */

	gchar *external_command;
	gpointer resume_data;

	FileUtilDoneFunc done_func;
	void (*details_func)(UtilityData *ud, FileData *fd);
	gboolean (*finalize_func)(FileData *fd);
	gboolean (*discard_func)(FileData *fd);

	/* progress dialog */
	GenericDialog *progress_gd;
	GtkWidget *progress_label;
	GtkWidget *progress_bar;
	GtkWidget *progress_spinner;
	GtkWidget *progress_button_stop;
	GtkWidget *progress_button_close;
	gint files_completed;
	gint files_total;
	gboolean cancelled;
};

enum {
	UTILITY_COLUMN_FD = 0,
	UTILITY_COLUMN_PIXBUF,
	UTILITY_COLUMN_PATH,
	UTILITY_COLUMN_NAME,
	UTILITY_COLUMN_SIDECARS,
	UTILITY_COLUMN_DEST_PATH,
	UTILITY_COLUMN_DEST_NAME,
	UTILITY_COLUMN_COUNT
};

struct UtilityDelayData {
	UtilityType type;
	UtilityPhase phase;
	GList *flist;
	gchar *dest_path;
	gchar *editor_key;
	GtkWidget *parent;
	guint idle_id; /* event source id */
	};

static void generic_dialog_image_set(UtilityData *ud, FileData *fd)
{
	ImageWindow *imd;
	GtkWidget *label;
	FileData *fd2 = nullptr;

	imd = static_cast<ImageWindow *>(g_object_get_data(G_OBJECT(ud->gd->dialog), "img_image"));
	label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(ud->gd->dialog), "img_label"));

	if (!imd) return;

	image_change_fd(imd, fd, 0.0);

	if (label)
		{
		g_autofree gchar *buf = g_strjoin("\n", text_from_time(fd->date), text_from_size(fd->size), NULL);
		gtk_label_set_text(GTK_LABEL(label), buf);
		}

	if (ud->type == UtilityType::RENAME || ud->type == UtilityType::COPY || ud->type == UtilityType::MOVE)
		{
		imd = static_cast<ImageWindow *>(g_object_get_data(G_OBJECT(ud->gd->dialog), "img_image2"));
		label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(ud->gd->dialog), "img_label2"));

		if (imd)
			{
			if (isfile(fd->change->dest))
				{
				fd2 = file_data_new_group(fd->change->dest);
				image_change_fd(imd, fd2, 0.0);
				if (label && fd->change->dest)
					{
					g_autofree gchar *buf = g_strjoin("\n", text_from_time(fd2->date), text_from_size(fd2->size), NULL);
					gtk_label_set_text(GTK_LABEL(label), buf);
					}
				file_data_unref(fd2);
				}
			else
				{
				image_change_fd(imd, nullptr, 0.0);
				if (label) gtk_label_set_text(GTK_LABEL(label), "");
				}
			}
		}
}

static gboolean file_util_write_metadata_first(UtilityType type, UtilityPhase phase, GList *flist, const gchar *dest_path, const gchar *editor_key, GtkWidget *parent);

static UtilityData *file_util_data_new(UtilityType type)
{
	UtilityData *ud;

	ud = g_new0(UtilityData, 1);

	ud->type = type;
	ud->phase = UtilityPhase::START;

	return ud;
}

static void file_util_data_free(UtilityData *ud)
{
	if (!ud) return;

	if (ud->update_idle_id) g_source_remove(ud->update_idle_id);
	if (ud->perform_idle_id) g_source_remove(ud->perform_idle_id);

	file_data_unref(ud->dir_fd);
	file_data_list_free(ud->content_list);
	file_data_list_free(ud->flist);

	if (ud->gd) generic_dialog_close(ud->gd);
	if (ud->progress_gd) generic_dialog_close(ud->progress_gd);

	g_free(ud->dest_path);
	g_free(ud->external_command);

	g_free(ud);
}

static GtkTreeViewColumn *file_util_dialog_add_list_column(GtkWidget *view, const gchar *text, gboolean image, gint n)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, text);
	gtk_tree_view_column_set_min_width(column, 4);
	if (image)
		{
		gtk_tree_view_column_set_min_width(column, 20);
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
		renderer = gtk_cell_renderer_pixbuf_new();
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "pixbuf", n);
		}
	else
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
		renderer = gtk_cell_renderer_text_new();
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "text", n);
		}
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);

	return column;
}

static void file_util_dialog_list_select(GtkWidget *view, gint n)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
	if (gtk_tree_model_iter_nth_child(store, &iter, nullptr, n))
		{
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
		gtk_tree_selection_select_iter(selection, &iter);
		}
}

static GtkWidget *file_util_dialog_add_list(GtkWidget *box, GList *list, gboolean full_paths, gboolean with_sidecars)
{
	GtkWidget *view;
	GtkListStore *store;

	GtkWidget *scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(UTILITY_COLUMN_COUNT, G_TYPE_POINTER, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), FALSE);

	file_util_dialog_add_list_column(view, "", TRUE, UTILITY_COLUMN_PIXBUF);

	if (full_paths)
		{
		file_util_dialog_add_list_column(view, _("Path"), FALSE, UTILITY_COLUMN_PATH);
		}
	else
		{
		file_util_dialog_add_list_column(view, _("Name"), FALSE, UTILITY_COLUMN_NAME);
		}

	gtk_widget_set_size_request(view, UTILITY_LIST_MIN_WIDTH, UTILITY_LIST_MIN_HEIGHT);
	gq_gtk_container_add(scrolled, view);
	gtk_widget_show(view);

	while (list)
		{
		auto fd = static_cast<FileData *>(list->data);
		GtkTreeIter iter;

		g_autofree gchar *sidecars = with_sidecars ? file_data_sc_list_to_string(fd) : nullptr;
		GdkPixbuf *icon = file_util_get_error_icon(fd, list, view);
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
				   UTILITY_COLUMN_FD, fd,
				   UTILITY_COLUMN_PIXBUF, icon,
				   UTILITY_COLUMN_PATH, fd->path,
				   UTILITY_COLUMN_NAME, fd->name,
				   UTILITY_COLUMN_SIDECARS, sidecars,
				   UTILITY_COLUMN_DEST_PATH, fd->change ? fd->change->dest : "error",
				   UTILITY_COLUMN_DEST_NAME, fd->change ? filename_from_path(fd->change->dest) : "error",
				   -1);

		list = list->next;
		}

	return view;
}


static gboolean file_util_perform_ci_internal(gpointer data);
static void file_util_dialog_run(UtilityData *ud);
static gint file_util_perform_ci_cb(gpointer resume_data, EditorFlags flags, GList *list, gpointer data);

/* call file_util_perform_ci_internal or start_editor_from_filelist_full */


static void file_util_resume_cb(GenericDialog *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	if (ud->external)
		editor_resume(ud->resume_data);
	else
		file_util_perform_ci_internal(ud);
}

static void file_util_abort_cb(GenericDialog *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	if (ud->external)
		editor_skip(ud->resume_data);
	else
		file_util_perform_ci_cb(nullptr, EDITOR_ERROR_SKIPPED, ud->flist, ud);

}

/* Progress dialog functions */

static void file_util_progress_close_cb(GenericDialog *gd, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	generic_dialog_close(gd);
	ud->progress_gd = nullptr;
	ud->progress_label = nullptr;
	ud->progress_bar = nullptr;
	ud->progress_spinner = nullptr;
	ud->progress_button_stop = nullptr;
	ud->progress_button_close = nullptr;
}

static void file_util_progress_cancel_cb(GenericDialog *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	ud->cancelled = TRUE;

	if (ud->progress_button_stop)
		gtk_widget_set_sensitive(ud->progress_button_stop, FALSE);
	if (ud->progress_label)
		gtk_label_set_text(GTK_LABEL(ud->progress_label), _("Cancelling..."));
}

static void file_util_progress_enable_close(UtilityData *ud)
{
	if (!ud->progress_gd) return;

	ud->progress_gd->cancel_cb = file_util_progress_close_cb;

	if (ud->progress_spinner)
		gtk_spinner_stop(GTK_SPINNER(ud->progress_spinner));
	if (ud->progress_button_stop)
		gtk_widget_set_sensitive(ud->progress_button_stop, FALSE);
	if (ud->progress_button_close)
		gtk_widget_set_sensitive(ud->progress_button_close, TRUE);
}

static void file_util_progress_window_new(UtilityData *ud)
{
	GtkWidget *hbox;
	const gchar *title = nullptr;

	/* Don't show progress for very few files */
	if (ud->files_total < 2) return;

	/* Determine dialog title based on operation type */
	switch (ud->type)
		{
		case UtilityType::COPY:
			title = _("Copying files");
			break;
		case UtilityType::MOVE:
			title = _("Moving files");
			break;
		case UtilityType::DELETE:
		case UtilityType::DELETE_LINK:
		case UtilityType::DELETE_FOLDER:
			title = _("Deleting files");
			break;
		case UtilityType::RENAME:
			title = _("Renaming files");
			break;
		default:
			title = _("Processing files");
			break;
		}

	ud->progress_gd = file_util_gen_dlg(title, "file_operation_progress", ud->parent, FALSE, nullptr, ud);

	ud->progress_button_stop = generic_dialog_add_button(ud->progress_gd, GQ_ICON_STOP, _("Stop"),
	                                                      file_util_progress_cancel_cb, FALSE);
	gtk_widget_set_sensitive(ud->progress_button_stop, TRUE);

	ud->progress_button_close = generic_dialog_add_button(ud->progress_gd, GQ_ICON_CLOSE, _("Close"),
	                                                       file_util_progress_close_cb, TRUE);
	gtk_widget_set_sensitive(ud->progress_button_close, FALSE);

	/* Status label */
	ud->progress_label = gtk_label_new(_("Starting..."));
	gtk_label_set_xalign(GTK_LABEL(ud->progress_label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(ud->progress_label), PANGO_ELLIPSIZE_MIDDLE);
	gq_gtk_box_pack_start(GTK_BOX(ud->progress_gd->vbox), ud->progress_label, FALSE, FALSE, 5);
	gtk_widget_show(ud->progress_label);

	/* Progress bar and spinner in hbox */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(ud->progress_gd->vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	ud->progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ud->progress_bar), 0.0);
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ud->progress_bar), TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), ud->progress_bar, TRUE, TRUE, 0);
	gtk_widget_show(ud->progress_bar);

	ud->progress_spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(ud->progress_spinner));
	gq_gtk_box_pack_start(GTK_BOX(hbox), ud->progress_spinner, FALSE, FALSE, 5);
	gtk_widget_show(ud->progress_spinner);

	/* Set default window size */
	gq_gtk_window_resize(GTK_WINDOW(ud->progress_gd->dialog), PROGRESS_WINDOW_WIDTH, PROGRESS_WINDOW_HEIGHT);

	gtk_widget_show(ud->progress_gd->dialog);
}

static void file_util_progress_update(UtilityData *ud)
{
	if (!ud->progress_gd) return;

	gdouble fraction = (ud->files_total > 0) ? static_cast<gdouble>(ud->files_completed) / ud->files_total : 0.0;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ud->progress_bar), fraction);

	g_autofree gchar *progress_text = g_strdup_printf(_("%d of %d files"), ud->files_completed, ud->files_total);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ud->progress_bar), progress_text);

	/* Update current file label if we have a current file */
	if (ud->flist && ud->flist->data)
		{
		auto fd = static_cast<FileData *>(ud->flist->data);
		g_autofree gchar *label_text = nullptr;

		/* Show sidecars (e.g., RAW+JPG) if processing with sidecars */
		if (ud->with_sidecars && fd->sidecar_files)
			{
			g_autofree gchar *sidecars = file_data_sc_list_to_string(fd);
			label_text = g_strdup_printf(_("Processing: %s %s"), fd->name, sidecars);
			}
		else
			{
			label_text = g_strdup_printf(_("Processing: %s"), fd->name);
			}

		gtk_label_set_text(GTK_LABEL(ud->progress_label), label_text);
		}
}

static void file_util_progress_close(UtilityData *ud)
{
	if (!ud->progress_gd) return;

	/* Update to show completion */
	if (ud->cancelled)
		{
		gtk_label_set_text(GTK_LABEL(ud->progress_label), _("Operation cancelled"));
		}
	else
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ud->progress_bar), 1.0);
		g_autofree gchar *text = g_strdup_printf(_("%d files completed"), ud->files_total);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ud->progress_bar), text);
		gtk_label_set_text(GTK_LABEL(ud->progress_label), _("Operation completed"));
		}

	file_util_progress_enable_close(ud);
}


static gint file_util_perform_ci_cb(gpointer resume_data, EditorFlags flags, GList *list, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	gint ret = EDITOR_CB_CONTINUE;

	ud->resume_data = resume_data;

	if (editor_errors_but_skipped(flags))
		{
		g_autoptr(GString) msg = g_string_new(editor_get_error_str(flags));
		g_string_append(msg, "\n");
		g_string_append(msg, ud->messages.fail);
		g_string_append(msg, "\n");

		while (list)
			{
			auto fd = static_cast<FileData *>(list->data);

			g_string_append(msg, fd->path);
			g_string_append(msg, "\n");
			list = list->next;
			}

		if (resume_data)
			{
			g_string_append(msg, _("\n Continue multiple file operation?"));
			GenericDialog *d = file_util_gen_dlg(ud->messages.fail, "dlg_confirm",
			                                     nullptr, TRUE,
			                                     file_util_abort_cb, ud);

			generic_dialog_add_message(d, GQ_ICON_DIALOG_WARNING, nullptr, msg->str, TRUE);

			generic_dialog_add_button(d, GQ_ICON_GO_NEXT, _("Co_ntinue"),
						  file_util_resume_cb, TRUE);
			gtk_widget_show(d->dialog);
			ret = EDITOR_CB_SUSPEND;
			}
		else
			{
			file_util_warning_dialog(ud->messages.fail, msg->str, GQ_ICON_DIALOG_ERROR, nullptr);
			}
		}


	while (list)  /* be careful, file_util_perform_ci_internal can pass ud->flist as list */
		{
		auto fd = static_cast<FileData *>(list->data);
		list = list->next;

		if (!editor_errors(flags)) /* files were successfully deleted, call the maint functions */
			{
			if (ud->with_sidecars)
				file_data_sc_apply_ci(fd);
			else
				file_data_apply_ci(fd);
			}

		ud->flist = g_list_remove(ud->flist, fd);

		if (ud->finalize_func)
			{
			ud->finalize_func(fd);
			}

		if (ud->with_sidecars)
			file_data_sc_free_ci(fd);
		else
			file_data_free_ci(fd);
		file_data_unref(fd);

		/* Update progress */
		ud->files_completed++;
		file_util_progress_update(ud);
		}

	if (!resume_data) /* end of the list */
		{
		ud->phase = UtilityPhase::DONE;
		file_util_progress_close(ud);
		file_util_dialog_run(ud);
		}

	return ret;
}


/*
 * Perform the operation described by FileDataChangeInfo on all files in the list
 * it is an alternative to start_editor_from_filelist_full, it should use similar interface
 */


static gboolean file_util_perform_ci_internal(gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	if (!ud->perform_idle_id)
		{
		/* this function was called directly
		   just setup idle callback and wait until we are called again
		*/

		/* this is removed when ud is destroyed */
		ud->perform_idle_id = g_idle_add(file_util_perform_ci_internal, ud);
		return G_SOURCE_CONTINUE;
		}

	/* Initialize progress dialog on first call */
	if (ud->files_total == 0 && ud->flist)
		{
		ud->files_total = g_list_length(ud->flist);
		ud->files_completed = 0;
		file_util_progress_window_new(ud);
		}

	/* Check if operation was cancelled */
	if (ud->cancelled)
		{
		file_util_perform_ci_cb(nullptr, EDITOR_ERROR_SKIPPED, ud->flist, ud);
		return G_SOURCE_REMOVE;
		}

	g_assert(ud->flist);

	if (ud->flist)
		{
		gint ret;

		/* take a single entry each time, this allows better control over the operation */
		GList *single_entry = g_list_append(nullptr, ud->flist->data);
		gboolean last = !ud->flist->next;
		EditorFlags status = EDITOR_ERROR_STATUS;

		if (ud->with_sidecars ? file_data_sc_perform_ci(static_cast<FileData *>(single_entry->data))
		                      : file_data_perform_ci(static_cast<FileData *>(single_entry->data)))
			status = static_cast<EditorFlags>(0); /* OK */

		ret = file_util_perform_ci_cb(GINT_TO_POINTER(!last), status, single_entry, ud);
		g_list_free(single_entry);

		if (ret == EDITOR_CB_SUSPEND || last) return G_SOURCE_REMOVE;

		if (ret == EDITOR_CB_SKIP)
			{
			file_util_perform_ci_cb(nullptr, EDITOR_ERROR_SKIPPED, ud->flist, ud);
			return G_SOURCE_REMOVE;
			}
		}

	return G_SOURCE_CONTINUE;
}

static void file_util_perform_ci_dir(UtilityData *ud, gboolean internal, gboolean ext_result)
{
	switch (ud->type)
		{
		case UtilityType::DELETE_LINK:
			{
			g_assert(ud->dir_fd->sidecar_files == nullptr); // directories should not have sidecars
			if ((internal && file_data_perform_ci(ud->dir_fd)) ||
			    (!internal && ext_result))
				{
				file_data_apply_ci(ud->dir_fd);
				}
			else
				{
				g_autofree gchar *text = g_strdup_printf("%s:\n\n%s", ud->messages.fail, ud->dir_fd->path);
				file_util_warning_dialog(ud->messages.fail, text, GQ_ICON_DIALOG_ERROR, nullptr);
				}
			file_data_free_ci(ud->dir_fd);
			break;
			}
		case UtilityType::DELETE_FOLDER:
			{
			FileData *fail = nullptr;
			GList *work;
			work = ud->content_list;
			while (work)
				{
				FileData *fd;

				fd = static_cast<FileData *>(work->data);
				work = work->next;

				if (!fail)
					{
					if ((internal && file_data_sc_perform_ci(fd)) ||
					    (!internal && ext_result))
						{
						file_data_sc_apply_ci(fd);
						}
					else
						{
						if (internal) fail = file_data_ref(fd);
						}
					}
				file_data_sc_free_ci(fd);
				}

			if (!fail)
				{
				g_assert(ud->dir_fd->sidecar_files == nullptr); // directories should not have sidecars
				if ((internal && file_data_sc_perform_ci(ud->dir_fd)) ||
				    (!internal && ext_result))
					{
					file_data_apply_ci(ud->dir_fd);
					}
				else
					{
					fail = file_data_ref(ud->dir_fd);
					}
				}

			if (fail)
				{
				GenericDialog *gd;

				g_autofree gchar *text = g_strdup_printf("%s:\n\n%s", ud->messages.fail, ud->dir_fd->path);
				gd = file_util_warning_dialog(ud->messages.fail, text, GQ_ICON_DIALOG_ERROR, nullptr);

				if (fail != ud->dir_fd)
					{
					pref_spacer(gd->vbox, PREF_PAD_GROUP);
					g_free(text);
					text = g_strdup_printf(_("Removal of folder contents failed at this file:\n\n%s"),
								fail->path);
					pref_label_new(gd->vbox, text);
					}

				file_data_unref(fail);
				}
			break;
			}
		case UtilityType::RENAME_FOLDER:
			{
			FileData *fail = nullptr;
			GList *work;
			g_assert(ud->dir_fd->sidecar_files == nullptr); // directories should not have sidecars

			if ((internal && file_data_sc_perform_ci(ud->dir_fd)) ||
			    (!internal && ext_result))
				{
				file_data_sc_apply_ci(ud->dir_fd);
				}
			else
				{
				fail = file_data_ref(ud->dir_fd);
				}


			work = ud->content_list;
			while (work)
				{
				FileData *fd;

				fd = static_cast<FileData *>(work->data);
				work = work->next;

				if (!fail)
					{
					file_data_sc_apply_ci(fd);
					}
				file_data_sc_free_ci(fd);
				}

			if (fail)
				{
				g_autofree gchar *text = g_strdup_printf("%s:\n\n%s", ud->messages.fail, ud->dir_fd->path);
				file_util_warning_dialog(ud->messages.fail, text, GQ_ICON_DIALOG_ERROR, nullptr);

				file_data_unref(fail);
				}
			break;
			}
		default:
			g_warning("unhandled operation");
		}
	ud->phase = UtilityPhase::DONE;
	file_util_dialog_run(ud);
}

static gint file_util_perform_ci_dir_cb(gpointer, EditorFlags flags, GList *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	file_util_perform_ci_dir(ud, FALSE, !editor_errors_but_skipped(flags));
	return EDITOR_CB_CONTINUE; /* does not matter, there was just single directory */
}

static void file_util_perform_ci(UtilityData *ud)
{
	switch (ud->type)
		{
		case UtilityType::COPY:
			ud->external_command = g_strdup(CMD_COPY);
			break;
		case UtilityType::MOVE:
			ud->external_command = g_strdup(CMD_MOVE);
			break;
		case UtilityType::RENAME:
		case UtilityType::RENAME_FOLDER:
			ud->external_command = g_strdup(CMD_RENAME);
			break;
		case UtilityType::DELETE:
		case UtilityType::DELETE_LINK:
		case UtilityType::DELETE_FOLDER:
			ud->external_command = g_strdup(CMD_DELETE);
			break;
		case UtilityType::FILTER:
		case UtilityType::EDITOR:
			g_assert(ud->external_command != nullptr); /* it should be already set */
			break;
		case UtilityType::WRITE_METADATA:
			ud->external_command = nullptr;
		}

	if (is_valid_editor_command(ud->external_command))
		{
		EditorFlags flags;

		ud->external = TRUE;

		if (ud->dir_fd)
			{
			flags = start_editor_from_file_full(ud->external_command, ud->dir_fd, file_util_perform_ci_dir_cb, ud);
			}
		else
			{
			if (editor_blocks_file(ud->external_command))
				{
				DEBUG_1("Starting %s and waiting for results", ud->external_command);
				flags = start_editor_from_filelist_full(ud->external_command, ud->flist, nullptr, file_util_perform_ci_cb, ud);
				}
			else
				{
				/* start the editor without callback and finish the operation internally */
				DEBUG_1("Starting %s and finishing the operation", ud->external_command);
				flags = start_editor_from_filelist(ud->external_command, ud->flist);
				file_util_perform_ci_internal(ud);
				}
			}

		if (editor_errors(flags))
			{
			g_autofree gchar *text = g_strdup_printf(_("%s\nUnable to start external command.\n"), editor_get_error_str(flags));
			file_util_warning_dialog(ud->messages.fail, text, GQ_ICON_DIALOG_ERROR, nullptr);

			ud->gd = nullptr;
			ud->phase = UtilityPhase::CANCEL;
			file_util_dialog_run(ud);
			}
		}
	else
		{
		ud->external = FALSE;
		if (ud->dir_fd)
			{
			file_util_perform_ci_dir(ud, TRUE, FALSE);
			}
		else
			{
			file_util_perform_ci_internal(ud);
			}
		}
}

static void file_util_check_resume_cb(GenericDialog *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	ud->phase = UtilityPhase::CHECKED;
	file_util_dialog_run(ud);
}

static void file_util_check_abort_cb(GenericDialog *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	ud->phase = UtilityPhase::START;
	file_util_dialog_run(ud);
}

static void file_util_check_ci(UtilityData *ud)
{
	gint error = CHANGE_OK;
	g_autofree gchar *desc = nullptr;

	if (ud->type != UtilityType::RENAME_FOLDER)
		{
		if (ud->dest_path && !isdir(ud->dest_path))
			{
			error = CHANGE_GENERIC_ERROR;
			desc = g_strdup_printf(_("%s is not a directory"), ud->dest_path);
			}
		else if (ud->dir_fd)
			{
			g_assert(ud->dir_fd->sidecar_files == nullptr); // directories should not have sidecars
			error = file_data_verify_ci(ud->dir_fd, ud->flist);
			if (error) desc = file_data_get_error_string(error);
			}
		else
			{
			error = file_data_verify_ci_list(ud->flist, &desc, ud->with_sidecars);
			}
		}
	else
		{
		if (isdir(ud->dest_path) || isfile(ud->dest_path))
			{
			error = CHANGE_DEST_EXISTS;
			desc = g_strdup_printf(_("%s already exists"), ud->dest_path);
			}
		}

	if (!error)
		{
		ud->phase = UtilityPhase::CHECKED;
		file_util_dialog_run(ud);
		return;
		}

	GenericDialog *d = file_util_gen_dlg(ud->messages.title, "dlg_confirm",
	                                     ud->parent, TRUE,
	                                     file_util_check_abort_cb, ud);
	if (!(error & CHANGE_ERROR_MASK))
		{
		/* just a warning */
		generic_dialog_add_message(d, GQ_ICON_DIALOG_WARNING, _("Really continue?"), desc, TRUE);

		generic_dialog_add_button(d, GQ_ICON_GO_NEXT, _("Co_ntinue"),
					  file_util_check_resume_cb, TRUE);
		}
	else
		{
		/* fatal error */
		generic_dialog_add_message(d, GQ_ICON_DIALOG_WARNING, _("This operation can't continue:"), desc, TRUE);
		}
	gtk_widget_show(d->dialog);
}





static void file_util_cancel_cb(GenericDialog *gd, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	generic_dialog_close(gd);

	ud->gd = nullptr;

	ud->phase = UtilityPhase::CANCEL;
	file_util_dialog_run(ud);
}

static void file_util_discard_cb(GenericDialog *gd, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	generic_dialog_close(gd);

	ud->gd = nullptr;

	ud->phase = UtilityPhase::DISCARD;
	file_util_dialog_run(ud);
}

static void file_util_ok_cb(GenericDialog *gd, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	generic_dialog_close(gd);

	ud->gd = nullptr;

	file_util_dialog_run(ud);
}

static void file_util_dest_folder_update_path(UtilityData *ud)
{
	g_free(ud->dest_path);

	g_autoptr(GFile) file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(ud->fdlg));
	gchar *path = g_file_get_path(file);

	ud->dest_path = path;

	switch (ud->type)
		{
		case UtilityType::COPY:
			file_data_sc_update_ci_copy_list(ud->flist, ud->dest_path);
			break;
		case UtilityType::MOVE:
			file_data_sc_update_ci_move_list(ud->flist, ud->dest_path);
			break;
		case UtilityType::FILTER:
		case UtilityType::EDITOR:
			file_data_sc_update_ci_unspecified_list(ud->flist, ud->dest_path);
			break;
		case UtilityType::DELETE:
		case UtilityType::DELETE_LINK:
		case UtilityType::DELETE_FOLDER:
		case UtilityType::RENAME:
		case UtilityType::RENAME_FOLDER:
		case UtilityType::WRITE_METADATA:
			g_warning("unhandled operation");
		}
}

static void file_util_fdlg_rename_cb(GtkFileChooserDialog *chooser, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	GenericDialog *d = nullptr;

	file_util_dest_folder_update_path(ud);

	if (isdir(ud->dest_path))
		{
		gq_gtk_widget_destroy(GTK_WIDGET(chooser));

		ud->fdlg = nullptr;
		file_util_dialog_run(ud);
		}
	else
		{
		/* During copy/move operations it is necessary to ensure that the
		 * target directory exists before continuing with the next step.
		 * If not revert to the select directory dialog_
		 */
		g_autofree gchar *desc = g_strdup_printf(_("%s is not a directory"), ud->dest_path);

		d = file_util_gen_dlg(ud->messages.title, "dlg_confirm",
					ud->parent, TRUE,
					file_util_check_abort_cb, ud);
		generic_dialog_add_message(d, GQ_ICON_DIALOG_WARNING, _("This operation can't continue:"), desc, TRUE);

		gtk_widget_show(d->dialog);
		ud->phase = UtilityPhase::START;

		gq_gtk_widget_destroy(GTK_WIDGET(chooser));
		ud->fdlg = nullptr;
		}
}

static void file_util_fdlg_ok_cb(GtkFileChooserDialog *chooser, gint response_id, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{

		g_autoptr(GFile) file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(chooser));
		if (file != nullptr)
			{
			g_autoptr(GFile) file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(chooser));
			g_autofree gchar *path = g_file_get_path(file);

			file_util_dest_folder_update_path(ud);

			if (file)
				{
				g_autoptr(GFile) parent = nullptr;

				if (g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, nullptr) == G_FILE_TYPE_REGULAR)
					{
					parent = g_file_get_parent(file);
					}

				if (parent)
					{
					g_autofree gchar *dirname = g_file_get_path(parent);
					history_list_add_to_key("move_copy", dirname, -1);
					}
				else
					{
					history_list_add_to_key("move_copy", path, -1);
					}
				}
			}

		gq_gtk_widget_destroy(GTK_WIDGET(chooser));

		ud->fdlg = nullptr;
		ud->phase = UtilityPhase::ENTERING;

		file_util_dialog_run(ud);
		}
	else if (response_id == GQ_RESPONSE_WITH_RENAME)
		{
		file_util_dest_folder_update_path(ud);
		ud->dest_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(ud->fdlg));
		file_util_fdlg_rename_cb(chooser, ud);
		gq_gtk_widget_destroy(GTK_WIDGET(ud->fdlg));
		}
	else
		{
		auto ud = static_cast<UtilityData *>(data);

		gq_gtk_widget_destroy(GTK_WIDGET(chooser));
		ud->fdlg = nullptr;

		ud->phase = UtilityPhase::CANCEL;
		file_util_dialog_run(ud);
		}
}

/* format: * = filename without extension, ## = number position, extension is kept */
static gchar *file_util_rename_multiple_auto_format_name(const gchar *format, const gchar *name, gint n)
{
	gchar *new_name;
	g_autofree gchar *parsed = nullptr;
	const gchar *ext;
	gchar *middle;
	gchar *pad_start;
	gchar *pad_end;
	gint padding;

	if (!format || !name) return nullptr;

	g_autofree gchar *tmp = g_strdup(format);
	pad_start = strchr(tmp, '#');
	if (pad_start)
		{
		pad_end = pad_start;
		padding = 0;
		while (*pad_end == '#')
			{
			pad_end++;
			padding++;
			}
		*pad_start = '\0';

		parsed = g_strdup_printf("%s%0*d%s", tmp, padding, n, pad_end);
		}
	else
		{
		parsed = g_steal_pointer(&tmp);
		}

	ext = registered_extension_from_path(name);

	middle = strchr(parsed, '*');
	if (middle)
		{
		*middle = '\0';
		middle++;

		g_autofree gchar *base = remove_extension_from_path(name);
		new_name = g_strconcat(parsed, base, middle, ext, NULL);
		}
	else
		{
		new_name = g_strconcat(parsed, ext, NULL);
		}

	return new_name;
}


static void file_util_rename_preview_update(UtilityData *ud)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreeIter iter_selected;
	const gchar *front;
	const gchar *end;
	const gchar *format;
	gboolean valid;
	gint start_n;
	gint padding;
	gint n;
	gint mode;

	mode = gtk_notebook_get_current_page(GTK_NOTEBOOK(ud->notebook));

	if (mode == UTILITY_RENAME)
		{
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ud->listview));
		if (gtk_tree_selection_get_selected(selection, &store, &iter))
			{
			FileData *fd;
			const gchar *dest = gq_gtk_entry_get_text(GTK_ENTRY(ud->rename_entry));

			gtk_tree_model_get(store, &iter, UTILITY_COLUMN_FD, &fd, -1);
			g_assert(ud->with_sidecars); /* sidecars must be renamed too, it would break the pairing otherwise */

			g_autofree gchar *dirname = g_path_get_dirname(fd->change->dest);
			g_autofree gchar *destname = g_build_filename(dirname, dest, NULL);
			switch (ud->type)
				{
				case UtilityType::RENAME:
					file_data_sc_update_ci_rename(fd, dest);
					break;
				case UtilityType::COPY:
					file_data_sc_update_ci_copy(fd, destname);
					break;
				case UtilityType::MOVE:
					file_data_sc_update_ci_move(fd, destname);
					break;
				default:;
				}
			generic_dialog_image_set(ud, fd);

			gtk_list_store_set(GTK_LIST_STORE(store), &iter,
				   UTILITY_COLUMN_DEST_PATH, fd->change->dest,
				   UTILITY_COLUMN_DEST_NAME, filename_from_path(fd->change->dest),
				   -1);
			}
		}
	else
		{
		front = gq_gtk_entry_get_text(GTK_ENTRY(ud->auto_entry_front));
		end = gq_gtk_entry_get_text(GTK_ENTRY(ud->auto_entry_end));
		padding = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ud->auto_spin_pad));

		format = gq_gtk_entry_get_text(GTK_ENTRY(ud->format_entry));

		g_free(options->cp_mv_rn.auto_end);
		options->cp_mv_rn.auto_end = g_strdup(end);
		options->cp_mv_rn.auto_padding = padding;

		if (mode == UTILITY_RENAME_FORMATTED)
			{
			start_n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ud->format_spin));
			options->cp_mv_rn.formatted_start = start_n;
			}
		else
			{
			start_n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ud->auto_spin_start));
			options->cp_mv_rn.auto_start = start_n;
			}

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(ud->listview));
		n = start_n;
		valid = gtk_tree_model_get_iter_first(store, &iter);
		while (valid)
			{
			g_autofree gchar *dest = nullptr;
			FileData *fd;
			gtk_tree_model_get(store, &iter, UTILITY_COLUMN_FD, &fd, -1);

			if (mode == UTILITY_RENAME_FORMATTED)
				{
				dest = file_util_rename_multiple_auto_format_name(format, fd->name, n);
				}
			else
				{
				dest = g_strdup_printf("%s%0*d%s", front, padding, n, end);
				}

			g_assert(ud->with_sidecars); /* sidecars must be renamed too, it would break the pairing otherwise */

			g_autofree gchar *dirname = g_path_get_dirname(fd->change->dest);
			g_autofree gchar *destname = g_build_filename(dirname, dest, NULL);

			switch (ud->type)
				{
				case UtilityType::RENAME:
					file_data_sc_update_ci_rename(fd, dest);
					break;
				case UtilityType::COPY:
					file_data_sc_update_ci_copy(fd, destname);
					break;
				case UtilityType::MOVE:
					file_data_sc_update_ci_move(fd, destname);
					break;
				default:;
				}

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ud->listview));
			gtk_tree_selection_get_selected(selection, &store, &iter_selected);
			g_autoptr(GtkTreePath) path_iter = gtk_tree_model_get_path(store, &iter);
			g_autoptr(GtkTreePath) path_selected = gtk_tree_model_get_path(store, &iter_selected);
			if (!gtk_tree_path_compare(path_iter, path_selected))
				{
				generic_dialog_image_set(ud, fd);
				}

			gtk_list_store_set(GTK_LIST_STORE(store), &iter,
					   UTILITY_COLUMN_DEST_PATH, fd->change->dest,
					   UTILITY_COLUMN_DEST_NAME, filename_from_path(fd->change->dest),
					   -1);
			n++;
			valid = gtk_tree_model_iter_next(store, &iter);
			}
		}

	/* Check the other entries in the list - if there are
	 * multiple destination filenames with the same name the
	 * error icons must be updated
	 */
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		FileData *fd;
		gtk_tree_model_get(store, &iter, UTILITY_COLUMN_FD, &fd, -1);

		gtk_list_store_set(GTK_LIST_STORE(store), &iter,
			   UTILITY_COLUMN_PIXBUF, file_util_get_error_icon(fd, ud->flist, ud->listview),
			   -1);
		valid = gtk_tree_model_iter_next(store, &iter);
		}

}

static void file_util_rename_preview_entry_cb(GtkWidget *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	file_util_rename_preview_update(ud);
}

static void file_util_rename_preview_adj_cb(GtkWidget *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	file_util_rename_preview_update(ud);
}

static gboolean file_util_rename_idle_cb(gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	file_util_rename_preview_update(ud);

	ud->update_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void file_util_rename_preview_order_cb(GtkTreeModel *, GtkTreePath *, GtkTreeIter *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);

	if (ud->update_idle_id) return;

	ud->update_idle_id = g_idle_add(file_util_rename_idle_cb, ud);
}


static gboolean file_util_preview_cb(GtkTreeSelection *, GtkTreeModel *store,
				     GtkTreePath *tpath, gboolean path_currently_selected,
				     gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	GtkTreeIter iter;
	FileData *fd = nullptr;

	if (path_currently_selected ||
	    !gtk_tree_model_get_iter(store, &iter, tpath)) return TRUE;

	gtk_tree_model_get(store, &iter, UTILITY_COLUMN_FD, &fd, -1);
	generic_dialog_image_set(ud, fd);

	ud->sel_fd = fd;

	if (ud->type == UtilityType::RENAME || ud->type == UtilityType::COPY || ud->type == UtilityType::MOVE)
		{
		const gchar *name = filename_from_path(fd->change->dest);

		gtk_widget_grab_focus(ud->rename_entry);
		gtk_label_set_text(GTK_LABEL(ud->rename_label), fd->name);
		g_signal_handlers_block_by_func(ud->rename_entry, (gpointer)(file_util_rename_preview_entry_cb), ud);
		gq_gtk_entry_set_text(GTK_ENTRY(ud->rename_entry), name);
		gtk_editable_select_region(GTK_EDITABLE(ud->rename_entry), 0, filename_base_length(name));
		g_signal_handlers_unblock_by_func(ud->rename_entry, (gpointer)file_util_rename_preview_entry_cb, ud);
		}

	return TRUE;
}



static void box_append_safe_delete_status(GenericDialog *gd)
{
	GtkWidget *label;

	g_autofree gchar *buf = file_util_safe_delete_status();
	label = pref_label_new(gd->vbox, buf);

	gtk_label_set_xalign(GTK_LABEL(label), 1.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);
	gtk_widget_set_sensitive(label, FALSE);
}

static void file_util_details_cb(GenericDialog *, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	if (ud->details_func && ud->sel_fd)
		{
		ud->details_func(ud, ud->sel_fd);
		}
}

static void file_util_dialog_init_simple_list(UtilityData *ud)
{
	GtkWidget *box;
	GtkTreeSelection *selection;
	g_autofree gchar *dir_msg = nullptr;

	const gchar *icon_name;
	const gchar *msg;

	/** @FIXME use ud->stock_id */
	if (ud->type == UtilityType::DELETE ||
	    ud->type == UtilityType::DELETE_LINK ||
	    ud->type == UtilityType::DELETE_FOLDER)
		{
		icon_name = GQ_ICON_DELETE;
		msg = _("Delete");
		}
	else
		{
		icon_name = GQ_ICON_OK;
		msg = "OK";
		}

	ud->gd = file_util_gen_dlg(ud->messages.title, "dlg_confirm",
				   ud->parent, FALSE,  file_util_cancel_cb, ud);
	if (ud->discard_func) generic_dialog_add_button(ud->gd, GQ_ICON_REVERT, _("Discard changes"), file_util_discard_cb, FALSE);
	if (ud->details_func) generic_dialog_add_button(ud->gd, GQ_ICON_DIALOG_INFO, _("File details"), file_util_details_cb, FALSE);

	generic_dialog_add_button(ud->gd, icon_name, msg, file_util_ok_cb, TRUE);

	if (ud->dir_fd)
		{
		dir_msg = g_strdup_printf("%s\n\n%s\n", ud->messages.desc_source_fd, ud->dir_fd->path);
		}
	else
		{
		dir_msg = g_strdup("");
		}

	box = generic_dialog_add_message(ud->gd, GQ_ICON_DIALOG_QUESTION,
					 ud->messages.question,
					 dir_msg, TRUE);

	box = pref_group_new(box, TRUE, ud->messages.desc_flist, GTK_ORIENTATION_HORIZONTAL);

	ud->listview = file_util_dialog_add_list(box, ud->flist, FALSE, ud->with_sidecars);
	if (ud->with_sidecars) file_util_dialog_add_list_column(ud->listview, _("Sidecars"), FALSE, UTILITY_COLUMN_SIDECARS);

	if (ud->type == UtilityType::WRITE_METADATA) file_util_dialog_add_list_column(ud->listview, _("Write to file"), FALSE, UTILITY_COLUMN_DEST_NAME);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ud->listview));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	gtk_tree_selection_set_select_function(selection, file_util_preview_cb, ud, nullptr);

	generic_dialog_add_image(ud->gd, box, nullptr, nullptr, FALSE, nullptr, nullptr, FALSE);

	if (ud->type == UtilityType::DELETE ||
	    ud->type == UtilityType::DELETE_LINK ||
	    ud->type == UtilityType::DELETE_FOLDER)
		box_append_safe_delete_status(ud->gd);

	gtk_widget_show(ud->gd->dialog);

	file_util_dialog_list_select(ud->listview, 0);
}

static void file_util_dialog_init_dest_folder(UtilityData *ud)
{
	FileChooserDialogData fcdd{};

	fcdd.action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	fcdd.accept_text = (ud->type == UtilityType::MOVE) ? _("Move") : _("Copy");
	fcdd.data = ud;
	fcdd.history_key = "move_copy";
	fcdd.response_callback = G_CALLBACK(file_util_fdlg_ok_cb);
	fcdd.title = (ud->type == UtilityType::MOVE) ? _("Geeqie - Move File") : _("Geeqie - Copy File");

	GtkFileChooserDialog *dialog = file_chooser_dialog_new(fcdd);

	ud->fdlg = dialog;

	GtkWidget *with_rename = gtk_button_new_with_label(_("With Rename"));

	gtk_dialog_add_action_widget(GTK_DIALOG(dialog), with_rename, GQ_RESPONSE_WITH_RENAME);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

static GtkWidget *furm_simple_vlabel(GtkWidget *box, const gchar *text, gboolean expand)
{
	GtkWidget *vbox;
	GtkWidget *label;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(box), vbox, expand, expand, 0);
	gtk_widget_show(vbox);

	label = gtk_label_new(text);
	gq_gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	return vbox;
}

static void file_util_dialog_init_source_dest(UtilityData *ud, gboolean second_image)
{
	GtkTreeModel *store;
	GtkWidget *box;
	GtkWidget *hbox;
	GtkWidget *box2;
	GtkWidget *table;
	GtkWidget *combo;
	GtkWidget *page;
	g_autofree gchar *destination_message = nullptr;

	ud->gd = file_util_gen_dlg(ud->messages.title, "dlg_confirm",
				   ud->parent, FALSE,  file_util_cancel_cb, ud);

	box = generic_dialog_add_message(ud->gd, nullptr, ud->messages.question, nullptr, TRUE);

	if (ud->discard_func) generic_dialog_add_button(ud->gd, GQ_ICON_REVERT, _("Discard changes"), file_util_discard_cb, FALSE);
	if (ud->details_func) generic_dialog_add_button(ud->gd, GQ_ICON_DIALOG_INFO, _("File details"), file_util_details_cb, FALSE);

	generic_dialog_add_button(ud->gd, GQ_ICON_OK, ud->messages.title, file_util_ok_cb, TRUE);

	if (ud->type == UtilityType::COPY || ud->type == UtilityType::MOVE)
		{
		destination_message = g_strconcat(ud->messages.desc_flist," to: ", ud->dest_path, NULL);
		}
	else
		{
		destination_message = g_strdup(ud->messages.desc_flist);
		}

	box = pref_group_new(box, TRUE, destination_message, GTK_ORIENTATION_HORIZONTAL);

	ud->listview = file_util_dialog_add_list(box, ud->flist, FALSE, ud->with_sidecars);
	file_util_dialog_add_list_column(ud->listview, _("Sidecars"), FALSE, UTILITY_COLUMN_SIDECARS);

	file_util_dialog_add_list_column(ud->listview, _("New name"), FALSE, UTILITY_COLUMN_DEST_NAME);

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ud->listview));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	gtk_tree_selection_set_select_function(selection, file_util_preview_cb, ud, nullptr);

	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(ud->listview), TRUE);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ud->listview));
	g_signal_connect(G_OBJECT(store), "row_changed",
			 G_CALLBACK(file_util_rename_preview_order_cb), ud);
	gtk_widget_set_size_request(ud->listview, 300, 150);

	if (second_image)
		{
		generic_dialog_add_image(ud->gd, box, nullptr, _("Source"), TRUE, nullptr, _("Destination"), TRUE);
		}
	else
		{
		generic_dialog_add_image(ud->gd, box, nullptr, nullptr, FALSE, nullptr, nullptr, FALSE);
		}

	gtk_widget_show(ud->gd->dialog);


	ud->notebook = gtk_notebook_new();

	gq_gtk_box_pack_start(GTK_BOX(ud->gd->vbox), ud->notebook, FALSE, FALSE, 0);
	gtk_widget_show(ud->notebook);


	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_notebook_append_page(GTK_NOTEBOOK(ud->notebook), page, gtk_label_new(_("Manual rename")));
	gtk_widget_show(page);

	table = pref_table_new(page, 2, 2, FALSE, FALSE);

	pref_table_label(table, 0, 0, _("Original name:"), GTK_ALIGN_END);
	ud->rename_label = pref_table_label(table, 1, 0, "", GTK_ALIGN_START);

	pref_table_label(table, 0, 1, _("New name:"), GTK_ALIGN_END);

	ud->rename_entry = gtk_entry_new();
	gq_gtk_grid_attach(GTK_GRID(table), ud->rename_entry, 1, 2, 1, 2, static_cast<GtkAttachOptions>(GTK_EXPAND | GTK_FILL), static_cast<GtkAttachOptions>(0), 0, 0);
	generic_dialog_attach_default(GENERIC_DIALOG(ud->gd), ud->rename_entry);
	gtk_widget_grab_focus(ud->rename_entry);

	g_signal_connect(G_OBJECT(ud->rename_entry), "changed",
			 G_CALLBACK(file_util_rename_preview_entry_cb), ud);

	gtk_widget_show(ud->rename_entry);

	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_notebook_append_page(GTK_NOTEBOOK(ud->notebook), page, gtk_label_new(_("Auto rename")));
	gtk_widget_show(page);


	hbox = pref_box_new(page, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

	box2 = furm_simple_vlabel(hbox, _("Begin text"), TRUE);

	combo = history_combo_new(&ud->auto_entry_front, "", "numerical_rename_prefix", -1);
	g_signal_connect(G_OBJECT(ud->auto_entry_front), "changed",
			 G_CALLBACK(file_util_rename_preview_entry_cb), ud);
	gq_gtk_box_pack_start(GTK_BOX(box2), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	box2 = furm_simple_vlabel(hbox, _("Start #"), FALSE);

	ud->auto_spin_start = pref_spin_new(box2, nullptr, nullptr,
					    0.0, 1000000.0, 1.0, 0, options->cp_mv_rn.auto_start,
					    G_CALLBACK(file_util_rename_preview_adj_cb), ud);

	box2 = furm_simple_vlabel(hbox, _("End text"), TRUE);

	combo = history_combo_new(&ud->auto_entry_end, options->cp_mv_rn.auto_end, "numerical_rename_suffix", -1);
	g_signal_connect(G_OBJECT(ud->auto_entry_end), "changed",
			 G_CALLBACK(file_util_rename_preview_entry_cb), ud);
	gq_gtk_box_pack_start(GTK_BOX(box2), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	ud->auto_spin_pad = pref_spin_new(page, _("Padding:"), nullptr,
					  1.0, 8.0, 1.0, 0, options->cp_mv_rn.auto_padding,
					  G_CALLBACK(file_util_rename_preview_adj_cb), ud);

	page = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_notebook_append_page(GTK_NOTEBOOK(ud->notebook), page, gtk_label_new(_("Formatted rename")));
	gtk_widget_show(page);

	hbox = pref_box_new(page, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

	box2 = furm_simple_vlabel(hbox, _("Format (* = original name, ## = numbers)"), TRUE);

	combo = history_combo_new(&ud->format_entry, "", "auto_rename_format", -1);
	g_signal_connect(G_OBJECT(ud->format_entry), "changed",
			 G_CALLBACK(file_util_rename_preview_entry_cb), ud);
	gq_gtk_box_pack_start(GTK_BOX(box2), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	box2 = furm_simple_vlabel(hbox, _("Start #"), FALSE);

	ud->format_spin = pref_spin_new(box2, nullptr, nullptr,
					0.0, 1000000.0, 1.0, 0, options->cp_mv_rn.formatted_start,
					G_CALLBACK(file_util_rename_preview_adj_cb), ud);

	file_util_dialog_list_select(ud->listview, 0);
}

static void file_util_finalize_all(UtilityData *ud)
{
	GList *work = ud->flist;

	if (ud->phase == UtilityPhase::CANCEL) return;
	if (ud->phase == UtilityPhase::DONE && !ud->finalize_func) return;
	if (ud->phase == UtilityPhase::DISCARD && !ud->discard_func) return;

	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		if (ud->phase == UtilityPhase::DONE) ud->finalize_func(fd);
		else if (ud->phase == UtilityPhase::DISCARD) ud->discard_func(fd);
		}
}

static gboolean file_util_exclude_fd(UtilityData *ud, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;

	if (!g_list_find(ud->flist, fd)) return FALSE;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(ud->listview));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		FileData *store_fd;
		gtk_tree_model_get(store, &iter, UTILITY_COLUMN_FD, &store_fd, -1);

		if (store_fd == fd)
			{
			gtk_list_store_remove(GTK_LIST_STORE(store), &iter);
			break;
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}

	ud->flist = g_list_remove(ud->flist, fd);

	if (ud->with_sidecars)
		file_data_sc_free_ci(fd);
	else
		file_data_free_ci(fd);

	file_data_unref(fd);
	return TRUE;
}

void file_util_dialog_run(UtilityData *ud)
{
	switch (ud->phase)
		{
		case UtilityPhase::START:
			/* create the dialogs */
			switch (ud->type)
				{
				case UtilityType::DELETE:
				case UtilityType::DELETE_LINK:
				case UtilityType::DELETE_FOLDER:
				case UtilityType::EDITOR:
				case UtilityType::WRITE_METADATA:
					file_util_dialog_init_simple_list(ud);
					ud->phase = UtilityPhase::ENTERING;
					break;
				case UtilityType::RENAME:
					file_util_dialog_init_source_dest(ud, TRUE);

					GdkRectangle rect;
					if (!options->save_dialog_window_positions || !generic_dialog_find_window("Rename", "dlg_confirm", rect))
						{
						gq_gtk_window_resize(GTK_WINDOW(ud->gd->dialog), RENAME_WINDOW_WIDTH, RENAME_WINDOW_HEIGHT);
						}
					ud->phase = UtilityPhase::ENTERING;
					break;
				case UtilityType::COPY:
				case UtilityType::MOVE:
					file_util_dialog_init_dest_folder(ud);
					ud->phase = UtilityPhase::INTERMEDIATE;
					break;
				case UtilityType::FILTER:
					file_util_dialog_init_dest_folder(ud);
					ud->phase = UtilityPhase::ENTERING;
					break;
				case UtilityType::RENAME_FOLDER:
					ud->phase = UtilityPhase::CANCEL; /**< @FIXME not handled for now */
					file_util_dialog_run(ud);
					return;
				}
			break;
		case UtilityPhase::INTERMEDIATE:
			switch (ud->type)
				{
				case UtilityType::COPY:
				case UtilityType::MOVE:
					file_util_dialog_init_source_dest(ud, TRUE);
					break;
				default:;
				}
			ud->phase = UtilityPhase::ENTERING;
			break;
		case UtilityPhase::ENTERING:
			file_util_check_ci(ud);
			break;
		case UtilityPhase::CHECKED:
			file_util_perform_ci(ud);
			break;
		case UtilityPhase::CANCEL:
		case UtilityPhase::DONE:
		case UtilityPhase::DISCARD:

			file_util_finalize_all(ud);

			/* both DISCARD and DONE finishes the operation for good */
			if (ud->done_func)
				ud->done_func((ud->phase != UtilityPhase::CANCEL), ud->dest_path);

			if (ud->with_sidecars)
				file_data_sc_free_ci_list(ud->flist);
			else
				file_data_free_ci_list(ud->flist);

			/* directory content is always handled including sidecars */
			file_data_sc_free_ci_list(ud->content_list);

			if (ud->dir_fd) file_data_free_ci(ud->dir_fd);
			file_util_data_free(ud);
			break;
		}
}




static void file_util_warn_op_in_progress(const gchar *title)
{
	file_util_warning_dialog(title, _("Another operation in progress.\n"), GQ_ICON_DIALOG_ERROR, nullptr);
}

static void file_util_details_dialog_close_cb(GtkWidget *, gpointer data)
{
	gq_gtk_widget_destroy(static_cast<GtkWidget *>(data));
}

static void file_util_details_dialog_destroy_cb(GtkWidget *widget, gpointer data)
{
	auto ud = static_cast<UtilityData *>(data);
	g_signal_handlers_disconnect_by_func(ud->gd->dialog, (gpointer)(file_util_details_dialog_close_cb), widget);
}


static void file_util_details_dialog_exclude(GenericDialog *gd, gpointer data, gboolean discard)
{
	auto ud = static_cast<UtilityData *>(data);
	auto fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(gd->dialog), "file_data"));

	if (!fd) return;
	file_util_exclude_fd(ud, fd);

	if (discard && ud->discard_func) ud->discard_func(fd);

	/* all files were excluded, this has the same effect as pressing the cancel button in the confirmation dialog*/
	if (!ud->flist)
		{
		/* both dialogs will be closed anyway, the signals would cause duplicate calls */
		g_signal_handlers_disconnect_by_func(ud->gd->dialog, (gpointer)(file_util_details_dialog_close_cb), gd->dialog);
		g_signal_handlers_disconnect_by_func(gd->dialog, (gpointer)(file_util_details_dialog_destroy_cb), ud);

		file_util_cancel_cb(ud->gd, ud);
		}
}

static void file_util_details_dialog_exclude_cb(GenericDialog *gd, gpointer data)
{
	file_util_details_dialog_exclude(gd, data, FALSE);
}

static void file_util_details_dialog_discard_cb(GenericDialog *gd, gpointer data)
{
	file_util_details_dialog_exclude(gd, data, TRUE);
}

static gchar *file_util_details_get_message(UtilityData *ud, FileData *fd, const gchar **icon_name)
{
	GString *message = g_string_new("");
	gint error;
	g_string_append_printf(message, _("File: '%s'\n"), fd->path);

	if (ud->with_sidecars && fd->sidecar_files)
		{
		GList *work = fd->sidecar_files;
		g_string_append(message, _("with sidecar files:\n"));

		while (work)
			{
			auto sfd = static_cast<FileData *>(work->data);
			work =work->next;
			g_string_append_printf(message, _(" '%s'\n"), sfd->path);
			}
		}

	g_string_append(message, _("\nStatus: "));

	error = ud->with_sidecars ? file_data_sc_verify_ci(fd, ud->flist) : file_data_verify_ci(fd, ud->flist);

	if (error)
		{
		g_autofree gchar *err_msg = file_data_get_error_string(error);
		g_string_append(message, err_msg);
		if (icon_name) *icon_name = (error & CHANGE_ERROR_MASK) ? GQ_ICON_DIALOG_ERROR : GQ_ICON_DIALOG_WARNING;
		}
	else
		{
		g_string_append(message, _("no problem detected"));
		if (icon_name) *icon_name = GQ_ICON_DIALOG_INFO;
		}

	return g_string_free(message, FALSE);;
}

static void file_util_details_dialog(UtilityData *ud, FileData *fd)
{
	GenericDialog *gd;
	GtkWidget *box;
	const gchar *icon_name;

	gd = file_util_gen_dlg(_("File details"), "details", ud->gd->dialog, TRUE, nullptr, ud);
	generic_dialog_add_button(gd, GQ_ICON_CLOSE, _("Close"), generic_dialog_dummy_cb, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_REMOVE, _("Exclude file"), file_util_details_dialog_exclude_cb, FALSE);

	g_object_set_data(G_OBJECT(gd->dialog), "file_data", fd);

	g_signal_connect(G_OBJECT(gd->dialog), "destroy",
			 G_CALLBACK(file_util_details_dialog_destroy_cb), ud);

	/* in case the ud->gd->dialog is closed during editing */
	g_signal_connect(G_OBJECT(ud->gd->dialog), "destroy",
			 G_CALLBACK(file_util_details_dialog_close_cb), gd->dialog);

	g_autofree gchar *message = file_util_details_get_message(ud, fd, &icon_name);

	box = generic_dialog_add_message(gd, icon_name, _("File details"), message, TRUE);

	generic_dialog_add_image(gd, box, fd, nullptr, FALSE, nullptr, nullptr, FALSE);

	gtk_widget_show(gd->dialog);
}

static void file_util_write_metadata_details_dialog(UtilityData *ud, FileData *fd)
{
	GenericDialog *gd;
	GtkWidget *box;
	GtkWidget *table;
	GtkWidget *frame;
	GtkWidget *label;
	GList *keys = nullptr;
	GList *work;
	g_autofree gchar *message2 = nullptr;
	gint i;
	const gchar *icon_name;

	if (fd && fd->modified_xmp)
		{
		keys = g_hash_table_get_keys(fd->modified_xmp);
		}

	g_assert(keys);


	gd = file_util_gen_dlg(_("Overview of changed metadata"), "details", ud->gd->dialog, TRUE, nullptr, ud);
	generic_dialog_add_button(gd, GQ_ICON_CLOSE, _("Close"), generic_dialog_dummy_cb, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_REMOVE, _("Exclude file"), file_util_details_dialog_exclude_cb, FALSE);
	generic_dialog_add_button(gd, GQ_ICON_REVERT, _("Discard changes"), file_util_details_dialog_discard_cb, FALSE);

	g_object_set_data(G_OBJECT(gd->dialog), "file_data", fd);

	g_signal_connect(G_OBJECT(gd->dialog), "destroy",
			 G_CALLBACK(file_util_details_dialog_destroy_cb), ud);

	/* in case the ud->gd->dialog is closed during editing */
	g_signal_connect(G_OBJECT(ud->gd->dialog), "destroy",
			 G_CALLBACK(file_util_details_dialog_close_cb), gd->dialog);

	g_autofree gchar *message1 = file_util_details_get_message(ud, fd, &icon_name);

	if (fd->change && fd->change->dest)
		{
		message2 = g_strdup_printf(_("The following metadata tags will be written to\n'%s'."), fd->change->dest);
		}
	else
		{
		message2 = g_strdup_printf("%s", _("The following metadata tags will be written to the image file itself."));
		}

	box = generic_dialog_add_message(gd, icon_name, _("Overview of changed metadata"), message1, TRUE);

	box = pref_group_new(box, TRUE, message2, GTK_ORIENTATION_HORIZONTAL);

	frame = pref_frame_new(box, TRUE, nullptr, GTK_ORIENTATION_HORIZONTAL, 2);
	table = pref_table_new(frame, 2, g_list_length(keys), FALSE, TRUE);

	work = keys;
	i = 0;
	while (work)
		{
		auto key = static_cast<const gchar *>(work->data);
		g_autofree gchar *title = exif_get_description_by_key(key);
		g_autofree gchar *title_f = g_strdup_printf("%s:", title);
		g_autofree gchar *value = metadata_read_string(fd, key, METADATA_FORMATTED);
		work = work->next;

		label = gtk_label_new(title_f);
		gtk_label_set_xalign(GTK_LABEL(label), 1.0);
		gtk_label_set_yalign(GTK_LABEL(label), 0.0);

		pref_label_bold(label, TRUE, FALSE);
		gq_gtk_grid_attach(GTK_GRID(table), label, 0, 1, i, i + 1,  GTK_FILL, GTK_FILL,  2, 2);
		gtk_widget_show(label);

		label = gtk_label_new(value);

		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_yalign(GTK_LABEL(label), 0.0);

		gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
		gq_gtk_grid_attach(GTK_GRID(table), label,  1, 2, i, i + 1, GTK_FILL, GTK_FILL,  2, 2);
		gtk_widget_show(label);

		i++;
		}

	generic_dialog_add_image(gd, box, fd, nullptr, FALSE, nullptr, nullptr, FALSE);

	gtk_widget_set_size_request(gd->dialog, DIALOG_WIDTH, -1);
	gtk_widget_show(gd->dialog);

	g_list_free(keys);
}


static void file_util_mark_ungrouped_files(GList *work)
{
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		file_data_set_regroup_when_finished(fd, TRUE);
		work = work->next;
		}
}

static void file_util_delete_full(FileData *source_fd, GList *flist, GtkWidget *parent, gboolean safe_delete, UtilityPhase phase, const FileUtilDoneFunc &done_func)
{
	if (source_fd)
		flist = g_list_append(flist, file_data_ref(source_fd));

	if (!flist) return;

	options->file_ops.safe_delete_enable = safe_delete;

	g_autoptr(FileDataList) ungrouped = nullptr;
	flist = file_data_process_groups_in_selection(flist, TRUE, &ungrouped);

	if (!file_data_sc_add_ci_delete_list(flist))
		{
		file_util_warn_op_in_progress(_("File deletion failed"));
		file_data_disable_grouping_list(ungrouped, FALSE);
		file_data_list_free(flist);
		return;
		}

	file_util_mark_ungrouped_files(ungrouped);

	UtilityData *ud = file_util_data_new(UtilityType::DELETE);

	ud->phase = phase;

	ud->with_sidecars = TRUE;

	ud->dir_fd = nullptr;
	ud->flist = flist;
	ud->content_list = nullptr;
	ud->parent = parent;
	ud->done_func = done_func;

	ud->details_func = file_util_details_dialog;

	const gchar *message;
	if (g_list_length(flist) > 1)
		{
		// @fixme message from here is never freed
		if(options->file_ops.safe_delete_enable)
			{
			message = g_strdup_printf("%s%d%s", _("⚠ This will move the following    "), g_list_length(flist), _("    files to the Trash bin"));
			}
		else
			{
			message = g_strdup_printf("%s%d%s",_("⚠ This will permanently delete the following    "), g_list_length(flist), _("    files"));
			}
		ud->messages.question = _("Delete files?");
		}
	else
		{
		if(options->file_ops.safe_delete_enable)
			{
			message = _("This will move the following file to the Trash bin");
			}
		else
			{
			message = _("This will permanently delete the following file");
			}
		ud->messages.question = _("Delete file?");
		}

	ud->messages.title = _("Delete");
	ud->messages.desc_flist = message;
	ud->messages.desc_source_fd = "";
	ud->messages.fail = _("File deletion failed");

	file_util_dialog_run(ud);
}


static void file_util_write_metadata_full(FileData *source_fd, GList *flist, GtkWidget *parent, UtilityPhase phase, const FileUtilDoneFunc &done_func)
{
	UtilityData *ud;

	if (source_fd)
		flist = g_list_append(flist, file_data_ref(source_fd));

	if (!flist) return;

	if (!file_data_add_ci_write_metadata_list(flist))
		{
		file_util_warn_op_in_progress(_("Can't write metadata"));
		file_data_list_free(flist);
		return;
		}

	ud = file_util_data_new(UtilityType::WRITE_METADATA);

	ud->phase = phase;

	ud->with_sidecars = FALSE; /* operate on individual files, not groups */

	ud->dir_fd = nullptr;
	ud->flist = flist;
	ud->content_list = nullptr;
	ud->parent = parent;

	ud->done_func = done_func;

	ud->details_func = file_util_write_metadata_details_dialog;
	ud->finalize_func = metadata_write_queue_remove;
	ud->discard_func = metadata_write_queue_remove;

	ud->messages.title = _("Write metadata");
	ud->messages.question = _("Write metadata?");
	ud->messages.desc_flist = _("This will write the changed metadata into the following files");
	ud->messages.desc_source_fd = "";
	ud->messages.fail = _("Metadata writing failed");

	file_util_dialog_run(ud);
}

static void file_util_move_full(FileData *source_fd, GList *flist, const gchar *dest_path, GtkWidget *parent, UtilityPhase phase)
{
	UtilityData *ud;

	if (source_fd)
		flist = g_list_append(flist, file_data_ref(source_fd));

	if (!flist) return;

	g_autoptr(FileDataList) ungrouped = nullptr;
	flist = file_data_process_groups_in_selection(flist, TRUE, &ungrouped);

	if (!file_data_sc_add_ci_move_list(flist, dest_path))
		{
		file_util_warn_op_in_progress(_("Move failed"));
		file_data_disable_grouping_list(ungrouped, FALSE);
		file_data_list_free(flist);
		return;
		}

	file_util_mark_ungrouped_files(ungrouped);

	ud = file_util_data_new(UtilityType::MOVE);

	ud->phase = phase;

	ud->with_sidecars = TRUE;

	ud->dir_fd = nullptr;
	ud->flist = flist;
	ud->content_list = nullptr;
	ud->parent = parent;
	ud->details_func = file_util_details_dialog;

	if (dest_path) ud->dest_path = g_strdup(dest_path);

	ud->messages.title = _("Move");
	ud->messages.question = _("Move files?");
	ud->messages.desc_flist = _("This will move the following files");
	ud->messages.desc_source_fd = "";
	ud->messages.fail = _("Move failed");

	file_util_dialog_run(ud);
}

static void file_util_copy_full(FileData *source_fd, GList *flist, const gchar *dest_path, GtkWidget *parent, UtilityPhase phase)
{
	UtilityData *ud;

	if (source_fd)
		flist = g_list_append(flist, file_data_ref(source_fd));

	if (!flist) return;

	if (file_util_write_metadata_first(UtilityType::COPY, phase, flist, dest_path, nullptr, parent))
		return;

	g_autoptr(FileDataList) ungrouped = nullptr;
	flist = file_data_process_groups_in_selection(flist, TRUE, &ungrouped);

	if (!file_data_sc_add_ci_copy_list(flist, dest_path))
		{
		file_util_warn_op_in_progress(_("Copy failed"));
		file_data_disable_grouping_list(ungrouped, FALSE);
		file_data_list_free(flist);
		return;
		}

	file_util_mark_ungrouped_files(ungrouped);

	ud = file_util_data_new(UtilityType::COPY);

	ud->phase = phase;

	ud->with_sidecars = TRUE;

	ud->dir_fd = nullptr;
	ud->flist = flist;
	ud->content_list = nullptr;
	ud->parent = parent;
	ud->details_func = file_util_details_dialog;

	if (dest_path) ud->dest_path = g_strdup(dest_path);

	ud->messages.title = _("Copy");
	ud->messages.question = _("Copy files?");
	ud->messages.desc_flist = _("This will copy the following files");
	ud->messages.desc_source_fd = "";
	ud->messages.fail = _("Copy failed");

	file_util_dialog_run(ud);
}

static void file_util_rename_full(FileData *source_fd, GList *flist, const gchar *dest_path, GtkWidget *parent, UtilityPhase phase)
{
	UtilityData *ud;

	if (source_fd)
		flist = g_list_append(flist, file_data_ref(source_fd));

	if (!flist) return;

	g_autoptr(FileDataList) ungrouped = nullptr;
	flist = file_data_process_groups_in_selection(flist, TRUE, &ungrouped);

	if (!file_data_sc_add_ci_rename_list(flist, dest_path))
		{
		file_util_warn_op_in_progress(_("Rename failed"));
		file_data_disable_grouping_list(ungrouped, FALSE);
		file_data_list_free(flist);
		return;
		}

	file_util_mark_ungrouped_files(ungrouped);

	ud = file_util_data_new(UtilityType::RENAME);

	ud->phase = phase;

	ud->with_sidecars = TRUE;

	ud->dir_fd = nullptr;
	ud->flist = flist;
	ud->content_list = nullptr;
	ud->parent = parent;

	ud->details_func = file_util_details_dialog;

	ud->messages.title = _("Rename");
	ud->messages.question = _("Rename files?");
	ud->messages.desc_flist = _("This will rename the following files");
	ud->messages.desc_source_fd = "";
	ud->messages.fail = _("Rename failed");

	file_util_dialog_run(ud);
}

static void file_util_start_editor_full(const gchar *key, FileData *source_fd, GList *flist, const gchar *dest_path, const gchar *working_directory, GtkWidget *parent, UtilityPhase phase)
{
	UtilityData *ud;

	if (editor_no_param(key))
		{
		g_autofree gchar *file_directory = nullptr;
		if (!working_directory)
			{
			/* working directory was not specified, try to extract it from the files */
			if (source_fd)
				file_directory = remove_level_from_path(source_fd->path);

			if (!file_directory && flist)
				file_directory = remove_level_from_path((static_cast<FileData *>(flist->data))->path);
			working_directory = file_directory;
			}

		/* just start the editor, don't care about files */
		start_editor(key, working_directory);
		file_data_list_free(flist);
		return;
		}


	if (source_fd)
		{
		/* flist is most probably NULL
		   operate on source_fd and it's sidecars
		*/
		flist = g_list_concat(filelist_copy(source_fd->sidecar_files), flist);
		flist = g_list_append(flist, file_data_ref(source_fd));
		}

	if (!flist) return;

	if (file_util_write_metadata_first(UtilityType::FILTER, phase, flist, dest_path, key, parent))
		return;

	g_autoptr(FileDataList) ungrouped = nullptr;
	flist = file_data_process_groups_in_selection(flist, TRUE, &ungrouped);

	if (!file_data_sc_add_ci_unspecified_list(flist, dest_path))
		{
		file_util_warn_op_in_progress(_("Can't run external editor"));
		file_data_disable_grouping_list(ungrouped, FALSE);
		file_data_list_free(flist);
		return;
		}

	file_util_mark_ungrouped_files(ungrouped);

	if (editor_is_filter(key))
		ud = file_util_data_new(UtilityType::FILTER);
	else
		ud = file_util_data_new(UtilityType::EDITOR);


	/* ask for destination if we don't have it */
	if (ud->type == UtilityType::FILTER && dest_path == nullptr) phase = UtilityPhase::START;

	ud->phase = phase;

	ud->with_sidecars = TRUE;

	ud->external_command = g_strdup(key);

	ud->dir_fd = nullptr;
	ud->flist = flist;
	ud->content_list = nullptr;
	ud->parent = parent;

	ud->details_func = file_util_details_dialog;

	if (dest_path) ud->dest_path = g_strdup(dest_path);

	ud->messages.title = _("Editor");
	ud->messages.question = _("Run editor?");
	ud->messages.desc_flist = _("This will copy the following files");
	ud->messages.desc_source_fd = "";
	ud->messages.fail = _("External command failed");

	file_util_dialog_run(ud);
}

static GList *file_util_delete_dir_remaining_folders(GList *dlist)
{
	GList *rlist = nullptr;

	while (dlist)
		{
		FileData *fd;

		fd = static_cast<FileData *>(dlist->data);
		dlist = dlist->next;

		if (!fd->name ||
		    (strcmp(fd->name, THUMB_FOLDER_GLOBAL) != 0 &&
		     strcmp(fd->name, THUMB_FOLDER_LOCAL) != 0 &&
		     strcmp(fd->name, GQ_CACHE_LOCAL_METADATA) != 0) )
			{
			rlist = g_list_prepend(rlist, fd);
			}
		}

	return g_list_reverse(rlist);
}

static gboolean file_util_delete_dir_empty_path(UtilityData *ud, FileData *fd, gint level)
{
	GList *work;

	DEBUG_1("deltree into: %s", fd->path);

	level++;
	if (level > UTILITY_DELETE_MAX_DEPTH)
		{
		log_printf("folder recursion depth past %d, giving up\n", UTILITY_DELETE_MAX_DEPTH);
		// ud->fail_fd = fd
		return FALSE;
		}

	g_autoptr(FileDataList) dlist = nullptr;
	g_autoptr(FileDataList) flist = nullptr;
	if (!filelist_read_lstat(fd, &flist, &dlist))
		{
		// ud->fail_fd = fd
		return FALSE;
		}

	gboolean ok = file_data_sc_add_ci_delete(fd);
	if (ok)
		{
		ud->content_list = g_list_prepend(ud->content_list, fd);
		}
	// ud->fail_fd = fd

	work = dlist;
	while (work && ok)
		{
		FileData *lfd;

		lfd = static_cast<FileData *>(work->data);
		work = work->next;

		ok = file_util_delete_dir_empty_path(ud, lfd, level);
		}

	work = flist;
	while (work && ok)
		{
		FileData *lfd;

		lfd = static_cast<FileData *>(work->data);
		work = work->next;

		DEBUG_1("deltree child: %s", lfd->path);

		ok = file_data_sc_add_ci_delete(lfd);
		if (ok)
			{
			ud->content_list = g_list_prepend(ud->content_list, lfd);
			}
		// ud->fail_fd = fd
		}

	DEBUG_1("deltree done: %s", fd->path);

	return ok;
}

static gboolean file_util_delete_dir_prepare(UtilityData *ud, GList *flist, GList *dlist)
{
	gboolean ok = TRUE;
	GList *work;


	work = dlist;
	while (work && ok)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		ok = file_util_delete_dir_empty_path(ud, fd, 0);
		}

	work = flist;
	if (ok && file_data_sc_add_ci_delete_list(flist))
		{
		ud->content_list = g_list_concat(filelist_copy(flist), ud->content_list);
		}
	else
		{
		ok = FALSE;
		}

	if (ok)
		{
		ok = file_data_sc_add_ci_delete(ud->dir_fd);
		}

	if (!ok)
		{
		work = ud->content_list;
		while (work)
			{
			FileData *fd;

			fd = static_cast<FileData *>(work->data);
			work = work->next;
			file_data_sc_free_ci(fd);
			}
		}

	return ok;
}

static void file_util_delete_dir_full(FileData *fd, GtkWidget *parent, UtilityPhase phase)
{
	GList *dlist;
	GList *flist;
	GList *rlist;

	if (!isdir(fd->path)) return;

	if (islink(fd->path))
		{
		UtilityData *ud;
		ud = file_util_data_new(UtilityType::DELETE_LINK);

		ud->phase = phase;
		ud->with_sidecars = TRUE;
		ud->dir_fd = file_data_ref(fd);
		ud->content_list = nullptr;
		ud->flist = nullptr;

		ud->parent = parent;

		ud->messages.title = _("Delete folder");
		ud->messages.question = _("Delete symbolic link?");
		ud->messages.desc_flist = "";
		ud->messages.desc_source_fd = _("This will delete the symbolic link.\nThe folder this link points to will not be deleted.");
		ud->messages.fail = _("Link deletion failed");

		file_util_dialog_run(ud);
		return;
		}

	if (!access_file(fd->path, W_OK | X_OK))
		{
		g_autofree gchar *text = g_strdup_printf(_("Unable to remove folder %s\nPermissions do not allow writing to the folder."), fd->path);
		file_util_warning_dialog(_("Delete failed"), text, GQ_ICON_DIALOG_ERROR, parent);

		return;
		}

	if (!filelist_read_lstat(fd, &flist, &dlist))
		{
		g_autofree gchar *text = g_strdup_printf(_("Unable to list contents of folder %s"), fd->path);
		file_util_warning_dialog(_("Delete failed"), text, GQ_ICON_DIALOG_ERROR, parent);

		return;
		}

	rlist = file_util_delete_dir_remaining_folders(dlist);
	if (rlist)
		{
		GenericDialog *gd;
		GtkWidget *box;

		gd = file_util_gen_dlg(_("Folder contains subfolders"), "dlg_warning",
					parent, TRUE, nullptr, nullptr);
		generic_dialog_add_button(gd, GQ_ICON_CLOSE, _("Close"), nullptr, TRUE);

		g_autofree gchar *text = g_strdup_printf(_("Unable to delete the folder:\n\n%s\n\nThis folder contains subfolders which must be moved before it can be deleted."),
					fd->path);
		box = generic_dialog_add_message(gd, GQ_ICON_DIALOG_WARNING,
						 _("Folder contains subfolders"),
						 text, TRUE);

		box = pref_group_new(box, TRUE, _("Subfolders:"), GTK_ORIENTATION_VERTICAL);

		rlist = filelist_sort_path(rlist);
		file_util_dialog_add_list(box, rlist, FALSE, FALSE);

		gtk_widget_show(gd->dialog);
		}
	else
		{
		UtilityData *ud;
		ud = file_util_data_new(UtilityType::DELETE_FOLDER);

		ud->phase = phase;
		ud->with_sidecars = TRUE;
		ud->dir_fd = file_data_ref(fd);
		ud->content_list = nullptr; /* will be filled by file_util_delete_dir_prepare */
		ud->flist = flist = filelist_sort_path(flist);

		ud->parent = parent;

		ud->messages.title = _("Delete folder");
		ud->messages.question = _("Delete folder?");
		ud->messages.desc_flist = _("The folder contains these files:");
		ud->messages.desc_source_fd = _("This will delete the folder.\nThe contents of this folder will also be deleted.");
		ud->messages.fail = _("File deletion failed");

		if (!file_util_delete_dir_prepare(ud, flist, dlist))
			{
			g_autofree gchar *text = g_strdup_printf(_("Unable to list contents of folder %s"), fd->path);
			file_util_warning_dialog(_("Delete failed"), text, GQ_ICON_DIALOG_ERROR, parent);
			file_data_unref(ud->dir_fd);
			file_util_data_free(ud);
			}
		else
			{
			file_data_list_free(dlist);
			file_util_dialog_run(ud);
			return;
			}
		}

	g_list_free(rlist);
	file_data_list_free(dlist);
	file_data_list_free(flist);
}

static gboolean file_util_rename_dir_scan(UtilityData *ud, FileData *fd)
{
	g_autoptr(FileDataList) dlist = nullptr;
	GList *flist;
	GList *work;

	gboolean ok = TRUE;

	if (!filelist_read_lstat(fd, &flist, &dlist))
		{
		// ud->fail_fd = fd
		return FALSE;
		}

	ud->content_list = g_list_concat(flist, ud->content_list);

	work = dlist;
	while (work && ok)
		{
		FileData *lfd;

		lfd = static_cast<FileData *>(work->data);
		work = work->next;

		ud->content_list = g_list_prepend(ud->content_list, file_data_ref(lfd));
		ok = file_util_rename_dir_scan(ud, lfd);
		}

	return ok;
}

static gboolean file_util_rename_dir_prepare(UtilityData *ud, const gchar *new_path)
{
	gboolean ok;
	GList *work;
	gint orig_len = strlen(ud->dir_fd->path);

	ok = file_util_rename_dir_scan(ud, ud->dir_fd);

	work = ud->content_list;

	while (ok && work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		g_assert(strncmp(fd->path, ud->dir_fd->path, orig_len) == 0);

		g_autofree gchar *np = g_strconcat(new_path, fd->path + orig_len, NULL);

		ok = file_data_sc_add_ci_rename(fd, np);

		DEBUG_1("Dir rename: %s -> %s", fd->path, np);
		}

	if (ok)
		{
		ok = file_data_sc_add_ci_rename(ud->dir_fd, new_path);
		}

	if (!ok)
		{
		work = ud->content_list;
		while (work)
			{
			FileData *fd;

			fd = static_cast<FileData *>(work->data);
			work = work->next;
			file_data_sc_free_ci(fd);
			}
		}

	return ok;
}


static void file_util_rename_dir_full(FileData *fd, const gchar *new_path, GtkWidget *parent, UtilityPhase phase, const FileUtilDoneFunc &done_func)
{
	UtilityData *ud;

	ud = file_util_data_new(UtilityType::RENAME_FOLDER);

	ud->phase = phase;
	ud->with_sidecars = TRUE; /* does not matter, the directory should not have sidecars
	                            and the content must be handled including sidecars */

	ud->dir_fd = file_data_ref(fd);
	ud->flist = nullptr;
	ud->content_list = nullptr;
	ud->parent = parent;

	ud->done_func = done_func;
	ud->dest_path = g_strdup(new_path);

	ud->messages.title = _("Rename");
	ud->messages.question = _("Rename folder?");
	ud->messages.desc_flist = _("The folder contains the following files");
	ud->messages.desc_source_fd = "";
	ud->messages.fail = _("Rename failed");

	if (!file_util_rename_dir_prepare(ud, new_path))
		{
		file_util_warn_op_in_progress(ud->messages.fail);
		file_util_data_free(ud);
		return;
		}

	file_util_dialog_run(ud);
}

static gboolean file_util_write_metadata_first_after_done(gpointer data)
{
	auto dd = static_cast<UtilityDelayData *>(data);

	/* start the delayed operation with original arguments */
	switch (dd->type)
		{
		case UtilityType::FILTER:
		case UtilityType::EDITOR:
			file_util_start_editor_full(dd->editor_key, nullptr, dd->flist, dd->dest_path, nullptr, dd->parent, dd->phase);
			break;
		case UtilityType::COPY:
			file_util_copy_full(nullptr, dd->flist, dd->dest_path, dd->parent, dd->phase);
			break;
		default:
			g_warning("unsupported type");
		}
	g_free(dd->dest_path);
	g_free(dd->editor_key);
	g_free(dd);
	return G_SOURCE_REMOVE;
}

static gboolean file_util_write_metadata_first(UtilityType type, UtilityPhase phase, GList *flist, const gchar *dest_path, const gchar *editor_key, GtkWidget *parent)
{
	GList *unsaved = nullptr;
	UtilityDelayData *dd;

	GList *work;

	work = flist;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (fd->change)
			{
			file_data_list_free(unsaved);
			return FALSE; /* another op. in progress, let the caller handle it */
			}

		if (fd->modified_xmp) /* has unsaved metadata */
			{
			unsaved = g_list_prepend(unsaved, file_data_ref(fd));
			}
		}

	if (!unsaved) return FALSE;

	/* save arguments of the original operation */

	dd = g_new0(UtilityDelayData, 1);

	dd->type = type;
	dd->phase = phase;
	dd->flist = flist;
	dd->dest_path = g_strdup(dest_path);
	dd->editor_key = g_strdup(editor_key);
	dd->parent = parent;

	const auto file_util_write_metadata_first_done = [dd](gboolean success, const gchar *)
	{
		if (success)
			{
			dd->idle_id = g_idle_add(file_util_write_metadata_first_after_done, dd);
			return;
			}

		/* the operation was cancelled */
		file_data_list_free(dd->flist);
		g_free(dd->dest_path);
		g_free(dd->editor_key);
		g_free(dd);
	};
	file_util_write_metadata(nullptr, unsaved, parent, FALSE, file_util_write_metadata_first_done);
	return TRUE;
}


/* full-featured entry points
*/

void file_util_delete(FileData *source_fd, GList *source_list, GtkWidget *parent, gboolean safe_delete)
{
	const gboolean confirm = safe_delete ? options->file_ops.confirm_move_to_trash : options->file_ops.confirm_delete;

	file_util_delete_full(source_fd, source_list, parent, safe_delete, confirm ? UtilityPhase::START : UtilityPhase::ENTERING, nullptr);
}

void file_util_delete_notify_done(FileData *source_fd, GList *source_list, GtkWidget *parent, gboolean safe_delete, const FileUtilDoneFunc &done_func)
{
	file_util_delete_full(source_fd, source_list, parent, safe_delete, options->file_ops.confirm_delete ? UtilityPhase::START : UtilityPhase::ENTERING, done_func);
}

void file_util_write_metadata(FileData *source_fd, GList *source_list, GtkWidget *parent, gboolean force_dialog, const FileUtilDoneFunc &done_func)
{
	file_util_write_metadata_full(source_fd, source_list, parent,
	                              ((options->metadata.save_in_image_file && options->metadata.confirm_write) || force_dialog) ? UtilityPhase::START : UtilityPhase::ENTERING,
	                              done_func);
}

void file_util_copy(FileData *source_fd, GList *source_list, const gchar *dest_path, GtkWidget *parent)
{
	file_util_copy_full(source_fd, source_list, dest_path, parent, UtilityPhase::START);
}

void file_util_move(FileData *source_fd, GList *source_list, const gchar *dest_path, GtkWidget *parent)
{
	file_util_move_full(source_fd, source_list, dest_path, parent, UtilityPhase::START);
}

void file_util_rename(FileData *source_fd, GList *source_list, GtkWidget *parent)
{
	file_util_rename_full(source_fd, source_list, nullptr, parent, UtilityPhase::START);
}

/* these avoid the location entry dialog unless there is an error, list must be files only and
 * dest_path must be a valid directory path
 */
void file_util_move_simple(GList *list, const gchar *dest_path, GtkWidget *parent)
{
	file_util_move_full(nullptr, list, dest_path, parent, UtilityPhase::ENTERING);
}

void file_util_copy_simple(GList *list, const gchar *dest_path, GtkWidget *parent)
{
	file_util_copy_full(nullptr, list, dest_path, parent, UtilityPhase::ENTERING);
}

void file_util_rename_simple(FileData *fd, const gchar *dest_path, GtkWidget *parent)
{
	file_util_rename_full(fd, nullptr, dest_path, parent, UtilityPhase::ENTERING);
}


void file_util_start_editor_from_file(const gchar *key, FileData *fd, GtkWidget *parent)
{
	file_util_start_editor_full(key, fd, nullptr, nullptr, nullptr, parent, UtilityPhase::ENTERING);
}

void file_util_start_editor_from_filelist(const gchar *key, GList *list, const gchar *working_directory, GtkWidget *parent)
{
	file_util_start_editor_full(key, nullptr, list, nullptr, working_directory, parent, UtilityPhase::ENTERING);
}

void file_util_start_filter_from_filelist(const gchar *key, GList *list, const gchar *dest_path, GtkWidget *parent)
{
	file_util_start_editor_full(key, nullptr, list, dest_path, nullptr, parent, UtilityPhase::ENTERING);
}

void file_util_delete_dir(FileData *fd, GtkWidget *parent)
{
	file_util_delete_dir_full(fd, parent, UtilityPhase::START);
}

struct CreateFolderdData
{
	FileUtilDoneFunc done_func;
};

static void create_folder_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	auto cfd = static_cast<CreateFolderdData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
#if HAVE_GTK4
		g_autoptr(GFile) folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(chooser));
		g_autofree gchar *current_folder = g_file_get_path(folder);
#else
		g_autofree gchar *current_folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(chooser));
#endif
		if (g_file_test(current_folder, G_FILE_TEST_IS_DIR))
			{
			cfd->done_func(TRUE, current_folder);
			}
		else
			{
			log_printf("warning, folder %s was not created\n", current_folder);

			g_autoptr(GNotification) notification = g_notification_new("Geeqie");
			GApplication *app = g_application_get_default ();

			g_notification_set_title(notification, _("Create Folder"));
			g_notification_set_body(notification, _("Folder was not created"));
			g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_URGENT);
			g_notification_set_default_action(notification, "app.null");

			g_application_send_notification(G_APPLICATION(app), "folder-not-created-notification", notification);
			}
		}

	g_free(cfd);
	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

void file_util_create_dir(const gchar *path, GtkWidget *parent, const FileUtilDoneFunc &done_func)
{
	if (!GTK_IS_WINDOW(parent))
		{
		parent = widget_get_toplevel(parent);
		}

	auto cfd = g_new0(CreateFolderdData, 1);
	cfd->done_func = done_func;

	FileChooserDialogData fcdd{};

	/* The select-folder interface is simpler for creating a new
	 * folder than the create-folder interface
	 */
	fcdd.action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	fcdd.accept_text = _("Close");
	fcdd.data = cfd;
	fcdd.filename = path;
	fcdd.response_callback = G_CALLBACK(create_folder_cb);
	fcdd.title = _("Geeqie - Create Folder");

	GtkFileChooserDialog *dialog = file_chooser_dialog_new(fcdd);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

void file_util_rename_dir(FileData *source_fd, const gchar *new_path, GtkWidget *parent, const FileUtilDoneFunc &done_func)
{
	file_util_rename_dir_full(source_fd, new_path, parent, UtilityPhase::ENTERING, done_func);
}

#if HAVE_GTK4
static GdkContentProvider * clipboard_build_provider(ClipboardData *cbd)
{
	g_autoptr(GdkContentProvider) provider = gdk_content_provider_new_union(NULL, 0);

	GList *work = cbd->path_list;

	/* Plain text version */
	g_autoptr(GString) text = g_string_new("");

	while (work)
		{
		const gchar *path = (const gchar *)work->data;
		work = work->next;

		if (cbd->quoted)
			{
			g_autofree gchar *q = g_shell_quote(path);
			g_string_append(text, q);
			}
		else
			{
			g_string_append(text, path);
			}

		if (work)
			{
			g_string_append_c(text, ' ');
			}
		}

	gdk_content_provider_union_add( provider, gdk_content_provider_new_typed(G_TYPE_STRING, g_strdup(text->str)));

	/* GNOME copied-files format */
	g_autoptr(GString) copied = g_string_new(cbd->action == ClipboardAction::CUT ? "cut" : "copy");

	work = cbd->path_list;
	while (work)
		{
		g_autofree gchar *uri =
			g_filename_to_uri((gchar *)work->data, NULL, NULL);
		g_string_append(copied, "\n");
		g_string_append(copied, uri);
		work = work->next;
		}

	gdk_content_provider_union_add(provider, gdk_content_provider_new_for_bytes("application/x-special-gnome-copied-files", g_bytes_new(copied->str, copied->len)));

	return g_steal_pointer(&provider);
}

static gboolean path_list_to_clipboard(GList *path_list, gboolean quoted, ClipboardAction action, GdkAtom)
{
	ClipboardData cbd = {};
	cbd.path_list = path_list;
	cbd.quoted = quoted;
	cbd.action = action;

	GdkDisplay *display = gdk_display_get_default();
	if (!display)
		{
		return FALSE;
		}

	GdkClipboard *clipboard = gdk_display_get_clipboard(display);
	if (!clipboard)
		{
		return FALSE;
		}

	GdkContentProvider *provider = clipboard_build_provider(&cbd);

	gdk_clipboard_set_content(clipboard, provider);
	g_object_unref(provider);

	GdkClipboard *primary = gdk_display_get_primary_clipboard(display);
	gdk_clipboard_set_content(primary, provider);

	return TRUE;
}
#else

/**
 * @brief
 * @param clipboard
 * @param selection_data
 * @param info
 * @param data #_ClipboardData
 *
 *
 */
static void clipboard_get_func(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer data)
{
	auto cbd = static_cast<ClipboardData *>(data);
	gchar *file_path;
	GList *work;

	g_autoptr(GString) path_list_str = g_string_new("");
	work = cbd->path_list;

	if (clipboard == gtk_clipboard_get(GDK_SELECTION_CLIPBOARD) && info == CLIPBOARD_X_SPECIAL_GNOME_COPIED_FILES)
		{
		switch (cbd->action)
			{
			case ClipboardAction::COPY:
				g_string_append(path_list_str, "copy");
				break;
			case ClipboardAction::CUT:
				g_string_append(path_list_str, "cut");
				break;
			}

		while (work)
			{
			file_path = static_cast<gchar *>(work->data);
			work = work->next;

			g_autofree gchar *file_path_uri = g_filename_to_uri(file_path, nullptr, nullptr);
			g_string_append(path_list_str, "\n");
			g_string_append(path_list_str, file_path_uri);
			}
		}
	else
		{
		while (work)
			{
			file_path = static_cast<gchar *>(work->data);
			work = work->next;

			if (cbd->quoted)
				{
				g_autofree gchar *file_path_quoted = g_shell_quote(file_path);
				g_string_append(path_list_str, file_path_quoted);
				}
			else
				{
				g_string_append(path_list_str, file_path);
				}

			if (work)
				{
				g_string_append_c(path_list_str, ' ');
				}
			}
		}

	gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), 8, reinterpret_cast<guchar *>(path_list_str->str), path_list_str->len);
}

/**
 * @brief
 * @param UNUSED
 * @param data _ClipboardData
 *
 *
 */
static void clipboard_clear_func(GtkClipboard *, gpointer data)
{
	auto cbd = static_cast<ClipboardData *>(data);

	g_list_free_full(cbd->path_list, g_free);
	g_free(cbd);
}

static gboolean path_list_to_clipboard(GList *path_list, gboolean quoted, ClipboardAction action, GdkAtom selection)
{
	auto *cbd = g_new0(ClipboardData, 1);
	cbd->path_list = path_list;
	cbd->quoted = quoted;
	cbd->action = action;

	return gtk_clipboard_set_with_data(gtk_clipboard_get(selection), target_types.data(), target_types.size(), clipboard_get_func, clipboard_clear_func, cbd);
}
#endif

/**
 * @brief
 * @param fd
 * @param quoted
 * @param action
 */
void file_util_copy_path_to_clipboard(FileData *fd, gboolean quoted, ClipboardAction action)
{
	if (!fd || !*fd->path) return;

	if (options->clipboard_selection == CLIPBOARD_PRIMARY || options->clipboard_selection == CLIPBOARD_BOTH)
		{
		GList *path_list = g_list_append(nullptr, g_strdup(fd->path));

		path_list_to_clipboard(path_list, quoted, action, GDK_SELECTION_PRIMARY);
		}

	if (options->clipboard_selection == CLIPBOARD_CLIPBOARD || options->clipboard_selection == CLIPBOARD_BOTH)
		{
		GList *path_list = g_list_append(nullptr, g_strdup(fd->path));

		path_list_to_clipboard(path_list, quoted, action, GDK_SELECTION_CLIPBOARD);
		}
}

/**
 * @brief
 * @param fd_list List of fd
 * @param quoted
 * @param action
 */
void file_util_path_list_to_clipboard(GList *fd_list, gboolean quoted, ClipboardAction action)
{
	// FIXME Is it safe to use FileList::to_path_list()?
	static const auto get_path_list = [](GList *fd_list)
	{
		GList *path_list = nullptr;

		for (GList *work = fd_list; work; work = work->next)
			{
			auto *fd = static_cast<FileData *>(work->data);

			if (!fd || !*fd->path) continue;

			path_list = g_list_append(path_list, g_strdup(fd->path));
			}

		return path_list;
	};

	if (options->clipboard_selection == CLIPBOARD_PRIMARY || options->clipboard_selection == CLIPBOARD_BOTH)
		{
		path_list_to_clipboard(get_path_list(fd_list), quoted, action, GDK_SELECTION_PRIMARY);
		}

	if (options->clipboard_selection == CLIPBOARD_CLIPBOARD || options->clipboard_selection == CLIPBOARD_BOTH)
		{
		path_list_to_clipboard(get_path_list(fd_list), quoted, action, GDK_SELECTION_CLIPBOARD);
		}

	file_data_list_free(fd_list);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
