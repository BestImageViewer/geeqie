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

#include <signal.h>
#include <sys/mman.h>

#ifdef G_OS_UNIX
#include <pwd.h>
#endif
#include <locale.h>

#include "main.h"

#include "cache.h"
#include "collect.h"
#include "collect-io.h"
#include "filedata.h"
#include "filefilter.h"
#include "history-list.h"
#include "image.h"
#include "img-view.h"
#include "layout-image.h"
#include "layout-util.h"
#include "misc.h"
#include "rcfile.h"
#include "remote.h"
#include "secure-save.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"
#include "cache-maint.h"
#include "thumb.h"
#include "metadata.h"
#include "exif.h"
#include "histogram.h"
#include "pixbuf-util.h"
#include "glua.h"
#include "whereami.h"

#ifdef HAVE_CLUTTER
#include <clutter-gtk/clutter-gtk.h>
#endif

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

gboolean thumb_format_changed = FALSE;
static RemoteConnection *remote_connection = nullptr;

gchar *gq_prefix;
gchar *gq_localedir;
gchar *gq_helpdir;
gchar *gq_htmldir;
gchar *gq_appdir;
gchar *gq_bindir;
gchar *gq_executable_path;
gchar *desktop_file_template;
gchar *instance_identifier;

#if defined(SA_SIGINFO)
void sig_handler_cb(int signo, siginfo_t *info, void *UNUSED(context))
{
	gchar hex_char[16];
	const gchar *signal_name = nullptr;
	gint i = 0;
	guint64 addr;
	guint64 char_index;
	ssize_t len;
#ifdef HAVE_EXECINFO_H
	gint bt_size;
	void *bt[1024];
#endif
	struct signals
		{
		gint sig_no;
		const gchar *sig_name;
		};
	struct signals signals_list[7];

	signals_list[0].sig_no = SIGABRT;
	signals_list[0].sig_name = "Abort";
	signals_list[1].sig_no = SIGBUS;
	signals_list[1].sig_name = "Bus error";
	signals_list[2].sig_no = SIGFPE;
	signals_list[2].sig_name = "Floating-point exception";
	signals_list[3].sig_no = SIGILL;
	signals_list[3].sig_name = "Illegal instruction";
	signals_list[4].sig_no = SIGIOT;
	signals_list[4].sig_name = "IOT trap";
	signals_list[5].sig_no = SIGSEGV;
	signals_list[5].sig_name = "Invalid memory reference";
	signals_list[6].sig_no = -1;
	signals_list[6].sig_name = "END";

	hex_char[0] = '0';
	hex_char[1] = '1';
	hex_char[2] = '2';
	hex_char[3] = '3';
	hex_char[4] = '4';
	hex_char[5] = '5';
	hex_char[6] = '6';
	hex_char[7] = '7';
	hex_char[8] = '8';
	hex_char[9] = '9';
	hex_char[10] = 'a';
	hex_char[11] = 'b';
	hex_char[12] = 'c';
	hex_char[13] = 'd';
	hex_char[14] = 'e';
	hex_char[15] = 'f';

	signal_name = "Unknown signal";
	while (signals_list[i].sig_no != -1)
		{
		if (signo == signals_list[i].sig_no)
			{
			signal_name = signals_list[i].sig_name;
			break;
			}
		i++;
		}

	len = write(STDERR_FILENO, "Geeqie fatal error\n", 19);
	len = write(STDERR_FILENO, "Signal: ", 8);
	len = write(STDERR_FILENO, signal_name, strlen(signal_name));
	len = write(STDERR_FILENO, "\n", 1);

	len = write(STDERR_FILENO, "Code: ", 6);
	len = write(STDERR_FILENO,  (info->si_code == SEGV_MAPERR) ? "Address not mapped" : "Invalid permissions", strlen((info->si_code == SEGV_MAPERR) ? "Address not mapped" : "Invalid permissions"));
	len = write(STDERR_FILENO, "\n", 1);

	len = write(STDERR_FILENO, "Address: ", 9);

	if (info->si_addr == nullptr)
		{
		len = write(STDERR_FILENO, "0x0\n", 4);
		}
	else
		{
		/* Assume the address is 64-bit */
		len = write(STDERR_FILENO, "0x", 2);
		addr = reinterpret_cast<guint64>(info->si_addr);

		for (i = 0; i < 16; i++)
			{
			char_index = addr & 0xf000000000000000;
			char_index = char_index >> 60;
			addr = addr << 4;

			len = write(STDERR_FILENO, &hex_char[char_index], 1);
			}
		len = write(STDERR_FILENO, "\n", 1);
		}

#ifdef HAVE_EXECINFO_H
	bt_size = backtrace(bt, 1024);
	backtrace_symbols_fd(bt, bt_size, STDERR_FILENO);
#endif

	exit(EXIT_FAILURE);
}
#else /* defined(SA_SIGINFO) */
void sig_handler_cb(int UNUSED(signo))
{
#ifdef HAVE_EXECINFO_H
	gint bt_size;
	void *bt[1024];
#endif

	write(STDERR_FILENO, "Geeqie fatal error\n", 19);
	write(STDERR_FILENO, "Signal: Segmentation fault\n", 27);

#ifdef HAVE_EXECINFO_H
	bt_size = backtrace(bt, 1024);
	backtrace_symbols_fd(bt, bt_size, STDERR_FILENO);
#endif

	exit(EXIT_FAILURE);
}
#endif /* defined(SA_SIGINFO) */

