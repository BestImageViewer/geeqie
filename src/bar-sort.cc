/*
 * Copyright (C) 2004 John Ellis
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

#include "bar-sort.h"

#include <set>
#include <string>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "actions.h"
#include "collect-io.h"
#include "collect.h"
#include "editors.h"
#include "filedata.h"
#include "history-list.h"
#include "intl.h"
#include "layout-image.h"
#include "layout.h"
#include "main-defines.h"
#include "rcfile.h"
#include "ui-bookmark.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "window.h"


/*
  *-------------------------------------------------------------------
  * sort bar
  *-------------------------------------------------------------------
  */

struct SortData : BarSort
{
	GtkWidget *vbox;
	GtkWidget *bookmarks;
	LayoutWindow *lw;

	GtkWidget *folder_group;
	GtkWidget *collection_group;
	GtkWidget *destination_hint;
	GtkWidget *folder_specific;
	gchar *source_path;
	gchar *folder_key;
	gboolean updating_folder;

	GtkWidget *add_button;
	GtkWidget *undo_button;
	BarSort::Action undo_action;
	GList *undo_src_list;
	GList *undo_dest_list;
	gchar *undo_collection;
};


#define SORT_KEY_FOLDERS     "sort_manager"
#define SORT_KEY_COLLECTIONS "sort_manager_collections"


static void bar_sort_undo_set(SortData *sd, GList *src_list, const gchar *dest);

static void bar_sort_collection_list_build(GtkWidget *bookmarks)
{
	history_list_free_key(SORT_KEY_COLLECTIONS);
	bookmark_list_set_key(bookmarks, SORT_KEY_COLLECTIONS);

	auto *dir_fd = file_data_new_dir(get_collections_dir());
	g_autoptr(FileDataList) list = nullptr;
	filelist_read(dir_fd, &list, nullptr);
	file_data_unref(dir_fd);

	std::set<std::string> paths;
	for (GList *work = list; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		paths.emplace(fd->path);
		}
	if (const HistoryList *external = history_list_find_by_key("sort_manager_external_collections"))
		{
		for (const auto &path : *external)
			{
			if (isfile(path.c_str())) paths.emplace(path);
			}
		}
	for (const auto &path : paths)
		{
		const gchar *filename = filename_from_path(path.c_str());
		g_autofree gchar *name = file_extension_match(path.c_str(), GQ_COLLECTION_EXT)
		                       ? remove_extension_from_path(filename) : g_strdup(filename);
		bookmark_list_add(bookmarks, name, path.c_str());
		}
}

static const gchar *bar_sort_destination_key(BarSort::Mode mode)
{
	return mode == BarSort::MODE_FOLDER ? "sort_manager_selected_folder" : "sort_manager_selected_collection";
}

static void bar_sort_destination_save(SortData *sd)
{
	if (sd->mode != BarSort::MODE_FOLDER && sd->mode != BarSort::MODE_COLLECTION) return;
	if (sd->mode == BarSort::MODE_FOLDER && g_strcmp0(sd->folder_key, SORT_KEY_FOLDERS) != 0) return;

	const gchar *path = bookmark_list_get_selected_path(sd->bookmarks);
	if (path) history_list_add_to_key(bar_sort_destination_key(sd->mode), path, 1);
}

static gchar *bar_sort_folder_key(const gchar *path)
{
	g_autofree gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path, -1);
	return g_strconcat("sort_manager_folder_", digest, nullptr);
}

