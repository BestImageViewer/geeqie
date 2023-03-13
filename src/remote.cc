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

#include "cache-maint.h"
#include "collect.h"
#include "collect-io.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "image.h"
#include "img-view.h"
#include "layout-image.h"
#include "layout-util.h"
#include "misc.h"
#include "pixbuf-renderer.h"
#include "slideshow.h"
#include "ui-fileops.h"
#include "rcfile.h"
#include "view-file.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include "glua.h"

#define SERVER_MAX_CLIENTS 8

#define REMOTE_SERVER_BACKLOG 4


#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif


static RemoteConnection *remote_client_open(const gchar *path);
static gint remote_client_send(RemoteConnection *rc, const gchar *text);
static void gr_raise(const gchar *text, GIOChannel *channel, gpointer data);

static LayoutWindow *lw_id = NULL; /* points to the window set by the --id option */

typedef struct _RemoteClient RemoteClient;
struct _RemoteClient {
	gint fd;
	guint channel_id; /* event source id */
	RemoteConnection *rc;
};

typedef struct _RemoteData RemoteData;
struct _RemoteData {
	CollectionData *command_collection;
	GList *file_list;
	gboolean single_dir;
};

/* To enable file names containing newlines to be processed correctly,
 * the --print0 remote option sets returned data to be terminated with a null
 * character rather a newline
 */
static gboolean print0 = FALSE;

/* Remote commands from main.cc are prepended with the current dir the remote
 * command was made from. Some remote commands require this. The
 * value is stored here
 */
static gchar *pwd = NULL;

/**
 * @brief Ensures file path is absolute.
 * @param[in] filename Filepath, absolute or relative to calling directory
 * @returns absolute path
 * 
 * If first character of input filepath is not the directory
 * separator, assume it as a relative path and prepend
 * the directory the remote command was initiated from
 * 
 * Return value must be freed with g_free()
 */
static gchar *set_pwd(gchar *filename)
{
	gchar *temp;

	if (strncmp(filename, G_DIR_SEPARATOR_S, 1) != 0)
		{
		temp = g_build_filename(pwd, filename, NULL);
		}
	else
		{
		temp = g_strdup(filename);
		}

	return temp;
}

