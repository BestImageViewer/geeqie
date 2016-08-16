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
#include "remote.h"

#include "collect.h"
#include "filedata.h"
#include "img-view.h"
#include "layout.h"
#include "layout_image.h"
#include "misc.h"
#include "slideshow.h"
#include "ui_fileops.h"
#include "rcfile.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>


#define SERVER_MAX_CLIENTS 8

#define REMOTE_SERVER_BACKLOG 4


#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif


static RemoteConnection *remote_client_open(const gchar *path);
static gint remote_client_send(RemoteConnection *rc, const gchar *text);


typedef struct _RemoteClient RemoteClient;
struct _RemoteClient {
	gint fd;
	guint channel_id; /* event source id */
	RemoteConnection *rc;
};

typedef struct _RemoteData RemoteData;
struct _RemoteData {
	CollectionData *command_collection;
};


static gboolean remote_server_client_cb(GIOChannel *source, GIOCondition condition, gpointer data)
{
	RemoteClient *client = data;
	RemoteConnection *rc;
	GIOStatus status = G_IO_STATUS_NORMAL;

	rc = client->rc;

	if (condition & G_IO_IN)
		{
		gchar *buffer = NULL;
		GError *error = NULL;
		gsize termpos;

		while ((status = g_io_channel_read_line(source, &buffer, NULL, &termpos, &error)) == G_IO_STATUS_NORMAL)
			{
			if (buffer)
				{
				buffer[termpos] = '\0';

				if (strlen(buffer) > 0)
					{
					if (rc->read_func) rc->read_func(rc, buffer, source, rc->read_data);
					g_io_channel_write_chars(source, "\n", -1, NULL, NULL); /* empty line finishes the command */
					g_io_channel_flush(source, NULL);
					}
				g_free(buffer);

				buffer = NULL;
				}
			}

		if (error)
			{
			log_printf("error reading socket: %s\n", error->message);
			g_error_free(error);
			}
		}

	if (condition & G_IO_HUP || status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR)
		{
		rc->clients = g_list_remove(rc->clients, client);

		DEBUG_1("HUP detected, closing client.");
		DEBUG_1("client count %d", g_list_length(rc->clients));

		g_source_remove(client->channel_id);
		close(client->fd);
		g_free(client);
		}

	return TRUE;
}

static void remote_server_client_add(RemoteConnection *rc, gint fd)
{
	RemoteClient *client;
	GIOChannel *channel;

	if (g_list_length(rc->clients) > SERVER_MAX_CLIENTS)
		{
		log_printf("maximum remote clients of %d exceeded, closing connection\n", SERVER_MAX_CLIENTS);
		close(fd);
		return;
		}

	client = g_new0(RemoteClient, 1);
	client->rc = rc;
	client->fd = fd;

	channel = g_io_channel_unix_new(fd);
	client->channel_id = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, G_IO_IN | G_IO_HUP,
						 remote_server_client_cb, client, NULL);
	g_io_channel_unref(channel);

	rc->clients = g_list_append(rc->clients, client);
	DEBUG_1("client count %d", g_list_length(rc->clients));
}

static void remote_server_clients_close(RemoteConnection *rc)
{
	while (rc->clients)
		{
		RemoteClient *client = rc->clients->data;

		rc->clients = g_list_remove(rc->clients, client);

		g_source_remove(client->channel_id);
		close(client->fd);
		g_free(client);
		}
}

static gboolean remote_server_read_cb(GIOChannel *source, GIOCondition condition, gpointer data)
{
	RemoteConnection *rc = data;
	gint fd;
	guint alen;

	fd = accept(rc->fd, NULL, &alen);
	if (fd == -1)
		{
		log_printf("error accepting socket: %s\n", strerror(errno));
		return TRUE;
		}

	remote_server_client_add(rc, fd);

	return TRUE;
}

static gboolean remote_server_exists(const gchar *path)
{
	RemoteConnection *rc;

	/* verify server up */
	rc = remote_client_open(path);
	remote_close(rc);

	if (rc) return TRUE;

	/* unable to connect, remove socket file to free up address */
	unlink(path);
	return FALSE;
}

