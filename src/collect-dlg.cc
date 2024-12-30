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

#include "collect-dlg.h"

#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "collect-io.h"
#include "collect.h"
#include "compat.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"
#include "utilops.h"

enum {
	DIALOG_SAVE,
	DIALOG_SAVE_CLOSE,
	DIALOG_LOAD,
	DIALOG_APPEND
};

static void collection_save_confirmed(GenericDialog *gdlg, gboolean overwrite, CollectionData *cd);

static void collection_confirm_ok_cb(GenericDialog *gdlg, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	collection_save_confirmed(gdlg, TRUE, cd);
}

static void collection_confirm_cancel_cb(GenericDialog *gdlg, gpointer)
{
	generic_dialog_close(gdlg);
}

static void collection_save_confirmed(GenericDialog *gdlg, gboolean overwrite, CollectionData *cd)
{
	GenericDialog *gdlg_overwrite;

	generic_dialog_close(gdlg);

	if (!overwrite && isfile(cd->collection_path))
		{
		gdlg_overwrite = file_util_gen_dlg(_("Overwrite collection"), "dlg_confirm", nullptr, FALSE, collection_confirm_cancel_cb, cd);
		generic_dialog_add_message(gdlg_overwrite, GQ_ICON_DIALOG_QUESTION, _("Overwrite existing collection?"), cd->name, TRUE);
		generic_dialog_add_button(gdlg_overwrite, GQ_ICON_OK, _("Overwrite"), collection_confirm_ok_cb, TRUE);

		gtk_widget_show(gdlg_overwrite->dialog);

		return;
		}

	if (!collection_save(cd, cd->collection_path))
		{
		g_autofree gchar *buf = g_strdup_printf(_("Failed to save the collection:\n%s"), cd->collection_path);
		file_util_warning_dialog(_("Save Failed"), buf, GQ_ICON_DIALOG_ERROR, GENERIC_DIALOG(gdlg)->dialog);
		}

	collection_unref(cd);
	collection_window_close_by_collection(cd);
}

static void collection_save_cb(GenericDialog *gd, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	cd->collection_path = g_strconcat(get_collections_dir(), G_DIR_SEPARATOR_S, gq_gtk_entry_get_text(GTK_ENTRY(cd->dialog_name_entry)), GQ_COLLECTION_EXT, nullptr);

	collection_save_confirmed(gd, FALSE, cd);
}

static void real_collection_button_pressed(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);
	GList *collection_list = nullptr;

	collect_manager_list(nullptr, nullptr, &collection_list);

	auto append_collection_path = static_cast<gchar *>(g_list_nth_data(collection_list, cd->collection_append_index));
	collection_load(cd, append_collection_path, COLLECTION_LOAD_APPEND);

	collection_unref(cd);
	g_list_free_full(collection_list, g_free);
}

static void collection_append_cb(GenericDialog *gd, gpointer data)
{
	real_collection_button_pressed(gd, data);
}

static void collection_save_or_load_dialog_close_cb(GenericDialog *gdlg, gpointer data)
{
	auto cd = static_cast<CollectionData *>(data);

	if (cd) collection_unref(cd);
	generic_dialog_close(gdlg);
}

static void collection_append_menu_cb(GtkWidget *collection_append_combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	*option = gtk_combo_box_get_active(GTK_COMBO_BOX(collection_append_combo));
}

