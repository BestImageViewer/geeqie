/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "log-capture-file.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <glib/gstdio.h>

#include "intl.h"

namespace
{
constexpr const char stop_marker[] = "\n# Capture stopped\n"; // NOTRANSLATE: Capture marker.
constexpr const char limit_marker[] = "\n# Capture stopped: size limit reached\n"; // NOTRANSLATE: Capture marker.
constexpr std::size_t footer_reserve = std::max(sizeof(stop_marker), sizeof(limit_marker)) - 1;

void capture_error(GError **error, int code)
{
	g_set_error_literal(error, G_FILE_ERROR, g_file_error_from_errno(code), g_strerror(code));
}
} // namespace

LogCaptureFile::LogCaptureFile()
	: LogCaptureFile({g_mkstemp_full, ::write, ::close})
{}

LogCaptureFile::LogCaptureFile(Io operations)
	: io(operations)
{}

LogCaptureFile::~LogCaptureFile()
{
	close_file();
}

bool LogCaptureFile::write_all(const gchar *text)
{
	std::size_t remaining = strlen(text);
	while (remaining != 0)
		{
		const ssize_t written = io.write(fd, text, remaining);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0)
			{
			last_error = written == 0 ? EIO : errno;
			return false;
			}
		text += written;
		remaining -= written;
		bytes += written;
		}
	return true;
}

void LogCaptureFile::close_file()
{
	if (fd < 0) return;
	if (io.close(fd) != 0 && !last_error) last_error = errno;
	fd = -1;
}

bool LogCaptureFile::start(const gchar *directory, const gchar *header, std::size_t max_bytes, GError **error)
{
	if (active())
		{
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_EXIST, _("A log capture is already running"));
		return false;
		}
	if (max_bytes < footer_reserve || strlen(header) > max_bytes - footer_reserve)
		{
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOSPC, _("The capture header exceeds the size limit"));
		return false;
		}
	g_autofree gchar *path = g_build_filename(directory, "geeqie-debug-XXXXXX.txt", nullptr);
	fd = io.create(path, O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0)
		{
		capture_error(error, errno);
		return false;
		}
	bytes = 0;
	limit = max_bytes;
	const int previous_error = last_error;
	const bool previous_limited = limited;
	last_error = 0;
	limited = false;
	if (!write_all(header))
		{
		close_file();
		g_unlink(path);
		capture_error(error, last_error);
		last_error = previous_error;
		limited = previous_limited;
		return false;
		}
	filename = path;
	return true;
}

bool LogCaptureFile::append(const gchar *text)
{
	if (!active()) return false;
	if (strlen(text) > limit - footer_reserve - bytes)
		{
		limited = true;
		write_all(limit_marker);
		close_file();
		return false;
		}
	if (write_all(text)) return true;
	close_file();
	return false;
}

bool LogCaptureFile::stop(GError **error)
{
	if (active())
		{
		write_all(stop_marker);
		close_file();
		}
	if (last_error)
		{
		capture_error(error, last_error);
		return false;
		}
	return true;
}
