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

#include "ui-utildlg.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>

#include <config.h>

#include "compat.h"
#include "filedata.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "rcfile.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-pathsel.h"
#include "ui-tabcomp.h"
#include "window.h"

namespace
{

struct DialogWindow
{
	GdkRectangle rect;
	gchar *title;
	gchar *role;
};

DialogWindow *dialog_window_find_by_title_and_role(const std::vector<DialogWindow *> &dialog_windows,
                                                   const gchar *title, const gchar *role)
{
	const auto dialog_window_has_title_and_role = [title, role](const DialogWindow *dw)
	{
		return g_strcmp0(dw->title, title) == 0 && g_strcmp0(dw->role, role) == 0;
	};

	auto it = std::find_if(dialog_windows.begin(), dialog_windows.end(),
	                       dialog_window_has_title_and_role);

	return (it != dialog_windows.end()) ? *it : nullptr;
}

std::vector<DialogWindow *> dialog_windows;

} // namespace

/*
 *-----------------------------------------------------------------------------
 * generic dialog
 *-----------------------------------------------------------------------------
 */

static void generic_dialog_save_window(const gchar *title, const gchar *role, GdkRectangle rect)
{
	DialogWindow *dw = dialog_window_find_by_title_and_role(dialog_windows, title, role);
	if (dw)
		{
		dw->rect = rect;
		return;
		}

	dw = g_new0(DialogWindow, 1);
	dw->rect = rect;
	dw->title = g_strdup(title);
	dw->role = g_strdup(role);

	dialog_windows.push_back(dw);
}

gboolean generic_dialog_find_window(const gchar *title, const gchar *role, GdkRectangle &rect)
{
	DialogWindow *dw = dialog_window_find_by_title_and_role(dialog_windows, title, role);
	if (!dw) return FALSE;

	rect = dw->rect;
	return TRUE;
}

void generic_dialog_close(GenericDialog *gd)
{
	/* The window title is modified in window.cc: window_new()
	 * by appending the string " - Geeqie"
	 */
	static const gchar *ident_string = " - " GQ_APPNAME;
	g_autofree gchar *full_title = g_strdup(gtk_window_get_title(GTK_WINDOW(gd->dialog)));
	g_autofree gchar *actual_title = strndup(full_title, g_strrstr(full_title, ident_string) - full_title);

	GdkRectangle rect = window_get_root_origin_geometry(gtk_widget_get_window(gd->dialog));

	generic_dialog_save_window(actual_title, gtk_window_get_role(GTK_WINDOW(gd->dialog)), rect);

	gq_gtk_widget_destroy(gd->dialog);
	g_free(gd);
}

static void generic_dialog_click_cb(GtkWidget *widget, gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);
	void (*func)(GenericDialog *, gpointer);
	gboolean auto_close;

	func = reinterpret_cast<void(*)(GenericDialog *, gpointer)>(g_object_get_data(G_OBJECT(widget), "dialog_function"));
	auto_close = gd->auto_close;

	if (func) func(gd, gd->data);
	if (auto_close) generic_dialog_close(gd);
}

static gboolean generic_dialog_default_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);

	if ((event->keyval == (GDK_KEY_Return) || (event->keyval == GDK_KEY_KP_Enter)) && gtk_widget_has_focus(widget)
	    && gd->default_cb)
		{
		gboolean auto_close;

		auto_close = gd->auto_close;
		gd->default_cb(gd, gd->data);
		if (auto_close) generic_dialog_close(gd);

		return TRUE;
		}
	return FALSE;
}

void generic_dialog_attach_default(GenericDialog *gd, GtkWidget *widget)
{
	if (!gd || !widget) return;
	g_signal_connect(G_OBJECT(widget), "key_press_event",
			 G_CALLBACK(generic_dialog_default_key_press_cb), gd);
}

static gboolean generic_dialog_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);
	gboolean auto_close = gd->auto_close;

	if (event->keyval == GDK_KEY_Escape)
		{
		if (gd->cancel_cb)
			{
			gd->cancel_cb(gd, gd->data);
			if (auto_close) generic_dialog_close(gd);
			}
		else
			{
			if (auto_close) generic_dialog_click_cb(widget, data);
			}
		return TRUE;
		}
	return FALSE;
}

