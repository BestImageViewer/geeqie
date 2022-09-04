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

#include "main.h"
#include "collect.h"
#include "collect-dlg.h"

#include "collect-io.h"
#include "utilops.h"
#include "ui-fileops.h"
#include "ui-tabcomp.h"
#include "ui-utildlg.h"


enum {
	DIALOG_SAVE,
	DIALOG_SAVE_CLOSE,
	DIALOG_LOAD,
	DIALOG_APPEND
};


static gboolean collection_save_confirmed(FileDialog *fd, gboolean overwrite, CollectionData *cd);


static void collection_confirm_ok_cb(GenericDialog *UNUSED(gd), gpointer data)
{
	FileDialog *fd = data;
	CollectionData *cd = GENERIC_DIALOG(fd)->data;

	if (!collection_save_confirmed(fd, TRUE, cd))
		{
		collection_unref(cd);
		file_dialog_close(fd);
		}
}

static void collection_confirm_cancel_cb(GenericDialog *UNUSED(gd), gpointer UNUSED(data))
{
	/* this is a no-op, so the cancel button is added */
}

static gboolean collection_save_confirmed(FileDialog *fd, gboolean overwrite, CollectionData *cd)
{
	gchar *buf;

	if (isdir(fd->dest_path))
		{
		buf = g_strdup_printf(_("Specified path:\n%s\nis a folder, collections are files"), fd->dest_path);
		file_util_warning_dialog(_("Invalid filename"), buf, GTK_STOCK_DIALOG_INFO, GENERIC_DIALOG(fd)->dialog);
		g_free(buf);
		return FALSE;
		}

	if (!overwrite && isfile(fd->dest_path))
		{
		GenericDialog *gd;

		gd = file_util_gen_dlg(_("Overwrite File"), "dlg_confirm",
					GENERIC_DIALOG(fd)->dialog, TRUE,
					collection_confirm_cancel_cb, fd);

		generic_dialog_add_message(gd, GTK_STOCK_DIALOG_QUESTION,
					   _("Overwrite existing file?"), fd->dest_path, TRUE);

		generic_dialog_add_button(gd, GTK_STOCK_OK, _("_Overwrite"), collection_confirm_ok_cb, TRUE);

		gtk_widget_show(gd->dialog);

		return TRUE;
		}

	if (!collection_save(cd, fd->dest_path))
		{
		buf = g_strdup_printf(_("Failed to save the collection:\n%s"), fd->dest_path);
		file_util_warning_dialog(_("Save Failed"), buf, GTK_STOCK_DIALOG_ERROR, GENERIC_DIALOG(fd)->dialog);
		g_free(buf);
		}

	collection_unref(cd);
	file_dialog_sync_history(fd, TRUE);

	if (fd->type == DIALOG_SAVE_CLOSE) collection_window_close_by_collection(cd);
	file_dialog_close(fd);

	return TRUE;
}

static void collection_save_cb(FileDialog *fd, gpointer data)
{
	CollectionData *cd = data;
	const gchar *path;

	path = fd->dest_path;

	/** @FIXME utf8 */
	if (!file_extension_match(path, GQ_COLLECTION_EXT))
		{
		gchar *buf;
		buf = g_strconcat(path, GQ_COLLECTION_EXT, NULL);
		gtk_entry_set_text(GTK_ENTRY(fd->entry), buf);
		g_free(buf);
		}

	collection_save_confirmed(fd, FALSE, cd);
}

static void real_collection_button_pressed(FileDialog *fd, gpointer data, gint append)
{
	CollectionData *cd = data;
	gboolean err = FALSE;
	gchar *text = NULL;

	if (!isname(fd->dest_path))
		{
		err = TRUE;
		text = g_strdup_printf(_("No such file '%s'."), fd->dest_path);
		}
	if (!err && isdir(fd->dest_path))
		{
		err = TRUE;
		text = g_strdup_printf(_("'%s' is a directory, not a collection file."), fd->dest_path);
		}
	if (!err && !access_file(fd->dest_path, R_OK))
		{
		err = TRUE;
		text = g_strdup_printf(_("You do not have read permissions on the file '%s'."), fd->dest_path);
		}

	if (err) {
		if  (text)
			{
			file_util_warning_dialog(_("Can not open collection file"), text, GTK_STOCK_DIALOG_ERROR, NULL);
			g_free(text);
		}
		return;
	}

	if (append)
		{
		collection_load(cd, fd->dest_path, COLLECTION_LOAD_APPEND);
		collection_unref(cd);
		}
	else
		{
		collection_window_new(fd->dest_path);
		}

	file_dialog_sync_history(fd, TRUE);
	file_dialog_close(fd);
}

static void collection_load_cb(FileDialog *fd, gpointer data)
{
	real_collection_button_pressed(fd, data, FALSE);
}

static void collection_append_cb(FileDialog *fd, gpointer data)
{
	real_collection_button_pressed(fd, data, TRUE);
}

static void collection_save_or_load_dialog_close_cb(FileDialog *fd, gpointer data)
{
	CollectionData *cd = data;

	if (cd) collection_unref(cd);
	file_dialog_close(fd);
}

static void collection_save_or_load_dialog(const gchar *path,
					   gint type, CollectionData *cd)
{
	FileDialog *fd;
	GtkWidget *parent = NULL;
	CollectWindow *cw;
	const gchar *title;
	const gchar *btntext;
	gpointer btnfunc;
	const gchar *stock_id;

	if (type == DIALOG_SAVE || type == DIALOG_SAVE_CLOSE)
		{
		if (!cd) return;
		title = _("Save collection");
		btntext = NULL;
		btnfunc = (gpointer)collection_save_cb;
		stock_id = GTK_STOCK_SAVE;
		}
	else if (type == DIALOG_LOAD)
		{
		title = _("Open collection");
		btntext = NULL;
		btnfunc = (gpointer)collection_load_cb;
		stock_id = GTK_STOCK_OPEN;
		}
	else
		{
		if (!cd) return;
		title = _("Append collection");
		btntext = _("_Append");
		btnfunc = (gpointer)collection_append_cb;
		stock_id = GTK_STOCK_ADD;
		}

	if (cd) collection_ref(cd);

	cw = collection_window_find(cd);
	if (cw) parent = cw->window;

	fd = file_util_file_dlg(title, "dlg_collection", parent,
			     collection_save_or_load_dialog_close_cb, cd);

	generic_dialog_add_message(GENERIC_DIALOG(fd), NULL, title, NULL, FALSE);
	file_dialog_add_button(fd, stock_id, btntext, (void (*)(FileDialog *, gpointer))btnfunc, TRUE);

	file_dialog_add_path_widgets(fd, get_collections_dir(), path,
				     "collection_load_save", GQ_COLLECTION_EXT, _("Collection Files"));

	fd->type = type;

	gtk_widget_show(GENERIC_DIALOG(fd)->dialog);
}

void collection_dialog_save_as(gchar *path, CollectionData *cd)
{
	if (!path) path = cd->path;
	if (!path) path = cd->name;

	collection_save_or_load_dialog(path, DIALOG_SAVE, cd);
}

void collection_dialog_save_close(gchar *path, CollectionData *cd)
{
	if (!path) path = cd->path;
	if (!path) path = cd->name;

	collection_save_or_load_dialog(path, DIALOG_SAVE_CLOSE, cd);
}

void collection_dialog_load(gchar *path)
{
	collection_save_or_load_dialog(path, DIALOG_LOAD, NULL);
}

void collection_dialog_append(gchar *path, CollectionData *cd)
{
	collection_save_or_load_dialog(path, DIALOG_APPEND, cd);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