void bar_sort_update(GtkWidget *bar)
{
	if (!bar) return;
	auto *sd = static_cast<SortData *>(g_object_get_data(G_OBJECT(bar), "bar_sort_data"));
	if (!sd) return;

	g_autofree gchar *path = sd->lw->dir_fd ? g_canonicalize_filename(sd->lw->dir_fd->path, nullptr) : nullptr;
	g_autofree gchar *key = path ? bar_sort_folder_key(path) : nullptr;
	g_autofree gchar *enabled_key = key ? g_strconcat(key, "_enabled", nullptr) : nullptr;
	const gboolean specific = enabled_key && g_strcmp0(history_list_find_last_path_by_key(enabled_key), "1") == 0;
	const gchar *folder_key = specific ? key : SORT_KEY_FOLDERS;

	sd->updating_folder = TRUE;
	gtk_widget_set_sensitive(sd->folder_specific, path != nullptr);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(sd->folder_specific), specific);
	gtk_widget_set_tooltip_text(sd->folder_specific, path);
	sd->updating_folder = FALSE;
	g_free(sd->source_path);
	sd->source_path = g_steal_pointer(&path);
	if (g_strcmp0(sd->folder_key, folder_key) == 0) return;

	if (sd->mode == BarSort::MODE_FOLDER) bar_sort_destination_save(sd);
	g_free(sd->folder_key);
	sd->folder_key = g_strdup(folder_key);
	if (sd->mode == BarSort::MODE_FOLDER)
		{
		bookmark_list_set_no_defaults(sd->bookmarks, specific);
		bookmark_list_set_key(sd->bookmarks, sd->folder_key);
		bookmark_list_set_selected_path(sd->bookmarks, nullptr);
		bar_sort_undo_set(sd, nullptr, nullptr);
		}
}

static void bar_sort_folder_specific_cb(GtkCheckButton *button, gpointer data)
{
	auto *sd = static_cast<SortData *>(data);
	if (sd->updating_folder || !sd->source_path) return;
	g_autofree gchar *key = bar_sort_folder_key(sd->source_path);
	g_autofree gchar *enabled_key = g_strconcat(key, "_enabled", nullptr);
	if (gtk_check_button_get_active(button)) history_list_add_to_key(enabled_key, "1", 1);
	else history_list_free_key(enabled_key);

	bar_sort_update(sd->vbox);
	layout_window_foreach([sd](LayoutWindow *lw)
		{
		if (lw != sd->lw) bar_sort_update(lw->bar_sort);
		});
}

static void bar_sort_mode_sync(SortData *sd, BarSort::Mode mode)
{
	if (sd->mode == mode) return;
	bar_sort_destination_save(sd);
	sd->mode = mode;

	gboolean folder_mode = (sd->mode == BarSort::MODE_FOLDER);

	bookmark_list_set_no_defaults(sd->bookmarks, !folder_mode || g_strcmp0(sd->folder_key, SORT_KEY_FOLDERS) != 0);
	bookmark_list_set_editable(sd->bookmarks, folder_mode);
	bookmark_list_set_only_directories(sd->bookmarks, folder_mode);
	bookmark_list_set_select_only_on_ctrl(sd->bookmarks, TRUE);

	if (folder_mode)
		{
		gtk_widget_set_visible(sd->collection_group, FALSE);
		gtk_widget_set_visible(sd->folder_group, TRUE);
		bookmark_list_set_key(sd->bookmarks, sd->folder_key);
		}
	else
		{
		gtk_widget_set_visible(sd->folder_group, FALSE);
		gtk_widget_set_visible(sd->collection_group, TRUE);
		bar_sort_collection_list_build(sd->bookmarks);
		}

	bookmark_list_set_selected_path(sd->bookmarks,
	                                folder_mode && g_strcmp0(sd->folder_key, SORT_KEY_FOLDERS) != 0
	                                ? nullptr : history_list_find_last_path_by_key(bar_sort_destination_key(sd->mode)));
	bar_sort_undo_set(sd, nullptr, nullptr);
}

static void bar_sort_mode_cb(GObject *object, GParamSpec *, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (gtk_drop_down_get_selected(GTK_DROP_DOWN(object)) == BarSort::MODE_FOLDER)
		{
		gtk_widget_set_tooltip_text(sd->add_button, _("Add Bookmark"));
		bar_sort_mode_sync(sd, BarSort::MODE_FOLDER);
		}
	else
		{
		gtk_widget_set_tooltip_text(sd->add_button, _("Add or create collection"));
		bar_sort_mode_sync(sd, BarSort::MODE_COLLECTION);
		}
}

