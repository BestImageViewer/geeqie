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

#ifndef UI_UTILDLG_H
#define UI_UTILDLG_H


#define GENERIC_DIALOG(gd) ((GenericDialog *)gd)

struct GenericDialog
{
	GtkWidget *dialog;	/**< window */
	GtkWidget *vbox;	/**< place to add widgets */

	GtkWidget *hbox;	/**< button hbox */

	gboolean auto_close;

	void (*default_cb)(GenericDialog *, gpointer);
	void (*cancel_cb)(GenericDialog *, gpointer);
	gpointer data;

	GtkWidget *cancel_button; /**< private */

};

struct FileDialog
{
	GenericDialog gd;

	GtkWidget *entry;

	gint type;

	FileData *source_fd;
	GList *source_list;

	gchar *dest_path;
};


/**
 * @headerfile generic_dialog_new
 * When parent is not NULL, the dialog is set as a transient of the window containing parent
 */
GenericDialog *generic_dialog_new(const gchar *title,
				  const gchar *role,
				  GtkWidget *parent, gboolean auto_close,
				  void (*cancel_cb)(GenericDialog *, gpointer), gpointer data);
void generic_dialog_close(GenericDialog *gd);

GtkWidget *generic_dialog_add_button(GenericDialog *gd, const gchar *icon_name, const gchar *text,
				     void (*func_cb)(GenericDialog *, gpointer), gboolean is_default);
void generic_dialog_attach_default(GenericDialog *gd, GtkWidget *widget);

GtkWidget *generic_dialog_add_message(GenericDialog *gd, const gchar *icon_stock_id,
				      const gchar *heading, const gchar *text, gboolean expand);

gboolean generic_dialog_get_alternative_button_order(GtkWidget *widget);

GenericDialog *warning_dialog(const gchar *heading, const gchar *text,
			      const gchar *icon_name, GtkWidget *parent);

FileDialog *file_dialog_new(const gchar *title,
			    const gchar *role,
			    GtkWidget *parent,
			    void (*cancel_cb)(FileDialog *, gpointer), gpointer data);
void file_dialog_close(FileDialog *fd);

GtkWidget *file_dialog_add_button(FileDialog *fd, const gchar *stock_id, const gchar *text,
				  void (*func_cb)(FileDialog *, gpointer), gboolean is_default);

/**
 * @headerfile file_dialog_add_path_widgets
 * default_path is default base directory, and is only used if no history
 * exists for history_key (HOME is used if default_path is NULL).
 * path can be a full path or only a file name. If name only, appended to
 * the default_path or the last history (see default_path)
 */
void file_dialog_add_path_widgets(FileDialog *fd, const gchar *default_path, const gchar *path,
				  const gchar *history_key, const gchar *filter, const gchar *filter_desc);

void file_dialog_clear_filter(FileDialog *fd);
void file_dialog_sync_history(FileDialog *fd, gboolean dir_only);

void generic_dialog_windows_load_config(const gchar **attribute_names, const gchar **attribute_values);
void generic_dialog_windows_write_config(GString *outstr, gint indent);

void appimage_notification();
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
