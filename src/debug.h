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

#ifndef _DEBUG_H
#define _DEBUG_H

#include <glib.h>

#include <config.h>

#define DOMAIN_DEBUG "debug"
#define DOMAIN_INFO  "info"

void log_domain_printf(const gchar *domain, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
void log_domain_print_debug(const gchar *domain, const gchar *file_name, int line_number, const gchar *function_name, const gchar *format, ...) G_GNUC_PRINTF(5, 6);
void log_print_file_data_dump(const gchar *file, gint line_number, const gchar *function_name);
void log_print_backtrace(const gchar *file, gint line_number, const gchar *function_name);

#define log_printf(...) log_domain_printf(DOMAIN_INFO, __VA_ARGS__)

#ifdef DEBUG

#define DEBUG_LEVEL_MIN 0
#define DEBUG_LEVEL_MAX 4

void set_regexp(const gchar *regexp);
gchar *get_regexp();
gint get_debug_level();
void set_debug_level(gint new_level);
void debug_level_add(gint delta);
gint required_debug_level(gint level);
const gchar *get_exec_time();
void init_exec_time();

#define DEBUG_N(n, ...) \
	G_STMT_START \
		{                                                      \
		_Pragma("GCC diagnostic push")                         \
		_Pragma("GCC diagnostic ignored  \"-Wformat\"")        \
		gint debug_level = get_debug_level(); \
		if (debug_level >= (n)) \
			{ \
			if (debug_level != 1) \
				{ \
				log_domain_print_debug(DOMAIN_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__); \
				} \
			else \
				{ \
				log_domain_printf(DOMAIN_DEBUG, __VA_ARGS__); \
				} \
			}                                                  \
		_Pragma("GCC diagnostic pop")                          \
		} \
	G_STMT_END

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
		gtk_widget_set_name(GTK_WIDGET(widget), g_strdup_printf("%s:%d", __FILE__, __LINE__)); \
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
#else /* DEBUG */

#define get_regexp() (0)
#define set_regexp(regexp) G_STMT_START { } G_STMT_END
#define get_debug_level() (0)
#define set_debug_level(new_level) G_STMT_START { } G_STMT_END
#define debug_level_add(delta) G_STMT_START { } G_STMT_END
#define required_debug_level(level) (0)
#define get_exec_time() ""
#define init_exec_time() G_STMT_START { } G_STMT_END

#define DEBUG_N(n, ...) G_STMT_START { } G_STMT_END

#define DEBUG_NAME(widget) G_STMT_START { } G_STMT_END
#define DEBUG_BT() G_STMT_START { } G_STMT_END
#define DEBUG_FD() G_STMT_START { } G_STMT_END

#endif /* DEBUG */

#define DEBUG_0(...) DEBUG_N(0, __VA_ARGS__)
#define DEBUG_1(...) DEBUG_N(1, __VA_ARGS__)
#define DEBUG_2(...) DEBUG_N(2, __VA_ARGS__)
#define DEBUG_3(...) DEBUG_N(3, __VA_ARGS__)
#define DEBUG_4(...) DEBUG_N(4, __VA_ARGS__)


#endif /* _DEBUG_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
