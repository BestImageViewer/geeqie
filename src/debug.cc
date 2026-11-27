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
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

#if HAVE_EXECINFO_H
#  include <execinfo.h>
#endif

#include "filedata.h"
#include "intl.h"
#include "log-queue.h"
#include "logwindow.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"

/*
 * Logging functions
 */
static gchar *regexp = nullptr;
static GMutex filter_mutex;
static GMutex output_mutex;

static bool log_matches_filter(const gchar *text)
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&filter_mutex);
	return !regexp || !command_line ||
	       g_regex_match_simple(regexp, text, static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0));
}

static bool log_file_active()
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&output_mutex);
	return command_line && command_line->log_file;
}

void log_set_file(const gchar *path)
{
	g_autofree gchar *copy = g_strdup(path);
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&output_mutex);
	if (!command_line) return;
	g_free(command_line->log_file);
	command_line->log_file = g_steal_pointer(&copy);
}

static LogQueue log_queue;
static GMutex queue_source_mutex;
static guint queue_source = 0;

static LogType log_normal_type(const gchar *buf)
{
	g_autofree gchar *buf_casefold = g_utf8_casefold(buf, -1);
	g_autofree gchar *error_casefold = g_utf8_casefold(_("error"), -1);
	g_autofree gchar *warning_casefold = g_utf8_casefold(_("warning"), -1);
	LogType type = LOG_NORMAL;
	if (g_str_has_prefix(buf_casefold, error_casefold)) type = LOG_ERROR;
	else if (g_str_has_prefix(buf_casefold, warning_casefold)) type = LOG_WARN;
	return type;
}

static gboolean log_queue_cb(gpointer)
{
	const auto batch = log_queue.take_batch();
	std::string text;
	LogType type = LOG_MSG;
	for (const auto &message : batch.messages)
		{
		const LogType message_type = message.type;
		if (message_type != type && !text.empty())
			{
			log_window_append(text.c_str(), type);
			text.clear();
			}
		type = message_type;
		text += message.text;
		}
	if (!text.empty()) log_window_append(text.c_str(), type);
	if (batch.dropped != 0)
		{
		g_autofree gchar *notice = g_strdup_printf(_("Log window overloaded: %zu messages omitted. Terminal and file output are unaffected.\n"), batch.dropped);
		log_window_append(notice, LOG_WARN);
		}
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&queue_source_mutex);
	if (log_queue.finish_batch()) return G_SOURCE_CONTINUE;
	queue_source = 0;
	return G_SOURCE_REMOVE;
}

void log_queue_shutdown()
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&queue_source_mutex);
	log_queue.close();
	if (queue_source != 0)
		{
		g_source_remove(queue_source);
		queue_source = 0;
		}
}

static void log_enqueue(const gchar *text, LogType type)
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&queue_source_mutex);
	if (log_queue.push(text, type))
		{
		queue_source = g_idle_add(log_queue_cb, nullptr);
		}
}

static void log_domain_print_message(const gchar *domain, const gchar *buf)
{
	g_autofree gchar *buf_nl = g_strconcat(buf, "\n", nullptr);

	if (!log_matches_filter(buf_nl))
		{
		return;
		}

	if (strcmp(domain, DOMAIN_DEBUG) == 0 || log_file_active())
		{
		print_term(false, buf_nl);
		}

	log_enqueue(buf_nl, strcmp(domain, DOMAIN_INFO) == 0 ? log_normal_type(buf_nl) : LOG_MSG);
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

	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&output_mutex);
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