static void collection_save_or_append_dialog(gint type, CollectionData *cd)
{
	GenericDialog *gdlg;
	const gchar *title;
	GList *collection_list = nullptr;

	if (!cd) return;

	collection_ref(cd);

	if (type == DIALOG_SAVE || type == DIALOG_SAVE_CLOSE)
		{
		GtkWidget *existing_collections;
		GtkWidget *save_as_label;
		GtkWidget *scrolled;
		GtkWidget *viewport;

		title = _("Save collection");

		gdlg = file_util_gen_dlg(title, "dlg_collection_save", nullptr, FALSE, collection_save_or_load_dialog_close_cb, cd);

		generic_dialog_add_message(GENERIC_DIALOG(gdlg), nullptr, title, _("Existing collections:"), FALSE);
		generic_dialog_add_button(gdlg, GQ_ICON_SAVE, _("Save"), collection_save_cb, TRUE);

		collect_manager_list(&collection_list, nullptr, nullptr);

		GString *out_string = g_string_new(nullptr);

		for (GList *work = collection_list; work != nullptr; work = work->next)
			{
			auto collection_name = static_cast<const gchar *>(work->data);
			out_string = g_string_append(out_string, collection_name);
			out_string = g_string_append(out_string, "\n");
			}
		g_list_free_full(collection_list, g_free);

		existing_collections = gtk_label_new(out_string->str);
		g_string_free(out_string, TRUE);

		scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_widget_show(scrolled);

		viewport = gtk_viewport_new(nullptr, nullptr);
		gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
		gq_gtk_container_add(GTK_WIDGET(viewport), existing_collections);
		gtk_widget_show(viewport);
		gtk_widget_show(existing_collections);
		gq_gtk_container_add(GTK_WIDGET(scrolled), viewport);

		gq_gtk_box_pack_start(GTK_BOX(gdlg->vbox), scrolled, TRUE,TRUE, 0);

		save_as_label = gtk_label_new(_("Save collection as:"));
		gq_gtk_box_pack_start(GTK_BOX(gdlg->vbox), save_as_label, FALSE,FALSE, 0);
		gtk_label_set_xalign(GTK_LABEL(save_as_label), 0.0);
		gtk_widget_show(save_as_label);

		cd->dialog_name_entry = gtk_entry_new();
		gtk_widget_show(cd->dialog_name_entry);

		gq_gtk_box_pack_start(GTK_BOX(gdlg->vbox), cd->dialog_name_entry, FALSE, FALSE, 0);

		gq_gtk_entry_set_text(GTK_ENTRY(cd->dialog_name_entry), cd->name);
		gtk_widget_grab_focus(cd->dialog_name_entry);
		gtk_widget_show(GENERIC_DIALOG(gdlg)->dialog);
		}
	else
		{
		CollectWindow *cw;
		GtkWidget *parent = nullptr;
		GtkWidget *collection_append_combo;

		title = _("Append collection");

		cw = collection_window_find(cd);
		if (cw) parent = cw->window;

		gdlg = file_util_gen_dlg(title, "dlg_collection_append", parent, true, nullptr, cd);

		generic_dialog_add_message(GENERIC_DIALOG(gdlg), nullptr, title, _("Select from existing collections:"), FALSE);
		generic_dialog_add_button(gdlg, GQ_ICON_CANCEL, _("Cancel"), nullptr, TRUE);
		generic_dialog_add_button(gdlg, GQ_ICON_ADD, _("_Append"), collection_append_cb, TRUE);

		collect_manager_list(&collection_list, nullptr, nullptr);

		collection_append_combo = gtk_combo_box_text_new();

		for (GList *work = collection_list; work != nullptr; work = work->next)
			{
			auto collection_name = static_cast<const gchar *>(work->data);
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(collection_append_combo), collection_name);
			}
		g_list_free_full(collection_list, g_free);

		gtk_combo_box_set_active(GTK_COMBO_BOX(collection_append_combo), 0);

		g_signal_connect(G_OBJECT(collection_append_combo), "changed", G_CALLBACK(collection_append_menu_cb), &cd->collection_append_index);

		gtk_widget_show(collection_append_combo);

		gq_gtk_box_pack_start(GTK_BOX(gdlg->vbox), collection_append_combo, TRUE,TRUE, 0);
		gtk_widget_show(GENERIC_DIALOG(gdlg)->dialog);
		}
}

void collection_dialog_save_as(CollectionData *cd)
{
	collection_save_or_append_dialog(DIALOG_SAVE, cd);
}

void collection_dialog_save_close(CollectionData *cd)
{
	collection_save_or_append_dialog(DIALOG_SAVE_CLOSE, cd);
}

void collection_dialog_append(CollectionData *cd)
{
	collection_save_or_append_dialog(DIALOG_APPEND, cd);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