static gboolean generic_dialog_delete_cb(GtkWidget *, GdkEventAny *, gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);
	gboolean auto_close;

	auto_close = gd->auto_close;

	if (gd->cancel_cb) gd->cancel_cb(gd, gd->data);
	if (auto_close) generic_dialog_close(gd);

	return TRUE;
}

static void generic_dialog_show_cb(GtkWidget *widget, gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);
	if (gd->cancel_button)
		{
		gtk_box_reorder_child(GTK_BOX(gd->hbox), gd->cancel_button, -1);
		}

	g_signal_handlers_disconnect_by_func(G_OBJECT(widget), (gpointer)(generic_dialog_show_cb), gd);
}

gboolean generic_dialog_get_alternative_button_order(GtkWidget *widget)
{
	GtkSettings *settings;
	GObjectClass *klass;
	gboolean alternative_order = FALSE;

	settings = gtk_settings_get_for_screen(gtk_widget_get_screen(widget));
	klass = G_OBJECT_CLASS(GTK_SETTINGS_GET_CLASS(settings));
	if (g_object_class_find_property(klass, "gtk-alternative-button-order"))
		{
		g_object_get(settings, "gtk-alternative-button-order", &alternative_order, NULL);
		}

	return alternative_order;
}

GtkWidget *generic_dialog_add_button(GenericDialog *gd, const gchar *icon_name, const gchar *text,
				     void (*func_cb)(GenericDialog *, gpointer), gboolean is_default)
{
	GtkWidget *button;
	gboolean alternative_order;

	button = pref_button_new(nullptr, icon_name, text,
				 G_CALLBACK(generic_dialog_click_cb), gd);

	gtk_widget_set_can_default(button, TRUE);
	g_object_set_data(G_OBJECT(button), "dialog_function", reinterpret_cast<void *>(func_cb));

	gq_gtk_container_add(GTK_WIDGET(gd->hbox), button);

	alternative_order = generic_dialog_get_alternative_button_order(gd->hbox);

	if (is_default)
		{
		gtk_widget_grab_default(button);
		gtk_widget_grab_focus(button);
		gd->default_cb = func_cb;

		if (!alternative_order) gtk_box_reorder_child(GTK_BOX(gd->hbox), button, -1);
		}
	else
		{
		if (!alternative_order) gtk_box_reorder_child(GTK_BOX(gd->hbox), button, 0);
		}

	gtk_widget_show(button);

	return button;
}

/**
 * @brief
 * @param gd
 * @param icon_stock_id
 * @param heading
 * @param text
 * @param expand Used as the "expand" and "fill" parameters in the eventual call to gq_gtk_box_pack_start()
 * @returns
 *
 *
 */
GtkWidget *generic_dialog_add_message(GenericDialog *gd, const gchar *icon_name,
				      const gchar *heading, const gchar *text, gboolean expand)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *label;

	hbox = pref_box_new(gd->vbox, expand, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	if (icon_name)
		{
		GtkWidget *image;

		image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DIALOG);
		gtk_widget_set_halign(GTK_WIDGET(image), GTK_ALIGN_CENTER);
		gtk_widget_set_valign(GTK_WIDGET(image), GTK_ALIGN_START);
		gq_gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
		gtk_widget_show(image);
		}

	vbox = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	if (heading)
		{
		label = pref_label_new(vbox, heading);
		pref_label_bold(label, TRUE, TRUE);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_yalign(GTK_LABEL(label), 0.5);
		}
	if (text)
		{
		label = pref_label_new(vbox, text);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_yalign(GTK_LABEL(label), 0.5);
		gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
		}

	return vbox;
}