/*
 *-----------------------------------------------------------------------------
 * keyboard functions
 *-----------------------------------------------------------------------------
 */

void keyboard_scroll_calc(gint *x, gint *y, GdkEventKey *event)
{
	static gint delta = 0;
	static guint32 time_old = 0;
	static guint keyval_old = 0;

	if (event->state & GDK_CONTROL_MASK)
		{
		if (*x < 0) *x = G_MININT / 2;
		if (*x > 0) *x = G_MAXINT / 2;
		if (*y < 0) *y = G_MININT / 2;
		if (*y > 0) *y = G_MAXINT / 2;

		return;
		}

	if (options->progressive_key_scrolling)
		{
		guint32 time_diff;

		time_diff = event->time - time_old;

		/* key pressed within 125ms ? (1/8 second) */
		if (time_diff > 125 || event->keyval != keyval_old) delta = 0;

		time_old = event->time;
		keyval_old = event->keyval;

		delta += 2;
		}
	else
		{
		delta = 8;
		}

	*x = *x * delta * options->keyboard_scroll_step;
	*y = *y * delta * options->keyboard_scroll_step;
}



/*
 *-----------------------------------------------------------------------------
 * command line parser (private) hehe, who needs popt anyway?
 *-----------------------------------------------------------------------------
 */

static void parse_command_line_add_file(const gchar *file_path, gchar **path, gchar **file,
					GList **list, GList **collection_list)
{
	gchar *path_parsed;

	path_parsed = g_strdup(file_path);
	parse_out_relatives(path_parsed);

	if (file_extension_match(path_parsed, GQ_COLLECTION_EXT))
		{
		*collection_list = g_list_append(*collection_list, path_parsed);
		}
	else
		{
		if (!*path) *path = remove_level_from_path(path_parsed);
		if (!*file) *file = g_strdup(path_parsed);
		*list = g_list_prepend(*list, path_parsed);
		}
}

static void parse_command_line_add_dir(const gchar *dir, gchar **UNUSED(path), gchar **UNUSED(file),
				       GList **UNUSED(list))
{
#if 0
	/* This is broken because file filter is not initialized yet.
	*/
	GList *files;
	gchar *path_parsed;
	FileData *dir_fd;

	path_parsed = g_strdup(dir);
	parse_out_relatives(path_parsed);
	dir_fd = file_data_new_dir(path_parsed);


	if (filelist_read(dir_fd, &files, NULL))
		{
		GList *work;

		files = filelist_filter(files, FALSE);
		files = filelist_sort_path(files);

		work = files;
		while (work)
			{
			FileData *fd = static_cast<FileData *>(work->data);
			if (!*path) *path = remove_level_from_path(fd->path);
			if (!*file) *file = g_strdup(fd->path);
			*list = g_list_prepend(*list, fd);

			work = work->next;
			}

		g_list_free(files);
		}

	g_free(path_parsed);
	file_data_unref(dir_fd);
#else
	DEBUG_1("multiple directories specified, ignoring: %s", dir);
#endif
}

static void parse_command_line_process_dir(const gchar *dir, gchar **path, gchar **file,
					   GList **list, gchar **first_dir)
{

	if (!*list && !*first_dir)
		{
		*first_dir = g_strdup(dir);
		}
	else
		{
		if (*first_dir)
			{
			parse_command_line_add_dir(*first_dir, path, file, list);
			g_free(*first_dir);
			*first_dir = nullptr;
			}
		parse_command_line_add_dir(dir, path, file, list);
		}
}

static void parse_command_line_process_file(const gchar *file_path, gchar **path, gchar **file,
					    GList **list, GList **collection_list, gchar **first_dir)
{

	if (*first_dir)
		{
		parse_command_line_add_dir(*first_dir, path, file, list);
		g_free(*first_dir);
		*first_dir = nullptr;
		}
	parse_command_line_add_file(file_path, path, file, list, collection_list);
}