static RemoteConnection *remote_server_open(const gchar *path)
{
	RemoteConnection *rc;
	struct sockaddr_un addr;
	gint sun_path_len;
	gint fd;
	GIOChannel *channel;

	if (remote_server_exists(path))
		{
		log_printf("Address already in use: %s\n", path);
		return NULL;
		}

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) return NULL;

	addr.sun_family = AF_UNIX;
	sun_path_len = MIN(strlen(path) + 1, UNIX_PATH_MAX);
	strncpy(addr.sun_path, path, sun_path_len);
	if (bind(fd, &addr, sizeof(addr)) == -1 ||
	    listen(fd, REMOTE_SERVER_BACKLOG) == -1)
		{
		log_printf("error subscribing to socket: %s\n", strerror(errno));
		close(fd);
		return NULL;
		}

	rc = g_new0(RemoteConnection, 1);

	rc->server = TRUE;
	rc->fd = fd;
	rc->path = g_strdup(path);

	channel = g_io_channel_unix_new(rc->fd);
	g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);

	rc->channel_id = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, G_IO_IN,
					     remote_server_read_cb, rc, NULL);
	g_io_channel_unref(channel);

	return rc;
}

static void remote_server_subscribe(RemoteConnection *rc, RemoteReadFunc *func, gpointer data)
{
	if (!rc || !rc->server) return;

	rc->read_func = func;
	rc->read_data = data;
}


static RemoteConnection *remote_client_open(const gchar *path)
{
	RemoteConnection *rc;
	struct stat st;
	struct sockaddr_un addr;
	gint sun_path_len;
	gint fd;

	if (stat(path, &st) != 0 || !S_ISSOCK(st.st_mode)) return NULL;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) return NULL;

	addr.sun_family = AF_UNIX;
	sun_path_len = MIN(strlen(path) + 1, UNIX_PATH_MAX);
	strncpy(addr.sun_path, path, sun_path_len);
	if (connect(fd, &addr, sizeof(addr)) == -1)
		{
		DEBUG_1("error connecting to socket: %s", strerror(errno));
		close(fd);
		return NULL;
		}

	rc = g_new0(RemoteConnection, 1);
	rc->server = FALSE;
	rc->fd = fd;
	rc->path = g_strdup(path);

	return rc;
}

static sig_atomic_t sigpipe_occured = FALSE;

static void sighandler_sigpipe(gint sig)
{
	sigpipe_occured = TRUE;
}

static gboolean remote_client_send(RemoteConnection *rc, const gchar *text)
{
	struct sigaction new_action, old_action;
	gboolean ret = FALSE;
	GError *error = NULL;
	GIOChannel *channel;

	if (!rc || rc->server) return FALSE;
	if (!text) return TRUE;

	sigpipe_occured = FALSE;

	new_action.sa_handler = sighandler_sigpipe;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	/* setup our signal handler */
	sigaction(SIGPIPE, &new_action, &old_action);

	channel = g_io_channel_unix_new(rc->fd);

	g_io_channel_write_chars(channel, text, -1, NULL, &error);
	g_io_channel_write_chars(channel, "\n", -1, NULL, &error);
	g_io_channel_flush(channel, &error);

	if (error)
		{
		log_printf("error reading socket: %s\n", error->message);
		g_error_free(error);
		ret = FALSE;;
		}
	else
		{
		ret = TRUE;
		}

	if (ret)
		{
		gchar *buffer = NULL;
		gsize termpos;
		while (g_io_channel_read_line(channel, &buffer, NULL, &termpos, &error) == G_IO_STATUS_NORMAL)
			{
			if (buffer)
				{
				if (buffer[0] == '\n') /* empty line finishes the command */
					{
					g_free(buffer);
					fflush(stdout);
					break;
					}
				buffer[termpos] = '\0';
				printf("%s\n", buffer);
				g_free(buffer);
				buffer = NULL;
				}
			}

		if (error)
			{
			log_printf("error reading socket: %s\n", error->message);
			g_error_free(error);
			ret = FALSE;
			}
		}


	/* restore the original signal handler */
	sigaction(SIGPIPE, &old_action, NULL);
	g_io_channel_unref(channel);
	return ret;
}

void remote_close(RemoteConnection *rc)
{
	if (!rc) return;

	if (rc->server)
		{
		remote_server_clients_close(rc);

		g_source_remove(rc->channel_id);
		unlink(rc->path);
		}

	if (rc->read_data)
		g_free(rc->read_data);

	close(rc->fd);

	g_free(rc->path);
	g_free(rc);
}

