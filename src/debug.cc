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

#include <sys/resource.h>
#include <sys/time.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>

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

/*
 * Logging functions
 */
static gchar *regexp = nullptr;

static gboolean log_msg_cb(gpointer data)
{
	auto buf = static_cast<gchar *>(data);
	log_window_append(buf, LOG_MSG);
	g_free(buf);
	return G_SOURCE_REMOVE;
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
	g_autofree gchar *buf_casefold = g_utf8_casefold(buf, -1);
	g_autofree gchar *error_casefold = g_utf8_casefold(_("error"), -1);
	g_autofree gchar *warning_casefold = g_utf8_casefold(_("warning"), -1);

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
	return G_SOURCE_REMOVE;
}

static void log_domain_print_message(const gchar *domain, const gchar *buf)
{
	gchar *buf_nl = g_strconcat(buf, "\n", NULL);

	if (regexp && command_line &&
	    !g_regex_match_simple(regexp, buf_nl, static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0)))
		{
		g_free(buf_nl);
		return;
		}

	print_term(false, buf_nl);

	if (strcmp(domain, DOMAIN_INFO) == 0)
		g_idle_add(log_normal_cb, buf_nl);
	else
		g_idle_add(log_msg_cb, buf_nl);
}

void log_domain_print_debug(const gchar *domain, const gchar *file_name, int line_number, const gchar *function_name, const gchar *format, ...)
{
	va_list ap;

	va_start(ap, format);
	g_autofree gchar *message = g_strdup_vprintf(format, ap);
	va_end(ap);

	g_autofree gchar *location = nullptr;
	if (options && options->log_window.timer_data)
		{
		location = g_strdup_printf("%s:%s:%d:%s:", get_exec_time(), file_name, line_number, function_name);
		}
	else
		{
		location = g_strdup_printf("%s:%d:%s:", file_name, line_number, function_name);
		}

	g_autofree gchar *buf = g_strconcat(location, message, NULL);
	log_domain_print_message(domain, buf);
}

void log_domain_printf(const gchar *domain, const gchar *format, ...)
{
	va_list ap;

	va_start(ap, format);
	g_autofree gchar *buf = g_strdup_vprintf(format, ap);
	va_end(ap);

	log_domain_print_message(domain, buf);
}

