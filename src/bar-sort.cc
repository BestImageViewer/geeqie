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

#include <string>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "collect-io.h"
#include "collect.h"
#include "compat.h"
#include "editors.h"
#include "filedata.h"
#include "history-list.h"
#include "intl.h"
#include "layout-image.h"
#include "layout.h"
#include "main-defines.h"
#include "options.h"
#include "rcfile.h"
#include "typedefs.h"
#include "ui-bookmark.h"
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

	gchar *name;
	GtkPopover *name_popover;

	GtkWidget *dialog_name_entry;

	GtkWidget *folder_group;
	GtkWidget *collection_group;

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
	FileData *dir_fd;
	GList *work;

	history_list_free_key(SORT_KEY_COLLECTIONS);
	bookmark_list_set_key(bookmarks, SORT_KEY_COLLECTIONS);

	dir_fd = file_data_new_dir(get_collections_dir());
	g_autoptr(FileDataList) list = nullptr;
	filelist_read(dir_fd, &list, nullptr);
	file_data_unref(dir_fd);

	list = filelist_sort_path(list);

	work = list;
	while (work)
		{
		FileData *fd;
		g_autofree gchar *name = nullptr;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (file_extension_match(fd->path, GQ_COLLECTION_EXT))
			{
			name = remove_extension_from_path(fd->name);
			}
		else
			{
			name = g_strdup(fd->name);
			}
		bookmark_list_add(bookmarks, name, fd->path);
		}
}

static void bar_sort_mode_sync(SortData *sd, BarSort::Mode mode)
{
	if (sd->mode == mode) return;
	sd->mode = mode;

	gboolean folder_mode = (sd->mode == BarSort::MODE_FOLDER);

	bookmark_list_set_no_defaults(sd->bookmarks, !folder_mode);
	bookmark_list_set_editable(sd->bookmarks, folder_mode);
	bookmark_list_set_only_directories(sd->bookmarks, folder_mode);

	if (folder_mode)
		{
		gtk_widget_hide(sd->collection_group);
		gtk_widget_show(sd->folder_group);
		bookmark_list_set_key(sd->bookmarks, SORT_KEY_FOLDERS);
		}
	else
		{
		gtk_widget_hide(sd->folder_group);
		gtk_widget_show(sd->collection_group);
		bar_sort_collection_list_build(sd->bookmarks);
		}

	bar_sort_undo_set(sd, nullptr, nullptr);
}

static void bar_sort_mode_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) == BarSort::MODE_FOLDER)
		{
		gtk_widget_set_tooltip_text(sd->add_button, _("Add Bookmark"));
		bar_sort_mode_sync(sd, BarSort::MODE_FOLDER);
		}
	else
		{
		gtk_widget_set_tooltip_text(sd->add_button, _("Create new Collection file"));
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
		/* we should create the undo_dest_list to use it later... */
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
					((sd->undo_src_list ) && sd->undo_dest_list));
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

			options->file_ops.safe_delete_enable = TRUE;

			file_util_delete(nullptr, delete_list, button);
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

static void bar_sort_bookmark_select_folder(SortData *sd, FileData *, const gchar *path)
{
	GList *orig_list;
	GList *action_list;
	GList *undo_src_list;

	if (!isdir(path)) return;

	orig_list = layout_selection_list(sd->lw);
	action_list = orig_list;
	undo_src_list = orig_list;
	orig_list = nullptr;

	bar_sort_undo_set(sd, undo_src_list, path);

	switch (sd->action)
		{
		case BarSort::COPY:
			file_util_copy_simple(action_list, path, sd->lw->window);
			action_list = nullptr;
			layout_image_next(sd->lw);
			break;

		case BarSort::MOVE:
			file_util_move_simple(action_list, path, sd->lw->window);
			action_list = nullptr;
			break;

		case BarSort::FILTER:
			file_util_start_filter_from_filelist(sd->filter_key, action_list, path, sd->lw->window);
			layout_image_next(sd->lw);
			break;

		default:
			break;
		}
}

