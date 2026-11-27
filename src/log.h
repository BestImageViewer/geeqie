/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef GQ_LOG_H
#define GQ_LOG_H

#include <glib.h>

#include "logwindow.h"

/** @brief Message severity, independent of debug categories and verbosity. */
enum class LogSeverity
{
	INFO,
	WARNING,
	ERROR
};

/** @brief Always available, including builds without debug logging.
 * Warnings and errors bypass debug and regex filters and use standard error.
 * Info messages use standard output and respect the regex filter.
 * All three also use the configured log file and the bounded log-window queue.
 */
void log_info(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
void log_warning(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
void log_error(const gchar *format, ...) G_GNUC_PRINTF(1, 2);

/** @return Newly allocated severity-labeled text, including a final newline. */
gchar *format_log_record(LogSeverity severity, const gchar *message);
LogType log_severity_type(LogSeverity severity);

#endif
