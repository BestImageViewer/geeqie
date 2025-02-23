/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Laurent Monin
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

#ifndef SECURE_SAVE_H
#define SECURE_SAVE_H

#include <cstdio>

#include <glib.h>

/**
 * @enum SecureSaveErrno
 * see err field in #SecureSaveInfo
 */
enum SecureSaveErrno {
	SS_ERR_NONE = 0,
	SS_ERR_DISABLED, /**< secsave is disabled. */
	SS_ERR_OUT_OF_MEM, /**< memory allocation failure */

	SS_ERR_OPEN_READ,
	SS_ERR_OPEN_WRITE,
	SS_ERR_STAT,
	SS_ERR_ACCESS,
	SS_ERR_MKSTEMP,
	SS_ERR_RENAME,
	SS_ERR_OTHER,
};

struct SecureSaveInfo {
	FILE *fp; /**< file stream pointer */
	gchar *file_name; /**< final file name */
	gchar *tmp_file_name; /**< temporary file name */
	gint err; /**< set to non-zero value in case of error */
	gboolean secure_save; /**< use secure save for this file, internal use only */
	gboolean preserve_perms; /**< whether to preserve perms, TRUE by default */
	gboolean preserve_mtime; /**< whether to preserve mtime, FALSE by default */
	gboolean unlink_on_error; /**< whether to remove temporary file on save failure, TRUE by default */
};

SecureSaveInfo *secure_open(const gchar *);

gint secure_close(SecureSaveInfo *);

gint secure_fputs(SecureSaveInfo *, const gchar *);
gint secure_fputc(SecureSaveInfo *, gint);

gint secure_fprintf(SecureSaveInfo *, const gchar *, ...) G_GNUC_PRINTF(2, 3);
size_t secure_fwrite(gconstpointer ptr, size_t size, size_t nmemb, SecureSaveInfo *ssi);

bool secsave_succeed();
const gchar *secsave_strerror();

#endif /* SECURE_SAVE_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