/*
 *-----------------------------------------------------------------------------
 * remote functions
 *-----------------------------------------------------------------------------
 */

static void gr_image_next(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_next(NULL);
}

static void gr_image_prev(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_prev(NULL);
}

static void gr_image_first(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_first(NULL);
}

static void gr_image_last(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_last(NULL);
}

static void gr_fullscreen_toggle(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_full_screen_toggle(NULL);
}

static void gr_fullscreen_start(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_full_screen_start(NULL);
}

static void gr_fullscreen_stop(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_full_screen_stop(NULL);
}

static void gr_slideshow_start_rec(const gchar *text, GIOChannel *channel, gpointer data)
{
	GList *list;
	FileData *dir_fd = file_data_new_dir(text);
	list = filelist_recursive(dir_fd);
	file_data_unref(dir_fd);
	if (!list) return;
//printf("length: %d\n", g_list_length(list));
	layout_image_slideshow_stop(NULL);
	layout_image_slideshow_start_from_list(NULL, list);
}

static void gr_slideshow_toggle(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_slideshow_toggle(NULL);
}

static void gr_slideshow_start(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_slideshow_start(NULL);
}

static void gr_slideshow_stop(const gchar *text, GIOChannel *channel, gpointer data)
{
	layout_image_slideshow_stop(NULL);
}

static void gr_slideshow_delay(const gchar *text, GIOChannel *channel, gpointer data)
{
	gdouble n;

	n = g_ascii_strtod(text, NULL);
	if (n < SLIDESHOW_MIN_SECONDS || n > SLIDESHOW_MAX_SECONDS)
		{
		printf_term("Remote slideshow delay out of range (%.1f to %.1f)\n",
			    SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS);
		return;
		}
	options->slideshow.delay = (gint)(n * 10.0 + 0.01);
}

static void gr_tools_show(const gchar *text, GIOChannel *channel, gpointer data)
{
	gboolean popped;
	gboolean hidden;

	if (layout_tools_float_get(NULL, &popped, &hidden) && hidden)
		{
		layout_tools_float_set(NULL, popped, FALSE);
		}
}

static void gr_tools_hide(const gchar *text, GIOChannel *channel, gpointer data)
{
	gboolean popped;
	gboolean hidden;

	if (layout_tools_float_get(NULL, &popped, &hidden) && !hidden)
		{
		layout_tools_float_set(NULL, popped, TRUE);
		}
}

static gboolean gr_quit_idle_cb(gpointer data)
{
	exit_program();

	return FALSE;
}

static void gr_quit(const gchar *text, GIOChannel *channel, gpointer data)
{
	/* schedule exit when idle, if done from within a
	 * remote handler remote_close will crash
	 */
	g_idle_add(gr_quit_idle_cb, NULL);
}

static void gr_file_load(const gchar *text, GIOChannel *channel, gpointer data)
{
	gchar *filename = expand_tilde(text);

	if (isfile(filename))
		{
		if (file_extension_match(filename, GQ_COLLECTION_EXT))
			{
			collection_window_new(filename);
			}
		else
			{
			layout_set_path(NULL, filename);
			}
		}
	else if (isdir(filename))
		{
		layout_set_path(NULL, filename);
		}
	else
		{
		log_printf("remote sent filename that does not exist:\"%s\"\n", filename);
		layout_set_path(NULL, homedir());
		}

	g_free(filename);
}

static void gr_config_load(const gchar *text, GIOChannel *channel, gpointer data)
{
	gchar *filename = expand_tilde(text);

	if (isfile(filename))
		{
		load_config_from_file(filename, FALSE);
		}
	else
		{
		log_printf("remote sent filename that does not exist:\"%s\"\n", filename);
		layout_set_path(NULL, homedir());
		}

	g_free(filename);
}

static void gr_get_sidecars(const gchar *text, GIOChannel *channel, gpointer data)
{
	gchar *filename = expand_tilde(text);
	FileData *fd = file_data_new_group(filename);

	GList *work;
	if (fd->parent) fd = fd->parent;

	g_io_channel_write_chars(channel, fd->path, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "\n", -1, NULL, NULL);

	work = fd->sidecar_files;

	while (work)
		{
		fd = work->data;
		work = work->next;
		g_io_channel_write_chars(channel, fd->path, -1, NULL, NULL);
		g_io_channel_write_chars(channel, "\n", -1, NULL, NULL);
		}
	g_free(filename);
}

