/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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
#include "debug.h"

#include "logwindow.h"
#include "ui-fileops.h"

#include <glib/gprintf.h>
#include <regex.h>

/*
 * Logging functions
 */
static gchar *regexp = NULL;

static gboolean log_msg_cb(gpointer data)
{
	gchar *buf = data;
	log_window_append(buf, LOG_MSG);
	g_free(buf);
	return FALSE;
}

/**
 * @brief Appends a user information message to the log window queue
 * @param data The message
 * @returns FALSE
 * 
 * If the first word of the message is either "error" or "warning"
 * (case insensitive) the message is color-coded appropriately
 */
static gboolean log_normal_cb(gpointer data)
{
	gchar *buf = data;
	gchar *buf_casefold = g_utf8_casefold(buf, -1);
	gchar *error_casefold = g_utf8_casefold(_("error"), -1);
	gchar *warning_casefold = g_utf8_casefold(_("warning"), -1);

	if (buf_casefold == g_strstr_len(buf_casefold, -1, error_casefold))
		{
		log_window_append(buf, LOG_ERROR);
		}
	else if (buf_casefold == g_strstr_len(buf_casefold, -1, warning_casefold))
		{
		log_window_append(buf, LOG_WARN);
		}
	else
		{
		log_window_append(buf, LOG_NORMAL);
		}

	g_free(buf);
	g_free(buf_casefold);
	g_free(error_casefold);
	g_free(warning_casefold);
	return FALSE;
}

void log_domain_print_message(const gchar *domain, gchar *buf)
{
	gchar *buf_nl;
	regex_t regex;
	gint ret_comp, ret_exec;

	buf_nl = g_strconcat(buf, "\n", NULL);

	if (regexp && command_line)
		{
			ret_comp = regcomp(&regex, regexp, 0);
			if (!ret_comp)
				{
				ret_exec = regexec(&regex, buf_nl, 0, NULL, 0);

				if (!ret_exec)
					{
					print_term(FALSE, buf_nl);
					if (strcmp(domain, DOMAIN_INFO) == 0)
						g_idle_add(log_normal_cb, buf_nl);
					else
						g_idle_add(log_msg_cb, buf_nl);
					}
				regfree(&regex);
				}
		}
	else
		{
		print_term(FALSE, buf_nl);
		if (strcmp(domain, DOMAIN_INFO) == 0)
			g_idle_add(log_normal_cb, buf_nl);
		else
			g_idle_add(log_msg_cb, buf_nl);
		}
	g_free(buf);
}

void log_domain_print_debug(const gchar *domain, const gchar *file_name, const gchar *function_name,
									int line_number, const gchar *format, ...)
{
	va_list ap;
	gchar *message;
	gchar *location;
	gchar *buf;

	va_start(ap, format);
	message = g_strdup_vprintf(format, ap);
	va_end(ap);

	if (options && options->log_window.timer_data)
		{
		location = g_strdup_printf("%s:%s:%s:%d:", get_exec_time(), file_name,
												function_name, line_number);
		}
	else
		{
		location = g_strdup_printf("%s:%s:%d:", file_name, function_name, line_number);
		}

	buf = g_strconcat(location, message, NULL);
	log_domain_print_message(domain,buf);
	g_free(location);
	g_free(message);
}

void log_domain_printf(const gchar *domain, const gchar *format, ...)
{
	va_list ap;
	gchar *buf;

	va_start(ap, format);
	buf = g_strdup_vprintf(format, ap);
	va_end(ap);

	log_domain_print_message(domain, buf);
}

/*
 * Debugging only functions
 */

#ifdef DEBUG

static gint debug_level = DEBUG_LEVEL_MIN;


gint get_debug_level(void)
{
	return debug_level;
}

void set_debug_level(gint new_level)
{
	debug_level = CLAMP(new_level, DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX);
}

void debug_level_add(gint delta)
{
	set_debug_level(debug_level + delta);
}

gint required_debug_level(gint level)
{
	return (debug_level >= level);
}

static gint timeval_delta(struct timeval *result, struct timeval *x, struct timeval *y)
{
	if (x->tv_usec < y->tv_usec)
		{
		gint nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
		}

	if (x->tv_usec - y->tv_usec > 1000000)
		{
		gint nsec = (x->tv_usec - y->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;
	}

	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;

	return x->tv_sec < y->tv_sec;
}

const gchar *get_exec_time(void)
{
	static gchar timestr[30];
	static struct timeval start_tv = {0, 0};
	static struct timeval previous = {0, 0};
	static gint started = 0;

	struct timeval tv = {0, 0};
	static struct timeval delta = {0, 0};

	gettimeofday(&tv, NULL);

	if (start_tv.tv_sec == 0) start_tv = tv;

	tv.tv_sec -= start_tv.tv_sec;
	if (tv.tv_usec >= start_tv.tv_usec)
		tv.tv_usec -= start_tv.tv_usec;
	else
		{
		tv.tv_usec += 1000000 - start_tv.tv_usec;
		tv.tv_sec -= 1;
		}

	if (started) timeval_delta(&delta, &tv, &previous);

	previous = tv;
	started = 1;

	g_snprintf(timestr, sizeof(timestr), "%5d.%06d (+%05d.%06d)", (gint)tv.tv_sec, (gint)tv.tv_usec, (gint)delta.tv_sec, (gint)delta.tv_usec);

	return timestr;
}

void init_exec_time(void)
{
	get_exec_time();
}

void set_regexp(gchar *cmd_regexp)
{
	regexp = g_strdup(cmd_regexp);
}

gchar *get_regexp(void)
{
	return g_strdup(regexp);
}

#endif /* DEBUG */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