static void parse_command_line(gint argc, gchar *argv[])
{
	GList *list = nullptr;
	GList *remote_list = nullptr;
	GList *remote_errors = nullptr;
	gboolean remote_do = FALSE;
	gchar *first_dir = nullptr;
	gchar *app_lock;
	gchar *pwd;
	gchar *current_dir;
	gchar *geometry = nullptr;
	GtkWidget *dialog_warning;
	GString *command_line_errors = g_string_new(nullptr);

	command_line = g_new0(CommandLine, 1);

	command_line->argc = argc;
	command_line->argv = argv;
	command_line->regexp = nullptr;

	if (argc > 1)
		{
		gint i;
		gchar *base_dir = get_current_dir();
		i = 1;
		while (i < argc)
			{
			gchar *cmd_line = path_to_utf8(argv[i]);
			gchar *cmd_all = g_build_filename(base_dir, cmd_line, NULL);

			if (cmd_line[0] == G_DIR_SEPARATOR && isdir(cmd_line))
				{
				parse_command_line_process_dir(cmd_line, &command_line->path, &command_line->file, &list, &first_dir);
				}
			else if (isdir(cmd_all))
				{
				parse_command_line_process_dir(cmd_all, &command_line->path, &command_line->file, &list, &first_dir);
				}
			else if (cmd_line[0] == G_DIR_SEPARATOR && isfile(cmd_line))
				{
				parse_command_line_process_file(cmd_line, &command_line->path, &command_line->file,
								&list, &command_line->collection_list, &first_dir);
				}
			else if (isfile(cmd_all))
				{
				parse_command_line_process_file(cmd_all, &command_line->path, &command_line->file,
								&list, &command_line->collection_list, &first_dir);
				}
			else if (download_web_file(cmd_line, FALSE, nullptr))
				{
				}
			else if (is_collection(cmd_line))
				{
				gchar *path = nullptr;

				path = collection_path(cmd_line);
				parse_command_line_process_file(path, &command_line->path, &command_line->file,
								&list, &command_line->collection_list, &first_dir);
				g_free(path);
				}
			else if (strncmp(cmd_line, "--debug", 7) == 0 && (cmd_line[7] == '\0' || cmd_line[7] == '='))
				{
				/* do nothing but do not produce warnings */
				}
			else if (strncmp(cmd_line, "--disable-clutter", 17) == 0 && (cmd_line[17] == '\0'))
				{
				/* do nothing but do not produce warnings */
				}
			else if (strcmp(cmd_line, "+t") == 0 ||
				 strcmp(cmd_line, "--with-tools") == 0)
				{
				command_line->tools_show = TRUE;

				remote_list = g_list_append(remote_list, g_strdup("+t"));
				}
			else if (strcmp(cmd_line, "-t") == 0 ||
				 strcmp(cmd_line, "--without-tools") == 0)
				{
				command_line->tools_hide = TRUE;

				remote_list = g_list_append(remote_list, g_strdup("-t"));
				}
			else if (strcmp(cmd_line, "-f") == 0 ||
				 strcmp(cmd_line, "--fullscreen") == 0)
				{
				command_line->startup_full_screen = TRUE;
				}
			else if (strcmp(cmd_line, "-s") == 0 ||
				 strcmp(cmd_line, "--slideshow") == 0)
				{
				command_line->startup_in_slideshow = TRUE;
				}
			else if (strcmp(cmd_line, "-l") == 0 ||
				 strcmp(cmd_line, "--list") == 0)
				{
				command_line->startup_command_line_collection = TRUE;
				}
			else if (strncmp(cmd_line, "--geometry=", 11) == 0)
				{
				if (!command_line->geometry) command_line->geometry = g_strdup(cmd_line + 11);
				}
			else if (strcmp(cmd_line, "-r") == 0 ||
				 strcmp(cmd_line, "--remote") == 0)
				{
				if (!remote_do)
					{
					remote_do = TRUE;
					remote_list = remote_build_list(remote_list, argc - i, &argv[i], &remote_errors);
					}
				}
			else if ((strcmp(cmd_line, "+w") == 0) ||
						strcmp(cmd_line, "--show-log-window") == 0)
				{
				command_line->log_window_show = TRUE;
				}
			else if (strncmp(cmd_line, "-o:", 3) == 0)
				{
				command_line->log_file = g_strdup(cmd_line + 3);
				}
			else if (strncmp(cmd_line, "--log-file:", 11) == 0)
				{
				command_line->log_file = g_strdup(cmd_line + 11);
				}
			else if (strncmp(cmd_line, "-g:", 3) == 0)
				{
				set_regexp(g_strdup(cmd_line+3));
				}
			else if (strncmp(cmd_line, "-grep:", 6) == 0)
				{
				set_regexp(g_strdup(cmd_line+3));
				}
			else if (strncmp(cmd_line, "-n", 2) == 0)
				{
				command_line->new_instance = TRUE;
				}
			else if (strncmp(cmd_line, "--new-instance", 14) == 0)
				{
				command_line->new_instance = TRUE;
				}
			else if (strcmp(cmd_line, "-rh") == 0 ||
				 strcmp(cmd_line, "--remote-help") == 0)
				{
				remote_help();
				exit(0);
				}
			else if (strcmp(cmd_line, "--blank") == 0)
				{
				command_line->startup_blank = TRUE;
				}
			else if (strcmp(cmd_line, "-v") == 0 ||
				 strcmp(cmd_line, "--version") == 0)
				{
				printf_term(FALSE, "%s %s GTK%d\n", GQ_APPNAME, VERSION, gtk_major_version);
				exit(0);
				}
			else if (strcmp(cmd_line, "--alternate") == 0)
				{
				/* enable faster experimental algorithm */
				log_printf("Alternate similarity algorithm enabled\n");
				image_sim_alternate_set(TRUE);
				}
			else if (strcmp(cmd_line, "-h") == 0 ||
				 strcmp(cmd_line, "--help") == 0)
				{
				printf_term(FALSE, "%s %s\n", GQ_APPNAME, VERSION);
				printf_term(FALSE, _("Usage: %s [options] [path]\n\n"), GQ_APPNAME_LC);
				print_term(FALSE, _("Valid options:\n"));
				print_term(FALSE, _("      --blank                      start with blank file list\n"));
				print_term(FALSE, _("      --cache-maintenance <path>   run cache maintenance in non-GUI mode\n"));
				print_term(FALSE, _("      --disable-clutter            disable use of Clutter library (i.e. GPU accel.)\n"));
				print_term(FALSE, _("  -f, --fullscreen                 start in full screen mode\n"));
				print_term(FALSE, _("      --geometry=WxH+XOFF+YOFF     set main window location\n"));
				print_term(FALSE, _("  -h, --help                       show this message\n"));
				print_term(FALSE, _("  -l, --list [files] [collections] open collection window for command line\n"));
				print_term(FALSE, _("  -n, --new-instance               open a new instance of Geeqie\n"));
				print_term(FALSE, _("  -o:, --log-file:<file>     save log data to file\n"));
				print_term(FALSE, _("  -r, --remote                     send following commands to open window\n"));
				print_term(FALSE, _("  -rh, --remote-help               print remote command list\n"));
				print_term(FALSE, _("  -s, --slideshow                  start in slideshow mode\n"));
				print_term(FALSE, _("  +t, --with-tools                 force show of tools\n"));
				print_term(FALSE, _("  -t, --without-tools              force hide of tools\n"));
				print_term(FALSE, _("  -v, --version                    print version info\n"));
				print_term(FALSE, _("  +w, --show-log-window            show log window\n"));
#ifdef DEBUG
				print_term(FALSE, _("      --debug[=level]              turn on debug output\n"));
				print_term(FALSE, _("  -g:, --grep:<regexp>     filter debug output\n"));
#endif

#if 0
				/* these options are not officially supported!
				 * only for testing new features, no need to translate them */
				print_term(FALSE, "  --alternate                use alternate similarity algorithm\n");
#endif
				print_term(FALSE, "\n");

				remote_help();


				exit(0);
				}
			else if (!remote_do)
				{
				command_line_errors = g_string_append(command_line_errors, cmd_line);
				command_line_errors = g_string_append(command_line_errors, "\n");
				}

			g_free(cmd_all);
			g_free(cmd_line);
			i++;
			}

		if (command_line_errors->len > 0)
			{
			dialog_warning = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", "Invalid parameter(s):");
			gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog_warning), "%s", command_line_errors->str);
			gtk_window_set_title(GTK_WINDOW(dialog_warning), GQ_APPNAME);
			gtk_window_set_keep_above(GTK_WINDOW(dialog_warning), TRUE);
			gtk_dialog_run(GTK_DIALOG(dialog_warning));
			gtk_widget_destroy(dialog_warning);
			g_string_free(command_line_errors, TRUE);

			exit(EXIT_FAILURE);
			}

		g_free(base_dir);
		parse_out_relatives(command_line->path);
		parse_out_relatives(command_line->file);
		}

	list = g_list_reverse(list);

	if (!command_line->path && first_dir)
		{
		command_line->path = first_dir;
		first_dir = nullptr;

		parse_out_relatives(command_line->path);
		}
	g_free(first_dir);

	if (!command_line->new_instance)
		{
		/* If Geeqie is already running, prevent a second instance
		 * from being started. Open a new window instead.
		 */
		app_lock = g_build_filename(get_rc_dir(), ".command", NULL);
		if (remote_server_exists(app_lock) && !remote_do)
			{
			remote_do = TRUE;
			if (command_line->geometry)
				{
				geometry = g_strdup_printf("--geometry=%s", command_line->geometry);
				remote_list = g_list_prepend(remote_list, geometry);
				}
			remote_list = g_list_prepend(remote_list, g_strdup("--new-window"));
			}
		g_free(app_lock);
		}

	if (remote_do)
		{
		if (remote_errors)
			{
			GList *work = remote_errors;

			while (work)
				{
				auto opt = static_cast<gchar *>(work->data);

				command_line_errors = g_string_append(command_line_errors, opt);
				command_line_errors = g_string_append(command_line_errors, "\n");
				work = work->next;
				}

			dialog_warning = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", "Invalid parameter(s):");
			gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog_warning), "%s", command_line_errors->str);
			gtk_window_set_title(GTK_WINDOW(dialog_warning), GQ_APPNAME);
			gtk_window_set_keep_above(GTK_WINDOW(dialog_warning), TRUE);
			gtk_dialog_run(GTK_DIALOG(dialog_warning));
			gtk_widget_destroy(dialog_warning);
			g_string_free(command_line_errors, TRUE);

			exit(EXIT_FAILURE);
			}

		/* prepend the current dir the remote command was made from,
		 * for use by any remote command that needs it
		 */
		current_dir = g_get_current_dir();
		pwd = g_strconcat("--PWD:", current_dir, NULL);
		remote_list = g_list_prepend(remote_list, pwd);

		remote_control(argv[0], remote_list, command_line->path, list, command_line->collection_list);
		/* There is no return to this point
		 */
		g_free(pwd);
		g_free(current_dir);
		}
	g_free(geometry);
	g_list_free(remote_list);

	if (list && list->next)
		{
		command_line->cmd_list = list;
		}
	else
		{
		string_list_free(list);
		command_line->cmd_list = nullptr;
		}

	if (command_line->startup_blank)
		{
		g_free(command_line->path);
		command_line->path = nullptr;
		g_free(command_line->file);
		command_line->file = nullptr;
		filelist_free(command_line->cmd_list);
		command_line->cmd_list = nullptr;
		string_list_free(command_line->collection_list);
		command_line->collection_list = nullptr;
		}
}