static void gr_get_destination(const gchar *text, GIOChannel *channel, gpointer data)
{
	gchar *filename = expand_tilde(text);
	FileData *fd = file_data_new_group(filename);

	if (fd->change && fd->change->dest)
		{
		g_io_channel_write_chars(channel, fd->change->dest, -1, NULL, NULL);
		g_io_channel_write_chars(channel, "\n", -1, NULL, NULL);
		}
	g_free(filename);
}

static void gr_file_view(const gchar *text, GIOChannel *channel, gpointer data)
{
	gchar *filename = expand_tilde(text);

	view_window_new(file_data_new_group(filename));
	g_free(filename);
}

static void gr_list_clear(const gchar *text, GIOChannel *channel, gpointer data)
{
	RemoteData *remote_data = data;

	if (remote_data->command_collection)
		{
		collection_unref(remote_data->command_collection);
		remote_data->command_collection = NULL;
		}
}

static void gr_list_add(const gchar *text, GIOChannel *channel, gpointer data)
{
	RemoteData *remote_data = data;
	gboolean new = TRUE;

	if (!remote_data->command_collection)
		{
		CollectionData *cd;

		cd = collection_new("");

		g_free(cd->path);
		cd->path = NULL;
		g_free(cd->name);
		cd->name = g_strdup(_("Command line"));

		remote_data->command_collection = cd;
		}
	else
		{
		new = (!collection_get_first(remote_data->command_collection));
		}

	if (collection_add(remote_data->command_collection, file_data_new_group(text), FALSE) && new)
		{
		layout_image_set_collection(NULL, remote_data->command_collection,
					    collection_get_first(remote_data->command_collection));
		}
}

static void gr_raise(const gchar *text, GIOChannel *channel, gpointer data)
{
	LayoutWindow *lw = NULL;

	if (layout_valid(&lw))
		{
		gtk_window_present(GTK_WINDOW(lw->window));
		}
}

typedef struct _RemoteCommandEntry RemoteCommandEntry;
struct _RemoteCommandEntry {
	gchar *opt_s;
	gchar *opt_l;
	void (*func)(const gchar *text, GIOChannel *channel, gpointer data);
	gboolean needs_extra;
	gboolean prefer_command_line;
	gchar *parameter;
	gchar *description;
};

static RemoteCommandEntry remote_commands[] = {
	/* short, long                  callback,               extra, prefer, parameter, description */
	{ "-n", "--next",               gr_image_next,          FALSE, FALSE, NULL, N_("next image") },
	{ "-b", "--back",               gr_image_prev,          FALSE, FALSE, NULL, N_("previous image") },
	{ NULL, "--first",              gr_image_first,         FALSE, FALSE, NULL, N_("first image") },
	{ NULL, "--last",               gr_image_last,          FALSE, FALSE, NULL, N_("last image") },
	{ "-f", "--fullscreen",         gr_fullscreen_toggle,   FALSE, TRUE,  NULL, N_("toggle full screen") },
	{ "-fs","--fullscreen-start",   gr_fullscreen_start,    FALSE, FALSE, NULL, N_("start full screen") },
	{ "-fS","--fullscreen-stop",    gr_fullscreen_stop,     FALSE, FALSE, NULL, N_("stop full screen") },
	{ "-s", "--slideshow",          gr_slideshow_toggle,    FALSE, TRUE,  NULL, N_("toggle slide show") },
	{ "-ss","--slideshow-start",    gr_slideshow_start,     FALSE, FALSE, NULL, N_("start slide show") },
	{ "-sS","--slideshow-stop",     gr_slideshow_stop,      FALSE, FALSE, NULL, N_("stop slide show") },
	{ NULL, "--slideshow-recurse:", gr_slideshow_start_rec, TRUE,  FALSE, N_("<FOLDER>"), N_("start recursive slide show in FOLDER") },
	{ "-d", "--delay=",             gr_slideshow_delay,     TRUE,  FALSE, N_("<[N][.M]>"), N_("set slide show delay to N.M seconds") },
	{ "+t", "--tools-show",         gr_tools_show,          FALSE, TRUE,  NULL, N_("show tools") },
	{ "-t", "--tools-hide",	        gr_tools_hide,          FALSE, TRUE,  NULL, N_("hide tools") },
	{ "-q", "--quit",               gr_quit,                FALSE, FALSE, NULL, N_("quit") },
	{ NULL, "--config-load:",       gr_config_load,         TRUE,  FALSE, N_("<FILE>"), N_("load configuration from FILE") },
	{ NULL, "--get-sidecars:",      gr_get_sidecars,        TRUE,  FALSE, N_("<FILE>"), N_("get list of sidecars of FILE") },
	{ NULL, "--get-destination:",  	gr_get_destination,     TRUE,  FALSE, N_("<FILE>"), N_("get destination path of FILE") },
	{ NULL, "file:",                gr_file_load,           TRUE,  FALSE, N_("<FILE>"), N_("open FILE") },
	{ NULL, "view:",                gr_file_view,           TRUE,  FALSE, N_("<FILE>"), N_("open FILE in new window") },
	{ NULL, "--list-clear",         gr_list_clear,          FALSE, FALSE, NULL, N_("clear command line collection list") },
	{ NULL, "--list-add:",          gr_list_add,            TRUE,  FALSE, N_("<FILE>"), N_("add FILE to command line collection list") },
	{ NULL, "raise",                gr_raise,               FALSE, FALSE, NULL, N_("bring the Geeqie window to the top") },
	{ NULL, NULL, NULL, FALSE, FALSE, NULL }
};

