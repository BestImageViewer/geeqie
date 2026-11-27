/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef GQ_LOG_CAPTURE_H
#define GQ_LOG_CAPTURE_H

#include <cstddef>
#include <string>

#include <glib.h>

inline constexpr std::size_t log_capture_max_bytes = 100 * 1024 * 1024;

/** @brief Snapshot of the active logging controls, for capture headers. */
std::string log_capture_settings();
/** @brief Create a private unique capture in the temporary directory.
 * Existing command-line log output is independent of this additional sink.
 * @param diagnostics Report text generated on the main thread
 */
bool log_capture_start(const gchar *diagnostics, GError **error);
bool log_capture_stop(GError **error);
bool log_capture_active();
bool log_capture_limit_reached();
/** @brief Record a setting immediately; never calls back into its getter. */
void log_capture_record_setting(const gchar *name, const gchar *value);
/** @return Newly allocated last capture path, or nullptr before first capture. */
gchar *log_capture_get_path();
/** @return Newly allocated write/close error, or nullptr when successful. */
gchar *log_capture_get_error();
/** @brief Append a complete record safely from any thread; never logs recursively. */
void log_capture_write(const gchar *text);

#endif
