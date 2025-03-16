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

#include <array>
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

namespace
{

struct HtmlBrowser {
	operator bool() const { return (binary && *binary) || (command && *command); }
	gchar *command_result() const;

	const gchar *binary;  /**< the binary to look for in the path */
	const gchar *command; /**< has 3 capabilities:
	                       *     nullptr  exec binary with html file path as command line
	                       *     string   exec string and use results for command line
	                       *     !string  use text following ! as command line, replacing optional %s with html file path */
};

constexpr std::array<HtmlBrowser, 10> html_browsers{{
	/* Our specific script */
	{GQ_APPNAME_LC "_html_browser", nullptr},
	/* Redhat has a nifty htmlview script to start the user's preferred browser */
	{"htmlview",                    nullptr},
	/* Debian has even better approach with alternatives */
	{"sensible-browser",            nullptr},
	/* GNOME 2 */
	{"gconftool-2",                 "gconftool-2 -g /desktop/gnome/url-handlers/http/command"},
	/* KDE */
	{"kfmclient",                   "!kfmclient exec \"%s\""},
	/* use fallbacks */
	{"firefox",                     nullptr},
	{"mozilla",                     nullptr},
	{"konqueror",                   nullptr},
	{"netscape",                    nullptr},
	{"opera",                       "!opera --remote 'openURL(%s,new-page)'"},
}};

gchar *HtmlBrowser::command_result() const
{
	gchar *result = nullptr;
	FILE *f;
	gchar buf[2048];
	gint l;

	if (!binary || binary[0] == '\0') return nullptr;
	if (!file_in_path(binary)) return nullptr;

	if (!command || command[0] == '\0') return g_strdup(binary);
	if (command[0] == '!') return g_strdup(command + 1);

	f = popen(command, "r");
	if (!f) return nullptr;

	while ((l = fread(buf, sizeof(gchar), sizeof(buf), f)) > 0)
		{
		if (!result)
			{
			gint n = 0;

			while (n < l && buf[n] != '\n' && buf[n] != '\r') n++;
			if (n > 0) result = g_strndup(buf, n);
			}
		}

	pclose(f);

	return result;
}

} // namespace

GtkWidget *window_new(const gchar *role, const gchar *icon, const gchar *icon_file, const gchar *subtitle)
{
	g_autofree gchar *title = nullptr;
	GtkWidget *window;

#if HAVE_GTK4
	window = gtk_window_new();
#else
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
	if (!window) return nullptr;

	if (subtitle)
		{
		title = g_strdup_printf("%s - %s", subtitle, GQ_APPNAME);
		}
	else
		{
		title = g_strdup_printf("%s", GQ_APPNAME);
		}

	gtk_window_set_title(GTK_WINDOW(window), title);

	window_set_icon(window, icon, icon_file);
	gtk_window_set_role(GTK_WINDOW(window), role);

	if (options->hide_window_decorations)
		{
		gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
		}

	return window;
}

void window_set_icon(GtkWidget *window, const gchar *icon, const gchar *file)
{
	if (!icon && !file) icon = PIXBUF_INLINE_ICON;

	if (icon)
		{
		GdkPixbuf *pixbuf;

		pixbuf = pixbuf_inline(icon);
		if (pixbuf)
			{
			gtk_window_set_icon(GTK_WINDOW(window), pixbuf);
			g_object_unref(pixbuf);
			}
		}
	else
		{
		gtk_window_set_icon_from_file(GTK_WINDOW(window), file, nullptr);
		}
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

static int help_browser_command(const gchar *command, const gchar *path)
{
	g_autofree gchar *result = nullptr;
	gchar *begin;
	gchar *end;
	int retval = -1;

	if (!command || !path) return retval;

	DEBUG_1("Help command pre \"%s\", \"%s\"", command, path);

	g_autofree gchar *buf = g_strdup(command);
	begin = strstr(buf, "%s");
	if (begin)
		{
		*begin = '\0';
		end = begin + 2;
		begin = buf;

		result = g_strdup_printf("%s%s%s &", begin, path, end);
		}
	else
		{
		result = g_strdup_printf("%s \"%s\" &", command, path);
		}

	DEBUG_1("Help command post [%s]", result);

	retval = runcmd(result);
	DEBUG_1("Help command exit code: %d", retval);

	return retval;
}

static void help_browser_run(const gchar *path)
{
	const auto try_browser = [path](const HtmlBrowser &html_browser)
	{
		if (!html_browser) return false;

		DEBUG_1("Trying browser: name=%s command=%s", html_browser.binary, html_browser.command);

		g_autofree gchar *result = html_browser.command_result();
		DEBUG_1("Result: %s", result ? result : "(null)");
		if (!result) return false;

		return help_browser_command(result, path) == 0;
	};

	if (try_browser({options->helpers.html_browser.command_name, options->helpers.html_browser.command_line})) return;

	for (const auto &html_browser : html_browsers)
		{
		if (try_browser(html_browser)) return;
		}

	log_printf("Unable to detect an installed browser.\n");
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