static RemoteCommandEntry *remote_command_find(const gchar *text, const gchar **offset)
{
	gboolean match = FALSE;
	gint i;

	i = 0;
	while (!match && remote_commands[i].func != NULL)
		{
		if (remote_commands[i].needs_extra)
			{
			if (remote_commands[i].opt_s &&
			    strncmp(remote_commands[i].opt_s, text, strlen(remote_commands[i].opt_s)) == 0)
				{
				if (offset) *offset = text + strlen(remote_commands[i].opt_s);
				return &remote_commands[i];
				}
			else if (remote_commands[i].opt_l &&
				 strncmp(remote_commands[i].opt_l, text, strlen(remote_commands[i].opt_l)) == 0)
				{
				if (offset) *offset = text + strlen(remote_commands[i].opt_l);
				return &remote_commands[i];
				}
			}
		else
			{
			if ((remote_commands[i].opt_s && strcmp(remote_commands[i].opt_s, text) == 0) ||
			    (remote_commands[i].opt_l && strcmp(remote_commands[i].opt_l, text) == 0))
				{
				if (offset) *offset = text;
				return &remote_commands[i];
				}
			}

		i++;
		}

	return NULL;
}

static void remote_cb(RemoteConnection *rc, const gchar *text, GIOChannel *channel, gpointer data)
{
	RemoteCommandEntry *entry;
	const gchar *offset;

	entry = remote_command_find(text, &offset);
	if (entry && entry->func)
		{
		entry->func(offset, channel, data);
		}
	else
		{
		log_printf("unknown remote command:%s\n", text);
		}
}

void remote_help(void)
{
	gint i;
	gchar *s_opt_param;
	gchar *l_opt_param;

	print_term(_("Remote command list:\n"));

	i = 0;
	while (remote_commands[i].func != NULL)
		{
		if (remote_commands[i].description)
			{
			s_opt_param = g_strconcat(remote_commands[i].opt_s, remote_commands[i].parameter, NULL);
			l_opt_param = g_strconcat(remote_commands[i].opt_l, remote_commands[i].parameter, NULL);
			printf_term("  %-11s%-1s %-30s%-s\n",
				    (remote_commands[i].opt_s) ? s_opt_param : "",
				    (remote_commands[i].opt_s && remote_commands[i].opt_l) ? "," : " ",
				    (remote_commands[i].opt_l) ? l_opt_param : "",
				    _(remote_commands[i].description));
			g_free(s_opt_param);
			g_free(l_opt_param);
			}
		i++;
		}
	printf_term(N_("\n  All other command line parameters are used as plain files if they exists.\n"));
}