static void parse_command_line_for_debug_option(gint argc, gchar *argv[])
{
#ifdef DEBUG
	const gchar *debug_option = "--debug";
	gint len = strlen(debug_option);

	if (argc > 1)
		{
		gint i;

		for (i = 1; i < argc; i++)
			{
			const gchar *cmd_line = argv[i];
			if (strncmp(cmd_line, debug_option, len) == 0)
				{
				gint cmd_line_len = strlen(cmd_line);

				/* we now increment the debug state for verbosity */
				if (cmd_line_len == len)
					debug_level_add(1);
				else if (cmd_line[len] == '=' && g_ascii_isdigit(cmd_line[len+1]))
					{
					gint n = atoi(cmd_line + len + 1);
					if (n < 0) n = 1;
					debug_level_add(n);
					}
				}
			}
		}

	DEBUG_1("debugging output enabled (level %d)", get_debug_level());
#endif
}

#ifdef HAVE_CLUTTER
static gboolean parse_command_line_for_clutter_option(gint argc, gchar *argv[])
{
	const gchar *clutter_option = "--disable-clutter";
	gint len = strlen(clutter_option);
	gboolean ret = FALSE;

	if (argc > 1)
		{
		gint i;

		for (i = 1; i < argc; i++)
			{
			const gchar *cmd_line = argv[i];
			if (strncmp(cmd_line, clutter_option, len) == 0)
				{
				ret = TRUE;
				}
			}
		}

	return ret;
}
#endif