/* this takes control of src_list */
static void bar_sort_undo_set(SortData *sd, GList *src_list, const gchar *dest)
{
	g_list_free_full(sd->undo_src_list, g_free);
	sd->undo_src_list = filelist_to_path_list(src_list);

	if (src_list)
		{
		/* we should create the undo_dest_list to use it later… */
		g_list_free_full(sd->undo_dest_list, g_free);
		sd->undo_dest_list=nullptr;

		for (GList *work = sd->undo_src_list; work; work = work->next)
			{
			const gchar *filename = filename_from_path(static_cast<gchar *>(work->data));
			gchar *dest_path = g_build_filename(dest, filename, NULL);
			sd->undo_dest_list = g_list_prepend(sd->undo_dest_list, dest_path);
			}
		sd->undo_dest_list = g_list_reverse(sd->undo_dest_list);
		}

	sd->undo_action = sd->action;

	if (sd->undo_button)
		{
		gtk_widget_set_sensitive(sd->undo_button,
		                         sd->undo_src_list && sd->undo_dest_list);
		}
}

static void bar_sort_undo_folder(SortData *sd, GtkWidget *button)
{
	gchar *origin;

	if (!sd->undo_src_list || !sd->undo_dest_list) return;

	switch (sd->undo_action)
		{
		case BarSort::MOVE:
			{
			GList *list = nullptr;
			for (GList *work = sd->undo_dest_list; work; work = work->next)
				{
				list = g_list_prepend(list, file_data_new_group(static_cast<gchar *>(work->data)));
				}

			const auto *src_path = static_cast<const gchar *>(sd->undo_src_list->data);
			g_autofree gchar *src_dir = remove_level_from_path(src_path);

			file_util_move_simple(list, src_dir, sd->lw->window);
			}
			break;

		case BarSort::COPY:
		case BarSort::FILTER:
			{
			GList *delete_list = nullptr;
			for (GList *work = sd->undo_dest_list; work; work = work->next)
				{
				delete_list = g_list_append(delete_list, file_data_new_group(static_cast<gchar *>(work->data)));
				}

			file_util_delete(nullptr, delete_list, button, TRUE);
			}
			break;

		default:
			break;
		}

	layout_refresh(sd->lw);
	origin = static_cast<gchar *>((sd->undo_src_list)->data);

	if (isfile(origin))
		{
		layout_image_set_fd(sd->lw, file_data_new_group(origin));
		}

	bar_sort_undo_set(sd, nullptr, nullptr);
}

static void bar_sort_undo_collection(SortData *sd)
{
	GList *work;

	work = sd->undo_src_list;
	while (work)
		{
		gchar *source;
		source = static_cast<gchar *>(work->data);
		work = work->next;
		collect_manager_remove(file_data_new_group(source), sd->undo_collection);
		}

	bar_sort_undo_set(sd, nullptr,  nullptr);
}

static void bar_sort_undo_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (sd->mode == BarSort::MODE_FOLDER)
		{
		bar_sort_undo_folder(sd, button);
		}
	else
		{
		bar_sort_undo_collection(sd);
		}
}

static void bar_sort_bookmark_select_folder(SortData *sd, const gchar *path)
{
	if (!isdir(path)) return;

	bookmark_list_set_selected_path(sd->bookmarks, path);
	g_autoptr(FileDataList) list = layout_selection_list(sd->lw);

	bar_sort_undo_set(sd, list, path);

	switch (sd->action)
		{
		case BarSort::COPY:
			file_util_copy_simple(g_steal_pointer(&list), path, sd->lw->window);
			layout_image_next(sd->lw);
			break;

		case BarSort::MOVE:
			file_util_move_simple(g_steal_pointer(&list), path, sd->lw->window);
			break;

		case BarSort::FILTER:
			file_util_start_filter_from_filelist(sd->filter_key, g_steal_pointer(&list), path, sd->lw->window);
			layout_image_next(sd->lw);
			break;

		default:
			break;
		}
}

