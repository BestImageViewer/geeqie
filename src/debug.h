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

#ifndef DEBUG_H
#define DEBUG_H

#include <glib.h>

#include <config.h>

#define DOMAIN_DEBUG "debug"
#define DOMAIN_INFO  "info"

void log_domain_printf(const gchar *domain, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
void log_set_file(const gchar *path);

void print_term(bool err, const gchar *text_utf8);

#define log_printf(...) log_domain_printf(DOMAIN_INFO, __VA_ARGS__)

#define printf_term(err, ...) \
	G_STMT_START \
		{ \
		g_autofree gchar *msg = g_strdup_printf(__VA_ARGS__); \
		print_term(err, msg); \
		} \
	G_STMT_END

#ifdef DEBUG

enum class DebugCategory
{
	GENERAL,
	THUMBNAILS,
	CACHE,
	IMAGE,
	RENDER,
	METADATA,
	FILES,
	UI,
	DUPLICATES,
	CONFIG,
	COLLECTIONS,
	EXTERNAL,
	COUNT
};

#define DEBUG_LEVEL_MIN 0
#define DEBUG_LEVEL_MAX 4

bool debug_domains_valid(const gchar *domains);
bool set_debug_domains(const gchar *domains);
gchar *get_debug_domains();
const gchar *get_debug_category_name(DebugCategory category);
bool debug_enabled(DebugCategory category, gint level);
bool get_debug_source();
void set_debug_source(bool enabled);
bool get_debug_timing();
void set_debug_timing(bool enabled);
/** @return Newly allocated formatted record, without a trailing newline. */
gchar *format_debug_record(DebugCategory category, gint level, const gchar *file, int line,
                           const gchar *function, const gchar *message);
void log_debug_category(DebugCategory category, gint level, const gchar *file, int line, const gchar *function,
                        const gchar *format, ...) G_GNUC_PRINTF(6, 7);

gint get_debug_level();
void set_debug_level(gint new_level);
void debug_level_add(gint delta);
gint required_debug_level(gint level);
/** Initialize the common monotonic timer without resetting an existing origin. */
void init_debug_timer();
void set_regexp(const gchar *regexp);
gchar *get_regexp();
void log_print_backtrace(const gchar *file, gint line_number, const gchar *function_name);
void log_print_file_data_dump(const gchar *file, gint line_number, const gchar *function_name);
void log_print_ru(const gchar *file, gint line_number, const gchar *function_name);

/** Format arguments are evaluated only when this category and level are enabled. */
#define DEBUG_LOG(category, level, ...) \
	G_STMT_START \
		{ \
		const auto gq_debug_category = (category); \
		const gint gq_debug_level = (level); \
		if (debug_enabled(gq_debug_category, gq_debug_level)) \
			{ \
			log_debug_category(gq_debug_category, gq_debug_level, __FILE__, __LINE__, __func__, __VA_ARGS__); \
			} \
		} \
	G_STMT_END

/** Temporary developer output; deliberately bypasses the runtime filters. */
#define DEBUG_0(...) log_debug_category(DebugCategory::GENERAL, 0, __FILE__, __LINE__, __func__, __VA_ARGS__)

/**
 * @brief For use with the GTKInspector (>GTK 3.14)
 *
 * To simplify finding where objects are declared
 * Sample command line call:
 * GTK_DEBUG=interactive src/geeqie
 */
#define DEBUG_NAME(widget) \
	G_STMT_START \
		{ \
		g_autofree gchar *name = g_strdup_printf("%s:%d", __FILE__, __LINE__); \
		gtk_widget_set_name(GTK_WIDGET(widget), name); \
		} \
	G_STMT_END

#define DEBUG_BT() \
	G_STMT_START \
		{ \
		log_print_backtrace(__FILE__, __LINE__, __func__); \
		} \
	G_STMT_END

#define DEBUG_FD() \
	G_STMT_START \
		{ \
		log_print_file_data_dump(__FILE__, __LINE__, __func__); \
		} \
	G_STMT_END

#define DEBUG_RU() \
	G_STMT_START \
		{ \
		log_print_ru(__FILE__, __LINE__, __func__); \
		} \
	G_STMT_END
#else /* DEBUG */

#define get_debug_level() (0)
#define set_debug_level(new_level) G_STMT_START { } G_STMT_END
#define debug_level_add(delta) G_STMT_START { } G_STMT_END
#define required_debug_level(level) (0)
#define set_debug_timing(enabled) G_STMT_START { } G_STMT_END
#define init_debug_timer() G_STMT_START { } G_STMT_END
#define set_regexp(regexp) G_STMT_START { } G_STMT_END
#define get_regexp() (0)

#define DEBUG_0(...) G_STMT_START { } G_STMT_END
#define DEBUG_LOG(category, level, ...) G_STMT_START { } G_STMT_END

#define DEBUG_NAME(widget) G_STMT_START { } G_STMT_END
#define DEBUG_BT() G_STMT_START { } G_STMT_END
#define DEBUG_FD() G_STMT_START { } G_STMT_END
#define DEBUG_RU() G_STMT_START { } G_STMT_END

#endif /* DEBUG */


#endif /* DEBUG_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
