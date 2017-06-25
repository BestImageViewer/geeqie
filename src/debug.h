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
#define log_printf(...) log_domain_printf(DOMAIN_INFO, __VA_ARGS__)

#ifdef DEBUG

#define DEBUG_LEVEL_MIN 0
#define DEBUG_LEVEL_MAX 4

void set_regexp(gchar *regexp);
gchar *get_regexp();
gint get_debug_level(void);
void set_debug_level(gint new_level);
void debug_level_add(gint delta);
gint required_debug_level(gint level);
const gchar *get_exec_time(void);
void init_exec_time(void);

#define DEBUG_N(n, ...) do \
				{ \
				gint debug_level = get_debug_level(); \
				if (debug_level >= (n)) 	\
					{ 		\
					if (debug_level != 1) log_domain_printf(DOMAIN_DEBUG, "%s:%d: ", __FILE__, __LINE__); \
					log_domain_printf(DOMAIN_DEBUG, __VA_ARGS__); \
					log_domain_printf(DOMAIN_DEBUG, "\n"); \
					} \
				} while (0)

#else /* DEBUG */

#define set_regexp(regexp) do { } while(0)
#define get_regexp() (NULL)
#define get_debug_level() (0)
#define set_debug_level(new_level) do { } while(0)
#define debug_level_add(delta) do { } while(0)
#define required_debug_level(level) (0)
#define get_exec_time() ""
#define init_exec_time() do { } while(0)

#define DEBUG_N(n, ...)  do { } while(0)

#endif /* DEBUG */

#define DEBUG_0(...) DEBUG_N(0, __VA_ARGS__)
#define DEBUG_1(...) DEBUG_N(1, __VA_ARGS__)
#define DEBUG_2(...) DEBUG_N(2, __VA_ARGS__)
#define DEBUG_3(...) DEBUG_N(3, __VA_ARGS__)
#define DEBUG_4(...) DEBUG_N(4, __VA_ARGS__)


#endif /* _DEBUG_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