void generic_dialog_windows_load_config(const gchar **attribute_names, const gchar **attribute_values)
{
	auto dw = g_new0(DialogWindow, 1);

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;
		if (READ_CHAR(*dw, title)) continue;
		if (READ_CHAR(*dw, role)) continue;
		if (READ_INT(dw->rect, x)) continue;
		if (READ_INT(dw->rect, y)) continue;
		if (READ_INT_FULL("w", dw->rect.width)) continue;
		if (READ_INT_FULL("h", dw->rect.height)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	if (dw->title && dw->title[0] != 0)
		{
		dialog_windows.push_back(dw);
		}
	else
		{
		g_free(dw->title);
		g_free(dw->role);
		g_free(dw);
		}
}

void generic_dialog_windows_write_config(GString *outstr, gint indent)
{
	if (!options->save_dialog_window_positions || dialog_windows.empty()) return;

	WRITE_NL(); WRITE_STRING("<%s>", "dialogs");
	indent++;

	for (const DialogWindow *dw : dialog_windows)
		{
		WRITE_NL(); WRITE_STRING("<window ");
		write_char_option(outstr, "title", dw->title);
		write_char_option(outstr, "role", dw->role);
		WRITE_INT(dw->rect, x);
		WRITE_INT(dw->rect, y);
		write_int_option(outstr, "w", dw->rect.width);
		write_int_option(outstr, "h", dw->rect.height);
		WRITE_STRING("/>");
		}

	indent--;
	WRITE_NL(); WRITE_STRING("</%s>", "dialogs");
}

static void generic_dialog_setup(GenericDialog *gd,
				 const gchar *title,
				 const gchar *role,
				 GtkWidget *parent, gboolean auto_close,
				 void (*cancel_cb)(GenericDialog *, gpointer), gpointer data)
{
	GtkWidget *vbox;
	GtkWidget *scrolled;

	gd->auto_close = auto_close;
	gd->data = data;
	gd->cancel_cb = cancel_cb;

	gd->dialog = window_new(role, nullptr, nullptr, title);
	DEBUG_NAME(gd->dialog);
	gtk_window_set_type_hint(GTK_WINDOW(gd->dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

	if (options->save_dialog_window_positions)
		{
		GdkRectangle rect;
		if (generic_dialog_find_window(title, role, rect))
			{
			gtk_window_set_default_size(GTK_WINDOW(gd->dialog), rect.width, rect.height);
			gq_gtk_window_move(GTK_WINDOW(gd->dialog), rect.x, rect.y);
			}
		}

	if (parent)
		{
		GtkWindow *window = nullptr;

		if (GTK_IS_WINDOW(parent))
			{
			window = GTK_WINDOW(parent);
			}
		else
			{
			GtkWidget *top;

			top = gtk_widget_get_toplevel(parent);
			if (GTK_IS_WINDOW(top) && gtk_widget_is_toplevel(top)) window = GTK_WINDOW(top);
			}

		if (window) gtk_window_set_transient_for(GTK_WINDOW(gd->dialog), window);
		}

	g_signal_connect(G_OBJECT(gd->dialog), "delete_event",
			 G_CALLBACK(generic_dialog_delete_cb), gd);
	g_signal_connect(G_OBJECT(gd->dialog), "key_press_event",
			 G_CALLBACK(generic_dialog_key_press_cb), gd);

	gtk_window_set_resizable(GTK_WINDOW(gd->dialog), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(gd->dialog), PREF_PAD_BORDER);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_BUTTON_SPACE);
	gq_gtk_container_add(GTK_WIDGET(scrolled), vbox);
	gq_gtk_container_add(GTK_WIDGET(gd->dialog), scrolled);
	gtk_widget_show(scrolled);

	gtk_widget_show(vbox);

	gd->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gq_gtk_box_pack_start(GTK_BOX(vbox), gd->vbox, TRUE, TRUE, 0);
	gtk_widget_show(gd->vbox);

	gd->hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(gd->hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(gd->hbox), PREF_PAD_BUTTON_GAP);
	gq_gtk_box_pack_start(GTK_BOX(vbox), gd->hbox, FALSE, FALSE, 0);
	gtk_widget_show(gd->hbox);

	if (gd->cancel_cb)
		{
		gd->cancel_button = generic_dialog_add_button(gd, GQ_ICON_CANCEL, _("Cancel"), gd->cancel_cb, TRUE);
		}
	else
		{
		gd->cancel_button = nullptr;
		}

	if (generic_dialog_get_alternative_button_order(gd->hbox))
		{
		g_signal_connect(G_OBJECT(gd->dialog), "show",
				 G_CALLBACK(generic_dialog_show_cb), gd);
		}

	gd->default_cb = nullptr;
}

/**
 * @brief When parent is not NULL, the dialog is set as a transient of the window containing parent
 */
GenericDialog *generic_dialog_new(const gchar *title,
				  const gchar *role,
				  GtkWidget *parent, gboolean auto_close,
				  void (*cancel_cb)(GenericDialog *, gpointer), gpointer data)
{
	GenericDialog *gd;

	gd = g_new0(GenericDialog, 1);
	generic_dialog_setup(gd, title, role,
			     parent, auto_close, cancel_cb, data);
	return gd;
}
/*
 *-----------------------------------------------------------------------------
 * simple warning dialog
 *-----------------------------------------------------------------------------
 */

static void warning_dialog_ok_cb(GenericDialog *, gpointer)
{
	/* no op */
}

GenericDialog *warning_dialog(const gchar *heading, const gchar *text,
			      const gchar *icon_name, GtkWidget *parent)
{
	GenericDialog *gd;

	gd = generic_dialog_new(heading, "warning", parent, TRUE, nullptr, nullptr);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", warning_dialog_ok_cb, TRUE);

	generic_dialog_add_message(gd, icon_name, heading, text, TRUE);

	gtk_widget_show(gd->dialog);

	return gd;
}

/*
 *-----------------------------------------------------------------------------
 * AppImage version update notification message
 *-----------------------------------------------------------------------------
 *
 * If the current version is not on GitHub, assume a newer one is available
 * and show a notification message.
 */

struct AppImageData
{
	GThreadPool *thread_pool;
};

static void show_new_appimage_notification(GtkApplication *app)
{
	auto *notification = g_notification_new("Geeqie");

	g_notification_set_title(notification, _("AppImage"));
	g_notification_set_body(notification, _("A new Geeqie AppImage is available"));
	g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
	g_notification_set_default_action(notification, "app.null");

	g_application_send_notification(G_APPLICATION(app), "new-appimage-notification", notification);

	g_object_unref(notification);
}

static void new_appimage_notification_func(gpointer, gpointer user_data)
{
	FILE *pipe;
	GNetworkMonitor *net_mon;
	GSocketConnectable *geeqie_github;
	auto app = static_cast<GtkApplication *>(user_data);
	constexpr gint max_buffer_size = 16384;
	char buffer[max_buffer_size];
	char result[max_buffer_size];
	gboolean internet_available = FALSE;

	/* If this is a release version, do not check for updates.
	 * Non-release version is e.g. 2.5+git20241117-167271b8
	 */
	if (g_strrstr(VERSION, "git"))
		{
		net_mon = g_network_monitor_get_default();
		geeqie_github = g_network_address_parse_uri("https://github.com/", 80, nullptr);

		if (geeqie_github)
			{
			internet_available = g_network_monitor_can_reach(net_mon, geeqie_github, nullptr, nullptr);
			g_object_unref(geeqie_github);
			}

		if (internet_available)
			{
			pipe = popen("curl --max-time 2 --silent https://api.github.com/repos/BestImageViewer/geeqie/releases/tags/continuous", "r");

			if (pipe == nullptr)
				{
				log_printf("Failed to get date from GitHub");
				}
			else
				{
				while (fgets(buffer, max_buffer_size, pipe) != nullptr)
					{
					strcat(result, buffer);
					}
				pclose(pipe);

				/* GitHub date looks like: "published_at": "2024-04-17T08:50:08Z" */
				gchar *start_date = g_strstr_len(result, -1, "published_at");

				if (start_date)
					{
					start_date += 16; // skip 'published_at": "' part
					start_date[10] = '\0'; // drop everything after YYYY-mm-dd part

					std::tm github_version_date{};
					strptime(start_date, "%Y-%m-%d", &github_version_date);

					/* VERSION looks like: 2.0.1+git20220116-c791cbee */
					g_auto(GStrv) version_split = g_strsplit_set(VERSION, "+-", -1);

					std::tm current_version_date{};
					strptime(version_split[1] + 3, "%Y%m%d", &current_version_date);

					if (mktime(&github_version_date) > mktime(&current_version_date))
						{
						show_new_appimage_notification(app);
						}
					}
				}
			}
		}
}

void new_appimage_notification(GtkApplication *app)
{
	AppImageData *appimage_data;

	appimage_data = g_new0(AppImageData, 1);

	appimage_data->thread_pool = g_thread_pool_new(new_appimage_notification_func, app, 1, FALSE, nullptr);
	g_thread_pool_push(appimage_data->thread_pool, appimage_data, nullptr);
}

/*
 *-----------------------------------------------------------------------------
 * generic file ops dialog routines
 *-----------------------------------------------------------------------------
 */

void file_dialog_close(FileDialog *fdlg)
{
	file_data_unref(fdlg->source_fd);
	g_free(fdlg->dest_path);
	if (fdlg->source_list) filelist_free(fdlg->source_list);

	generic_dialog_close(GENERIC_DIALOG(fdlg));
}

FileDialog *file_dialog_new(const gchar *title,
			    const gchar *role,
			    GtkWidget *parent,
			    void (*cancel_cb)(FileDialog *, gpointer), gpointer data)
{
	FileDialog *fdlg = nullptr;

	fdlg = g_new0(FileDialog, 1);

	generic_dialog_setup(GENERIC_DIALOG(fdlg), title,
			     role, parent, FALSE,
			     reinterpret_cast<void(*)(GenericDialog *, gpointer)>(cancel_cb), data);

	return fdlg;
}

GtkWidget *file_dialog_add_button(FileDialog *fdlg, const gchar *stock_id, const gchar *text,
				  void (*func_cb)(FileDialog *, gpointer), gboolean is_default)
{
	return generic_dialog_add_button(GENERIC_DIALOG(fdlg), stock_id, text,
					 reinterpret_cast<void(*)(GenericDialog *, gpointer)>(func_cb), is_default);
}

static void file_dialog_entry_cb(GtkWidget *, gpointer data)
{
	auto fdlg = static_cast<FileDialog *>(data);
	g_free(fdlg->dest_path);
	fdlg->dest_path = remove_trailing_slash(gq_gtk_entry_get_text(GTK_ENTRY(fdlg->entry)));
}

static void file_dialog_entry_enter_cb(const gchar *, gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);

	file_dialog_entry_cb(nullptr, data);

	if (gd->default_cb) gd->default_cb(gd, gd->data);
}

/**
 * @brief Default_path is default base directory, and is only used if no history
 * exists for history_key (HOME is used if default_path is NULL).
 * path can be a full path or only a file name. If name only, appended to
 * the default_path or the last history (see default_path)
 */
void file_dialog_add_path_widgets(FileDialog *fdlg, const gchar *default_path, const gchar *path,
				  const gchar *history_key, const gchar *filter, const gchar *filter_desc)
{
	GtkWidget *tabcomp;
	GtkWidget *list;

	if (fdlg->entry) return;

	tabcomp = tab_completion_new_with_history(&fdlg->entry, nullptr,
		  history_key, -1, file_dialog_entry_enter_cb, fdlg);
	gq_gtk_box_pack_end(GTK_BOX(GENERIC_DIALOG(fdlg)->vbox), tabcomp, FALSE, FALSE, 0);
	generic_dialog_attach_default(GENERIC_DIALOG(fdlg), fdlg->entry);
	gtk_widget_show(tabcomp);

	if (path && path[0] == G_DIR_SEPARATOR)
		{
		fdlg->dest_path = g_strdup(path);
		}
	else
		{
		const gchar *base;

		base = tab_completion_set_to_last_history(fdlg->entry);

		if (!base) base = default_path;
		if (!base) base = homedir();

		if (path)
			{
			fdlg->dest_path = g_build_filename(base, path, NULL);
			}
		else
			{
			fdlg->dest_path = g_strdup(base);
			}
		}

	list = path_selection_new_with_files(fdlg->entry, fdlg->dest_path, filter, filter_desc);
	path_selection_add_select_func(fdlg->entry, file_dialog_entry_enter_cb, fdlg);
	gq_gtk_box_pack_end(GTK_BOX(GENERIC_DIALOG(fdlg)->vbox), list, TRUE, TRUE, 0);
	gtk_widget_show(list);

	gtk_widget_grab_focus(fdlg->entry);
	if (fdlg->dest_path)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(fdlg->entry), fdlg->dest_path);
		gtk_editable_set_position(GTK_EDITABLE(fdlg->entry), strlen(fdlg->dest_path));
		}

	g_signal_connect(G_OBJECT(fdlg->entry), "changed",
			 G_CALLBACK(file_dialog_entry_cb), fdlg);
}

void file_dialog_sync_history(FileDialog *fdlg, gboolean dir_only)
{
	if (!fdlg->dest_path) return;

	if (!dir_only ||
	    (dir_only && isdir(fdlg->dest_path)) )
		{
		tab_completion_append_to_history(fdlg->entry, fdlg->dest_path);
		}
	else
		{
		g_autofree gchar *buf = remove_level_from_path(fdlg->dest_path);
		tab_completion_append_to_history(fdlg->entry, buf);
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
