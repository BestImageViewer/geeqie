/*
 * Copyright (C) 2006 John Ellis
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

#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <config.h>

#if HAVE_CLUTTER
#  include <clutter-gtk/clutter-gtk.h>
#  include <clutter/clutter.h>
#endif

#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#ifdef ENABLE_NLS
#  include <libintl.h>
#endif

#include "cache-maint.h"
#include "cache.h"
#include "collect-io.h"
#include "collect.h"
#include "command-line-handling.h"
#include "compat-deprecated.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#if HAVE_LUA
#  include "glua.h"
#endif
#include "histogram.h"
#include "history-list.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "logwindow.h"
#include "main-defines.h"
#include "metadata.h"
#include "options.h"
#include "pixbuf-util.h"
#include "third-party/whereami.h"
#include "thumb.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"

#if ENABLE_UNIT_TESTS
#  include "gtest/gtest.h"
#endif

gboolean thumb_format_changed = FALSE;

gchar *gq_prefix;
gchar *gq_localedir;
gchar *gq_helpdir;
gchar *gq_htmldir;
gchar *gq_appdir;
gchar *gq_bindir;
gchar *gq_executable_path;
gchar *desktop_file_template;
gchar *instance_identifier;

namespace
{

const gchar *option_context_description = _(" \
All other command line parameters are used as plain files if they exist, or a URL or a folder.\n \
The name of a collection, with or without either path or extension (.gqv) may be used.\n\n \
If more than one folder is on the command line, only the last will be used.\n\n \
If more than one file is on the command line:\n \
    If they are in the same folder, that folder will be opened and those files will be selected.\n \
    If they are not in the same folder, a new Collection containing those files will be opened.\n\n \
To run Geeqie as a new instance, use:\n \
GQ_NEW_INSTANCE=y[es] geeqie\n \
Normally a single set of configuration files is used for all instances.\n \
However, the environment variables XDG_CONFIG_HOME, XDG_CACHE_HOME, XDG_DATA_HOME\n \
can be used to modify this behavior on an individual basis e.g.\n \
XDG_CONFIG_HOME=/tmp/a XDG_CACHE_HOME=/tmp/b GQ_NEW_INSTANCE=y geeqie\n\n \
To disable Clutter use:\n \
GQ_DISABLE_CLUTTER=y[es] geeqie\n\n \
To run or stop Geeqie in cache maintenance (non-GUI) mode use:\n \
GQ_CACHE_MAINTENANCE=y[es] geeqie --help\n \
Note that bash command line completion does not work in this mode.\n\n \
User manual: https://www.geeqie.org/help/GuideIndex.html\n \
           : https://www.geeqie.org/help-pdf/help.pdf");

const gchar *option_context_description_cache_maintenance = _(" \
This is a command line program that will recursively remove orphaned thumbnails and\n \
.sim files, and create thumbnails and similarity data for all images found under FOLDER.\n\n \
It may also be called from cron or anacron thus enabling automatic updating of the cached\n \
data for all your images.\n\n \
Note that bash command line completion does not work in this mode.\n\n \
User manual: https://www.geeqie.org/help/GuideIndex.html\n \
           : https://www.geeqie.org/help-pdf/help.pdf");

GOptionEntry command_line_options[] =
{
	{ "action"                    ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("execute keyboard action (https://www.geeqie.org/help/GuideReferenceRemoteKeyboardActions.html)"), "<ACTION>" },
	{ "action-list"               ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("list available keyboard actions (some are redundant)")                        , nullptr },
	{ "back"                      , 'b', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("previous image")                                                              , nullptr },
	{ "cache-metadata"            ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("clean the metadata cache")                                                    , nullptr },
	{ "cache-render"              ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("render thumbnails")                                                           , "<folder>" },
	{ "cache-render-recurse"      ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("render thumbnails recursively")                                               , "<folder>" },
	{ "cache-render-shared"       ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("render thumbnails (see Help)")                                                , "<folder>" },
	{ "cache-render-shared-recurse",  0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("render thumbnails recursively (see Help)")                                    , "<folder>" },
	{ "cache-shared"              ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("clear or clean shared thumbnail cache")                                       , "clean|clear" },
	{ "cache-thumbs"              ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("clear or clean thumbnail cache")                                              , "clear|clean" },
	{ "close-window"              ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("close window")                                                                , nullptr },
	{ "config-load"               ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("load configuration from FILE")                                                , "<FILE>" },
#ifdef DEBUG
/* G_OPTION_ARG_STRING and not G_OPTION_ARG_INT is required because of
 * the way an integer zero is handled (the parameter is not passed on).
 */
	{ "debug"                     ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("debug output: 0 none, 1 to 4 increasing level of verbosity")                  , "0|1|2|3|4" },