static void bar_sort_bookmark_select_collection(SortData *sd, const gchar *path)
{
	bookmark_list_set_selected_path(sd->bookmarks, path);
	g_autoptr(FileDataList) list = nullptr;

	switch (sd->selection)
		{
		case BarSort::SELECTION_IMAGE:
			{
			FileData *source = layout_image_get_fd(sd->lw);
			if (!source) return;

			list = g_list_append(nullptr, file_data_ref(source));
			}
			break;
		case BarSort::SELECTION_SELECTED:
			list = layout_selection_list(sd->lw);
			break;
		default:
			break;
		}

	if (!list)
		{
		bar_sort_undo_set(sd, nullptr, nullptr);
		return;
		}

	bar_sort_undo_set(sd, list, path);
	g_free(sd->undo_collection);
	sd->undo_collection = g_strdup(path);

	g_list_foreach(list, reinterpret_cast<GFunc>(collect_manager_add), const_cast<gchar *>(path));
}

static void bar_sort_bookmark_drop_folder(SortData *sd, const gchar *path, GList *list)
{
	if (!isdir(path) || !list) return;

	bookmark_list_set_selected_path(sd->bookmarks, path);
	bar_sort_undo_set(sd, list, path);

	switch (sd->action)
		{
		case BarSort::COPY:
			file_util_copy_simple(filelist_copy(list), path, sd->lw->window);
			break;

		case BarSort::MOVE:
			file_util_move_simple(filelist_copy(list), path, sd->lw->window);
			break;

		case BarSort::FILTER:
			file_util_start_filter_from_filelist(sd->filter_key, filelist_copy(list), path, sd->lw->window);
			break;

		default:
			break;
		}
}

static void bar_sort_bookmark_drop_collection(SortData *sd, const gchar *path, GList *list)
{
	if (!path || !list) return;

	bookmark_list_set_selected_path(sd->bookmarks, path);
	bar_sort_undo_set(sd, list, path);
	g_free(sd->undo_collection);
	sd->undo_collection = g_strdup(path);

	g_list_foreach(list, reinterpret_cast<GFunc>(collect_manager_add), const_cast<gchar *>(path));
}

void bar_sort_add_selection(GtkWidget *bar)
{
	if (!bar || !gtk_widget_is_visible(bar)) return;

	auto *sd = static_cast<SortData *>(g_object_get_data(G_OBJECT(bar), "bar_sort_data"));
	if (!sd) return;

	g_autofree gchar *path = g_strdup(bookmark_list_get_selected_path(sd->bookmarks));
	if (!path) return;

	if (sd->mode == BarSort::MODE_FOLDER)
		{
		bar_sort_bookmark_select_folder(sd, path);
		}
	else if (isfile(path))
		{
		bar_sort_bookmark_select_collection(sd, path);
		}
}

static void bar_sort_bookmark_drop(SortData *sd, const gchar *path, GList *list)
{
	if (!path || !list) return;

	if (sd->mode == BarSort::MODE_FOLDER)
		{
		bar_sort_bookmark_drop_folder(sd, path, list);
		}
	else
		{
		bar_sort_bookmark_drop_collection(sd, path, list);
		}
}

static void bar_sort_set_action(SortData *sd, BarSort::Action action, const gchar *filter_key)
{
	sd->action = action;
	if (action == BarSort::FILTER)
		{
		if (!filter_key) filter_key = "";
		sd->filter_key = g_strdup(filter_key);
		}
	else
		{
		sd->filter_key = nullptr;
		}
}