static void bar_sort_bookmark_select_collection(SortData *sd, FileData *source, const gchar *path)
{
	GList *list = nullptr;

	switch (sd->selection)
		{
		case BarSort::SELECTION_IMAGE:
			list = g_list_append(nullptr, file_data_ref(source));
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
	sd->undo_collection = g_strdup(path);

	while (list)
		{
		FileData *image_fd;

		image_fd = static_cast<FileData *>(list->data);
		list = list->next;
		collect_manager_add(image_fd, path);
		}
}

static void bar_sort_bookmark_select(const gchar *path, gpointer data)
{
	auto sd = static_cast<SortData *>(data);
	FileData *source;

	source = layout_image_get_fd(sd->lw);
	if (!path || !source) return;

	if (sd->mode == BarSort::MODE_FOLDER)
		{
		bar_sort_bookmark_select_folder(sd, source, path);
		}
	else
		{
		bar_sort_bookmark_select_collection(sd, source, path);
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

static void bar_sort_set_copy_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SortData *>(data);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) return;
	bar_sort_set_action(sd, BarSort::COPY, nullptr);
}

static void bar_sort_set_move_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SortData *>(data);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) return;
	bar_sort_set_action(sd, BarSort::MOVE, nullptr);
}

static void bar_sort_set_filter_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SortData *>(data);
	const gchar *key;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) return;
	key = static_cast<const gchar *>(g_object_get_data(G_OBJECT(button), "filter_key"));
	bar_sort_set_action(sd, BarSort::FILTER, key);
}

static void bar_filter_help_cb(GenericDialog *, gpointer)
{
	help_window_show("GuidePluginsConfig.html#Geeqieextensions");
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

	gtk_widget_show(gd->dialog);
}

static gboolean bar_filter_message_cb(GtkWidget *, GdkEventButton *event, gpointer)
{
	if (event->button != MOUSE_BUTTON_RIGHT) return FALSE;

	bar_filter_help_dialog();

	return TRUE;
}

static void bar_sort_help_cb(gpointer)
{
	bar_filter_help_dialog();
}

static void bar_sort_set_selection(SortData *sd, BarSort::Selection selection)
{
	sd->selection = selection;
}

static void bar_sort_set_selection_image_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SortData *>(data);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) return;
	bar_sort_set_selection(sd, BarSort::SELECTION_IMAGE);
}

static void bar_sort_set_selection_selected_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SortData *>(data);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) return;
	bar_sort_set_selection(sd, BarSort::SELECTION_SELECTED);
}

static void bar_sort_add_response_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		if (sd->mode == BarSort::MODE_FOLDER)
			{
			gboolean empty_name = (sd->name == nullptr);

			g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
			g_autofree gchar *selected_dir = g_file_get_path(file);

			bookmark_list_add(sd->bookmarks, empty_name ? filename_from_path(selected_dir) : sd->name, selected_dir);

			g_free(sd->name);
			sd->name = nullptr;
			}
		}

	if (response_id == GQ_RESPONSE_NAME_CLICKED)
		{
		gtk_popover_popup(GTK_POPOVER(sd->name_popover));
		gq_gtk_widget_show_all(GTK_WIDGET(sd->name_popover));
		gtk_widget_grab_focus(GTK_WIDGET(sd->name_popover));

		return;
		}

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void name_entry_activate_cb(GtkEntry *entry, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	g_free(sd->name);
	sd->name = g_strdup(gtk_entry_get_text(entry));

	gtk_popover_popdown(GTK_POPOVER(sd->name_popover));
}

static void new_collection_file_save_failed_cb(GtkDialog *dialog, gint, gpointer)
{
	gq_gtk_widget_destroy(GTK_WIDGET(dialog));
}

static gboolean save_new_collection(GtkFileChooser *chooser, gpointer data)
{
	auto sd = static_cast<SortData *>(data);
	CollectionData *cd;
	gboolean ret;

	g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
	g_autofree gchar *path = g_file_get_path(file);

	cd = collection_new(path);
	if (collection_save(cd, path))
		{
		bar_sort_collection_list_build(sd->bookmarks);
		ret = TRUE;
		}
	else
		{
		GtkWidget *new_collection_file_save_failed = gtk_message_dialog_new_with_markup(GTK_WINDOW(chooser), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, _("<b>File save failed.</b>\n\nFile \"%s\" was not saved."), path);
		gtk_window_set_modal(GTK_WINDOW(new_collection_file_save_failed), TRUE);

		g_signal_connect(new_collection_file_save_failed, "response", G_CALLBACK(new_collection_file_save_failed_cb), nullptr);

		gtk_widget_show(new_collection_file_save_failed);

		ret = FALSE;
		}

	collection_unref(cd);

	return ret;
}