#endif
	{ "delay"                     , 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("set slide show delay to Hrs Mins N.M seconds,")                               , "<[H:][M:][N][.M]>" },
	{ "dupes"                     ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("find duplicates in folder")                                                   , "<FOLDER>" },
	{ "dupes-export"              ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,   nullptr, _("export duplicates search result")                                             , nullptr },
	{ "dupes-recurse"             ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("find duplicates in folder recursively")                                       , "<FOLDER>" },
	{ "file"                      ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("open FILE or URL bring Geeqie window to the top")                             , "<FILE>|<URL>" },
	{ "File"                      ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("open FILE or URL do not bring Geeqie window to the top")                      , "<FILE>|<URL>" },
	{ "file-extensions"           ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("list known file extensions")                                                  , nullptr },
	{ "first"                     ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("first image")                                                                 , nullptr },
	{ "fullscreen"                , 'f', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("start / toggle in full screen mode")                                          , nullptr },
	{ "geometry"                  ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("set main window location and geometry")                                       , "<W>x<H>[+<XOFF>+<YOFF>]" },
	{ "get-collection"            ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("get collection content")                                                      , "<COLLECTION>" },
	{ "get-collection-list"       ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("get collection list")                                                         , nullptr },
	{ "get-destination"           ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("get destination path of FILE (https://www.geeqie.org/help/GuidePluginsConfig.html)"), "<FILE>" },
	{ "get-file-info"             ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("get file info")                                                               , nullptr},
	{ "get-filelist"              ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("get list of files and class")                                                 , "[<FOLDER>]" },
	{ "get-filelist-recurse"      ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("get list of files and class recursive")                                       , "[<FOLDER>]" },
	{ "get-rectangle"             ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("get rectangle coordinates")                                                   , nullptr },
	{ "get-render-intent"         ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("get render intent")                                                           , nullptr },
	{ "get-selection"             ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("get list of selected files")                                                  , nullptr },
	{ "get-sidecars"              ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("get list of sidecars of FILE")                                                , "<FILE>" },
	{ "get-window-list"           ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("get list of windows")                                                         , nullptr },
#ifdef DEBUG
	{ "grep"                      , 'g', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("filter debug output")                                                         , "<regexp>" },
#endif
	{ "id"                        ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("window id for following commands")                                            , "<ID>" },
	{ "last"                      ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("last image")                                                                  , nullptr },
	{ "log-file"                  , 'o', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("save log data to file")                                                       , "<file>" },
	{ "lua"                       ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("run lua script on FILE")                                                      , "<FILE>,<lua script>" },
	{ "new-window"                ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("new window")                                                                  , nullptr },
	{ "next"                      , 'n', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("next image")                                                                  , nullptr },
	{ "pixel-info"                ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("print pixel info of mouse pointer on current image")                          , nullptr },
	{ "print0"                    ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("terminate returned data with null character instead of newline")              , nullptr },
	{ "quit"                      , 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("quit")                                                                        , nullptr },
	{ "raise"                     ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("bring the Geeqie window to the top")                                          , nullptr },
	{ "selection-add"             ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("adds the current file (or the specified file) to the current selection")      ,"[<FILE>]" },
	{ "selection-clear"           ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("clears the current selection")                                                , nullptr },
	{ "selection-remove"          ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("removes the current file (or the specified file) from the current selection") ,"[<FILE>]" },
	{ "show-log-window"           , 'w', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("show log window")                                                             , nullptr },
	{ "slideshow-recurse"         ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("start recursive slide show in FOLDER")                                        ,"<FOLDER>" },
	{ "slideshow"                 , 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("toggle slide show")                                                           , nullptr },
	{ "tell"                      ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("print filename [and Collection] of current image")                            , nullptr },
	{ "tools"                     , 't', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("toggle tools")                                                                , nullptr },
	{ "version"                   , 'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("print version info")                                                          , nullptr },
	{ "view"                      ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("open FILE in new window")                                                     , "<FILE>"  },
	{ nullptr                     ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, nullptr                                                                          , nullptr },
};

GOptionEntry command_line_options_cache_maintenance[] =
{
	{ "cache-maintenance", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, _("execute cache maintenance recursively on FOLDER"), "<FOLDER>" },
	{ "quit"             , 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, _("stop cache maintenance")                         , nullptr },
	{ nullptr            ,   0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE  , nullptr, nullptr                                             , nullptr },
};

const gchar *get_signal_name(int signo)
{
	static const struct signals
	{
		gint sig_no;
		const gchar *sig_name;
	} signals_list[6] = {
		{ SIGABRT, "Abort" },
		{ SIGBUS, "Bus error" },
		{ SIGFPE, "Floating-point exception" },
		{ SIGILL, "Illegal instruction" },
		{ SIGIOT, "IOT trap" }, // @todo Same value as SIGABRT. Remove SIGIOT?
		{ SIGSEGV, "Invalid memory reference" },
	};
	const auto it = std::find_if(std::cbegin(signals_list), std::cend(signals_list),
	                             [signo](const signals &s){ return s.sig_no == signo; });
	return (it != std::cend(signals_list)) ? it->sig_name : "Unknown signal";
}