template<BarSort::Action action>
static void bar_sort_set_action_cb(GtkWidget *button, gpointer data)
{
	if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(button))) return;

	auto *sd = static_cast<SortData *>(data);
	const auto *key = static_cast<const gchar *>(g_object_get_data(G_OBJECT(button), "filter_key"));
	bar_sort_set_action(sd, action, key);
}

static void bar_filter_help_cb(GenericDialog *, gpointer)
{
	help_window_show("GuidePluginsConfig.html#Geeqieextensions"); // NOTRANSLATE: Documentation filename.
}

static void bar_filter_help_dialog()
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Sort Manager Operations"),
				"sort_manager_operations", nullptr, TRUE, nullptr, nullptr);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_INFO,
				_("Sort Manager Operations"), _("Additional operations utilising plugins\nmay be included by setting:\n\nX-Geeqie-Filter=true\n\nin the plugin file."), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_HELP, _("Help"), bar_filter_help_cb, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", nullptr, TRUE);

	gtk_window_present(GTK_WINDOW(gd->dialog));
}

static gboolean bar_filter_message_common(guint button)
{
	if (button != GDK_BUTTON_SECONDARY) return FALSE;

	bar_filter_help_dialog();

	return TRUE;
}

static void bar_filter_message_cb(GtkGestureClick *gesture, gint, gdouble, gdouble, gpointer)
{
	bar_filter_message_common(gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));
}

static void bar_sort_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuideSidebarsSortManager.html"); // NOTRANSLATE: Documentation filename.
}

template<BarSort::Selection selection>
static void bar_sort_set_selection_cb(GtkWidget *button, gpointer data)
{
	if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(button))) return;

	auto *sd = static_cast<SortData *>(data);
	sd->selection = selection;
}

gboolean bar_sort_collection_add(const gchar *path)
{
	if (!path || !file_extension_match(path, GQ_COLLECTION_EXT)) return FALSE;

	g_autoptr(CollectionData) cd = collection_new(path);
	if (isfile(path))
		{
		if (!collection_load(cd, path, COLLECTION_LOAD_NONE)) return FALSE;
		}
	else
		{
		if (isname(path) || !collection_save(cd, path)) return FALSE;
		}

	g_autofree gchar *canonical_path = g_canonicalize_filename(path, nullptr);
	history_list_add_to_key("sort_manager_external_collections", canonical_path, 0);
	return TRUE;
}

static void new_collection_file_response_cb(GFile *file, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (file != nullptr)
		{
		g_autoptr(GFile) parent = g_file_get_parent(file);

		g_autofree gchar *path = g_file_get_path(file);
		if (bar_sort_collection_add(path))
			{
			bar_sort_collection_list_build(sd->bookmarks);
			bookmark_list_set_selected_path(sd->bookmarks, path);
			if (parent != nullptr)
				{
				g_autofree gchar *dirname = g_file_get_path(parent);
				history_list_add_to_key("open_collection", dirname, -1);
				}
			}
		else
			{
			g_autoptr(GtkAlertDialog) dialog = gtk_alert_dialog_new("%s", _("Unable to add collection."));
			gtk_alert_dialog_set_detail(dialog, _("Choose a valid .gqv collection or a new .gqv filename in a writable folder."));
			gtk_alert_dialog_show(dialog, GTK_WINDOW(sd->lw->window));
			}
		}
}

static void bar_sort_add_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (sd->mode == BarSort::MODE_FOLDER)
		{
		bookmark_add_dialog(_("Add Bookmark - Geeqie"), sd->bookmarks);
		}
	else
		{
		FileDialogData fdd{};

		fdd.action = FileDialogAction::SAVE;
		fdd.accept_text = _("Add");
		fdd.accept_existing = TRUE;
		fdd.callback = new_collection_file_response_cb;
		fdd.data = sd;
		fdd.filename = get_collections_dir();
		fdd.filter = GQ_COLLECTION_EXT;
		fdd.filter_description = _("Collection files");
		fdd.history_key = "open_collection";
		fdd.suggested_name = _("Untitled.gqv");
		fdd.title = _("Add or create collection");

		file_dialog_show(fdd);
		}
}

