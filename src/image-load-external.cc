/*
 * Copyright (C) 2021 - The Geeqie Team
 *
 * Author: Colin Clark
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

#include "image-load-external.h"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "filedata.h"
#include "image-load.h"
#include "intl.h"
#include "ui-fileops.h"

namespace
{

GMutex preview_mutex;
gint foreground_waiters = 0;

struct ImageLoaderExternal : public ImageLoaderBackend
{
public:
	~ImageLoaderExternal() override;

	void init(AreaUpdatedCb area_updated_cb, SizePreparedCb size_prepared_cb, gpointer data) override;
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	GdkPixbuf *get_pixbuf() override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;

private:
	AreaUpdatedCb area_updated_cb;
	gpointer data;

	GdkPixbuf *pixbuf = nullptr;
};

gboolean ImageLoaderExternal::write(const guchar *, gsize &chunk_size, gsize count, GError **error)
{
	auto il = static_cast<ImageLoader *>(data);

	g_autofree gchar *randname = nullptr;
	const int fd = g_file_open_tmp("geeqie_external_preview_XXXXXX", &randname, error);
	if (fd < 0) return FALSE;
	g_close(fd, nullptr);
	const gchar *argv[] = {il->external_extract, il->fd->path, randname, nullptr};
	g_autoptr(GError) command_error = nullptr;
	const int status = image_loader_run_external(il, argv, &command_error);
	// Preserve the script contract: usable output is accepted regardless of exit status.
	if (status >= 0) pixbuf = gdk_pixbuf_new_from_file(randname, error);
	unlink_file(randname);
	if (!pixbuf)
		{
		if (command_error && (!error || !*error)) g_propagate_error(error, g_steal_pointer(&command_error));
		return FALSE;
		}

	area_updated_cb(nullptr, 0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), data);

	chunk_size = count;
	return TRUE;
}

void ImageLoaderExternal::init(AreaUpdatedCb area_updated_cb, SizePreparedCb, gpointer data)
{
	this->area_updated_cb = area_updated_cb;
	this->data = data;
}

GdkPixbuf *ImageLoaderExternal::get_pixbuf()
{
	return pixbuf;
}

gchar *ImageLoaderExternal::get_format_name()
{
	return g_strdup("external");
}

gchar **ImageLoaderExternal::get_format_mime_types()
{
	static const gchar *mime[] = {"application/octet-stream", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

ImageLoaderExternal::~ImageLoaderExternal()
{
	if (pixbuf) g_object_unref(pixbuf);
}

} // namespace

gint image_loader_run_external(ImageLoader *il, const gchar *const *argv, GError **error)
{
	if (!argv || !argv[0] || !*argv[0])
		{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("External preview command is not configured"));
		return -1;
		}
	// Foreground requests take precedence over queued thumbnail work.
	struct Ticket
	{
		bool foreground;
		explicit Ticket(bool foreground) : foreground(foreground)
			{
			if (foreground) g_atomic_int_inc(&foreground_waiters);
			}
		~Ticket()
			{
			if (foreground) g_atomic_int_add(&foreground_waiters, -1);
			}
	} ticket(il->idle_priority <= G_PRIORITY_DEFAULT_IDLE);
	while ((!ticket.foreground && g_atomic_int_get(&foreground_waiters) > 0) || !g_mutex_trylock(&preview_mutex))
		{
		if (g_cancellable_set_error_if_cancelled(il->cancellable, error)) return -1;
		g_usleep(5000);
		}
	struct Unlock
	{
		~Unlock() { g_mutex_unlock(&preview_mutex); }
	} unlock;
	if (g_cancellable_set_error_if_cancelled(il->cancellable, error)) return -1;

	GPid pid = 0;
	if (!g_spawn_async(nullptr, const_cast<gchar **>(argv), nullptr,
	                   static_cast<GSpawnFlags>(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH),
	                   [](gpointer) { setpgid(0, 0); }, nullptr, &pid, error)) return -1;

	int status = 0;
	while (true)
		{
		if (g_cancellable_is_cancelled(il->cancellable))
			{
			// Keep the child unreaped until both signals have been sent, pinning its process-group ID.
			kill(-pid, SIGTERM);
			g_usleep(100000);
			kill(-pid, SIGKILL);
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
			g_spawn_close_pid(pid);
			g_cancellable_set_error_if_cancelled(il->cancellable, error);
			return -1;
			}
		const auto result = waitpid(pid, &status, WNOHANG);
		if (result == pid) break;
		if (result < 0 && errno != EINTR)
			{
			g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "%s", g_strerror(errno));
			g_spawn_close_pid(pid);
			return -1;
			}
		g_usleep(5000);
		}
	g_spawn_close_pid(pid);
	if (g_spawn_check_wait_status(status, error)) return 0;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_external()
{
	return std::make_unique<ImageLoaderExternal>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