void sig_handler_cb(int signo, siginfo_t *info, void *)
{
	[[maybe_unused]] ssize_t len = write(STDERR_FILENO, "Geeqie fatal error\n", 19);

	const gchar *signal_name = get_signal_name(signo);
	len = write(STDERR_FILENO, "Signal: ", 8);
	len = write(STDERR_FILENO, signal_name, strlen(signal_name));
	len = write(STDERR_FILENO, "\n", 1);

	const gchar *code_descr = (info->si_code == SEGV_MAPERR) ? "Address not mapped" : "Invalid permissions";
	len = write(STDERR_FILENO, "Code: ", 6);
	len = write(STDERR_FILENO, code_descr, strlen(code_descr));
	len = write(STDERR_FILENO, "\n", 1);

	len = write(STDERR_FILENO, "Address: 0x", 11);
	if (info->si_addr == nullptr)
		{
		len = write(STDERR_FILENO, "0", 1);
		}
	else
		{
		/* Assume the address is 64-bit */
		const gchar hex_char[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
		                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
		auto addr = reinterpret_cast<guint64>(info->si_addr);

		for (gint i = 0; i < 16; i++)
			{
			guint64 char_index = addr & 0xf000000000000000;
			char_index = char_index >> 60;
			addr = addr << 4;

			len = write(STDERR_FILENO, &hex_char[char_index], 1);
			}
		}
	len = write(STDERR_FILENO, "\n", 1);

#if HAVE_EXECINFO_H
	void *bt[1024];
	const int bt_size = backtrace(bt, 1024);
	backtrace_symbols_fd(bt, bt_size, STDERR_FILENO);
#endif

	BACKTRACE(DEBUG);

	exit(EXIT_FAILURE);
}

gboolean search_command_line_for_option(const gint argc, const gchar* const argv[], const gchar* option_name)
{
	const gint name_len = strlen(option_name);

	for (gint i = 1; i < argc; i++)
		{
		const gchar *current_arg = argv[i];
		// TODO(xsdg): This actually only checks prefixes.  We should
		// probably replace this with strcmp, since strlen already has
		// the shortcomings of strcmp (as compared to strncmp).
		//
		// That said, people may be unknowingly relying on the lenience
		// of this parsing strategy, so that's also something to consider.
		if (strncmp(current_arg, option_name, name_len) == 0)
			{
			return TRUE;
			}
		}

	return FALSE;
}

gboolean search_command_line_for_unit_test_option(gint argc, gchar *argv[])
{
	return search_command_line_for_option(argc, argv, "--run-unit-tests");
}

/**
 * @brief Show log window for config. file errors
 * @param GSimpleAction
 * @param GVariant
 * @param gpointer
 *
 * When a config. file error is detected, a notification is displayed.
 * If the user clicks on the notification button, the log window is displayed.
 */
void config_file_error_notification_clicked_cb(GSimpleAction *, GVariant *, gpointer)
{
	LayoutWindow *lw = get_current_layout();

	log_window_new(lw);
}

/**
 * @brief Null action
 * @param GSimpleAction
 * @param GVariant
 * @param gpointer
 *
 * This is required for the AppImage notification.
 * If the user clicks on the notification and a default action is
 * not defined, the action taken is to activate the app (again).
 * The default action is linked to this callback.
 */
void null_activated_cb(GSimpleAction *, GVariant *, gpointer)
{
}

/**
 * @brief Notification Quit button pressed
 * @param action
 * @param parameter
 * @param app
 *
 *
 */
void quit_activated_cb(GSimpleAction *, GVariant *, gpointer app)
{
	g_application_quit(G_APPLICATION(app));
}

/*
 *-----------------------------------------------------------------------------
 * startup, init, and exit
 *-----------------------------------------------------------------------------
 */

#define RC_HISTORY_NAME "history"
#define RC_MARKS_NAME "marks"

void setup_env_path()
{
	const gchar *old_path = g_getenv("PATH");
	g_autofree gchar *path = g_strconcat(gq_bindir, ":", old_path, NULL);
	g_setenv("PATH", path, TRUE);
}

void keys_load()
{
	g_autofree gchar *path = g_build_filename(get_rc_dir(), RC_HISTORY_NAME, NULL);
	history_list_load(path);
}

void keys_save()
{
	g_autofree gchar *path = g_build_filename(get_rc_dir(), RC_HISTORY_NAME, NULL);
	history_list_save(path);
}

void marks_load()
{
	g_autofree gchar *path = g_build_filename(get_rc_dir(), RC_MARKS_NAME, NULL);
	marks_list_load(path);
}

void marks_save(gboolean save)
{
	g_autofree gchar *path = g_build_filename(get_rc_dir(), RC_MARKS_NAME, NULL);
	marks_list_save(path, save);
}

void mkdir_if_not_exists(const gchar *path)
{
	if (isdir(path)) return;

	log_printf(_("Creating %s dir:%s\n"), GQ_APPNAME, path);

	if (!recursive_mkdir_if_not_exists(path, 0755))
		{
		log_printf(_("Could not create dir:%s\n"), path);
		}
}

void gq_accel_map_print(
		    gpointer 	data,
		    const gchar	*accel_path,
		    guint	accel_key,
		    GdkModifierType accel_mods,
		    gboolean	changed)
{
	auto gstring = static_cast<GString *>(data);

	if (!changed)
		g_string_append(gstring, "; ");

	g_string_append(gstring, "(gtk_accel_path \"");

	g_autofree gchar *accel_path_escaped = g_strescape(accel_path, nullptr);
	g_string_append(gstring, accel_path_escaped);

	g_string_append(gstring, "\" \"");

	g_autofree gchar *name = gtk_accelerator_name(accel_key, accel_mods);
	g_autofree gchar *name_escaped = g_strescape(name, nullptr);
	g_string_append(gstring, name_escaped);

	g_string_append(gstring, "\")\n");
}

gboolean gq_accel_map_save(const gchar *path)
{
	g_autofree gchar *pathl = path_from_utf8(path);

	g_autoptr(GString) gstring = g_string_new("; ");
	if (g_get_prgname())
		g_string_append(gstring, g_get_prgname());
	g_string_append(gstring, " GtkAccelMap rc-file         -*- scheme -*-\n");
	g_string_append(gstring, "; this file is an automated accelerator map dump\n");
	g_string_append(gstring, ";\n");

	gtk_accel_map_foreach(gstring, gq_accel_map_print);

	secure_save(pathl, gstring->str, -1);

	return TRUE;
}

gchar *accep_map_filename()
{
	return g_build_filename(get_rc_dir(), "accels", NULL);
}

void accel_map_save()
{
	g_autofree gchar *path = accep_map_filename();
	gq_accel_map_save(path);
}

void accel_map_load()
{
	g_autofree gchar *path = accep_map_filename();
	g_autofree gchar *pathl = path_from_utf8(path);
	gtk_accel_map_load(pathl);
}

void gq_gtk_css_load()
{
	/* Load gtk.css file from the rc directory */
	g_autofree gchar *path = g_build_filename(get_rc_dir(), "gtk.css", nullptr);
	g_autofree gchar *pathl = path_from_utf8(path);
	if (access(pathl, R_OK) != 0) return;

	g_autoptr(GtkCssProvider) css_provider = gtk_css_provider_new();
	if (!gtk_css_provider_load_from_path(css_provider, pathl, nullptr)) return;

	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
	                                          GTK_STYLE_PROVIDER(css_provider),
	                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
}

void exit_program_final()
{
	GFile *archive_file;

	 /* make sure that external editors are loaded, we would save incomplete configuration otherwise */
	layout_editors_reload_finish();

	collect_manager_flush();

	/* Save the named windows */
	if (layout_window_count() > 1)
		{
		layout_window_foreach([](LayoutWindow *lw)
		{
			if (!g_str_has_prefix(lw->options.id, "lw"))
				{
				save_layout(lw);
				}
		});
		}

	save_options(options);
	keys_save();
	accel_map_save();

	LayoutWindow *lw = get_current_layout();
	if (lw)
		{
		layout_free(lw);
		}

	/* Delete any files/folders in /tmp that have been created by the open archive function */
	g_autofree gchar *instance_archive_dir = g_build_filename(g_get_tmp_dir(), GQ_ARCHIVE_DIR, instance_identifier, NULL);
	if (isdir(instance_archive_dir))
		{
		archive_file = g_file_new_for_path(instance_archive_dir);
		rmdir_recursive(archive_file, nullptr, nullptr);
		g_object_unref(archive_file);
		}

	/* If there are still sub-dirs created by another instance, this will fail
	 * but that does not matter */
	g_autofree gchar *archive_dir = g_build_filename(g_get_tmp_dir(), GQ_ARCHIVE_DIR, NULL);
	if (isdir(archive_dir))
		{
		archive_file = g_file_new_for_path(archive_dir);
		g_file_delete(archive_file, nullptr, nullptr);
		g_object_unref(archive_file);
		}

	exit(EXIT_SUCCESS);
}

GenericDialog *exit_dialog = nullptr;

void exit_confirm_cancel_cb(GenericDialog *gd, gpointer)
{
	exit_dialog = nullptr;
	generic_dialog_close(gd);
}

void exit_confirm_exit_cb(GenericDialog *gd, gpointer)
{
	exit_dialog = nullptr;
	generic_dialog_close(gd);

	exit_program_final();
}

gint exit_confirm_dlg()
{
	GtkWidget *parent;

	if (exit_dialog)
		{
		gtk_window_present(GTK_WINDOW(exit_dialog->dialog));
		return TRUE;
		}

	if (!collection_window_modified_exists() && (layout_window_count() == 1)) return FALSE;

	parent = nullptr;
	LayoutWindow *lw = get_current_layout();
	if (lw)
		{
		parent = lw->window;
		}

	g_autofree gchar *exit_msg = g_strdup_printf("%s - %s", GQ_APPNAME, _("exit"));
	exit_dialog = generic_dialog_new(exit_msg, "exit", parent, FALSE,
	                                 exit_confirm_cancel_cb, nullptr);

	g_autoptr(GString) message = g_string_new(nullptr);

	if (collection_window_modified_exists())
		{
		message = g_string_append(message, _("Collections have been modified.\n"));
		}

	if (layout_window_count() > 1)
		{
		g_string_append_printf(message, _("%d windows are open.\n\n"), layout_window_count());
		}

	message = g_string_append(message, _("Quit anyway?"));

	g_autofree gchar *quit_msg = g_strdup_printf(_("Quit %s"), GQ_APPNAME);
	generic_dialog_add_message(exit_dialog, GQ_ICON_DIALOG_QUESTION, quit_msg, message->str, TRUE);
	generic_dialog_add_button(exit_dialog, GQ_ICON_QUIT, _("Quit"), exit_confirm_exit_cb, TRUE);

	gtk_widget_show(exit_dialog->dialog);

	return TRUE;
}

void exit_program_write_metadata_cb(gint success, const gchar *)
{
	if (success) exit_program();
}

/* This code attempts to handle situation when a file mmaped by image_loader
 * or by exif loader is truncated by some other process.
 * This code is incorrect according to POSIX, because:
 *
 *   mmap is not async-signal-safe and thus may not be called from a signal handler
 *
 *   mmap must be called with a valid file descriptor.  POSIX requires that
 *   a fildes argument of -1 must cause mmap to return EBADF.
 *
 * See https://github.com/BestImageViewer/geeqie/issues/1052 for discussion of
 * an alternative approach.
 */
/** @FIXME this probably needs some better ifdefs. Please report any compilation problems */
/** @FIXME This section needs revising */

#if defined(SIGBUS) && defined(SA_SIGINFO)
[[maybe_unused]] void sigbus_handler_cb(int, [[maybe_unused]] siginfo_t *info, void *)
{
	/*
	 * @FIXME Design and implement a POSIX-acceptable approach,
	 * after first documenting the sitations where SIGBUS occurs.
	 * See https://github.com/BestImageViewer/geeqie/issues/1052 for discussion
	 */

	DEBUG_1("SIGBUS %p NOT HANDLED", info->si_addr);
	exit(EXIT_FAILURE);
}
#endif

void setup_sig_handler()
{
	struct sigaction sigsegv_action;
	sigfillset(&sigsegv_action.sa_mask);
	sigsegv_action.sa_sigaction = sig_handler_cb;
	sigsegv_action.sa_flags = SA_SIGINFO;

	sigaction(SIGABRT, &sigsegv_action, nullptr);
	sigaction(SIGBUS, &sigsegv_action, nullptr);
	sigaction(SIGFPE, &sigsegv_action, nullptr);
	sigaction(SIGILL, &sigsegv_action, nullptr);
	sigaction(SIGIOT, &sigsegv_action, nullptr);
	sigaction(SIGSEGV, &sigsegv_action, nullptr);
}

void set_theme_bg_color()
{
	GdkRGBA bg_color;
	GdkRGBA theme_color;
	GtkStyleContext *style_context;

	if (!options->image.use_custom_border_color)
		{
		LayoutWindow *lw = layout_window_first();

		style_context = gtk_widget_get_style_context(lw->window);
		deprecated_gtk_style_context_get_background_color(style_context, GTK_STATE_FLAG_NORMAL, &bg_color);

		theme_color.red = bg_color.red  ;
		theme_color.green = bg_color.green  ;
		theme_color.blue = bg_color.blue ;

		layout_window_foreach([&theme_color](LayoutWindow *lw)
		{
			image_background_set_color(lw->image, &theme_color);
		});
		}

	view_window_colors_update();
}

void theme_change_cb(GSettings *iface, gchar, gpointer)
{
	set_theme_bg_color();

	g_autofree gchar *scheme = g_settings_get_string(iface, "color-scheme");
	const gboolean prefer_dark_theme = (g_strcmp0(scheme, "prefer-dark") == 0);

	GtkSettings *settings = gtk_settings_get_default();
	g_object_set(settings, "gtk-application-prefer-dark-theme", prefer_dark_theme, nullptr);
}

/**
 * @brief Set up the application paths
 *
 * This function is required for use of AppImages. AppImages are
 * relocatable, and therefore cannot use fixed paths to various components.
 * These paths were originally #defines created during compilation.
 * They are now variables, all defined relative to one level above the
 * directory that the executable is run from.
 */
void create_application_paths()
{
	gint length;

	length = wai_getExecutablePath(nullptr, 0, nullptr);
	g_autofree auto *path = static_cast<gchar *>(malloc(length + 1));
	wai_getExecutablePath(path, length, nullptr);
	path[length] = '\0';

	gq_executable_path = g_strdup(path);
	g_autofree gchar *dirname = g_path_get_dirname(gq_executable_path);
	gq_prefix = g_path_get_dirname(dirname);

	gq_localedir = g_build_filename(gq_prefix, GQ_LOCALEDIR, NULL);
	gq_helpdir = g_build_filename(gq_prefix, GQ_HELPDIR, NULL);
	gq_htmldir = g_build_filename(gq_prefix, GQ_HTMLDIR, NULL);
	gq_appdir = g_build_filename(gq_prefix, GQ_APPDIR, NULL);
	gq_bindir = g_build_filename(gq_prefix, GQ_BINDIR, NULL);
	desktop_file_template = g_build_filename(gq_appdir, "org.geeqie.template.desktop", NULL);
}

gint command_line_cb(GtkApplication *app, GApplicationCommandLine *app_command_line, gpointer)
{
	gint ret;

	ret = process_command_line((app), app_command_line, nullptr);

	g_application_activate(G_APPLICATION(app));

	return ret;
}

gint shutdown_cache_maintenance_cb(GtkApplication *, gpointer)
{
	exit(EXIT_SUCCESS);
}

void startup_common(GtkApplication *, gpointer)
{
	/* seg. fault handler */
	setup_sig_handler();

	/* init execution time counter (debug only) */
	init_exec_time();

	create_application_paths();

	/* setup locale, i18n */
	setlocale(LC_ALL, "");

#ifdef ENABLE_NLS
	bindtextdomain(PACKAGE, gq_localedir);
	bind_textdomain_codeset(PACKAGE, "UTF-8");
	textdomain(PACKAGE);
#endif

	exif_init();

#if HAVE_LUA
	lua_init();
#endif

	/* setup random seed for random slideshow */
	srand(time(nullptr));

#if 0
	/* See later comment; this handler leads to UB. */
	setup_sigbus_handler();
#endif

	/* register global notify functions */
	file_data_register_notify_func(cache_notify_cb, nullptr, NOTIFY_PRIORITY_HIGH);
	file_data_register_notify_func(thumb_notify_cb, nullptr, NOTIFY_PRIORITY_HIGH);
	file_data_register_notify_func(histogram_notify_cb, nullptr, NOTIFY_PRIORITY_HIGH);
	file_data_register_notify_func(collect_manager_notify_cb, nullptr, NOTIFY_PRIORITY_LOW);
	file_data_register_notify_func(metadata_notify_cb, nullptr, NOTIFY_PRIORITY_LOW);

	gq_gtk_css_load();

	if (gtk_major_version < GTK_MAJOR_VERSION ||
	        (gtk_major_version == GTK_MAJOR_VERSION && gtk_minor_version < GTK_MINOR_VERSION) )
		{
		log_printf("!!! This is a friendly warning.\n");
		log_printf("!!! The version of GTK+ in use now is older than when %s was compiled.\n", GQ_APPNAME);
		log_printf("!!!  compiled with GTK+-%d.%d\n", GTK_MAJOR_VERSION, GTK_MINOR_VERSION);
		log_printf("!!!   running with GTK+-%u.%u\n", gtk_major_version, gtk_minor_version);
		log_printf("!!! %s may quit unexpectedly with a relocation error.\n", GQ_APPNAME);
		}

	DEBUG_1("%s main: pixbuf_inline_register_stock_icons", get_exec_time());
	gtk_icon_theme_add_resource_path(gtk_icon_theme_get_default(), GQ_RESOURCE_PATH_ICONS);
	pixbuf_inline_register_stock_icons();

	DEBUG_1("%s main: setting default options before commandline handling", get_exec_time());
	options = init_options(nullptr);
	setup_default_options(options);
	/* Generate a unique identifier used by the open archive function */
	instance_identifier = g_strdup_printf("%x", g_random_int());

	DEBUG_1("%s main: mkdir_if_not_exists", get_exec_time());
	/* these functions don't depend on config file */
	mkdir_if_not_exists(get_rc_dir());
	mkdir_if_not_exists(get_collections_dir());
	mkdir_if_not_exists(get_thumbnails_cache_dir());
	mkdir_if_not_exists(get_metadata_cache_dir());
	mkdir_if_not_exists(get_window_layouts_dir());

	setup_env_path();

	keys_load();
	accel_map_load();

	command_line = g_new0(CommandLine, 1);
}

void activate_cb(GtkApplication *, gpointer)
{
	LayoutWindow *lw = get_current_layout();

	/* If Geeqie is not running and a command line option like --version
	 * is executed, display of the Geeqie window has to be inhibited.
	 *
	 * The startup signal is issued before the command_line signal, therefore
	 * the window layout processing is done before the command line processing.
	 *
	 * Function layout_new() does not execute a gtk_window_show()
	 * if this is the first window - i.e. Geeqie is not yet fully running.
	 *
	 * The activate signal is issued in command_line_cb() after the
	 * command line signal has been processed. This function will
	 * issue the gtk_window_show() command.
	 *
	 * In the case of a text-output option that does not require the window,
	 * the shutdown happens in process_command_line().
	 */
	if (lw->window)
		{
		gtk_widget_show(lw->window);

		/** @FIXME This is not a fix - it is a workaround
		 * https://github.com/BestImageViewer/geeqie/issues/1765
		 * The reason for the bug is unknown.
		 * The origin is in commit 0ec522a0 - convert Geeqie to GtkApplication
		 */
		layout_refresh(lw);
		}
}

void startup_cb(GtkApplication *app, gpointer)
{
	startup_common(app, nullptr);

	const gchar *gq_disable_clutter = g_getenv("GQ_DISABLE_CLUTTER");

	if (gq_disable_clutter && (gq_disable_clutter[0] == 'y' || gq_disable_clutter[0] == 'Y'))
		{
		options->disable_gpu = TRUE;
		}

	/* restore session from the config file */

	if (!load_options(options))
		{
		/* load_options calls these functions after it parses global options, we have to call it here if it fails */
		filter_add_defaults();
		filter_rebuild();
		}

	/* If this is the first run with multiple OSD tabs, fill OSD_1 with the user's last setting.
	 * If the user has intentionally set OSD_1 template to null, that will cause a problem...
	 */
	if (options->image_overlay_n.template_string[0] == nullptr)
		{
		options->image_overlay_n.template_string[0] = g_strdup(options->image_overlay.template_string);
		options->image_overlay_n.font[0] = g_strdup(options->image_overlay.font);
		options->image_overlay_n.text_red[0] = options->image_overlay.text_red;
		options->image_overlay_n.text_green[0] = options->image_overlay.text_green;
		options->image_overlay_n.text_blue[0] = options->image_overlay.text_blue;
		options->image_overlay_n.text_alpha[0] = options->image_overlay.text_alpha;
		options->image_overlay_n.background_red[0] = options->image_overlay.background_red;
		options->image_overlay_n.background_green[0] = options->image_overlay.background_green;
		options->image_overlay_n.background_blue[0] = options->image_overlay.background_blue;
		options->image_overlay_n.background_alpha[0] = options->image_overlay.background_alpha;
		}

#if HAVE_CLUTTER
	/** @FIXME For the background of this see:
	 * https://github.com/BestImageViewer/geeqie/issues/397
	 * The feature CLUTTER_FEATURE_SWAP_EVENTS indictates if the
	 * system is liable to exhibit this problem.
	 * The user is provided with an override in Preferences/Behavior
	 */
	if (!options->override_disable_gpu && !options->disable_gpu)
		{
		DEBUG_1("CLUTTER_FEATURE_SWAP_EVENTS %d",clutter_feature_available(CLUTTER_FEATURE_SWAP_EVENTS));
		if (clutter_feature_available(CLUTTER_FEATURE_SWAP_EVENTS) != 0)
			{
			options->disable_gpu = TRUE;
			}
		}
#endif

	/* handle missing config file and commandline additions*/
	if (!layout_window_first())
		{
		/* broken or no config file or no <layout> section */
		layout_new_from_default();
		}

	layout_editors_reload_start();

	marks_load();

	GSettings *iface = g_settings_new("org.gnome.desktop.interface");
	g_signal_connect(iface, "changed::color-scheme", G_CALLBACK(theme_change_cb), nullptr);

	set_theme_bg_color();

	/* Show a notification if the server has a newer AppImage version */
	if (options->appimage_notifications)
		{
		if (g_getenv("APPDIR") && strstr(g_getenv("APPDIR"), "/tmp/.mount_Geeqie"))
			{
			new_appimage_notification(app);
			}
		else if (g_strstr_len(gq_executable_path, -1, "squashfs-root"))
			{
			/* Probably running an extracted AppImage */
			new_appimage_notification(app);
			}
		}

	gtk_application_window_new(app);
}

void startup_cache_maintenance_cb(GtkApplication *app, gpointer)
{
	startup_common(app, nullptr);

	g_application_hold(G_APPLICATION(app));
}

} // namespace