GList *remote_build_list(GList *list, gint argc, gchar *argv[], GList **errors)
{
	gint i;

	i = 1;
	while (i < argc)
		{
		RemoteCommandEntry *entry;

		entry = remote_command_find(argv[i], NULL);
		if (entry)
			{
			list = g_list_append(list, argv[i]);
			}
		else if (errors && !isfile(argv[i]))
			{
			*errors = g_list_append(*errors, argv[i]);
			}
		i++;
		}

	return list;
}

/**
 * \param arg_exec Binary (argv0)
 * \param remote_list Evaluated and recognized remote commands
 * \param path The current path
 * \param cmd_list List of all non collections in Path
 * \param collection_list List of all collections in argv
 */
void remote_control(const gchar *arg_exec, GList *remote_list, const gchar *path,
		    GList *cmd_list, GList *collection_list)
{
	RemoteConnection *rc;
	gboolean started = FALSE;
	gchar *buf;

	buf = g_build_filename(get_rc_dir(), ".command", NULL);
	rc = remote_client_open(buf);
	if (!rc)
		{
		GString *command;
		GList *work;
		gint retry_count = 12;
		gboolean blank = FALSE;

		printf_term(_("Remote %s not running, starting..."), GQ_APPNAME);

		command = g_string_new(arg_exec);

		work = remote_list;
		while (work)
			{
			gchar *text;
			RemoteCommandEntry *entry;

			text = work->data;
			work = work->next;

			entry = remote_command_find(text, NULL);
			if (entry)
				{
				if (entry->prefer_command_line)
					{
					remote_list = g_list_remove(remote_list, text);
					g_string_append(command, " ");
					g_string_append(command, text);
					}
				if (entry->opt_l && strcmp(entry->opt_l, "file:") == 0)
					{
					blank = TRUE;
					}
				}
			}

		if (blank || cmd_list || path) g_string_append(command, " --blank");
		if (get_debug_level()) g_string_append(command, " --debug");

		g_string_append(command, " &");
		runcmd(command->str);
		g_string_free(command, TRUE);

		while (!rc && retry_count > 0)
			{
			usleep((retry_count > 10) ? 500000 : 1000000);
			rc = remote_client_open(buf);
			if (!rc) print_term(".");
			retry_count--;
			}

		print_term("\n");

		started = TRUE;
		}
	g_free(buf);

	if (rc)
		{
		GList *work;
		const gchar *prefix;
		gboolean use_path = TRUE;
		gboolean sent = FALSE;

		work = remote_list;
		while (work)
			{
			gchar *text;
			RemoteCommandEntry *entry;

			text = work->data;
			work = work->next;

			entry = remote_command_find(text, NULL);
			if (entry &&
			    entry->opt_l &&
			    strcmp(entry->opt_l, "file:") == 0) use_path = FALSE;

			remote_client_send(rc, text);

			sent = TRUE;
			}

		if (cmd_list && cmd_list->next)
			{
			prefix = "--list-add:";
			remote_client_send(rc, "--list-clear");
			}
		else
			{
			prefix = "file:";
			}

		work = cmd_list;
		while (work)
			{
			FileData *fd;
			gchar *text;

			fd = work->data;
			work = work->next;

			text = g_strconcat(prefix, fd->path, NULL);
			remote_client_send(rc, text);
			g_free(text);

			sent = TRUE;
			}

		if (path && !cmd_list && use_path)
			{
			gchar *text;

			text = g_strdup_printf("file:%s", path);
			remote_client_send(rc, text);
			g_free(text);

			sent = TRUE;
			}

		work = collection_list;
		while (work)
			{
			const gchar *name;
			gchar *text;

			name = work->data;
			work = work->next;

			text = g_strdup_printf("file:%s", name);
			remote_client_send(rc, text);
			g_free(text);

			sent = TRUE;
			}

		if (!started && !sent)
			{
			remote_client_send(rc, "raise");
			}
		}
	else
		{
		print_term(_("Remote not available\n"));
		}

	_exit(0);
}

RemoteConnection *remote_server_init(gchar *path, CollectionData *command_collection)
{
	RemoteConnection *remote_connection = remote_server_open(path);
	RemoteData *remote_data = g_new(RemoteData, 1);

	remote_data->command_collection = command_collection;

	remote_server_subscribe(remote_connection, remote_cb, remote_data);
	return remote_connection;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