static gboolean parse_command_line_for_cache_maintenance_option(gint argc, gchar *argv[])
{
	const gchar *cache_maintenance_option = "--cache-maintenance";
	gint len = strlen(cache_maintenance_option);
	gboolean ret = FALSE;

	if (argc >= 2)
		{
		const gchar *cmd_line = argv[1];
		if (strncmp(cmd_line, cache_maintenance_option, len) == 0)
			{
			ret = TRUE;
			}
		}

	return ret;
}

static void process_command_line_for_cache_maintenance_option(gint argc, gchar *argv[])
{
	gchar *rc_path;
	gchar *folder_path = nullptr;
	gsize size;
	gsize i = 0;
	gchar *buf_config_file;
	gint diff_count;

	if (argc >= 3)
		{
		folder_path = expand_tilde(argv[2]);

		if (isdir(folder_path))
			{
			rc_path = g_build_filename(get_rc_dir(), RC_FILE_NAME, NULL);

			if (isfile(rc_path))
				{
				if (g_file_get_contents(rc_path, &buf_config_file, &size, nullptr))
					{
					while (i < size)
						{
						diff_count = strncmp("</global>", &buf_config_file[i], 9);
						if (diff_count == 0)
							{
							break;
							}
						i++;
						}
					/* Load only the <global> section */
					load_config_from_buf(buf_config_file, i + 9, FALSE);

					if (options->thumbnails.enable_caching)
						{
						cache_maintenance(folder_path);
						}
					else
						{
						print_term(TRUE, "Caching not enabled\n");
						exit(EXIT_FAILURE);
						}
					g_free(buf_config_file);
					}
				else
					{
					print_term(TRUE, g_strconcat(_("Cannot load "), rc_path, "\n", NULL));
					exit(EXIT_FAILURE);
					}
				}
			else
				{
				print_term(TRUE, g_strconcat(_("Configuration file path "), rc_path, _(" is not a file\n"), NULL));
				exit(EXIT_FAILURE);
				}
			g_free(rc_path);
			}
		else
			{
			print_term(TRUE, g_strconcat(argv[2], _(" is not a folder\n"), NULL));
			exit(EXIT_FAILURE);
			}
		g_free(folder_path);
		}
	else
		{
		print_term(TRUE, _("No path parameter given\n"));
		exit(EXIT_FAILURE);
		}
}

/*
 *-----------------------------------------------------------------------------
 * startup, init, and exit
 *-----------------------------------------------------------------------------
 */

#define RC_HISTORY_NAME "history"
#define RC_MARKS_NAME "marks"

static void setup_env_path()
{
	const gchar *old_path = g_getenv("PATH");
	gchar *path = g_strconcat(gq_bindir, ":", old_path, NULL);
        g_setenv("PATH", path, TRUE);
	g_free(path);
}

static void keys_load()
{
	gchar *path;

	path = g_build_filename(get_rc_dir(), RC_HISTORY_NAME, NULL);
	history_list_load(path);
	g_free(path);
}

static void keys_save()
{
	gchar *path;

	path = g_build_filename(get_rc_dir(), RC_HISTORY_NAME, NULL);
	history_list_save(path);
	g_free(path);
}

static void marks_load()
{
	gchar *path;

	path = g_build_filename(get_rc_dir(), RC_MARKS_NAME, NULL);
	marks_list_load(path);
	g_free(path);
}

static void marks_save(gboolean save)
{
	gchar *path;

	path = g_build_filename(get_rc_dir(), RC_MARKS_NAME, NULL);
	marks_list_save(path, save);
	g_free(path);
}

static void mkdir_if_not_exists(const gchar *path)
{
	if (isdir(path)) return;

	log_printf(_("Creating %s dir:%s\n"), GQ_APPNAME, path);

	if (!recursive_mkdir_if_not_exists(path, 0755))
		{
		log_printf(_("Could not create dir:%s\n"), path);
		}
}


/* We add to duplicate and modify  gtk_accel_map_print() and gtk_accel_map_save()
 * to improve the reliability in special cases (especially when disk is full)
 * These functions are now using secure saving stuff.
 */
static void gq_accel_map_print(
		    gpointer 	data,
		    const gchar	*accel_path,
		    guint	accel_key,
		    GdkModifierType accel_mods,
		    gboolean	changed)
{
	GString *gstring = g_string_new(changed ? nullptr : "; ");
	auto ssi = static_cast<SecureSaveInfo *>(data);
	gchar *tmp, *name;

	g_string_append(gstring, "(gtk_accel_path \"");

	tmp = g_strescape(accel_path, nullptr);
	g_string_append(gstring, tmp);
	g_free(tmp);

	g_string_append(gstring, "\" \"");

	name = gtk_accelerator_name(accel_key, accel_mods);
	tmp = g_strescape(name, nullptr);
	g_free(name);
	g_string_append(gstring, tmp);
	g_free(tmp);

	g_string_append(gstring, "\")\n");

	secure_fwrite(gstring->str, sizeof(*gstring->str), gstring->len, ssi);

	g_string_free(gstring, TRUE);
}