void bar_sort_close(GtkWidget *bar)
{
	SortData *sd;

	sd = static_cast<SortData *>(g_object_get_data(G_OBJECT(bar), "bar_sort_data"));
	if (!sd) return;

	widget_remove_from_parent(sd->vbox);
}

static void bar_sort_destroy(gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	g_free(sd->filter_key);
	g_free(sd->source_path);
	g_free(sd->folder_key);
	g_list_free_full(sd->undo_src_list, g_free);
	g_list_free_full(sd->undo_dest_list, g_free);
	g_free(sd->undo_collection);
	g_free(sd);
}

static GtkWidget *bar_sort_new(LayoutWindow *lw, const BarSort &bar_sort)
{
	SortData *sd;
	GtkWidget *buttongrp;
	GtkWidget *label;
	GtkWidget *tbar;
	gboolean have_filter;
	GtkWidget *button;

	if (!lw) return nullptr;

	sd = g_new0(SortData, 1);

	sd->lw = lw;

	sd->action = bar_sort.action;

	if (sd->action == BarSort::FILTER && (!bar_sort.filter_key || !bar_sort.filter_key[0]))
		{
		sd->action = BarSort::COPY;
		}

	sd->selection = bar_sort.selection;
	sd->undo_src_list = nullptr;
	sd->undo_dest_list = nullptr;
	sd->undo_collection = nullptr;

	sd->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	DEBUG_NAME(sd->vbox);
	g_object_set_data_full(G_OBJECT(sd->vbox), "bar_sort_data", sd, bar_sort_destroy);

	label = gtk_label_new(_("Sort Manager"));
	pref_label_bold(label, TRUE, FALSE);
	gtk_box_append(GTK_BOX(sd->vbox), label);

	static const char *sort_mode_items[] = { _("Folders"), _("Collections"), nullptr };
	GtkWidget *drop_down = gtk_drop_down_new_from_strings(sort_mode_items);
	gtk_box_append(GTK_BOX(sd->vbox), drop_down);

	g_signal_connect(G_OBJECT(drop_down), "notify::selected",
	                 G_CALLBACK(bar_sort_mode_cb), sd);

	sd->folder_group = pref_box_new(sd->vbox, FALSE, GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(sd->folder_group);
	gtk_widget_set_tooltip_text(sd->folder_group, _("See the Help file for additional functions"));
	sd->folder_specific = pref_checkbox_new(sd->folder_group, _("Use destinations for this folder"), FALSE,
	                                        G_CALLBACK(bar_sort_folder_specific_cb), sd);
	const auto wrap_button_label = [](GtkWidget *button)
		{
		auto *label = gtk_check_button_get_child(GTK_CHECK_BUTTON(button));
		if (!GTK_IS_LABEL(label)) return;
		gtk_label_set_wrap(GTK_LABEL(label), TRUE);
		gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
		gtk_label_set_max_width_chars(GTK_LABEL(label), 32);
		};
	wrap_button_label(sd->folder_specific);

	buttongrp = pref_radiobutton_new(sd->folder_group, nullptr, _("Copy"), sd->action == BarSort::COPY,
	                                 G_CALLBACK(bar_sort_set_action_cb<BarSort::COPY>), sd);
	GtkGesture *copy_gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(copy_gesture), GDK_BUTTON_SECONDARY);
	g_signal_connect(copy_gesture, "pressed", G_CALLBACK(bar_filter_message_cb), NULL);
	gtk_widget_add_controller(buttongrp, GTK_EVENT_CONTROLLER(copy_gesture));
	button = pref_radiobutton_new(sd->folder_group, buttongrp, _("Move"), sd->action == BarSort::MOVE,
	                              G_CALLBACK(bar_sort_set_action_cb<BarSort::MOVE>), sd);
	GtkGesture *move_gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(move_gesture), GDK_BUTTON_SECONDARY);
	g_signal_connect(move_gesture, "pressed", G_CALLBACK(bar_filter_message_cb), NULL);
	gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(move_gesture));


	have_filter = FALSE;
	EditorsList editors_list = editor_list_get();
	for (const EditorDescription *editor : editors_list)
		{
		if (!editor_is_filter(editor->key)) continue;

		gchar *key = g_strdup(editor->key);

		gboolean select = (sd->action == BarSort::FILTER && strcmp(key, bar_sort.filter_key) == 0);
		if (select)
			{
			bar_sort_set_action(sd, sd->action, key);
			have_filter = TRUE;
			}

		GtkWidget *button = pref_radiobutton_new(sd->folder_group, buttongrp, editor->name, select,
		                                         G_CALLBACK(bar_sort_set_action_cb<BarSort::FILTER>), sd);
		wrap_button_label(button);
		GtkGesture *filter_gesture = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(filter_gesture), GDK_BUTTON_SECONDARY);
		g_signal_connect(filter_gesture, "pressed", G_CALLBACK(bar_filter_message_cb), NULL);
		gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(filter_gesture));

		g_object_set_data_full(G_OBJECT(button), "filter_key", key, g_free);
		}

	if (sd->action == BarSort::FILTER && !have_filter) sd->action = BarSort::COPY;

	sd->collection_group = pref_box_new(sd->vbox, FALSE, GTK_ORIENTATION_VERTICAL, 0);

	buttongrp = pref_radiobutton_new(sd->collection_group, nullptr, _("Add current image"),
	                                 sd->selection == BarSort::SELECTION_IMAGE,
	                                 G_CALLBACK(bar_sort_set_selection_cb<BarSort::SELECTION_IMAGE>), sd);
	pref_radiobutton_new(sd->collection_group, buttongrp, _("Add selected images"),
	                     sd->selection == BarSort::SELECTION_SELECTED,
	                     G_CALLBACK(bar_sort_set_selection_cb<BarSort::SELECTION_SELECTED>), sd);

	const auto bar_sort_bookmark_select = [sd](const gchar *path)
	{
		if (!path) return;

		if (sd->mode == BarSort::MODE_FOLDER)
			{
			bar_sort_bookmark_select_folder(sd, path);
			}
		else
			{
			bar_sort_bookmark_select_collection(sd, path);
			}
	};
	sd->bookmarks = bookmark_list_new(SORT_KEY_FOLDERS, bar_sort_bookmark_select);
	bookmark_list_set_tooltip_func(sd->bookmarks, [](const gchar *path) -> std::string
	{
		g_autofree gchar *shortcut = action_accelerator_label("win.main-win-sort-add-selection");
		g_autofree gchar *shortcut_hint = shortcut
		                               ? g_strdup_printf(_("%s to process images using the highlighted destination and selected option in the Sort Manager."), shortcut)
		                               : nullptr;
		g_autofree gchar *text = g_strjoin("\n", path, "",
		                                 _("Click to set the destination and process images."),
		                                 _("Ctrl+click to set the destination only."),
		                                 shortcut_hint, nullptr);
		return text;
	});
	bookmark_list_set_drop_func(sd->bookmarks, [sd](const gchar *path, GList *list)
	{
		bar_sort_bookmark_drop(sd, path, list);
	});
	DEBUG_NAME(sd->bookmarks);
	gtk_widget_set_hexpand(sd->bookmarks, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(sd->vbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->bookmarks, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(sd->vbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(sd->vbox), sd->bookmarks);

	sd->destination_hint = gtk_label_new(_("Ctrl+click selects a destination without adding images."));
	gtk_label_set_wrap(GTK_LABEL(sd->destination_hint), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(sd->destination_hint), 32);
	gtk_label_set_xalign(GTK_LABEL(sd->destination_hint), 0.0);
	gtk_widget_add_css_class(sd->destination_hint, "dim-label");
	gtk_widget_set_hexpand(sd->destination_hint, TRUE);
	gtk_box_append(GTK_BOX(sd->vbox), sd->destination_hint);

	tbar = pref_toolbar_new(sd->vbox);
	DEBUG_NAME(tbar);

	sd->add_button = pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE,
					     _("Add Bookmark"),
					     G_CALLBACK(bar_sort_add_cb), sd);
	sd->undo_button = pref_toolbar_button(tbar, GQ_ICON_UNDO, _("Undo"), FALSE,
					      _("Undo last image"),
					      G_CALLBACK(bar_sort_undo_cb), sd);
	pref_toolbar_button(tbar, GQ_ICON_HELP, _("Help"), FALSE,
	                    _("Sort Manager help"),
	                    G_CALLBACK(bar_sort_help_cb), sd);

	sd->mode = static_cast<BarSort::Mode>(-1);
	bar_sort_update(sd->vbox);
	bar_sort_mode_sync(sd, bar_sort.mode);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(drop_down), static_cast<guint>(sd->mode));

	return sd->vbox;
}