namespace
{

constexpr const gchar *debug_category_names[] = {"general", "thumbnails", "cache", "image", "render", "metadata", "files", "ui", "duplicates", "config", "collections", "external"}; // NOTRANSLATE: Debug domain identifiers.
static_assert(G_N_ELEMENTS(debug_category_names) == static_cast<guint>(DebugCategory::COUNT));
constexpr guint all_debug_domains = (1U << G_N_ELEMENTS(debug_category_names)) - 1;
std::atomic<guint> debug_domains{all_debug_domains};
std::atomic<gint> debug_level{DEBUG_LEVEL_MIN};
GMutex settings_mutex;
std::atomic<bool> debug_source{false};
std::atomic<bool> debug_timing{true};
std::atomic<guint> next_thread_id{1};

gint64 debug_elapsed_time()
{
	static const gint64 start = g_get_monotonic_time();
	return g_get_monotonic_time() - start;
}

bool parse_debug_domains(const gchar *domains, guint &mask)
{
	if (!domains) return false;
	g_autofree gchar *text = g_strdup(domains);
	g_strstrip(text);
	if (g_strcmp0(text, "all") == 0 || g_strcmp0(text, "none") == 0)
		{
		mask = g_strcmp0(text, "all") == 0 ? all_debug_domains : 0;
		return true;
		}

	mask = 0;
	g_auto(GStrv) names = g_strsplit(text, ",", -1);
	for (gchar **name = names; *name; ++name)
		{
		g_strstrip(*name);
		guint index = 0;
		while (index < G_N_ELEMENTS(debug_category_names) &&
		       g_strcmp0(*name, debug_category_names[index]) != 0) ++index;
		if (index == G_N_ELEMENTS(debug_category_names)) return false;
		mask |= 1U << index;
		}
	return mask != 0;
}

} // namespace

const gchar *get_debug_category_name(DebugCategory category)
{
	const auto index = static_cast<guint>(category);
	return index < G_N_ELEMENTS(debug_category_names) ? debug_category_names[index] : nullptr;
}

bool debug_domains_valid(const gchar *domains)
{
	guint mask;
	return parse_debug_domains(domains, mask);
}

bool set_debug_domains(const gchar *domains)
{
	guint mask;
	if (!parse_debug_domains(domains, mask)) return false;
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&settings_mutex);
	debug_domains.store(mask, std::memory_order_relaxed);
	return true;
}

gchar *get_debug_domains()
{
	const guint mask = debug_domains.load(std::memory_order_relaxed);
	if (mask == all_debug_domains) return g_strdup("all");
	if (mask == 0) return g_strdup("none");
	g_autoptr(GString) names = g_string_new(nullptr);
	for (guint index = 0; index < G_N_ELEMENTS(debug_category_names); ++index)
		{
		if (!(mask & (1U << index))) continue;
		if (names->len) g_string_append_c(names, ',');
		g_string_append(names, debug_category_names[index]);
		}
	return g_string_free(g_steal_pointer(&names), FALSE);
}

bool debug_enabled(DebugCategory category, gint level)
{
	const auto index = static_cast<guint>(category);
	return level > 0 && level <= get_debug_level() && index < G_N_ELEMENTS(debug_category_names) &&
	       (debug_domains.load(std::memory_order_relaxed) & (1U << index)) != 0;
}

bool get_debug_source()
{
	return debug_source.load(std::memory_order_relaxed);
}

void set_debug_source(bool enabled)
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&settings_mutex);
	debug_source.store(enabled, std::memory_order_relaxed);
}

bool get_debug_timing()
{
	return debug_timing.load(std::memory_order_relaxed);
}

void set_debug_timing(bool enabled)
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&settings_mutex);
	debug_timing.store(enabled, std::memory_order_relaxed);
}