static void collection_exists_response_cb(GtkDialog *dialog, gint, gpointer)
{
	gq_gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void new_collection_file_response_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		g_autofree gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

		if (g_file_test(filename, G_FILE_TEST_EXISTS))
			{
			GtkWidget *collection_exists = gtk_message_dialog_new(GTK_WINDOW(chooser), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, _("File \"%s\" already exists."), filename);
			gtk_window_set_modal(GTK_WINDOW(collection_exists), TRUE);

			g_signal_connect(collection_exists, "response", G_CALLBACK(collection_exists_response_cb), nullptr);

			gtk_widget_show(collection_exists);
			}
		else
			{
			if (save_new_collection(chooser, sd))
				{
				gq_gtk_widget_destroy(GTK_WIDGET(chooser));
				}
			}
		}
	else
		{
		gq_gtk_widget_destroy(GTK_WIDGET(chooser));
		}
}

static void bar_sort_add_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	if (sd->mode == BarSort::MODE_FOLDER)
		{
		GtkWidget *dialog;
		GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

		dialog = gtk_file_chooser_dialog_new(_("Add Bookmark - Geeqie"), nullptr, action, _("_Cancel"), GTK_RESPONSE_CANCEL, _("Add"), GTK_RESPONSE_ACCEPT, _("Name"), GQ_RESPONSE_NAME_CLICKED, nullptr);

		gtk_widget_set_tooltip_text(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GQ_RESPONSE_NAME_CLICKED), _("Optional alias name for the shortcut.\nThis may be amended or added from the Sort Manager pane.\nIf none given, the basename of the folder is used"));

		GtkWidget *entry = gtk_entry_new();
		GtkWidget *name_popover = gtk_popover_new(GTK_WIDGET(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GQ_RESPONSE_NAME_CLICKED)));
		sd->name_popover = GTK_POPOVER(name_popover);

		g_signal_connect(GTK_ENTRY(entry), "activate", G_CALLBACK(name_entry_activate_cb), sd);

		gtk_popover_set_position(GTK_POPOVER(name_popover), GTK_POS_BOTTOM);
		gq_gtk_container_add(GTK_WIDGET(name_popover), entry);
		gtk_container_set_border_width(GTK_CONTAINER(name_popover), 6);

		gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);

		g_signal_connect(dialog, "response", G_CALLBACK(bar_sort_add_response_cb), sd);

		gq_gtk_widget_show_all(GTK_WIDGET(dialog));
		}
	else
		{
		GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Create empty Collection file"), GTK_WINDOW(sd->lw->window), GTK_FILE_CHOOSER_ACTION_SAVE, _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Save"), GTK_RESPONSE_ACCEPT, nullptr);

		GtkFileFilter *all_filter = gtk_file_filter_new();
		gtk_file_filter_set_name(all_filter, _("All files"));
		gtk_file_filter_add_pattern(all_filter, "*");
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

		GtkFileFilter *collections_filter = gtk_file_filter_new();
		gtk_file_filter_set_name(collections_filter, _("Collections files"));
		gtk_file_filter_add_pattern(collections_filter, "*.gqv");
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), collections_filter);

		gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), collections_filter);

#if HAVE_GTK4
		g_autoptr(GFile) path = g_file_new_for_path(get_collections_dir(), nullptr);
		gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dialog), path, nullptr);
#else
		gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dialog), get_collections_dir(), nullptr);
#endif

		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), _("Untitled.gqv"));
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), get_collections_dir());

		g_signal_connect(dialog, "response", G_CALLBACK(new_collection_file_response_cb), sd);

		gtk_window_present(GTK_WINDOW(dialog));
		}
}

void bar_sort_close(GtkWidget *bar)
{
	SortData *sd;

	sd = static_cast<SortData *>(g_object_get_data(G_OBJECT(bar), "bar_sort_data"));
	if (!sd) return;

	gq_gtk_widget_destroy(sd->vbox);
}

