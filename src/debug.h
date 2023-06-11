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

#define DOMAIN_DEBUG "debug"
#define DOMAIN_INFO  "info"

void log_domain_printf(const gchar *domain, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
void log_domain_print_debug(const gchar *domain, const gchar *file_name, const gchar *function_name,
							int line_number, const gchar *format, ...) G_GNUC_PRINTF(5, 6);
void log_print_file_data_dump(const gchar *file, const gchar *function_name, gint line_number);
void log_print_backtrace(const gchar *file, const gchar *function_name, gint line_number);

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

#define DEBUG_N(n, ...) do \
				{ \
				gint debug_level = get_debug_level(); \
				if (debug_level >= (n)) 	\
					{ 		\
					if (debug_level != 1) \
						{ \
						log_domain_print_debug(DOMAIN_DEBUG, __FILE__, __func__, __LINE__, __VA_ARGS__); \
						} \
					else \
						{ \
						log_domain_printf(DOMAIN_DEBUG, __VA_ARGS__); \
						} \
					} \
				} while (0)

/**
 * @brief For use with the GTKInspector (>GTK 3.14)
 *
 * To simplify finding where objects are declared
 * Sample command line call:
 * GTK_DEBUG=interactive src/geeqie
 */
#define DEBUG_NAME(widget) do \
				{ \
				gtk_widget_set_name(GTK_WIDGET(widget), g_strdup_printf("%s:%d", __FILE__, __LINE__)); \
				} while(0)

#define DEBUG_BT() do \
				{ \
				log_print_backtrace(__FILE__, __func__, __LINE__); \
				} while(0)

#define DEBUG_FD() do \
				{ \
				log_print_file_data_dump(__FILE__, __func__, __LINE__); \
				} while(0)
#else /* DEBUG */

#define get_regexp() (0)
#define set_regexp(regexp) do { } while(0)
#define get_debug_level() (0)
#define set_debug_level(new_level) do { } while(0)
#define debug_level_add(delta) do { } while(0)
#define required_debug_level(level) (0)
#define get_exec_time() ""
#define init_exec_time() do { } while(0)

#define DEBUG_N(n, ...)  do { } while(0)

#define DEBUG_NAME(widget) do { } while(0)
#define DEBUG_BT() do { } while(0)
#define DEBUG_FD() do { } while(0)

#endif /* DEBUG */

#define DEBUG_0(...) DEBUG_N(0, __VA_ARGS__)
#define DEBUG_1(...) DEBUG_N(1, __VA_ARGS__)
#define DEBUG_2(...) DEBUG_N(2, __VA_ARGS__)
#define DEBUG_3(...) DEBUG_N(3, __VA_ARGS__)
#define DEBUG_4(...) DEBUG_N(4, __VA_ARGS__)


#endif /* _DEBUG_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
