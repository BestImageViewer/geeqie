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

#include <string.h>

#include "main.h"
#include "ui-utildlg.h"

#include "filedata.h"
#include "rcfile.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-pathsel.h"
#include "ui-tabcomp.h"
#include "window.h"

/*
 *-----------------------------------------------------------------------------
 * generic dialog
 *-----------------------------------------------------------------------------
 */

typedef struct _DialogWindow DialogWindow;
struct _DialogWindow
{
	gint x;
	gint y;
	gint w;
	gint h;
	gchar *title;
	gchar *role;
};

static GList *dialog_windows = nullptr;

static void generic_dialog_save_window(const gchar *title, const gchar *role, gint x, gint y, gint h, gint w)
{
	GList *work;

	work = g_list_first(dialog_windows);
	while (work)
		{
		auto dw = static_cast<DialogWindow *>(work->data);
		if (g_strcmp0(dw->title ,title) == 0 && g_strcmp0(dw->role, role) == 0)
			{
			dw->x = x;
			dw->y = y;
			dw->w = w;
			dw->h = h;
			return;
			}
		work = work->next;
		}

	auto dw = g_new0(DialogWindow, 1);
	dw->title = g_strdup(title);
	dw->role = g_strdup(role);
	dw->x = x;
	dw->y = y;
	dw->w = w;
	dw->h = h;

	dialog_windows = g_list_append(dialog_windows, dw);
}

static gboolean generic_dialog_find_window(const gchar *title, const gchar *role, gint *x, gint *y, gint *h, gint *w)
{
	GList *work;

	work = g_list_first(dialog_windows);
	while (work)
		{
		auto dw = static_cast<DialogWindow *>(work->data);

		if (g_strcmp0(dw->title,title) == 0 && g_strcmp0(dw->role, role) == 0)
			{
			*x = dw->x;
			*y = dw->y;
			*w = dw->w;
			*h = dw->h;
			return TRUE;
			}
		work = work->next;
		}
	return FALSE;
}