static gboolean gq_accel_map_save(const gchar *path)
{
	gchar *pathl;
	SecureSaveInfo *ssi;
	GString *gstring;

	pathl = path_from_utf8(path);
	ssi = secure_open(pathl);
	g_free(pathl);
	if (!ssi)
		{
		log_printf(_("error saving file: %s\n"), path);
		return FALSE;
		}

	gstring = g_string_new("; ");
	if (g_get_prgname())
		g_string_append(gstring, g_get_prgname());
	g_string_append(gstring, " GtkAccelMap rc-file         -*- scheme -*-\n");
	g_string_append(gstring, "; this file is an automated accelerator map dump\n");
	g_string_append(gstring, ";\n");

	secure_fwrite(gstring->str, sizeof(*gstring->str), gstring->len, ssi);

	g_string_free(gstring, TRUE);

	gtk_accel_map_foreach(ssi, gq_accel_map_print);

	if (secure_close(ssi))
		{
		log_printf(_("error saving file: %s\nerror: %s\n"), path,
			   secsave_strerror(secsave_errno));
		return FALSE;
		}

	return TRUE;
}

static gchar *accep_map_filename()
{
	return g_build_filename(get_rc_dir(), "accels", NULL);
}

static void accel_map_save()
{
	gchar *path;

	path = accep_map_filename();
	gq_accel_map_save(path);
	g_free(path);
}

static void accel_map_load()
{
	gchar *path;
	gchar *pathl;

	path = accep_map_filename();
	pathl = path_from_utf8(path);
	gtk_accel_map_load(pathl);
	g_free(pathl);
	g_free(path);
}

static void gtkrc_load()
{
	gchar *path;
	gchar *pathl;

	/* If a gtkrc file exists in the rc directory, add it to the
	 * list of files to be parsed at the end of gtk_init() */
	path = g_build_filename(get_rc_dir(), "gtkrc", NULL);
	pathl = path_from_utf8(path);
	if (access(pathl, R_OK) == 0)
		gtk_rc_add_default_file(pathl);
	g_free(pathl);
	g_free(path);
}

static void exit_program_final()
{
	LayoutWindow *lw = nullptr;
	GList *list;
	LayoutWindow *tmp_lw;
	gchar *archive_dir;
	GFile *archive_file;

	 /* make sure that external editors are loaded, we would save incomplete configuration otherwise */
	layout_editors_reload_finish();

	remote_close(remote_connection);

	collect_manager_flush();

	/* Save the named windows */
	if (layout_window_list && layout_window_list->next)
		{
		list = layout_window_list;
		while (list)
			{
			tmp_lw = static_cast<LayoutWindow *>(list->data);
			if (!g_str_has_prefix(tmp_lw->options.id, "lw"))
				{
				save_layout(static_cast<_LayoutWindow *>(list->data));
				}
			list = list->next;
			}
		}

	save_options(options);
	keys_save();
	accel_map_save();

	if (layout_valid(&lw))
		{
		layout_free(lw);
		}

	/* Delete any files/folders in /tmp that have been created by the open archive function */
	archive_dir = g_build_filename(g_get_tmp_dir(), GQ_ARCHIVE_DIR, instance_identifier, NULL);
	if (isdir(archive_dir))
		{
		archive_file = g_file_new_for_path(archive_dir);
		rmdir_recursive(archive_file, nullptr, nullptr);
		g_free(archive_dir);
		g_object_unref(archive_file);
		}

	/* If there are still sub-dirs created by another instance, this will fail
	 * but that does not matter */
	archive_dir = g_build_filename(g_get_tmp_dir(), GQ_ARCHIVE_DIR, NULL);
	if (isdir(archive_dir))
		{
		archive_file = g_file_new_for_path(archive_dir);
		g_file_delete(archive_file, nullptr, nullptr);
		g_free(archive_dir);
		g_object_unref(archive_file);
		}

	secure_close(command_line->ssi);

	gtk_main_quit();
}

static GenericDialog *exit_dialog = nullptr;

static void exit_confirm_cancel_cb(GenericDialog *gd, gpointer UNUSED(data))
{
	exit_dialog = nullptr;
	generic_dialog_close(gd);
}

static void exit_confirm_exit_cb(GenericDialog *gd, gpointer UNUSED(data))
{
	exit_dialog = nullptr;
	generic_dialog_close(gd);
	exit_program_final();
}

static gint exit_confirm_dlg()
{
	GtkWidget *parent;
	LayoutWindow *lw;
	gchar *msg;

	if (exit_dialog)
		{
		gtk_window_present(GTK_WINDOW(exit_dialog->dialog));
		return TRUE;
		}

	if (!collection_window_modified_exists()) return FALSE;

	parent = nullptr;
	lw = nullptr;
	if (layout_valid(&lw))
		{
		parent = lw->window;
		}

	msg = g_strdup_printf("%s - %s", GQ_APPNAME, _("exit"));
	exit_dialog = generic_dialog_new(msg,
				"exit", parent, FALSE,
				exit_confirm_cancel_cb, nullptr);
	g_free(msg);
	msg = g_strdup_printf(_("Quit %s"), GQ_APPNAME);
	generic_dialog_add_message(exit_dialog, GTK_STOCK_DIALOG_QUESTION,
				   msg, _("Collections have been modified. Quit anyway?"), TRUE);
	g_free(msg);
	generic_dialog_add_button(exit_dialog, GTK_STOCK_QUIT, nullptr, exit_confirm_exit_cb, TRUE);

	gtk_widget_show(exit_dialog->dialog);

	return TRUE;
}

