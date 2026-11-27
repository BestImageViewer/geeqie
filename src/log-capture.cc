/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "log-capture.h"

#include <atomic>
#include <sstream>

#include "debug.h"
#include "log-capture-file.h"

namespace
{
GMutex capture_mutex;
std::atomic<bool> capture_running{false};
LogCaptureFile capture_file;

void publish_state()
{
	capture_running.store(capture_file.active(), std::memory_order_relaxed);
}
} // namespace

std::string log_capture_settings()
{
	std::ostringstream out;
#ifdef DEBUG
	g_autofree gchar *domains = get_debug_domains();
	g_autofree gchar *regexp = get_regexp();
	out << "debug_level=" << get_debug_level() << '\n'; // NOTRANSLATE: Capture field.
	out << "debug_domains=" << domains << '\n'; // NOTRANSLATE: Capture field.
	out << "debug_source=" << get_debug_source() << '\n'; // NOTRANSLATE: Capture field.
	out << "debug_timing_delta=" << get_debug_timing() << '\n'; // NOTRANSLATE: Capture field.
	out << "debug_regexp=" << (regexp ? regexp : "") << '\n'; // NOTRANSLATE: Capture field.
#else
	out << "debug_logging=unavailable\n"; // NOTRANSLATE: Capture field.
#endif
	return out.str();
}

bool log_capture_start(const gchar *diagnostics, GError **error)
{
	const auto settings = log_capture_settings();
	g_autoptr(GDateTime) now = g_date_time_new_now_utc();
	g_autofree gchar *timestamp = g_date_time_format_iso8601(now);
	g_autofree gchar *header = g_strdup_printf("Geeqie debug capture\nstarted_utc=%s\nsize_limit_bytes=%zu\n%s\n%s\n--- Log messages ---\n", timestamp, log_capture_max_bytes, settings.c_str(), diagnostics ? diagnostics : ""); // NOTRANSLATE: Capture header.
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&capture_mutex);
	const bool started = capture_file.start(g_get_tmp_dir(), header, log_capture_max_bytes, error);
	publish_state();
	return started;
}

bool log_capture_stop(GError **error)
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&capture_mutex);
	const bool stopped = capture_file.stop(error);
	publish_state();
	return stopped;
}

bool log_capture_active()
{
	return capture_running.load(std::memory_order_relaxed);
}

bool log_capture_limit_reached()
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&capture_mutex);
	return capture_file.limit_reached();
}

gchar *log_capture_get_path()
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&capture_mutex);
	return capture_file.path().empty() ? nullptr : g_strdup(capture_file.path().c_str());
}

gchar *log_capture_get_error()
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&capture_mutex);
	return capture_file.error_code() ? g_strdup(g_strerror(capture_file.error_code())) : nullptr;
}

void log_capture_write(const gchar *text)
{
	if (!log_capture_active()) return;
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&capture_mutex);
	capture_file.append(text);
	publish_state();
}

void log_capture_record_setting(const gchar *name, const gchar *value)
{
	if (!log_capture_active()) return;
	g_autofree gchar *escaped = g_strescape(value ? value : "", nullptr);
	g_autoptr(GDateTime) now = g_date_time_new_now_utc();
	g_autofree gchar *timestamp = g_date_time_format_iso8601(now);
	g_autofree gchar *record = g_strdup_printf("\n# Logging settings changed\nchanged_utc=%s\n%s=%s\n", timestamp, name, escaped); // NOTRANSLATE: Capture fields.
	log_capture_write(record);
}
