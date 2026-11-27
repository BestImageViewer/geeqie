/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef GQ_LOG_CAPTURE_FILE_H
#define GQ_LOG_CAPTURE_FILE_H

#include <sys/types.h>

#include <cstddef>
#include <string>

#include <glib.h>

/** @brief Bounded capture file; the owner must synchronize access.
 * The I/O interface allows deterministic failure tests without changing process
 * limits, filling a filesystem, or modifying the application's global capture.
 */
class LogCaptureFile
{
public:
	struct Io
	{
		int (*create)(gchar *, int, int);
		ssize_t (*write)(int, const void *, std::size_t);
		int (*close)(int);
	};

	LogCaptureFile();
	explicit LogCaptureFile(Io operations);
	~LogCaptureFile();
	LogCaptureFile(const LogCaptureFile &) = delete;
	LogCaptureFile &operator=(const LogCaptureFile &) = delete;

	bool start(const gchar *directory, const gchar *header, std::size_t max_bytes, GError **error);
	bool append(const gchar *text);
	bool stop(GError **error);
	bool active() const { return fd >= 0; }
	bool limit_reached() const { return limited; }
	int error_code() const { return last_error; }
	const std::string &path() const { return filename; }

private:
	bool write_all(const gchar *text);
	void close_file();
	Io io;
	int fd = -1;
	std::string filename;
	std::size_t bytes = 0;
	std::size_t limit = 0;
	int last_error = 0;
	bool limited = false;
};

#endif