static void exit_program_write_metadata_cb(gint success, const gchar *UNUSED(dest_path), gpointer UNUSED(data))
{
	if (success) exit_program();
}

void exit_program()
{
	layout_image_full_screen_stop(nullptr);

	if (metadata_write_queue_confirm(FALSE, exit_program_write_metadata_cb, nullptr)) return;

	options->marks_save ? marks_save(TRUE) : marks_save(FALSE);

	if (exit_confirm_dlg()) return;

	exit_program_final();
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

#if defined(SIGBUS) && defined(SA_SIGINFO)
static void sigbus_handler_cb(int UNUSED(signum), siginfo_t *info, void *UNUSED(context))
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

//static void setup_sigbus_handler(void)
//{
//#if defined(SIGBUS) && defined(SA_SIGINFO)
	//struct sigaction sigbus_action;
	//sigfillset(&sigbus_action.sa_mask);
	//sigbus_action.sa_sigaction = sigbus_handler_cb;
	//sigbus_action.sa_flags = SA_SIGINFO;

	//sigaction(SIGBUS, &sigbus_action, NULL);
//#endif
//}

static void setup_sig_handler()
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

static void set_theme_bg_color()
{
	GdkRGBA bg_color;
	GdkColor theme_color;
	GtkStyleContext *style_context;
	GList *work;
	LayoutWindow *lw;

	if (!options->image.use_custom_border_color)
		{
		work = layout_window_list;
		lw = static_cast<LayoutWindow *>(work->data);

		style_context = gtk_widget_get_style_context(lw->window);
		gtk_style_context_get_background_color(style_context, GTK_STATE_FLAG_NORMAL, &bg_color);

		theme_color.red = bg_color.red * 65535;
		theme_color.green = bg_color.green * 65535;
		theme_color.blue = bg_color.blue * 65535;

		while (work)
			{
			lw = static_cast<LayoutWindow *>(work->data);
			image_background_set_color(lw->image, &theme_color);
			work = work->next;
			}
		}

	view_window_colors_update();
}

static gboolean theme_change_cb(GObject *UNUSED(gobject), GParamSpec *UNUSED(pspec), gpointer UNUSED(data))
{
	set_theme_bg_color();

	return FALSE;
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
static void create_application_paths()
{
	gchar *dirname;
	gint length;
	gchar *path;

	length = wai_getExecutablePath(nullptr, 0, nullptr);
	path = static_cast<gchar *>(malloc(length + 1));
	wai_getExecutablePath(path, length, nullptr);
	path[length] = '\0';

	gq_executable_path = g_strdup(path);
	dirname = g_path_get_dirname(gq_executable_path);
	gq_prefix = g_path_get_dirname(dirname);

	gq_localedir = g_build_filename(gq_prefix, GQ_LOCALEDIR, NULL);
	gq_helpdir = g_build_filename(gq_prefix, GQ_HELPDIR, NULL);
	gq_htmldir = g_build_filename(gq_prefix, GQ_HTMLDIR, NULL);
	gq_appdir = g_build_filename(gq_prefix, GQ_APPDIR, NULL);
	gq_bindir = g_build_filename(gq_prefix, GQ_BINDIR, NULL);
	desktop_file_template = g_build_filename(gq_appdir, "template.desktop", NULL);

	g_free(dirname);
	g_free(path);
}

gint main(gint argc, gchar *argv[])
{
	CollectionData *first_collection = nullptr;
	gchar *buf;
	CollectionData *cd = nullptr;
	gboolean disable_clutter = FALSE;
	gboolean single_dir = TRUE;
	LayoutWindow *lw;
	GtkSettings *default_settings;

	gdk_set_allowed_backends("x11,*");

	gdk_threads_init();
	gdk_threads_enter();

	/* seg. fault handler */
#ifdef HAVE_DEVELOPER
	backward::SignalHandling sh{};
#else
	setup_sig_handler();
#endif

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

#ifdef HAVE_LUA
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


	gtkrc_load();

	parse_command_line_for_debug_option(argc, argv);
	DEBUG_1("%s main: gtk_init", get_exec_time());
#ifdef HAVE_CLUTTER
	if (parse_command_line_for_clutter_option(argc, argv))
		{
		disable_clutter	= TRUE;
		gtk_init(&argc, &argv);
		}
	else
		{
		if (gtk_clutter_init(&argc, &argv) != CLUTTER_INIT_SUCCESS)
			{
			log_printf("Can't initialize clutter-gtk.\nStart Geeqie with the option \"geeqie --disable-clutter\"");
			runcmd("zenity --error --title=\"Geeqie\" --text \"Can't initialize clutter-gtk.\n\nStart Geeqie with the option:\n geeqie --disable-clutter\" --width=300");
			exit(1);
			}
		}
#else
	gtk_init(&argc, &argv);
#endif

	if (gtk_major_version < GTK_MAJOR_VERSION ||
	    (gtk_major_version == GTK_MAJOR_VERSION && gtk_minor_version < GTK_MINOR_VERSION) )
		{
		log_printf("!!! This is a friendly warning.\n");
		log_printf("!!! The version of GTK+ in use now is older than when %s was compiled.\n", GQ_APPNAME);
		log_printf("!!!  compiled with GTK+-%d.%d\n", GTK_MAJOR_VERSION, GTK_MINOR_VERSION);
		log_printf("!!!   running with GTK+-%d.%d\n", gtk_major_version, gtk_minor_version);
		log_printf("!!! %s may quit unexpectedly with a relocation error.\n", GQ_APPNAME);
		}

	DEBUG_1("%s main: pixbuf_inline_register_stock_icons", get_exec_time());
	gtk_icon_theme_add_resource_path(gtk_icon_theme_get_default(), GQ_RESOURCE_PATH_ICONS);
	pixbuf_inline_register_stock_icons();

	DEBUG_1("%s main: setting default options before commandline handling", get_exec_time());
	options = init_options(nullptr);
	setup_default_options(options);
	if (disable_clutter)
		{
		options->disable_gpu = TRUE;
		}

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

	if (parse_command_line_for_cache_maintenance_option(argc, argv))
		{
		process_command_line_for_cache_maintenance_option(argc, argv);
		}
	else
		{
		DEBUG_1("%s main: parse_command_line", get_exec_time());
		parse_command_line(argc, argv);

		keys_load();
		accel_map_load();

		/* restore session from the config file */


		DEBUG_1("%s main: load_options", get_exec_time());
		if (!load_options(options))
			{
			/* load_options calls these functions after it parses global options, we have to call it here if it fails */
			filter_add_defaults();
			filter_rebuild();
			}

	#ifdef HAVE_CLUTTER
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
		if (!layout_window_list)
			{
			/* broken or no config file or no <layout> section */
			layout_new_from_default();
			}

		layout_editors_reload_start();

		/* If no --list option, open a separate collection window for each
		 * .gqv file on the command line
		 */
		if (command_line->collection_list && !command_line->startup_command_line_collection)
			{
			GList *work;

			work = command_line->collection_list;
			while (work)
				{
				CollectWindow *cw;
				const gchar *path;

				path = static_cast<const gchar *>(work->data);
				work = work->next;

				cw = collection_window_new(path);
				if (!first_collection && cw) first_collection = cw->cd;
				}
			}

		if (command_line->log_file)
			{
			gchar *pathl;
			gchar *path = g_strdup(command_line->log_file);

			pathl = path_from_utf8(path);
			command_line->ssi = secure_open(pathl);
			}

		/* If there is a files list on the command line and no --list option,
		 * check if they are all in the same folder
		 */
		if (command_line->cmd_list && !(command_line->startup_command_line_collection))
			{
			GList *work;
			gchar *path = nullptr;

			work = command_line->cmd_list;

			while (work && single_dir)
				{
				gchar *dirname;

				dirname = g_path_get_dirname(static_cast<const gchar *>(work->data));
				if (!path)
					{
					path = g_strdup(dirname);
					}
				else
					{
					if (g_strcmp0(path, dirname) != 0)
						{
						single_dir = FALSE;
						}
					}
				g_free(dirname);
				work = work->next;
				}
			g_free(path);
			}

		/* Files from multiple folders, or --list option given
		 * then open an unnamed collection and insert all files
		 */
		if ((command_line->cmd_list && !single_dir) || (command_line->startup_command_line_collection && command_line->cmd_list))
			{
			GList *work;
			CollectWindow *cw;

			cw = collection_window_new(nullptr);
			cd = cw->cd;

			collection_path_changed(cd);

			work = command_line->cmd_list;
			while (work)
				{
				FileData *fd;

				fd = file_data_new_simple(static_cast<const gchar *>(work->data));
				collection_add(cd, fd, FALSE);
				file_data_unref(fd);
				work = work->next;
				}

			work = command_line->collection_list;
			while (work)
				{
				collection_load(cd, static_cast<gchar *>(work->data), COLLECTION_LOAD_APPEND);
				work = work->next;
				}

			if (cd->list) layout_image_set_collection(nullptr, cd, static_cast<CollectInfo *>(cd->list->data));

			/* mem leak, we never unref this collection when !startup_command_line_collection
			 * (the image view of the main window does not hold a ref to the collection)
			 * this is sort of unavoidable, for if it did hold a ref, next/back
			 * may not work as expected when closing collection windows.
			 *
			 * collection_unref(cd);
			 */

			}
		else if (first_collection)
			{
			layout_image_set_collection(nullptr, first_collection,
						    collection_get_first(first_collection));
			}

		/* If the files on the command line are from one folder, select those files
		 * unless it is a command line collection - then leave focus on collection window
		 */
		lw = nullptr;
		layout_valid(&lw);

		if (single_dir && command_line->cmd_list && !command_line->startup_command_line_collection)
			{
			GList *work;
			GList *selected;
			FileData *fd;

			selected = nullptr;
			work = command_line->cmd_list;
			while (work)
				{
				fd = file_data_new_simple(static_cast<gchar *>(work->data));
				selected = g_list_append(selected, fd);
				file_data_unref(fd);
				work = work->next;
				}
			layout_select_list(lw, selected);
			}

		buf = g_build_filename(get_rc_dir(), ".command", NULL);
		remote_connection = remote_server_init(buf, cd);
		g_free(buf);

		marks_load();

		default_settings = gtk_settings_get_default();
		g_signal_connect(default_settings, "notify::gtk-theme-name", G_CALLBACK(theme_change_cb), NULL);
		set_theme_bg_color();
		}

	/* Show a fade-out notification window if the server has a newer AppImage version */
	if (options->appimage_notifications && g_getenv("APPDIR") && strstr(g_getenv("APPDIR"), "/tmp/.mount_Geeqie"))
		{
		appimage_notification();
		}

	DEBUG_1("%s main: gtk_main", get_exec_time());
	gtk_main();

	gdk_threads_leave();
	return 0;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
