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

#include "main.h"
#include "window.h"

#include "misc.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-help.h"
#include "ui-misc.h"
#include "ui-utildlg.h"

GtkWidget *window_new(GtkWindowType type, const gchar *role, const gchar *icon,
		      const gchar *icon_file, const gchar *subtitle)
{
	gchar *title;
	GtkWidget *window;

	window = gtk_window_new(type);
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
	g_free(title);

	window_set_icon(window, icon, icon_file);
	gtk_window_set_role(GTK_WINDOW(window), role);

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

static gchar *command_result(const gchar *binary, const gchar *command)
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

static int help_browser_command(const gchar *command, const gchar *path)
{
	gchar *result;
	gchar *buf;
	gchar *begin;
	gchar *end;
	int retval = -1;

	if (!command || !path) return retval;

	DEBUG_1("Help command pre \"%s\", \"%s\"", command, path);

	buf = g_strdup(command);
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
	g_free(buf);

	DEBUG_1("Help command post [%s]", result);

	retval = runcmd(result);
	DEBUG_1("Help command exit code: %d", retval);

	g_free(result);
	return retval;
}

/*
 * each set of 2 strings is one browser:
 *   the 1st is the binary to look for in the path
 *   the 2nd has 3 capabilities:
 *        NULL     exec binary with html file path as command line
 *        string   exec string and use results for command line
 *        !string  use text following ! as command line, replacing optional %s with html file path
*/
static const gchar *html_browsers[] =
{
	/* Our specific script */
	GQ_APPNAME_LC "_html_browser", nullptr,
	/* Redhat has a nifty htmlview script to start the user's preferred browser */
	"htmlview",	nullptr,
	/* Debian has even better approach with alternatives */
	"sensible-browser", nullptr,
	/* GNOME 2 */
	"gconftool-2",	"gconftool-2 -g /desktop/gnome/url-handlers/http/command",
	/* KDE */
	"kfmclient",	"!kfmclient exec \"%s\"",
	/* use fallbacks */
	"firefox",	nullptr,
	"mozilla",	nullptr,
	"konqueror",	nullptr,
	"netscape",	nullptr,
	"opera",	"!opera --remote 'openURL(%s,new-page)'",
	nullptr,		nullptr
};

static void help_browser_run(const gchar *path)
{
	const gchar *name = options->helpers.html_browser.command_name;
	const gchar *cmd = options->helpers.html_browser.command_line;
	gchar *result = nullptr;
	gint i;

	i = 0;
	while (!result)
		{
		if ((name && *name) || (cmd && *cmd)) {
			DEBUG_1("Trying browser: name=%s command=%s", name, cmd);
			result = command_result(name, cmd);
			DEBUG_1("Result: %s", result);
			if (result)
				{
				int ret = help_browser_command(result, path);

				if (ret == 0) break;
				g_free(result);
				result = nullptr;
			}
		}
		if (!html_browsers[i]) break;
		name = html_browsers[i++];
		cmd = html_browsers[i++];
		}

	if (!result)
		{
		log_printf("Unable to detect an installed browser.\n");
		return;
		}

	g_free(result);
}

/*
 *-----------------------------------------------------------------------------
 * help window
 *-----------------------------------------------------------------------------
 */

static GtkWidget *help_window = nullptr;

static void help_window_destroy_cb(GtkWidget *, gpointer)
{
	help_window = nullptr;
}

void help_window_show(const gchar *key)
{
	gchar *path;

	if (key && strstr(key, ".html") != nullptr)
		{
		path = g_build_filename(gq_htmldir, key, NULL);
		if (!isfile(path))
			{
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
		g_free(path);
		return;
		}

	if (help_window)
		{
		gtk_window_present(GTK_WINDOW(help_window));
		if (key) help_window_set_key(help_window, key);
		return;
		}

	if (!strcmp(key, "release_notes"))
		{
		path = g_build_filename(gq_helpdir, "README.html", NULL);
		if (isfile(path))
			{
			g_free(path);
			path = g_build_filename("file://", gq_helpdir, "README.html", NULL);
			help_browser_run(path);
			g_free(path);
			}
		else
			{
			g_free(path);
			path = g_build_filename(gq_helpdir, "README.md", NULL);
			help_window = help_window_new(_("Help"), "help", path, key);
			g_free(path);

			g_signal_connect(G_OBJECT(help_window), "destroy",
					 G_CALLBACK(help_window_destroy_cb), NULL);
			}
		}
	else
		{
		path = g_build_filename(gq_helpdir, "ChangeLog.html", NULL);
		if (isfile(path))
			{
			g_free(path);
			path = g_build_filename("file://", gq_helpdir, "ChangeLog.html", NULL);
			help_browser_run(path);
			g_free(path);
			}
		else
			{
			g_free(path);
			path = g_build_filename(gq_helpdir, "ChangeLog", NULL);
			help_window = help_window_new(_("Help"), "help", path, key);
			g_free(path);

			g_signal_connect(G_OBJECT(help_window), "destroy",
					 G_CALLBACK(help_window_destroy_cb), NULL);
			}

		}
}

/*
 *-----------------------------------------------------------------------------
 * on-line help search dialog
 *-----------------------------------------------------------------------------
 */

struct HelpSearchData {
	GenericDialog *gd;
	GtkWidget *edit_widget;
	gchar *text_entry;
};

static void help_search_window_show_icon_press(GtkEntry *, GtkEntryIconPosition, GdkEvent *, gpointer userdata)
{
	auto hsd = static_cast<HelpSearchData *>(userdata);

	g_free(hsd->text_entry);
	hsd->text_entry = g_strdup("");
	gtk_entry_set_text(GTK_ENTRY(hsd->edit_widget), hsd->text_entry);
}

static void help_search_window_ok_cb(GenericDialog *, gpointer data)
{
	auto hsd = static_cast<HelpSearchData *>(data);
	gchar *search_command;

	search_command = g_strconcat(options->help_search_engine,
						gtk_entry_get_text(GTK_ENTRY(hsd->edit_widget)),
						NULL);
	help_browser_run(search_command);
	g_free(search_command);

	g_free(hsd);
}

static void help_search_window_cancel_cb(GenericDialog *, gpointer data)
{
	auto hsd = static_cast<HelpSearchData *>(data);

	g_free(hsd);
}

void help_search_window_show()
{
	GenericDialog *gd;
	GtkWidget *table;
	GtkWidget *label1;
	GtkWidget *label2;

	auto hsd = g_new0(HelpSearchData, 1);
	hsd->gd = gd = generic_dialog_new(_("On-line help search"), "help_search",
				nullptr, TRUE,
				help_search_window_cancel_cb, hsd);
	generic_dialog_add_message(gd, nullptr, _("Search the on-line help files.\n"), nullptr, FALSE);

	generic_dialog_add_button(gd, GTK_STOCK_OK, nullptr,
				  help_search_window_ok_cb, TRUE);

	label1 = pref_label_new(GENERIC_DIALOG(gd)->vbox, _("Search engine:"));
	gtk_label_set_xalign(GTK_LABEL(label1), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label1), 0.5);

	label2 = pref_label_new(GENERIC_DIALOG(gd)->vbox, options->help_search_engine);
	gtk_label_set_xalign(GTK_LABEL(label2), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label2), 0.5);

	pref_spacer(GENERIC_DIALOG(gd)->vbox, 0);

	table = pref_table_new(gd->vbox, 3, 1, FALSE, TRUE);
	pref_table_label(table, 0, 0, _("Search terms:"), 1.0);
	hsd->edit_widget = gtk_entry_new();
	gtk_widget_set_size_request(hsd->edit_widget, 300, -1);
	gtk_table_attach_defaults(GTK_TABLE(table), hsd->edit_widget, 1, 2, 0, 1);
	generic_dialog_attach_default(gd, hsd->edit_widget);
	gtk_widget_show(hsd->edit_widget);

	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(hsd->edit_widget),
						GTK_ENTRY_ICON_SECONDARY, "edit-clear");
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(hsd->edit_widget),
						GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	g_signal_connect(GTK_ENTRY(hsd->edit_widget), "icon-press",
						G_CALLBACK(help_search_window_show_icon_press), hsd);

	gtk_widget_grab_focus(hsd->edit_widget);

	gtk_widget_show(gd->dialog);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
