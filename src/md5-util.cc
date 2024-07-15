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
 */

#include "md5-util.h"

#include <cstdio>

#include "typedefs.h"

namespace
{

constexpr gsize MD5_SIZE = 16;

/**
 * md5_update_from_file: get the md5 hash of a file
 * @md5: MD5 checksumming context
 * @path: file name
 * @return: TRUE on success
 *
 * Get the md5 hash of a file.
 **/
gboolean md5_update_from_file(GChecksum *md5, const gchar *path)
{
	guchar tmp_buf[1024];
	gint nb_bytes_read;

	g_autoptr(FILE) fp = fopen(path, "r");
	if (!fp) return FALSE;

	while ((nb_bytes_read = fread(tmp_buf, sizeof (guchar), sizeof(tmp_buf), fp)) > 0)
		{
		g_checksum_update(md5, tmp_buf, nb_bytes_read);
		}

	return ferror(fp) == 0;
}

} // namespace

/**
 * @brief Get the md5 hash of a buffer
 * @buffer: byte buffer
 * @buffer_size: buffer size (in bytes)
 * @return: hash as a hexadecimal string
 *
 * Get the md5 hash of a buffer. The result is returned
 * as a hexadecimal string.
 **/
gchar *md5_get_string(const guchar *buffer, gint buffer_size)
{
	g_autoptr(GChecksum) md5 = g_checksum_new(G_CHECKSUM_MD5);
	if (!md5) return nullptr;

	g_checksum_update(md5, buffer, buffer_size);

	return g_strdup(g_checksum_get_string(md5));
}

/**
 * @brief Get the md5 hash of a file
 * @filename: file name
 * @digest: 16 bytes buffer receiving the hash code.
 * @return: TRUE on success
 *
 * Get the md5 hash of a file. The result is put in
 * the 16 bytes buffer @digest .
 **/
gboolean md5_get_digest_from_file(const gchar *path, guchar digest[16])
{
	g_autoptr(GChecksum) md5 = g_checksum_new(G_CHECKSUM_MD5);
	if (!md5) return FALSE;

	if (!md5_update_from_file(md5, path)) return FALSE;

	gsize digest_size = MD5_SIZE;
	g_checksum_get_digest(md5, digest, &digest_size);
	if (digest_size != MD5_SIZE) return FALSE;

	return TRUE;
}

/**
 * @brief Get the md5 hash of a file
 * @filename: file name
 * @return: hash as a hexadecimal string
 *
 * Get the md5 hash of a file. The result is returned
 * as a hexadecimal string.
 **/
gchar *md5_get_string_from_file(const gchar *path)
{
	g_autoptr(GChecksum) md5 = g_checksum_new(G_CHECKSUM_MD5);
	if (!md5) return nullptr;

	if (!md5_update_from_file(md5, path)) return nullptr;

	return g_strdup(g_checksum_get_string(md5));
}

/**
 * @brief Convert digest to a NULL terminated text string, in ascii encoding
 * These to and from text string converters were borrowed from
 * the libgnomeui library, where they are name thumb_digest_to/from_ascii
 *
 * this version of the from text util does buffer length checking,
 * and assumes a NULL terminated string.
 */

gchar *md5_digest_to_text(const guchar digest[16])
{
	static gchar hex_digits[] = "0123456789abcdef";
	gchar *result;
	constexpr gsize result_size = 2 * MD5_SIZE;

	result = static_cast<gchar *>(g_malloc((result_size + 1) * sizeof(gchar)));
	for (gsize i = 0; i < MD5_SIZE; i++)
		{
		result[2*i] = hex_digits[digest[i] >> 4];
		result[2*i+1] = hex_digits[digest[i] & 0xf];
		}
	result[result_size] = '\0';

	return result;
}

/**
 * @brief Convert digest from a NULL terminated text string, in ascii encoding
 */
gboolean md5_digest_from_text(const gchar *text, guchar digest[16])
{
	for (gsize i = 0; i < MD5_SIZE; i++)
		{
		if (text[2*i] == '\0' || text[2*i+1] == '\0') return FALSE;
		digest[i] = g_ascii_xdigit_value(text[2*i]) << 4 |
			    g_ascii_xdigit_value(text[2*i + 1]);
		}

	return TRUE;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