void generic_dialog_close(GenericDialog *gd)
{
	gchar *ident_string;
	gchar *full_title;
	gchar *actual_title;
	gint x, y, h, w;

	gdk_window_get_root_origin(gtk_widget_get_window (gd->dialog), &x, &y);
	w = gdk_window_get_width(gtk_widget_get_window (gd->dialog));
	h = gdk_window_get_height(gtk_widget_get_window (gd->dialog));

	/* The window title is modified in window.cc: window_new()
	 * by appending the string " - Geeqie"
	 */
	ident_string = g_strconcat(" - ", GQ_APPNAME, NULL);
	full_title = g_strdup(gtk_window_get_title(GTK_WINDOW(gd->dialog)));
	actual_title = strndup(full_title, g_strrstr(full_title, ident_string) - full_title);

	generic_dialog_save_window(actual_title, gtk_window_get_role(GTK_WINDOW(gd->dialog)), x, y, w, h);

	gtk_widget_destroy(gd->dialog);
	g_free(gd);
	g_free(ident_string);
	g_free(full_title);
	g_free(actual_title);
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

	if (event->keyval == GDK_KEY_Return && gtk_widget_has_focus(widget)
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

static gboolean generic_dialog_delete_cb(GtkWidget *UNUSED(w), GdkEventAny *UNUSED(event), gpointer data)
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

GtkWidget *generic_dialog_add_button(GenericDialog *gd, const gchar *stock_id, const gchar *text,
				     void (*func_cb)(GenericDialog *, gpointer), gboolean is_default)
{
	GtkWidget *button;
	gboolean alternative_order;

	button = pref_button_new(nullptr, stock_id, text, FALSE,
				 G_CALLBACK(generic_dialog_click_cb), gd);

	gtk_widget_set_can_default(button, TRUE);
	g_object_set_data(G_OBJECT(button), "dialog_function", reinterpret_cast<void *>(func_cb));

	gtk_container_add(GTK_CONTAINER(gd->hbox), button);

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
 * @param expand Used as the "expand" and "fill" parameters in the eventual call to gtk_box_pack_start()
 * @returns
 *
 *
 */
GtkWidget *generic_dialog_add_message(GenericDialog *gd, const gchar *icon_stock_id,
				      const gchar *heading, const gchar *text, gboolean expand)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *label;

	hbox = pref_box_new(gd->vbox, expand, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	if (icon_stock_id)
		{
		GtkWidget *image;

		image = gtk_image_new_from_stock(icon_stock_id, GTK_ICON_SIZE_DIALOG);
		gtk_widget_set_halign(GTK_WIDGET(image), GTK_ALIGN_CENTER);
		gtk_widget_set_valign(GTK_WIDGET(image), GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
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
	auto dw =  g_new0(DialogWindow, 1);
	gchar *title = nullptr;
	gchar *role = nullptr;
	gint x = 0;
	gint y = 0;
	gint w = 0;
	gint h = 0;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;
		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_CHAR_FULL("role", role)) continue;
		if (READ_INT_FULL("x", x)) continue;
		if (READ_INT_FULL("y", y)) continue;
		if (READ_INT_FULL("w", w)) continue;
		if (READ_INT_FULL("h", h)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	if (title && title[0] != 0)
		{
		dw->title = g_strdup(title);
		dw->role = g_strdup(role);
		dw->x = x;
		dw->y = y;
		dw->w = w;
		dw->h = h;

		dialog_windows = g_list_append(dialog_windows, dw);
		}
}

void generic_dialog_windows_write_config(GString *outstr, gint indent)
{
	GList *work;

	if (options->save_dialog_window_positions && dialog_windows)
		{
		WRITE_NL(); WRITE_STRING("<%s>", "dialogs");
		indent++;

		work = g_list_first(dialog_windows);
		while (work)
			{
			auto dw = static_cast<DialogWindow *>(work->data);
			WRITE_NL(); WRITE_STRING("<window ");
			write_char_option(outstr, indent + 1, "title", dw->title);
			write_char_option(outstr, indent + 1, "role", dw->role);
			WRITE_INT(*dw, x);
			WRITE_INT(*dw, y);
			WRITE_INT(*dw, w);
			WRITE_INT(*dw, h);
			WRITE_STRING("/>");
			work = work->next;
			}
		indent--;
		WRITE_NL(); WRITE_STRING("</%s>", "dialogs");
		}
}

static void generic_dialog_setup(GenericDialog *gd,
				 const gchar *title,
				 const gchar *role,
				 GtkWidget *parent, gboolean auto_close,
				 void (*cancel_cb)(GenericDialog *, gpointer), gpointer data)
{
	GtkWidget *vbox;
	gint x, y, w, h;
	GtkWidget *scrolled;

	gd->auto_close = auto_close;
	gd->data = data;
	gd->cancel_cb = cancel_cb;

	gd->dialog = window_new(GTK_WINDOW_TOPLEVEL, role, nullptr, nullptr, title);
	DEBUG_NAME(gd->dialog);
	gtk_window_set_type_hint(GTK_WINDOW(gd->dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

	if (options->save_dialog_window_positions)
		{
		if (generic_dialog_find_window(title, role, &x, &y, &w, &h))
			{
			gtk_window_set_default_size(GTK_WINDOW(gd->dialog), w, h);
			gtk_window_move(GTK_WINDOW(gd->dialog), x, y);
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

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_BUTTON_SPACE);
	gtk_container_add(GTK_CONTAINER(scrolled), vbox);
	gtk_container_add(GTK_CONTAINER(gd->dialog), scrolled);
	gtk_widget_show(scrolled);

	gtk_widget_show(vbox);

	gd->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_box_pack_start(GTK_BOX(vbox), gd->vbox, TRUE, TRUE, 0);
	gtk_widget_show(gd->vbox);

	gd->hbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(gd->hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(gd->hbox), PREF_PAD_BUTTON_GAP);
	gtk_box_pack_start(GTK_BOX(vbox), gd->hbox, FALSE, FALSE, 0);
	gtk_widget_show(gd->hbox);

	if (gd->cancel_cb)
		{
		gd->cancel_button = generic_dialog_add_button(gd, GTK_STOCK_CANCEL, nullptr, gd->cancel_cb, TRUE);
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

static void warning_dialog_ok_cb(GenericDialog *UNUSED(gd), gpointer UNUSED(data))
{
	/* no op */
}

GenericDialog *warning_dialog(const gchar *heading, const gchar *text,
			      const gchar *icon_stock_id, GtkWidget *parent)
{
	GenericDialog *gd;

	gd = generic_dialog_new(heading, "warning", parent, TRUE, nullptr, nullptr);
	generic_dialog_add_button(gd, GTK_STOCK_OK, nullptr, warning_dialog_ok_cb, TRUE);

	generic_dialog_add_message(gd, icon_stock_id, heading, text, TRUE);

	gtk_widget_show(gd->dialog);

	return gd;
}

/*
 *-----------------------------------------------------------------------------
 * AppImage version update notification message with fade-out
 *-----------------------------------------------------------------------------
 *
 * It is expected that the version file on the server has the following format
 * for the first two lines in these formats:
 *
 * 1. x86_64 AppImage - e.g. Geeqie-v2.0+20221113-x86_64.AppImage
 * 2. arm AppImage - e.g. Geeqie-v2.0+20221025-aarch64.AppImage
 */

typedef struct _AppImageData AppImageData;
struct _AppImageData
{
	GFile *version_file;
	GDataInputStream *data_input_stream;
	GFileInputStream *file_input_stream;
	GThreadPool *thread_pool;
	GtkWidget *window;
	guint id;
};

static gboolean appimage_notification_close_cb(gpointer data)
{
	auto appimage_data = static_cast<AppImageData *>(data);

	if (appimage_data->window && gtk_window_get_opacity(GTK_WINDOW(appimage_data->window)) != 0)
		{
		g_source_remove(appimage_data->id);
		}

	if (appimage_data->window)
		{
		gtk_widget_destroy(appimage_data->window);
		}

	g_object_unref(appimage_data->data_input_stream);
	g_object_unref(appimage_data->file_input_stream);
	g_object_unref(appimage_data->version_file);
	g_thread_pool_free(appimage_data->thread_pool, TRUE, TRUE);
	g_free(appimage_data);

	return G_SOURCE_REMOVE;
}

static gboolean appimage_notification_fade_cb(gpointer data)
{
	auto appimage_data = static_cast<AppImageData *>(data);

	gtk_window_set_opacity(GTK_WINDOW(appimage_data->window), (gtk_window_get_opacity(GTK_WINDOW(appimage_data->window)) - 0.02));

	if (gtk_window_get_opacity(GTK_WINDOW(appimage_data->window)) == 0)
		{
		g_idle_add(appimage_notification_close_cb, data);

		return FALSE;
		}

	return TRUE;
}

static gboolean user_close_cb(GtkWidget *UNUSED(widget), GdkEvent *UNUSED(event), gpointer data)
{
	auto appimage_data = static_cast<AppImageData *>(data);

	g_idle_add(appimage_notification_close_cb, appimage_data);

	return FALSE;
}

static void show_notification_message(gchar *version_string_from_file, AppImageData *appimage_data)
{
	GtkWidget *label;
	gint server_version;
	gint running_version;
	gchar *version_string;

	server_version = atoi(strtok(strstr(version_string_from_file, "+") + 1, "-") );
	version_string = g_strdup(strstr(VERSION, "git"));

	/* If a release version is running, do not look for updates */
	if (version_string)
		{
		running_version = atoi(strtok(version_string + 3, "-") );
		g_free(version_string);

		if (server_version > running_version)
			{
			appimage_data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
			label = gtk_label_new (_("A new Geeqie AppImage is available"));
			gtk_widget_show(label);
			gtk_container_add(GTK_CONTAINER(appimage_data->window), label);
			gtk_window_set_decorated(GTK_WINDOW(appimage_data->window), FALSE);
			gtk_widget_set_size_request(appimage_data->window, 300, 40);
			gtk_window_set_gravity(GTK_WINDOW(appimage_data->window), GDK_GRAVITY_NORTH_EAST);
			gtk_window_move(GTK_WINDOW(appimage_data->window), (gdk_screen_width() * 0.8), (gdk_screen_height() / 20));
			gtk_window_set_skip_taskbar_hint(GTK_WINDOW(appimage_data->window), TRUE);
			gtk_window_set_focus_on_map(GTK_WINDOW(appimage_data->window), FALSE);
			g_signal_connect(appimage_data->window, "focus-in-event", G_CALLBACK(user_close_cb), appimage_data);
			appimage_data->id = g_timeout_add(100, appimage_notification_fade_cb, appimage_data);
			gtk_widget_show((appimage_data->window));
			}
		else
			{
			g_idle_add(appimage_notification_close_cb, appimage_data);
			}
		}
}

#ifdef __arm__
static void appimage_data_arm_read_line_async_ready_cb(GObject *source_object, GAsyncResult *res, gpointer data)
{
	auto appimage_data = static_cast<AppImageData *>(data);
	gsize length;
	gchar *result;

	result = g_data_input_stream_read_line_finish(appimage_data->data_input_stream, res, &length, nullptr);

	if (result && strstr(result, "-aarch64.AppImage"))
		{
		show_notification_message(result, appimage_data);
		}
	else
		{
		g_idle_add(appimage_notification_close_cb, data);
		}

	g_free(result);
}
#endif

static void appimage_data_x86_read_line_async_ready_cb(GObject *UNUSED(source_object), GAsyncResult *res, gpointer data)
{
	auto appimage_data = static_cast<AppImageData *>(data);
	gsize length;
	gchar *result;

	result = g_data_input_stream_read_line_finish(appimage_data->data_input_stream, res, &length, nullptr);

#ifdef __x86_64__
	if (result && strstr(result, "-x86_64.AppImage"))
		{
		show_notification_message(result, appimage_data);
		}
	else
		{
		g_idle_add(appimage_notification_close_cb, data);
		}
#endif

#ifdef __arm__
	g_data_input_stream_read_line_async(appimage_data->data_input_stream, G_PRIORITY_LOW, nullptr, appimage_data_arm_read_line_async_ready_cb, appimage_data);
#endif

	g_free(result);
}

static void appimage_notification_func(gpointer data, gpointer UNUSED(user_data))
{
	auto appimage_data = static_cast<AppImageData *>(data);
	GNetworkMonitor *net_mon;
	GSocketConnectable *geeqie_github_io;
	gboolean internet_available = FALSE;

	net_mon = g_network_monitor_get_default();
	geeqie_github_io = g_network_address_parse_uri(APPIMAGE_VERSION_FILE, 80, nullptr);
	if (geeqie_github_io)
		{
		internet_available = g_network_monitor_can_reach(net_mon, geeqie_github_io, nullptr, nullptr);
		g_object_unref(geeqie_github_io);
		}

	if (internet_available)
		{
		appimage_data->version_file = g_file_new_for_uri(APPIMAGE_VERSION_FILE);
		appimage_data->file_input_stream = g_file_read(appimage_data->version_file, nullptr, nullptr);
		appimage_data->data_input_stream = g_data_input_stream_new(G_INPUT_STREAM(appimage_data->file_input_stream));

		g_data_input_stream_read_line_async(appimage_data->data_input_stream, G_PRIORITY_LOW, nullptr, appimage_data_x86_read_line_async_ready_cb, appimage_data);
		}
	else
		{
		g_thread_pool_free(appimage_data->thread_pool, TRUE, TRUE);
		g_free(appimage_data);
		}
}

void appimage_notification()
{
	AppImageData *appimage_data;

	appimage_data = g_new0(AppImageData, 1);

	appimage_data->thread_pool = g_thread_pool_new(appimage_notification_func, appimage_data, 1, FALSE, nullptr);
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

static void file_dialog_entry_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	auto fdlg = static_cast<FileDialog *>(data);
	g_free(fdlg->dest_path);
	fdlg->dest_path = remove_trailing_slash(gtk_entry_get_text(GTK_ENTRY(fdlg->entry)));
}

static void file_dialog_entry_enter_cb(const gchar *UNUSED(path), gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);

	file_dialog_entry_cb(nullptr, data);

	if (gd->default_cb) gd->default_cb(gd, gd->data);
}

void file_dialog_add_path_widgets(FileDialog *fdlg, const gchar *default_path, const gchar *path,
				  const gchar *history_key, const gchar *filter, const gchar *filter_desc)
{
	GtkWidget *tabcomp;
	GtkWidget *list;

	if (fdlg->entry) return;

	tabcomp = tab_completion_new_with_history(&fdlg->entry, nullptr,
		  history_key, -1, file_dialog_entry_enter_cb, fdlg);
	gtk_box_pack_end(GTK_BOX(GENERIC_DIALOG(fdlg)->vbox), tabcomp, FALSE, FALSE, 0);
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
	gtk_box_pack_end(GTK_BOX(GENERIC_DIALOG(fdlg)->vbox), list, TRUE, TRUE, 0);
	gtk_widget_show(list);

	gtk_widget_grab_focus(fdlg->entry);
	if (fdlg->dest_path)
		{
		gtk_entry_set_text(GTK_ENTRY(fdlg->entry), fdlg->dest_path);
		gtk_editable_set_position(GTK_EDITABLE(fdlg->entry), strlen(fdlg->dest_path));
		}

	g_signal_connect(G_OBJECT(fdlg->entry), "changed",
			 G_CALLBACK(file_dialog_entry_cb), fdlg);
}

//void file_dialog_add_filter(FileDialog *fdlg, const gchar *filter, const gchar *filter_desc, gboolean set)
//{
	//if (!fdlg->entry) return;
	//path_selection_add_filter(fdlg->entry, filter, filter_desc, set);
//}

//void file_dialog_clear_filter(FileDialog *fdlg)
//{
	//if (!fdlg->entry) return;
	//path_selection_clear_filter(fdlg->entry);
//}

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
		gchar *buf = remove_level_from_path(fdlg->dest_path);
		tab_completion_append_to_history(fdlg->entry, buf);
		g_free(buf);
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