GtkWidget *bar_sort_new_from_config(LayoutWindow *lw, const gchar **, const gchar **)
{
	GtkWidget *bar;

	bar = bar_sort_new(lw, lw->options.bar_sort);

	gtk_widget_set_visible(bar, lw->bar_sort_enabled);
	return bar;
}

/**
 * @brief Sets the bar_sort_enabled flag
 * @param lw
 * @param attribute_names
 * @param attribute_values
 *
 * Called from rcfile when processing geeqierc.xml on start-up.
 * It is necessary to set the bar_sort_enabled flag because
 * the sort manager and desktop files are set up in the idle loop, and
 * setup is not yet completed during initialisation.
 * The flag is checked in layout_editors_reload_idle_cb.
 */
void bar_sort_cold_start(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values)
{
	gint action = BarSort::COPY;
	gint mode = BarSort::MODE_FOLDER;
	gint selection = BarSort::SELECTION_IMAGE;

	lw->bar_sort_enabled = TRUE;
	lw->options.bar_sort.filter_key = nullptr;

	while (attribute_names && *attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_BOOL_FULL("enabled", lw->bar_sort_enabled)) continue;
		if (READ_INT_CLAMP_FULL("action", action, 0, BarSort::ACTION_COUNT - 1)) continue;
		if (READ_INT_CLAMP_FULL("mode", mode, 0, BarSort::MODE_COUNT - 1)) continue;
		if (READ_INT_CLAMP_FULL("selection", selection, 0, BarSort::SELECTION_COUNT - 1)) continue;
		if (READ_CHAR(lw->options.bar_sort, filter_key)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str()); // NOTRANSLATE: Diagnostic message.
		}

	lw->options.bar_sort.action = static_cast<BarSort::Action>(action);
	lw->options.bar_sort.mode = static_cast<BarSort::Mode>(mode);
	lw->options.bar_sort.selection = static_cast<BarSort::Selection>(selection);
}

GtkWidget *bar_sort_new_default(LayoutWindow *lw)
{
	return bar_sort_new_from_config(lw, nullptr, nullptr);
}

void bar_sort_write_config(GtkWidget *bar, RcString &rc)
{
	if (!bar) return;

	auto *sd = static_cast<SortData *>(g_object_get_data(G_OBJECT(bar), "bar_sort_data"));
	if (!sd) return;

	bar_sort_destination_save(sd);

	WRITE_NL(); WRITE_STRING("<bar_sort ");
	WRITE_BOOL_FULL("enabled", gtk_widget_get_visible(bar));
	WRITE_INT(*sd, mode);
	WRITE_INT(*sd, action);
	WRITE_INT(*sd, selection);
	WRITE_CHAR(*sd, filter_key);
	WRITE_STRING("/>");
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
