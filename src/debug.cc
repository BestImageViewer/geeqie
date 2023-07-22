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

#include "debug.h"

#include <regex.h>
#include <sys/time.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <config.h>

#if HAVE_EXECINFO_H
#  include <execinfo.h>
#endif

#include "filedata.h"
#include "intl.h"
#include "logwindow.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"

/*
 * Logging functions
 */
static gchar *regexp = nullptr;

static gboolean log_msg_cb(gpointer data)
{
	auto buf = static_cast<gchar *>(data);
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
	auto buf = static_cast<gchar *>(data);
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
	gint ret_comp;
	gint ret_exec;

	buf_nl = g_strconcat(buf, "\n", NULL);

	if (regexp && command_line)
		{
			ret_comp = regcomp(&regex, regexp, 0);
			if (!ret_comp)
				{
				ret_exec = regexec(&regex, buf_nl, 0, nullptr, 0);

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

void log_domain_print_debug(const gchar *domain, const gchar *file_name, int line_number, const gchar *function_name, const gchar *format, ...)
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
		location = g_strdup_printf("%s:%s:%d:%s:", get_exec_time(), file_name, line_number, function_name);
		}
	else
		{
		location = g_strdup_printf("%s:%d:%s:", file_name, line_number, function_name);
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


gint get_debug_level()
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

const gchar *get_exec_time()
{
	static gchar timestr[30];
	static struct timeval start_tv = {0, 0};
	static struct timeval previous = {0, 0};
	static gint started = 0;

	struct timeval tv = {0, 0};
	static struct timeval delta = {0, 0};

	gettimeofday(&tv, nullptr);

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

	g_snprintf(timestr, sizeof(timestr), "%5d.%06d (+%05d.%06d)", static_cast<gint>(tv.tv_sec), static_cast<gint>(tv.tv_usec), static_cast<gint>(delta.tv_sec), static_cast<gint>(delta.tv_usec));

	return timestr;
}

void init_exec_time()
{
	get_exec_time();
}

void set_regexp(const gchar *cmd_regexp)
{
	regexp = g_strdup(cmd_regexp);
}

gchar *get_regexp()
{
	return g_strdup(regexp);
}

#if HAVE_EXECINFO_H
/**
 * @brief Backtrace of geeqie files
 * @param file
 * @param function
 * @param line
 *
 * Requires command line program addr2line \n
 * Prints the contents of the backtrace buffer for Geeqie files. \n
 * Format printed is: \n
 * <full path to source file>:<line number>
 *
 * The log window F1 command and Edit/Preferences/Behavior/Log Window F1
 * Command may be used to open an editor at a backtrace location.
 */
void log_print_backtrace(const gchar *file, gint line, const gchar *function)
{
	FILE *fp;
	char **bt_syms;
	char path[2048];
	gchar *address_offset;
	gchar *cmd_line;
	gchar *exe_path;
	gchar *function_name = nullptr;
	gchar *paren_end;
	gchar *paren_start;
	gint bt_size;
	gint i;
	void *bt[1024];

	if (runcmd(reinterpret_cast<const gchar *>("which addr2line >/dev/null 2>&1")) == 0)
		{
		exe_path = g_path_get_dirname(gq_executable_path);
		bt_size = backtrace(bt, 1024);
		bt_syms = backtrace_symbols(bt, bt_size);

		log_printf("Backtrace start");
		log_printf("%s/../%s:%d %s\n", exe_path, file, line, function);

		/* Last item is always "??:?", so ignore it */
		for (i = 1; i < bt_size - 1; i++)
			{
			if (strstr(bt_syms[i], GQ_APPNAME_LC))
				{
				paren_start = g_strstr_len(bt_syms[i], -1, "(");
				paren_end = g_strstr_len(bt_syms[i], -1, ")");
				address_offset = g_strndup(paren_start + 1, paren_end - paren_start - 1);

				cmd_line = g_strconcat("addr2line -p -f -C -e ", gq_executable_path, " ", address_offset, NULL);

				fp = popen(cmd_line, "r");
				if (fp == nullptr)
					{
					log_printf("Failed to run command: %s", cmd_line);
					}
				else
					{
					while (fgets(path, sizeof(path), fp) != nullptr)
						{
						/* Remove redundant newline */
						path[strlen(path) - 1] = '\0';

						if (g_strstr_len(path, strlen(path), "(") != nullptr)
							{
							function_name = g_strndup(path, g_strstr_len(path, strlen(path), "(") - path);
							}
						else
							{
							function_name = g_strdup("");
							}
						log_printf("%s %s", g_strstr_len(path, -1, "at ") + 3, function_name);

						g_free(function_name);
						}
					}

				pclose(fp);

				g_free(address_offset);
				g_free(cmd_line);
				}
			}
		log_printf("Backtrace end");

		free(bt_syms);
		g_free(exe_path);
		}
}
#else
void log_print_backtrace(const gchar *, gint, const gchar *)
{
}
#endif

/**
 * @brief Print ref. count and image name
 * @param file
 * @param function
 * @param line
 *
 * Print image ref. count and full path name of all images in
 * the file_data_pool.
 */
void log_print_file_data_dump(const gchar *file, const gchar *function, gint line)
{
	gchar *exe_path;

	exe_path = g_path_get_dirname(gq_executable_path);

	log_printf("FileData dump start");
	log_printf("%s/../%s:%d %s\n", exe_path, file, line, function);

	file_data_dump();

	log_printf("FileData dump end");

	g_free(exe_path);
}

#endif /* DEBUG */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
