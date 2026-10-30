/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DATE_TIME_H
#define DATE_TIME_H

#include <ctime>

#include <glib.h>

/** Validate a UTF-8 format containing POSIX strftime conversions. Empty selects the locale default. */
bool date_time_format_is_valid(const gchar *format);
/** Format wall-clock fields without timezone conversion. Returned UTF-8 text is owned by the caller.
 * nullptr selects the configured format; empty or invalid formats use the locale default.
 * Optional fractional seconds are appended to conversions containing seconds.
 */
gchar *date_time_format(const std::tm &time, const gchar *format = nullptr, const gchar *subseconds = nullptr);
/** Format a Unix timestamp in the local timezone. */
gchar *date_time_format(time_t timestamp, const gchar *format = nullptr);

#endif