gchar *format_debug_record(DebugCategory category, gint level, const gchar *file, int line,
                           const gchar *function, const gchar *message)
{
	const gint64 elapsed = debug_elapsed_time();
	thread_local const guint thread_id = next_thread_id.fetch_add(1, std::memory_order_relaxed);
	thread_local gint64 previous = elapsed;
	const gint64 delta = elapsed - previous;
	previous = elapsed;
	const auto index = static_cast<guint>(category);
	const gchar *name = index < G_N_ELEMENTS(debug_category_names) ? debug_category_names[index] : "unknown";
	g_autoptr(GString) prefix = g_string_new(nullptr);
	g_string_append_printf(prefix, "[+%" G_GINT64_FORMAT ".%06" G_GINT64_FORMAT "] [%s] [debug:%d] [pid:%ld] [thread:%u] ",
	                       elapsed / G_USEC_PER_SEC, elapsed % G_USEC_PER_SEC, name, level,
	                       static_cast<long>(getpid()), thread_id);
	if (get_debug_timing())
		{
		g_string_append_printf(prefix, "[delta:%" G_GINT64_FORMAT ".%06" G_GINT64_FORMAT "] ",
		                       delta / G_USEC_PER_SEC, delta % G_USEC_PER_SEC);
		}
	if (get_debug_source())
		{
		g_string_append_printf(prefix, "%s:%d:%s: ", file ? file : "?", line, function ? function : "?");
		}

	g_autofree gchar *text = g_strdup(message ? message : "");
	gsize length = strlen(text);
	while (length && text[length - 1] == '\n') text[--length] = '\0';
	g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
	g_autoptr(GString) record = g_string_new(nullptr);
	for (gchar **part = lines; *part; ++part)
		{
		if (part != lines) g_string_append_c(record, '\n');
		g_string_append(record, prefix->str);
		g_string_append(record, *part);
		}
	if (record->len == 0) g_string_append(record, prefix->str);
	return g_string_free(g_steal_pointer(&record), FALSE);
}

void log_debug_category(DebugCategory category, gint level, const gchar *file, int line, const gchar *function,
                        const gchar *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	g_autofree gchar *message = g_strdup_vprintf(format, arguments);
	va_end(arguments);
	g_autofree gchar *record = format_debug_record(category, level, file, line, function, message);
	log_domain_print_message(DOMAIN_DEBUG, record);
}


gint get_debug_level()
{
	return debug_level.load(std::memory_order_relaxed);
}

void set_debug_level(gint new_level)
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&settings_mutex);
	debug_level.store(std::clamp(new_level, DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX), std::memory_order_relaxed);
}

void debug_level_add(gint delta)
{
	set_debug_level(get_debug_level() + delta);
}

gint required_debug_level(gint level)
{
	return (get_debug_level() >= level);
}

void init_debug_timer()
{
	debug_elapsed_time();
}

const gchar *get_exec_time()
{
	const gint64 elapsed = debug_elapsed_time();
	thread_local gint64 previous = elapsed;
	const gint64 delta = elapsed - previous;
	previous = elapsed;
	thread_local gchar timestr[64];
	g_snprintf(timestr, sizeof(timestr), "%12.6f (+%012.6f)",
	           static_cast<double>(elapsed) / G_USEC_PER_SEC, static_cast<double>(delta) / G_USEC_PER_SEC);
	return timestr;
}

void init_exec_time()
{
	init_debug_timer();
}

void set_regexp(const gchar *cmd_regexp)
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&filter_mutex);
	g_free(regexp);
	regexp = g_strdup(cmd_regexp);
}

gchar *get_regexp()
{
	g_autoptr(GMutexLocker) lock = g_mutex_locker_new(&filter_mutex);
	return g_strdup(regexp);
}

#if HAVE_EXECINFO_H
/**
 * @brief Extract the function name from an addr2line result
 * @param input Symbol description, optionally followed by a source location
 * @return Function name, or an empty string if no function name is found
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

/**
 * @brief Backtrace of geeqie files
 * @param file
 * @param function
 * @param line
 *
 * Requires command line program addr2line \n
 * Prints the contents of the backtrace buffer for Geeqie files. \n
 * Format printed is: \n
 * @code
 * <full path to source file>:<line number>
 * @endcode
 *
 * The log window F1 command and Edit/Preferences/Behavior/Log Window F1
 * Command may be used to open an editor at a backtrace location.
 */

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
					log_printf("Failed to run command: %s", cmd_line); // NOTRANSLATE: Diagnostic message.
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
		log_printf("Backtrace end"); // NOTRANSLATE: Diagnostic message.

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

	log_printf("FileData dump start"); // NOTRANSLATE: Diagnostic message.
	log_printf("%s/../%s:%d %s\n", exe_path, file, line, function);

	file_data_dump();

	log_printf("FileData dump end"); // NOTRANSLATE: Diagnostic message.
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
