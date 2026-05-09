/*
 * Copyright (C) 1993 Branko Lankester
 * Copyright (C) 1993 Colin Plumb
 * Copyright (C) 1995 Erik Troan
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
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
 *
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to md5_init, call md5_update as
 * needed on buffers full of bytes, and then call md5_Final, which
 * will fill a supplied 16-byte array with the digest.
 */

#ifndef MD5_UTIL_H
#define MD5_UTIL_H

#include <array>

#include <glib.h>

inline constexpr gsize MD5_SIZE = 16;

using Md5Digest = std::array<guchar, MD5_SIZE>;

gchar *md5_get_string(const guchar *buffer, gint buffer_size);

gboolean md5_get_digest_from_file(const gchar *path, Md5Digest &digest);

gchar *md5_get_string_from_file(const gchar *path);

gchar *md5_digest_to_text(const Md5Digest &digest);

gboolean md5_digest_from_text(const gchar *text, Md5Digest &digest);


#endif	/* MD5_UTILS_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