static void bar_sort_destroy(gpointer data)
{
	auto sd = static_cast<SortData *>(data);

	g_free(sd->filter_key);
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
	GtkWidget *combo;
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
	gq_gtk_box_pack_start(GTK_BOX(sd->vbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	combo = gtk_combo_box_text_new();
	gq_gtk_box_pack_start(GTK_BOX(sd->vbox), combo, FALSE, FALSE, 0);
	gtk_widget_show(combo);

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Folders"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Collections"));

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(bar_sort_mode_cb), sd);

	sd->folder_group = pref_box_new(sd->vbox, FALSE, GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(sd->folder_group);
	gtk_widget_set_tooltip_text(sd->folder_group, _("See the Help file for additional functions"));

	buttongrp = pref_radiobutton_new(sd->folder_group, nullptr,
	                                 _("Copy"), sd->action == BarSort::COPY,
	                                 G_CALLBACK(bar_sort_set_copy_cb), sd);
	g_signal_connect(G_OBJECT(buttongrp), "button_press_event", G_CALLBACK(bar_filter_message_cb), NULL);
	button = pref_radiobutton_new(sd->folder_group, buttongrp,
	                              _("Move"), sd->action == BarSort::MOVE,
	                              G_CALLBACK(bar_sort_set_move_cb), sd);
	g_signal_connect(G_OBJECT(button), "button_press_event", G_CALLBACK(bar_filter_message_cb), NULL);


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

		GtkWidget *button = pref_radiobutton_new(sd->folder_group, buttongrp,
		                                         editor->name, select,
		                                         G_CALLBACK(bar_sort_set_filter_cb), sd);
		g_signal_connect(G_OBJECT(button), "button_press_event", G_CALLBACK(bar_filter_message_cb), NULL);

		g_object_set_data_full(G_OBJECT(button), "filter_key", key, g_free);
		}

	if (sd->action == BarSort::FILTER && !have_filter) sd->action = BarSort::COPY;

	sd->collection_group = pref_box_new(sd->vbox, FALSE, GTK_ORIENTATION_VERTICAL, 0);

	buttongrp = pref_radiobutton_new(sd->collection_group, nullptr,
	                                 _("Add image"), sd->selection == BarSort::SELECTION_IMAGE,
	                                 G_CALLBACK(bar_sort_set_selection_image_cb), sd);
	pref_radiobutton_new(sd->collection_group, buttongrp,
	                     _("Add selection"), sd->selection == BarSort::SELECTION_SELECTED,
	                     G_CALLBACK(bar_sort_set_selection_selected_cb), sd);

	sd->bookmarks = bookmark_list_new(SORT_KEY_FOLDERS, bar_sort_bookmark_select, sd);
	DEBUG_NAME(sd->bookmarks);
	gq_gtk_box_pack_start(GTK_BOX(sd->vbox), sd->bookmarks, TRUE, TRUE, 0);
	gtk_widget_show(sd->bookmarks);

	tbar = pref_toolbar_new(sd->vbox);
	DEBUG_NAME(tbar);

	sd->add_button = pref_toolbar_button(tbar, GQ_ICON_ADD, _("Add"), FALSE,
					     _("Add Bookmark"),
					     G_CALLBACK(bar_sort_add_cb), sd);
	sd->undo_button = pref_toolbar_button(tbar, GQ_ICON_UNDO, _("Undo"), FALSE,
					      _("Undo last image"),
					      G_CALLBACK(bar_sort_undo_cb), sd);
	pref_toolbar_button(tbar, GQ_ICON_HELP, _("Help"), FALSE,
	                    _("Functions additional to Copy and Move"),
	                    G_CALLBACK(bar_sort_help_cb), sd);

	sd->mode = static_cast<BarSort::Mode>(-1);
	bar_sort_mode_sync(sd, bar_sort.mode);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), static_cast<gint>(sd->mode));

	return sd->vbox;
}

GtkWidget *bar_sort_new_from_config(LayoutWindow *lw, const gchar **, const gchar **)
{
	GtkWidget *bar;

	bar = bar_sort_new(lw, lw->options.bar_sort);

	if (lw->bar_sort_enabled) gtk_widget_show(bar);
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
		if (READ_CHAR_FULL("filter_key", lw->options.bar_sort.filter_key)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	lw->options.bar_sort.action = static_cast<BarSort::Action>(action);
	lw->options.bar_sort.mode = static_cast<BarSort::Mode>(mode);
	lw->options.bar_sort.selection = static_cast<BarSort::Selection>(selection);
}

GtkWidget *bar_sort_new_default(LayoutWindow *lw)
{
	return bar_sort_new_from_config(lw, nullptr, nullptr);
}

void bar_sort_write_config(GtkWidget *bar, GString *outstr, gint indent)
{
	SortData *sd;

	if (!bar) return;
	sd = static_cast<SortData *>(g_object_get_data(G_OBJECT(bar), "bar_sort_data"));
	if (!sd) return;

	WRITE_NL(); WRITE_STRING("<bar_sort ");
	write_bool_option(outstr, "enabled", gtk_widget_get_visible(bar));
	WRITE_INT(*sd, mode);
	WRITE_INT(*sd, action);
	WRITE_INT(*sd, selection);
	WRITE_CHAR(*sd, filter_key);
	WRITE_STRING("/>");
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
