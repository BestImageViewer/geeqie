/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#include "window.h"

#include <cstdio>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>

#include <config.h>

#include "intl.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-help.h"
#include "ui-misc.h"
#include "ui-utildlg.h"

GtkWidget *window_new(const gchar *role, const gchar *icon, const gchar *subtitle)
{
	GApplication *app = g_application_get_default();

	GtkWidget *window = gtk_application_window_new(GTK_APPLICATION(app));
	if (!window) return nullptr;

	g_autofree gchar *title = nullptr;
	if (subtitle)
		{
		title = g_strdup_printf("%s - %s", subtitle, GQ_APPNAME);
		}
	else
		{
		title = g_strdup_printf("%s", GQ_APPNAME);
		}

	gtk_window_set_title(GTK_WINDOW(window), title);

	g_autoptr(GdkPixbuf) pixbuf = pixbuf_inline(icon ? icon : PIXBUF_INLINE_ICON);
	if (pixbuf)
		{
		gtk_window_set_icon(GTK_WINDOW(window), pixbuf);
		}

	gtk_window_set_role(GTK_WINDOW(window), role);

	if (options->hide_window_decorations)
		{
		gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
		}

	return window;
}

gboolean window_maximized(GtkWidget *window)
{
	GdkWindowState state;

	if (!window || !gtk_widget_get_window(window)) return FALSE;

	state = gdk_window_get_state(gtk_widget_get_window(window));
	return !!(state & GDK_WINDOW_STATE_MAXIMIZED);
}

/*
 *-----------------------------------------------------------------------------
 * Open browser with the help Documentation
 *-----------------------------------------------------------------------------
 */

static void help_browser_run(const gchar *path)
{
	g_autoptr(GError) error = nullptr;

	g_app_info_launch_default_for_uri(path, nullptr, &error);

	if (error)
		{
		log_printf("Error opening %s in the default browser\n", error->message);
		}
}

/*
 *-----------------------------------------------------------------------------
 * help window
 *-----------------------------------------------------------------------------
 */

static void help_window_destroy_cb(GtkWidget *, gpointer data)
{
	auto **help_window = static_cast<GtkWidget **>(data);
	*help_window = nullptr;
}

void help_window_show(const gchar *key)
{
	if (key && strstr(key, ".html") != nullptr)
		{
		g_autofree gchar *path = g_build_filename(gq_htmldir, key, NULL);
		if (!isfile(path))
			{
			g_free(path);
			if (g_strcmp0(key, "index.html") == 0)
				{
				path = g_build_filename("https://www.geeqie.org/help/", "GuideIndex.html", NULL);
				}
			else
				{
				path = g_build_filename("https://www.geeqie.org/help/", key, NULL);
				}
			}
		help_browser_run(path);
		return;
		}

	static GtkWidget *help_window = nullptr;
	if (help_window)
		{
		gtk_window_present(GTK_WINDOW(help_window));
		if (key) help_window_set_key(help_window, key);
		return;
		}

	const auto open_help_file = [key](const gchar *html_file, const gchar *text_file)
	{
		g_autofree gchar *html_path = g_build_filename(gq_helpdir, html_file, NULL);
		if (isfile(html_path))
			{
			g_autofree gchar *url = g_strdup_printf("file://%s", html_path);
			help_browser_run(url);
			}
		else
			{
			g_autofree gchar *text_path = g_build_filename(gq_helpdir, text_file, NULL);
			help_window = help_window_new(_("Help"), "help", text_path, key);

			g_signal_connect(G_OBJECT(help_window), "destroy",
			                 G_CALLBACK(help_window_destroy_cb), &help_window);
			}
	};

	if (!strcmp(key, "release_notes"))
		{
		open_help_file("README.html", "README.md");
		}
	else
		{
		open_help_file("ChangeLog.html", "ChangeLog");
		}
}

/*
 *-----------------------------------------------------------------------------
 * on-line help search dialog
 *-----------------------------------------------------------------------------
 */

static void help_search_window_show_icon_press(GtkEntry *edit_widget, GtkEntryIconPosition, GdkEvent *, gpointer)
{
	gq_gtk_entry_set_text(edit_widget, "");
}

static void help_search_window_ok_cb(GenericDialog *, gpointer data)
{
	auto *edit_widget = GTK_ENTRY(data);

	g_autofree gchar *search_command = g_strconcat(options->help_search_engine,
	                                               gq_gtk_entry_get_text(edit_widget),
	                                               NULL);
	help_browser_run(search_command);
}

void help_search_window_show()
{
	GtkWidget *table;
	GtkWidget *label1;
	GtkWidget *label2;

	GtkWidget *edit_widget = gtk_entry_new();
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(edit_widget),
	                                  GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text(GTK_ENTRY(edit_widget),
	                                GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	gtk_widget_set_size_request(edit_widget, 300, -1);
	g_signal_connect(GTK_ENTRY(edit_widget), "icon-press",
	                 G_CALLBACK(help_search_window_show_icon_press), nullptr);

	GenericDialog *gd = generic_dialog_new(_("On-line help search"), "help_search",
	                                       nullptr, TRUE,
	                                       nullptr, edit_widget);
	generic_dialog_add_message(gd, nullptr, _("Search the on-line help files.\n"), nullptr, FALSE);

	generic_dialog_add_button(gd, GQ_ICON_OK, "OK",
				  help_search_window_ok_cb, TRUE);

	label1 = pref_label_new(GENERIC_DIALOG(gd)->vbox, _("Search engine:"));
	gtk_label_set_xalign(GTK_LABEL(label1), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label1), 0.5);

	label2 = pref_label_new(GENERIC_DIALOG(gd)->vbox, options->help_search_engine);
	gtk_label_set_xalign(GTK_LABEL(label2), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label2), 0.5);

	pref_spacer(GENERIC_DIALOG(gd)->vbox, 0);

	table = pref_table_new(gd->vbox, 3, 1, FALSE, TRUE);
	pref_table_label(table, 0, 0, _("Search terms:"), GTK_ALIGN_END);
	gq_gtk_grid_attach_default(GTK_GRID(table), edit_widget, 1, 2, 0, 1);
	generic_dialog_attach_default(gd, edit_widget);
	gtk_widget_show(edit_widget);

	gtk_widget_grab_focus(edit_widget);

	gtk_widget_show(gd->dialog);
}

void help_pdf()
{
	g_autofree gchar *path = g_build_filename(gq_helpdir, "help.pdf", NULL);

	if (!isfile(path))
		{
		g_free(path);
		path = g_build_filename("https://www.geeqie.org/help-pdf/help.pdf", NULL);
		}

	g_autofree gchar *command = g_strdup_printf("xdg-open %s", path);

	g_autoptr(GError) error = nullptr;
	if (!g_spawn_command_line_async(command, &error))
		{
		log_printf(_("Warning: Failed to execute command: %s\n"), error->message);
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