void print_term(bool err, const gchar *text_utf8)
{
	g_autofree gchar *text_l = g_locale_from_utf8(text_utf8, -1, nullptr, nullptr, nullptr);
	const gchar *text = text_l ? text_l : text_utf8;

	fputs(text, err ? stderr : stdout);

	if (command_line && command_line->log_file)
		{
		std::ofstream log(command_line->log_file, std::ios::app);
		log << text_utf8 << std::endl; // NOLINT(performance-avoid-endl)
		}
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
	debug_level = std::clamp(new_level, DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX);
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
		gint nsec = ((y->tv_usec - x->tv_usec) / 1000000) + 1;
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
	g_free(regexp);
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

static std::string extract_function_name(const std::string &input)
{
	/* String may be of style:
	 * (anonymous namespace)::osd_tag_button_new((anonymous namespace)::OsdTag const&, _GtkWidget*) at \
	 *  /home/<project src>/build/../src/osd.cc:137
	 * or:
	 * osd_new(int, _GtkWidget*) at /home/<project src>/build/../src/osd.cc:319 (discriminator 1)
	 *
	 * Only the function name is required
	 */

	/* Strip everything after " at "
	 */
	size_t at_pos = input.find(" at ");
	std::string lhs = (at_pos == std::string::npos) ? input : input.substr(0, at_pos);

	/* Regex to match: optional namespaces + function name + '('
	 * e.g.  (anonymous namespace)::osd_tag_button_new(
	 *        osd_new(
	 */
	std::regex re(R"((?:[\w\(\) ]+::)*(\w+)\s*\()");
	std::smatch m;

	if (std::regex_search(lhs, m, re))
		{
		return m[1].str();   // capture group 1 = function name only
		}

	return "";
}

void log_print_backtrace(const gchar *file, gint line, const gchar *function)
{
	FILE *fp;
	char **bt_syms;
	char path[2048];
	gchar *paren_end;
	gchar *paren_start;
	gint bt_size;
	gint i;
	void *bt[1024];

	if (runcmd(reinterpret_cast<const gchar *>("which addr2line >/dev/null 2>&1")) == 0)
		{
		g_autofree gchar *exe_path = g_path_get_dirname(gq_executable_path);
		bt_size = backtrace(bt, 1024);
		bt_syms = backtrace_symbols(bt, bt_size);

		log_printf("\nBacktrace start");
		log_printf("%s/../%s:%d %s\n", exe_path, file, line, function);

		/* Last item is always "??:?", so ignore it */
		for (i = 1; i < bt_size - 1; i++)
			{
			if (strstr(bt_syms[i], GQ_APPNAME_LC))
				{
				paren_start = g_strstr_len(bt_syms[i], -1, "(");
				paren_end = g_strstr_len(bt_syms[i], -1, ")");
				g_autofree gchar *address_offset = g_strndup(paren_start + 1, paren_end - paren_start - 1);

				g_autofree gchar *cmd_line = g_strconcat("addr2line -p -f -C -e ", gq_executable_path, " ", address_offset, NULL);

				fp = popen(cmd_line, "r");
				if (fp == nullptr)
					{
					log_printf("Failed to run command: %s", cmd_line);
					}
				else
					{
					while (fgets(path, sizeof(path), fp) != nullptr)
						{
						if (path[0] == '\0') continue;

						/* Remove redundant newline */
						const size_t path_len = strlen(path) - 1;
						path[path_len] = '\0';

						gchar *paren = g_strstr_len(path, path_len, "(");
						g_autofree gchar *function_name = paren ? g_strndup(path, paren - path) : g_strdup("");

						log_printf("\n%s", g_strstr_len(path, -1, "at ") + 3);
						log_printf("%s", extract_function_name(path).c_str());

						std::string line = path;

						size_t at_pos = line.find("at ");
						if (at_pos != std::string::npos)
							{
							at_pos += 3; // move past "at "

							size_t end_pos = line.find(' ', at_pos);
							if (end_pos == std::string::npos)
								end_pos = line.length();

							std::string extracted = line.substr(at_pos, end_pos - at_pos);

							// Step 1: Split into path and line number
							size_t colon_pos = extracted.rfind(':');
							if (colon_pos == std::string::npos)
								{
								std::cerr << _("Invalid input format\n");
								return ;
								}

							std::string path = extracted.substr(0, colon_pos);
							int line_number = std::stoi(extracted.substr(colon_pos + 1));

							// Step 2: Open the file
							std::ifstream file(path);
							if (!file)
								{
								std::cerr << _("Failed to open file: ") << path << "\n";
								return ;
								}

							// Step 3: Read all lines
							std::vector<std::string> lines;
							std::string line;
							while (std::getline(file, line))
								{
								lines.push_back(line);
								}

							// Step 4: Print 4 lines before and after the target line
							int start = std::max(0, line_number - 5); // line_number is 1-based
							int end = std::min(static_cast<int>(lines.size()), line_number + 4);

							for (int i = start; i < end; ++i)
								{
								// Highlight the target line
								if (i == line_number - 1)
									{
									log_printf(">%d | %s", i + 1, lines[i].c_str());
									}
								else
									{
									log_printf(" %d | %s", i + 1, lines[i].c_str());
									}
								}
							}
						}
					}

				pclose(fp);
				}
			}
		log_printf("Backtrace end");

		free(bt_syms);
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
void log_print_file_data_dump(const gchar *file, gint line, const gchar *function)
{
	g_autofree gchar *exe_path = g_path_get_dirname(gq_executable_path);

	log_printf("FileData dump start");
	log_printf("%s/../%s:%d %s\n", exe_path, file, line, function);

	file_data_dump();

	log_printf("FileData dump end");
}

/**
 * @brief Print memory usage and runtime
 * @param file
 * @param line
 * @param function
 *
 */
void log_print_ru(const gchar *file, gint line, const gchar *function)
{
	struct rusage r_usage;

	getrusage(RUSAGE_SELF,&r_usage);

	log_printf("%s:%s:%d kB:%ld", file, function, line, r_usage.ru_maxrss);
	log_printf("%s:%s:%d secs:%ld", file, function, line, r_usage.ru_utime.tv_sec);
}

#endif /* DEBUG */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