void exit_program()
{
	layout_image_full_screen_stop(nullptr);

	if (metadata_write_queue_confirm(FALSE, exit_program_write_metadata_cb)) return;

	marks_save(options->marks_save);

	if (exit_confirm_dlg())
		{
		return;
		}

	exit_program_final();
}

gint main(gint argc, gchar *argv[])
{
	gint status;
	GtkApplication *app;
	// We handle unit tests here because it takes the place of running the
	// rest of the app.
	if (search_command_line_for_unit_test_option(argc, argv))
		{
#if ENABLE_UNIT_TESTS
		testing::InitGoogleTest(&argc, argv);
		return RUN_ALL_TESTS();
#else
		fprintf(stderr, "Unit tests are not enabled in this build.\n");
		return 1;
#endif
		}

#if HAVE_CLUTTER
	const gchar *gq_disable_clutter = g_getenv("GQ_DISABLE_CLUTTER");

	if (!gq_disable_clutter || tolower(gq_disable_clutter[0]) != 'y')
		{
		if (gtk_clutter_init(nullptr, nullptr) != CLUTTER_INIT_SUCCESS)
			{
			fprintf(stderr,
				_("Can't initialize clutter-gtk. \n \
				To start Geeqie use: \n \
				GQ_DISABLE_CLUTTER=y geeqie\n\n"));

			return EXIT_FAILURE;
			}
		}
#endif
	const gchar *gq_cache_maintenance = g_getenv("GQ_CACHE_MAINTENANCE");
	if (gq_cache_maintenance && tolower(gq_cache_maintenance[0]) == 'y')
		{
		app = gtk_application_new("org.geeqie.cache-maintenance", static_cast<GApplicationFlags>( G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_SEND_ENVIRONMENT));
		g_application_add_main_option_entries(G_APPLICATION(app), command_line_options_cache_maintenance);

		g_application_set_option_context_parameter_string (G_APPLICATION(app), \
_("\n\nUsage for cache maintenance:\n \
GQ_CACHE_MAINTENANCE=y[es] geeqie OPTION"));

		g_autofree gchar *version_string = g_strconcat(
_("Geeqie Cache Maintenance. \n \
Version: Geeqie "), VERSION, nullptr);

		g_application_set_option_context_summary (G_APPLICATION(app), version_string);
		g_application_set_option_context_description (G_APPLICATION(app),option_context_description_cache_maintenance);

		g_signal_connect(app, "startup", G_CALLBACK(startup_cache_maintenance_cb), nullptr);
		g_signal_connect(app, "command-line", G_CALLBACK(process_command_line_cache_maintenance), nullptr);
		g_signal_connect(app, "shutdown", G_CALLBACK(shutdown_cache_maintenance_cb), nullptr);

		/* The quit action is linked to the Quit button on the notifications */
		GSimpleAction *quit_action = g_simple_action_new("quit", nullptr);
		g_signal_connect(quit_action, "activate", G_CALLBACK(quit_activated_cb), app);
		g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(quit_action));

		status = g_application_run(G_APPLICATION(app), argc, argv);

		g_object_unref(app);

		return status;
		}

	const gchar *gq_new_instance = g_getenv("GQ_NEW_INSTANCE");
	if (gq_new_instance && tolower(gq_new_instance[0]) == 'y')
		{
		app = gtk_application_new("org.geeqie.Geeqie", static_cast<GApplicationFlags>(G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_NON_UNIQUE | G_APPLICATION_SEND_ENVIRONMENT));
		}
	else
		{
		app = gtk_application_new("org.geeqie.Geeqie", static_cast<GApplicationFlags>(G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_SEND_ENVIRONMENT)) ;
		}

	g_application_add_main_option_entries(G_APPLICATION(app), command_line_options);

	g_application_set_option_context_parameter_string (G_APPLICATION(app), "[path...]");

	g_autofree gchar *version_string = g_strconcat(
_("Geeqie is an image viewer.\n \
Version: Geeqie "), VERSION, nullptr);

	g_application_set_option_context_summary (G_APPLICATION(app), version_string);
	g_application_set_option_context_description (G_APPLICATION(app), option_context_description);

	g_signal_connect(app, "activate", G_CALLBACK(activate_cb), nullptr);
	g_signal_connect(app, "command-line", G_CALLBACK(command_line_cb), nullptr);
	g_signal_connect(app, "startup", G_CALLBACK(startup_cb), nullptr);

	/* The null action is required for the AppImage notification */
	GSimpleAction *null_action = g_simple_action_new("null", nullptr);
	g_signal_connect(null_action, "activate", G_CALLBACK(null_activated_cb), app);
	g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(null_action));

	/* Used only for config. file error notifications */
	GSimpleAction *config_file_error_notification_action = g_simple_action_new("config-file-error", nullptr);
    g_signal_connect(config_file_error_notification_action, "activate", G_CALLBACK(config_file_error_notification_clicked_cb), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(config_file_error_notification_action));

	status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);

	return status;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