static gboolean remote_server_client_cb(GIOChannel *source, GIOCondition condition, gpointer data)
{
	RemoteClient *client = data;
	RemoteConnection *rc;
	GIOStatus status = G_IO_STATUS_NORMAL;

	lw_id = NULL;
	rc = client->rc;

	if (condition & G_IO_IN)
		{
		gchar *buffer = NULL;
		GError *error = NULL;
		gsize termpos;
		/** @FIXME it should be possible to terminate the command with a null character */
		g_io_channel_set_line_term(source, "<gq_end_of_command>", -1);
		while ((status = g_io_channel_read_line(source, &buffer, NULL, &termpos, &error)) == G_IO_STATUS_NORMAL)
			{
			if (buffer)
				{
				buffer[termpos] = '\0';

				if (strlen(buffer) > 0)
					{
					if (rc->read_func) rc->read_func(rc, buffer, source, rc->read_data);
					g_io_channel_write_chars(source, "<gq_end_of_command>", -1, NULL, NULL); /* empty line finishes the command */
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

static gboolean remote_server_read_cb(GIOChannel *UNUSED(source), GIOCondition UNUSED(condition), gpointer data)
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

gboolean remote_server_exists(const gchar *path)
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
	if (bind(fd, (const struct sockaddr*)&addr, sizeof(addr)) == -1 ||
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
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
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

static sig_atomic_t sigpipe_occurred = FALSE;

static void sighandler_sigpipe(gint UNUSED(sig))
{
	sigpipe_occurred = TRUE;
}

static gboolean remote_client_send(RemoteConnection *rc, const gchar *text)
{
	struct sigaction new_action, old_action;
	gboolean ret = FALSE;
	GError *error = NULL;
	GIOChannel *channel;

	if (!rc || rc->server) return FALSE;
	if (!text) return TRUE;

	sigpipe_occurred = FALSE;

	new_action.sa_handler = sighandler_sigpipe;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	/* setup our signal handler */
	sigaction(SIGPIPE, &new_action, &old_action);

	channel = g_io_channel_unix_new(rc->fd);

	g_io_channel_write_chars(channel, text, -1, NULL, &error);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, &error);
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
		g_io_channel_set_line_term(channel, "<gq_end_of_command>", -1);
		while (g_io_channel_read_line(channel, &buffer, NULL, &termpos, &error) == G_IO_STATUS_NORMAL)
			{
			if (buffer)
				{
				if (g_strstr_len(buffer, -1, "<gq_end_of_command>") == buffer) /* empty line finishes the command */
					{
					g_free(buffer);
					fflush(stdout);
					break;
					}
				buffer[termpos] = '\0';
				if (g_strstr_len(buffer, -1, "print0") != 0)
					{
					print0 = TRUE;
					}
				else
					{
					if (print0)
						{
						printf("%s%c", buffer, 0);
						}
					else
						{
						printf("%s\n", buffer);
						}
					}
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

static void gr_image_next(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_next(lw_id);
}

static void gr_new_window(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	LayoutWindow *lw = NULL;

	if (!layout_valid(&lw)) return;

	lw_id = layout_new_from_default();

	layout_set_path(lw_id, pwd);
}

static gboolean gr_close_window_cb()
{
	if (!layout_valid(&lw_id)) return FALSE;

	layout_menu_close_cb(NULL, lw_id);

	return FALSE;
}

static void gr_close_window(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	g_idle_add(gr_close_window_cb, NULL);
}

static void gr_image_prev(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_prev(lw_id);
}

static void gr_image_first(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_first(lw_id);
}

static void gr_image_last(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_last(lw_id);
}

static void gr_fullscreen_toggle(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_full_screen_toggle(lw_id);
}

static void gr_fullscreen_start(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_full_screen_start(lw_id);
}

static void gr_fullscreen_stop(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_full_screen_stop(lw_id);
}

static void gr_lw_id(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	lw_id = layout_find_by_layout_id(text);
	if (!lw_id)
		{
		log_printf("remote sent window ID that does not exist:\"%s\"\n",text);
		}
	layout_valid(&lw_id);
}

static void gr_slideshow_start_rec(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	GList *list;
	gchar *tilde_filename;

	tilde_filename = expand_tilde(text);

	FileData *dir_fd = file_data_new_dir(tilde_filename);
	g_free(tilde_filename);

	layout_valid(&lw_id);
	list = filelist_recursive_full(dir_fd, lw_id->sort_method, lw_id->sort_ascend);
	file_data_unref(dir_fd);
	if (!list) return;
//printf("length: %d\n", g_list_length(list));
	layout_image_slideshow_stop(lw_id);
	layout_image_slideshow_start_from_list(lw_id, list);
}

static void gr_cache_thumb(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	if (!g_strcmp0(text, "clear"))
		{
		cache_maintain_home_remote(FALSE, TRUE, NULL);
		}
	else if (!g_strcmp0(text, "clean"))
		{
		cache_maintain_home_remote(FALSE, FALSE, NULL);
		}
}

static void gr_cache_shared(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	if (!g_strcmp0(text, "clear"))
		cache_manager_standard_process_remote(TRUE);
	else if (!g_strcmp0(text, "clean"))
		cache_manager_standard_process_remote(FALSE);
}

static void gr_cache_metadata(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	cache_maintain_home_remote(TRUE, FALSE, NULL);
}

static void gr_cache_render(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	cache_manager_render_remote(text, FALSE, FALSE, NULL);
}

static void gr_cache_render_recurse(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	cache_manager_render_remote(text, TRUE, FALSE, NULL);
}

static void gr_cache_render_standard(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	if(options->thumbnails.spec_standard)
		{
		cache_manager_render_remote(text, FALSE, TRUE, NULL);
		}
}

static void gr_cache_render_standard_recurse(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	if(options->thumbnails.spec_standard)
		{
		cache_manager_render_remote(text, TRUE, TRUE, NULL);
		}
}

static void gr_slideshow_toggle(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_slideshow_toggle(lw_id);
}

static void gr_slideshow_start(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_slideshow_start(lw_id);
}

static void gr_slideshow_stop(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	layout_image_slideshow_stop(lw_id);
}

static void gr_slideshow_delay(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	gdouble t1, t2, t3, n;
	gint res;

	res = sscanf(text, "%lf:%lf:%lf", &t1, &t2, &t3);
	if (res == 3)
		{
		n = (t1 * 3600) + (t2 * 60) + t3;
		if (n < SLIDESHOW_MIN_SECONDS || n > SLIDESHOW_MAX_SECONDS ||
				t1 >= 24 || t2 >= 60 || t3 >= 60)
			{
			printf_term(TRUE, "Remote slideshow delay out of range (%.1f to %.1f)\n",
								SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS);
			return;
			}
		}
	else if (res == 2)
		{
		n = t1 * 60 + t2;
		if (n < SLIDESHOW_MIN_SECONDS || n > SLIDESHOW_MAX_SECONDS ||
				t1 >= 60 || t2 >= 60)
			{
			printf_term(TRUE, "Remote slideshow delay out of range (%.1f to %.1f)\n",
								SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS);
			return;
			}
		}
	else if (res == 1)
		{
		n = t1;
		if (n < SLIDESHOW_MIN_SECONDS || n > SLIDESHOW_MAX_SECONDS)
			{
			printf_term(TRUE, "Remote slideshow delay out of range (%.1f to %.1f)\n",
								SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS);
			return;
			}
		}
	else
		{
		n = 0;
		}

	options->slideshow.delay = (gint)(n * 10.0 + 0.01);
}

static void gr_tools_show(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	gboolean popped;
	gboolean hidden;

	if (layout_tools_float_get(lw_id, &popped, &hidden) && hidden)
		{
		layout_tools_float_set(lw_id, popped, FALSE);
		}
}

static void gr_tools_hide(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	gboolean popped;
	gboolean hidden;

	if (layout_tools_float_get(lw_id, &popped, &hidden) && !hidden)
		{
		layout_tools_float_set(lw_id, popped, TRUE);
		}
}

static gboolean gr_quit_idle_cb(gpointer UNUSED(data))
{
	exit_program();

	return FALSE;
}

static void gr_quit(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	/* schedule exit when idle, if done from within a
	 * remote handler remote_close will crash
	 */
	g_idle_add(gr_quit_idle_cb, NULL);
}

static void gr_file_load_no_raise(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	gchar *filename;
	gchar *tilde_filename;

	if (!download_web_file(text, TRUE, NULL))
		{
		tilde_filename = expand_tilde(text);
		filename = set_pwd(tilde_filename);

		if (isfile(filename))
			{
			if (file_extension_match(filename, GQ_COLLECTION_EXT))
				{
				collection_window_new(filename);
				}
			else
				{
				layout_set_path(lw_id, filename);
				}
			}
		else if (isdir(filename))
			{
			layout_set_path(lw_id, filename);
			}
		else
			{
			log_printf("remote sent filename that does not exist:\"%s\"\n", filename);
			layout_set_path(lw_id, homedir());
			}

		g_free(filename);
		g_free(tilde_filename);
		}
}

static void gr_file_load(const gchar *text, GIOChannel *channel, gpointer data)
{
	gr_file_load_no_raise(text, channel, data);

	gr_raise(text, channel, data);
}

static void gr_pixel_info(const gchar *UNUSED(text), GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *pixel_info;
	gint x_pixel, y_pixel;
	gint width, height;
	gint r_mouse, g_mouse, b_mouse;
	PixbufRenderer *pr;

	if (!layout_valid(&lw_id)) return;

	pr = (PixbufRenderer*)lw_id->image->pr;

	if (pr)
		{
		pixbuf_renderer_get_image_size(pr, &width, &height);
		if (width < 1 || height < 1) return;

		pixbuf_renderer_get_mouse_position(pr, &x_pixel, &y_pixel);

		if (x_pixel >= 0 && y_pixel >= 0)
			{
			pixbuf_renderer_get_pixel_colors(pr, x_pixel, y_pixel,
							 &r_mouse, &g_mouse, &b_mouse);

			pixel_info = g_strdup_printf(_("[%d,%d]: RGB(%3d,%3d,%3d)"),
						 x_pixel, y_pixel,
						 r_mouse, g_mouse, b_mouse);

			g_io_channel_write_chars(channel, pixel_info, -1, NULL, NULL);
			g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

			g_free(pixel_info);
			}
		else
			{
			return;
			}
		}
	else
		{
		return;
		}
}

static void gr_rectangle(const gchar *UNUSED(text), GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *rectangle_info;
	PixbufRenderer *pr;
	gint x1, y1, x2, y2;

	if (!options->draw_rectangle) return;
	if (!layout_valid(&lw_id)) return;

	pr = (PixbufRenderer*)lw_id->image->pr;

	if (pr)
		{
		image_get_rectangle(&x1, &y1, &x2, &y2);
		rectangle_info = g_strdup_printf(_("%dx%d+%d+%d"),
					(x2 > x1) ? x2 - x1 : x1 - x2,
					(y2 > y1) ? y2 - y1 : y1 - y2,
					(x2 > x1) ? x1 : x2,
					(y2 > y1) ? y1 : y2);

		g_io_channel_write_chars(channel, rectangle_info, -1, NULL, NULL);
		g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

		g_free(rectangle_info);
		}
}

static void gr_render_intent(const gchar *UNUSED(text), GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *render_intent;

	switch (options->color_profile.render_intent)
		{
		case 0:
			render_intent = g_strdup("Perceptual");
			break;
		case 1:
			render_intent = g_strdup("Relative Colorimetric");
			break;
		case 2:
			render_intent = g_strdup("Saturation");
			break;
		case 3:
			render_intent = g_strdup("Absolute Colorimetric");
			break;
		default:
			render_intent = g_strdup("none");
			break;
		}

	g_io_channel_write_chars(channel, render_intent, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	g_free(render_intent);
}

static void get_filelist(const gchar *text, GIOChannel *channel, gboolean recurse)
{
	GList *list = NULL;
	FileFormatClass format_class;
	FileData *dir_fd;
	FileData *fd;
	GString *out_string = g_string_new(NULL);
	GList *work;
	gchar *tilde_filename;

	if (strcmp(text, "") == 0)
		{
		if (layout_valid(&lw_id))
			{
			dir_fd = file_data_new_dir(lw_id->dir_fd->path);
			}
		else
			{
			return;
			}
		}
	else
		{
		tilde_filename = expand_tilde(text);
		if (isdir(tilde_filename))
			{
			dir_fd = file_data_new_dir(tilde_filename);
			}
		else
			{
			g_free(tilde_filename);
			return;
			}
		g_free(tilde_filename);
		}

	if (recurse)
		{
		list = filelist_recursive(dir_fd);
		}
	else
		{
		filelist_read(dir_fd, &list, NULL);
		}

	work = list;
	while (work)
		{
		fd = work->data;
		g_string_append_printf(out_string, "%s", fd->path);
		format_class = filter_file_get_class(fd->path);

		switch (format_class)
			{
			case FORMAT_CLASS_IMAGE:
				out_string = g_string_append(out_string, "    Class: Image");
				break;
			case FORMAT_CLASS_RAWIMAGE:
				out_string = g_string_append(out_string, "    Class: RAW image");
				break;
			case FORMAT_CLASS_META:
				out_string = g_string_append(out_string, "    Class: Metadata");
				break;
			case FORMAT_CLASS_VIDEO:
				out_string = g_string_append(out_string, "    Class: Video");
				break;
			case FORMAT_CLASS_COLLECTION:
				out_string = g_string_append(out_string, "    Class: Collection");
				break;
			case FORMAT_CLASS_DOCUMENT:
				out_string = g_string_append(out_string, "    Class: Document");
				break;
			case FORMAT_CLASS_ARCHIVE:
				out_string = g_string_append(out_string, "    Class: Archive");
				break;
			case FORMAT_CLASS_UNKNOWN:
				out_string = g_string_append(out_string, "    Class: Unknown");
				break;
			default:
				out_string = g_string_append(out_string, "    Class: Unknown");
				break;
			}
		out_string = g_string_append(out_string, "\n");
		work = work->next;
		}

	g_io_channel_write_chars(channel, out_string->str, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	g_string_free(out_string, TRUE);
	filelist_free(list);
	file_data_unref(dir_fd);
}

static void gr_get_selection(const gchar *UNUSED(text), GIOChannel *channel, gboolean UNUSED(data))
{
	if (!layout_valid(&lw_id)) return;

	GList *selected = layout_selection_list(lw_id);  // Keep copy to free.
	GString *out_string = g_string_new(NULL);

	GList *work = selected;
	while (work)
		{
		FileData *fd = work->data;
		g_assert(fd->magick == FD_MAGICK);

		g_string_append_printf(out_string, "%s    %s\n",
				       fd->path,
				       format_class_list[filter_file_get_class(fd->path)]);

		work = work->next;
		}

	g_io_channel_write_chars(channel, out_string->str, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	filelist_free(selected);
	g_string_free(out_string, TRUE);
}

static void gr_selection_add(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	if (!layout_valid(&lw_id)) return;

	FileData *fd_to_select = NULL;
	if (strcmp(text, "") == 0)
		{
		// No file specified, use current fd.
		fd_to_select = layout_image_get_fd(lw_id);
		}
	else
		{
		// Search through the current file list for a file matching the specified path.
		// "Match" is either a basename match or a file path match.
		gchar *path = expand_tilde(text);
		gchar *filename = g_path_get_basename(path);
		gchar *slash_plus_filename = g_strdup_printf("%s%s", G_DIR_SEPARATOR_S, filename);

		GList *file_list = layout_list(lw_id);
		for (GList *work = file_list; work && !fd_to_select; work = work->next)
			{
			FileData *fd = work->data;
			if (!strcmp(path, fd->path) || g_str_has_suffix(fd->path, slash_plus_filename))
				{
				fd_to_select = file_data_ref(fd);
				continue;  // will exit loop.
				}

			for (GList *sidecar = fd->sidecar_files; sidecar && !fd_to_select; sidecar = sidecar->next)
				{
				FileData *side_fd = sidecar->data;
				if (!strcmp(path, side_fd->path)
	                            || g_str_has_suffix(side_fd->path, slash_plus_filename))
					{
					fd_to_select = file_data_ref(side_fd);
					continue;  // will exit both nested loops.
					}
				}
			}

		if (!fd_to_select)
			{
			log_printf("remote sent --selection-add filename that could not be found: \"%s\"\n",
				   filename);
			}

		filelist_free(file_list);
		g_free(slash_plus_filename);
		g_free(filename);
		g_free(path);
		}

	if (fd_to_select)
		{
		GList *to_select = g_list_append(NULL, fd_to_select);
		// Using the "_list" variant doesn't clear the existing selection.
		layout_select_list(lw_id, to_select);
		filelist_free(to_select);
		}
}

static void gr_collection(const gchar *text, GIOChannel *channel, gpointer UNUSED(data))
{
	GString *contents = g_string_new(NULL);

	if (is_collection(text))
		{
		collection_contents(text, &contents);
		}
	else
		{
		return;
		}

	g_io_channel_write_chars(channel, contents->str, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	g_string_free(contents, TRUE);
}

static void gr_collection_list(const gchar *UNUSED(text), GIOChannel *channel, gpointer UNUSED(data))
{

	GList *collection_list = NULL;
	GList *work;
	GString *out_string = g_string_new(NULL);

	collect_manager_list(&collection_list, NULL, NULL);

	work = collection_list;
	while (work)
		{
		const gchar *collection_name = work->data;
		out_string = g_string_append(out_string, g_strdup(collection_name));
		out_string = g_string_append(out_string, "\n");

		work = work->next;
		}

	g_io_channel_write_chars(channel, out_string->str, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	string_list_free(collection_list);
	g_string_free(out_string, TRUE);
}

static gboolean wait_cb(const gpointer data)
{
	gint position = GPOINTER_TO_INT(data);
	gint x = position >> 16;
	gint y = position - (x << 16);

	gtk_window_move(GTK_WINDOW(lw_id->window), x, y);

	return FALSE;
}

static void gr_geometry(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	gchar **geometry;

	if (!layout_valid(&lw_id) || !text)
		{
		return;
		}

	if (text[0] == '+')
		{
		geometry = g_strsplit_set(text, "+", 3);
		if (geometry[1] != NULL && geometry[2] != NULL )
			{
			gtk_window_move(GTK_WINDOW(lw_id->window), atoi(geometry[1]), atoi(geometry[2]));
			}
		}
	else
		{
		geometry = g_strsplit_set(text, "+x", 4);
		if (geometry[0] != NULL && geometry[1] != NULL)
			{
			gtk_window_resize(GTK_WINDOW(lw_id->window), atoi(geometry[0]), atoi(geometry[1]));
			}
		if (geometry[2] != NULL && geometry[3] != NULL)
			{
			/* There is an occasional problem with a window_move immediately after a window_resize */
			g_idle_add(wait_cb, GINT_TO_POINTER((atoi(geometry[2]) << 16) + atoi(geometry[3])));
			}
		}
	g_strfreev(geometry);
}

static void gr_filelist(const gchar *text, GIOChannel *channel, gpointer UNUSED(data))
{
	get_filelist(text, channel, FALSE);
}

static void gr_filelist_recurse(const gchar *text, GIOChannel *channel, gpointer UNUSED(data))
{
	get_filelist(text, channel, TRUE);
}

static void gr_file_tell(const gchar *UNUSED(text), GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *out_string;
	gchar *collection_name = NULL;

	if (!layout_valid(&lw_id)) return;

	if (image_get_path(lw_id->image))
		{
		if (lw_id->image->collection && lw_id->image->collection->name)
			{
			collection_name = remove_extension_from_path(lw_id->image->collection->name);
			out_string = g_strconcat(image_get_path(lw_id->image), "    Collection: ", collection_name, NULL);
			}
		else
			{
			out_string = g_strconcat(image_get_path(lw_id->image), NULL);
			}
		}
	else
		{
		out_string = g_strconcat(lw_id->dir_fd->path, G_DIR_SEPARATOR_S, NULL);
		}

	g_io_channel_write_chars(channel, out_string, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	g_free(collection_name);
	g_free(out_string);
}

static void gr_file_info(const gchar *UNUSED(text), GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *filename;
	FileData *fd;
	gchar *country_name;
	gchar *country_code;
	gchar *timezone;
	gchar *local_time;
	GString *out_string;
	FileFormatClass format_class;

	if (!layout_valid(&lw_id)) return;

	if (image_get_path(lw_id->image))
		{
		filename = g_strdup(image_get_path(lw_id->image));
		fd = file_data_new_group(filename);
		out_string = g_string_new(NULL);

		format_class = filter_file_get_class(image_get_path(lw_id->image));
		if (format_class)
			{
			g_string_append_printf(out_string, _("Class: %s\n"), format_class_list[format_class]);
			}

		if (fd->page_total > 1)
			{
			g_string_append_printf(out_string, _("Page no: %d/%d\n"), fd->page_num + 1, fd->page_total);
			}

		if (fd->exif)
			{
			country_name = exif_get_data_as_text(fd->exif, "formatted.countryname");
			if (country_name)
				{
				g_string_append_printf(out_string, _("Country name: %s\n"), country_name);
				g_free(country_name);
				}

			country_code = exif_get_data_as_text(fd->exif, "formatted.countrycode");
			if (country_name)
				{
				g_string_append_printf(out_string, _("Country code: %s\n"), country_code);
				g_free(country_code);
				}

			timezone = exif_get_data_as_text(fd->exif, "formatted.timezone");
			if (timezone)
				{
				g_string_append_printf(out_string, _("Timezone: %s\n"), timezone);
				g_free(timezone);
				}

			local_time = exif_get_data_as_text(fd->exif, "formatted.localtime");
			if (local_time)
				{
				g_string_append_printf(out_string, ("Local time: %s\n"), local_time);
				g_free(local_time);
				}
			}

		g_io_channel_write_chars(channel, out_string->str, -1, NULL, NULL);
		g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

		g_string_free(out_string, TRUE);
		file_data_unref(fd);
		g_free(filename);
		}
}

static gchar *config_file_path(const gchar *param)
{
	gchar *path = NULL;
	gchar *full_name = NULL;

	if (file_extension_match(param, ".xml"))
		{
		path = g_build_filename(get_window_layouts_dir(), param, NULL);
		}
	else if (file_extension_match(param, NULL))
		{
		full_name = g_strconcat(param, ".xml", NULL);
		path = g_build_filename(get_window_layouts_dir(), full_name, NULL);
		}

	if (!isfile(path))
		{
		g_free(path);
		path = NULL;
		}

	g_free(full_name);
	return path;
}

static gboolean is_config_file(const gchar *param)
{
	gchar *name = NULL;

	name = config_file_path(param);
	if (name)
		{
		g_free(name);
		return TRUE;
		}
	return FALSE;
}

static void gr_config_load(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	gchar *filename = expand_tilde(text);

	if (!g_strstr_len(filename, -1, G_DIR_SEPARATOR_S))
		{
		if (is_config_file(filename))
			{
			gchar *tmp = config_file_path(filename);
			g_free(filename);
			filename = tmp;
			}
		}

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

static void gr_get_sidecars(const gchar *text, GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *filename = expand_tilde(text);
	FileData *fd = file_data_new_group(filename);

	GList *work;
	if (fd->parent) fd = fd->parent;

	g_io_channel_write_chars(channel, fd->path, -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	work = fd->sidecar_files;

	while (work)
		{
		fd = work->data;
		work = work->next;
		g_io_channel_write_chars(channel, fd->path, -1, NULL, NULL);
		g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);
		}
	g_free(filename);
}

static void gr_get_destination(const gchar *text, GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *filename = expand_tilde(text);
	FileData *fd = file_data_new_group(filename);

	if (fd->change && fd->change->dest)
		{
		g_io_channel_write_chars(channel, fd->change->dest, -1, NULL, NULL);
		g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);
		}
	g_free(filename);
}

static void gr_file_view(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	gchar *filename;
	gchar *tilde_filename = expand_tilde(text);

	filename = set_pwd(tilde_filename);

	view_window_new(file_data_new_group(filename));
	g_free(filename);
	g_free(tilde_filename);
}

static void gr_list_clear(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer data)
{
	RemoteData *remote_data = data;

	remote_data->command_collection = NULL;
	remote_data->file_list = NULL;
	remote_data->single_dir = TRUE;
}

static void gr_list_add(const gchar *text, GIOChannel *UNUSED(channel), gpointer data)
{
	RemoteData *remote_data = data;
	gboolean is_new = TRUE;
	gchar *path = NULL;
	FileData *fd;
	FileData *first;

	/** @FIXME Should check if file is in current dir, has tilde or is relative */
	if (!isfile(text))
		{
		log_printf("Warning: File does not exist --remote --list-add:%s", text);

		return;
		}

	/* If there is a files list on the command line
	 * check if they are all in the same folder
	 */
	if (remote_data->single_dir)
		{
		GList *work;
		work = remote_data->file_list;
		while (work && remote_data->single_dir)
			{
			gchar *dirname;
			dirname = g_path_get_dirname(((FileData *)work->data)->path);
			if (!path)
				{
				path = g_strdup(dirname);
				}
			else
				{
				if (g_strcmp0(path, dirname) != 0)
					{
					remote_data->single_dir = FALSE;
					}
				}
			g_free(dirname);
			work = work->next;
			}
		g_free(path);
		}

	gchar *pathname = g_path_get_dirname(text);
	layout_set_path(lw_id, pathname);
	g_free(pathname);

	fd = file_data_new_simple(text);
	remote_data->file_list = g_list_append(remote_data->file_list, fd);
	file_data_unref(fd);

	vf_select_none(lw_id->vf);
	remote_data->file_list = g_list_reverse(remote_data->file_list);

	layout_select_list(lw_id, remote_data->file_list);
	layout_refresh(lw_id);
	first = (FileData *)(g_list_first(vf_selection_get_list(lw_id->vf))->data);
	layout_set_fd(lw_id, first);

		CollectionData *cd;
		CollectWindow *cw;

	if (!remote_data->command_collection && !remote_data->single_dir)
		{
		cw = collection_window_new(NULL);
		cd = cw->cd;

		collection_path_changed(cd);

		remote_data->command_collection = cd;
		}
	else if (!remote_data->single_dir)
		{
		is_new = (!collection_get_first(remote_data->command_collection));
		}

	if (!remote_data->single_dir)
		{
		layout_image_set_collection(lw_id, remote_data->command_collection, collection_get_first(remote_data->command_collection));
		if (collection_add(remote_data->command_collection, file_data_new_group(text), FALSE) && is_new)
			{
			layout_image_set_collection(lw_id, remote_data->command_collection, collection_get_first(remote_data->command_collection));
			}
		}
}

static void gr_raise(const gchar *UNUSED(text), GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	if (layout_valid(&lw_id))
		{
		gtk_window_present(GTK_WINDOW(lw_id->window));
		}
}

static void gr_pwd(const gchar *text, GIOChannel *UNUSED(channel), gpointer UNUSED(data))
{
	LayoutWindow *lw = NULL;

	layout_valid(&lw);

	g_free(pwd);
	pwd = g_strdup(text);
	lw_id = lw;
}

static void gr_print0(const gchar *UNUSED(text), GIOChannel *channel, gpointer UNUSED(data))
{
	g_io_channel_write_chars(channel, "print0", -1, NULL, NULL);
	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);
}

#ifdef HAVE_LUA
static void gr_lua(const gchar *text, GIOChannel *channel, gpointer UNUSED(data))
{
	gchar *result = NULL;
	gchar **lua_command;

	lua_command = g_strsplit(text, ",", 2);

	if (lua_command[0] && lua_command[1])
		{
		FileData *fd = file_data_new_group(lua_command[0]);
		result = g_strdup(lua_callvalue(fd, lua_command[1], NULL));
		if (result)
			{
			g_io_channel_write_chars(channel, result, -1, NULL, NULL);
			}
		else
			{
			g_io_channel_write_chars(channel, N_("lua error: no data"), -1, NULL, NULL);
			}
		}
	else
		{
		g_io_channel_write_chars(channel, N_("lua error: no data"), -1, NULL, NULL);
		}

	g_io_channel_write_chars(channel, "<gq_end_of_command>", -1, NULL, NULL);

	g_strfreev(lua_command);
	g_free(result);
}
#endif

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
	{ "-b", "--back",               gr_image_prev,          FALSE, FALSE, NULL, N_("previous image") },
	{ NULL, "--close-window",       gr_close_window,        FALSE, FALSE, NULL, N_("close window") },
	{ NULL, "--config-load:",       gr_config_load,         TRUE,  FALSE, N_("<FILE>|layout ID"), N_("load configuration from FILE") },
	{ "-cm","--cache-metadata",      gr_cache_metadata,               FALSE, FALSE, NULL, N_("clean the metadata cache") },
	{ "-cr:", "--cache-render:",    gr_cache_render,        TRUE, FALSE, N_("<folder>  "), N_(" render thumbnails") },
	{ "-crr:", "--cache-render-recurse:", gr_cache_render_recurse, TRUE, FALSE, N_("<folder> "), N_("render thumbnails recursively") },
	{ "-crs:", "--cache-render-shared:", gr_cache_render_standard, TRUE, FALSE, N_("<folder> "), N_(" render thumbnails (see Help)") },
	{ "-crsr:", "--cache-render-shared-recurse:", gr_cache_render_standard_recurse, TRUE, FALSE, N_("<folder>"), N_(" render thumbnails recursively (see Help)") },
	{ "-cs:", "--cache-shared:",    gr_cache_shared,        TRUE, FALSE, N_("clear|clean"), N_("clear or clean shared thumbnail cache") },
	{ "-ct:", "--cache-thumbs:",    gr_cache_thumb,         TRUE, FALSE, N_("clear|clean"), N_("clear or clean thumbnail cache") },
	{ "-d", "--delay=",             gr_slideshow_delay,     TRUE,  FALSE, N_("<[H:][M:][N][.M]>"), N_("set slide show delay to Hrs Mins N.M seconds") },
	{ NULL, "--first",              gr_image_first,         FALSE, FALSE, NULL, N_("first image") },
	{ "-f", "--fullscreen",         gr_fullscreen_toggle,   FALSE, TRUE,  NULL, N_("toggle full screen") },
	{ NULL, "--file:",              gr_file_load,           TRUE,  FALSE, N_("<FILE>|<URL>"), N_("open FILE or URL, bring Geeqie window to the top") },
	{ NULL, "file:",                gr_file_load,           TRUE,  FALSE, N_("<FILE>|<URL>"), N_("open FILE or URL, bring Geeqie window to the top") },
	{ NULL, "--File:",              gr_file_load_no_raise,  TRUE,  FALSE, N_("<FILE>|<URL>"), N_("open FILE or URL, do not bring Geeqie window to the top") },
	{ NULL, "File:",                gr_file_load_no_raise,  TRUE,  FALSE, N_("<FILE>|<URL>"), N_("open FILE or URL, do not bring Geeqie window to the top") },
	{ "-fs","--fullscreen-start",   gr_fullscreen_start,    FALSE, FALSE, NULL, N_("start full screen") },
	{ "-fS","--fullscreen-stop",    gr_fullscreen_stop,     FALSE, FALSE, NULL, N_("stop full screen") },
	{ NULL, "--geometry=",          gr_geometry,            TRUE, FALSE, N_("<GEOMETRY>"), N_("set window geometry") },
	{ NULL, "--get-collection:",    gr_collection,          TRUE,  FALSE, N_("<COLLECTION>"), N_("get collection content") },
	{ NULL, "--get-collection-list", gr_collection_list,    FALSE, FALSE, NULL, N_("get collection list") },
	{ NULL, "--get-destination:",  	gr_get_destination,     TRUE,  FALSE, N_("<FILE>"), N_("get destination path of FILE (See Plugins Configuration)") },
	{ NULL, "--get-file-info",      gr_file_info,           FALSE, FALSE, NULL, N_("get file info") },
	{ NULL, "--get-filelist:",      gr_filelist,            TRUE,  FALSE, N_("[<FOLDER>]"), N_("get list of files and class") },
	{ NULL, "--get-filelist-recurse:", gr_filelist_recurse, TRUE,  FALSE, N_("[<FOLDER>]"), N_("get list of files and class recursive") },
	{ NULL, "--get-rectangle",      gr_rectangle,           FALSE, FALSE, NULL, N_("get rectangle co-ordinates") },
	{ NULL, "--get-render-intent",  gr_render_intent,       FALSE, FALSE, NULL, N_("get render intent") },
	{ NULL, "--get-selection",      gr_get_selection,       FALSE, FALSE, NULL, N_("get list of selected files") },
	{ NULL, "--get-sidecars:",      gr_get_sidecars,        TRUE,  FALSE, N_("<FILE>"), N_("get list of sidecars of FILE") },
	{ NULL, "--id:",                gr_lw_id,               TRUE, FALSE, N_("<ID>"), N_("window id for following commands") },
	{ NULL, "--last",               gr_image_last,          FALSE, FALSE, NULL, N_("last image") },
	{ NULL, "--list-add:",          gr_list_add,            TRUE,  FALSE, N_("<FILE>"), N_("add FILE to command line collection list") },
	{ NULL, "--list-clear",         gr_list_clear,          FALSE, FALSE, NULL, N_("clear command line collection list") },
#ifdef HAVE_LUA
	{ NULL, "--lua:",               gr_lua,                 TRUE, FALSE, N_("<FILE>,<lua script>"), N_("run lua script on FILE") },
#endif
	{ NULL, "--new-window",         gr_new_window,          FALSE, FALSE, NULL, N_("new window") },
	{ "-n", "--next",               gr_image_next,          FALSE, FALSE, NULL, N_("next image") },
	{ NULL, "--pixel-info",         gr_pixel_info,          FALSE, FALSE, NULL, N_("print pixel info of mouse pointer on current image") },
	{ NULL, "--print0",             gr_print0,              TRUE, FALSE, NULL, N_("terminate returned data with null character instead of newline") },
	{ NULL, "--PWD:",               gr_pwd,                 TRUE, FALSE, N_("<PWD>"), N_("use PWD as working directory for following commands") },
	{ "-q", "--quit",               gr_quit,                FALSE, FALSE, NULL, N_("quit") },
	{ NULL, "--raise",              gr_raise,               FALSE, FALSE, NULL, N_("bring the Geeqie window to the top") },
	{ NULL, "raise",                gr_raise,               FALSE, FALSE, NULL, N_("bring the Geeqie window to the top") },
	{ NULL, "--selection-add:",     gr_selection_add,       TRUE,  FALSE, N_("[<FILE>]"), N_("adds the current file (or the specified file) to the current selection") },
	{ "-s", "--slideshow",          gr_slideshow_toggle,    FALSE, TRUE,  NULL, N_("toggle slide show") },
	{ NULL, "--slideshow-recurse:", gr_slideshow_start_rec, TRUE,  FALSE, N_("<FOLDER>"), N_("start recursive slide show in FOLDER") },
	{ "-ss","--slideshow-start",    gr_slideshow_start,     FALSE, FALSE, NULL, N_("start slide show") },
	{ "-sS","--slideshow-stop",     gr_slideshow_stop,      FALSE, FALSE, NULL, N_("stop slide show") },
	{ NULL, "--tell",               gr_file_tell,           FALSE, FALSE, NULL, N_("print filename [and Collection] of current image") },
	{ "+t", "--tools-show",         gr_tools_show,          FALSE, TRUE,  NULL, N_("show tools") },
	{ "-t", "--tools-hide",	        gr_tools_hide,          FALSE, TRUE,  NULL, N_("hide tools") },
	{ NULL, "--view:",              gr_file_view,           TRUE,  FALSE, N_("<FILE>"), N_("open FILE in new window") },
	{ NULL, "view:",                gr_file_view,           TRUE,  FALSE, N_("<FILE>"), N_("open FILE in new window") },
	{ NULL, NULL, NULL, FALSE, FALSE, NULL, NULL }
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

static void remote_cb(RemoteConnection *UNUSED(rc), const gchar *text, GIOChannel *channel, gpointer data)
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

	print_term(FALSE, _("Remote command list:\n"));

	i = 0;
	while (remote_commands[i].func != NULL)
		{
		if (remote_commands[i].description)
			{
			s_opt_param = g_strdup(remote_commands[i].opt_s  ? remote_commands[i].opt_s : "" );
			l_opt_param = g_strconcat(remote_commands[i].opt_l, remote_commands[i].parameter, NULL);
			printf_term(FALSE, "  %-4s %-40s%-s\n",
					s_opt_param,
					l_opt_param,
					_(remote_commands[i].description));
			g_free(s_opt_param);
			g_free(l_opt_param);
			}
		i++;
		}
	printf_term(FALSE, _("\n\n  All other command line parameters are used as plain files if they exist.\n\n  The name of a collection, with or without either path or extension (.gqv) may be used.\n"));
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
		else if (errors && !isname(argv[i]))
			{
			*errors = g_list_append(*errors, argv[i]);
			}
		i++;
		}

	return list;
}

/**
 * @param arg_exec Binary (argv0)
 * @param remote_list Evaluated and recognized remote commands
 * @param path The current path
 * @param cmd_list List of all non collections in Path (gchar *path)
 * @param collection_list List of all collections in argv
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

		printf_term(FALSE, _("Remote %s not running, starting..."), GQ_APPNAME);

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
				/* If Geeqie is not running, stop the --new-window command opening a second window */
				if (g_strcmp0(text, "--new-window") == 0)
					{
					remote_list = g_list_remove(remote_list, text);
					}
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
			if (!rc) print_term(FALSE, ".");
			retry_count--;
			}

		print_term(FALSE, "\n");

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
			gchar *text;

			text = g_strconcat(prefix, work->data, NULL);
			remote_client_send(rc, text);
			g_free(text);
			work = work->next;

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
		print_term(TRUE, _("Remote not available\n"));
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
