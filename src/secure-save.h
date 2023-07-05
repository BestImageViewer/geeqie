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

#include <glib.h>
#include <stddef.h>

#include "typedefs.h"

extern SecureSaveErrno secsave_errno; /**< internal secsave error number */

SecureSaveInfo *secure_open(const gchar *);

gint secure_close(SecureSaveInfo *);

gint secure_fputs(SecureSaveInfo *, const gchar *);
gint secure_fputc(SecureSaveInfo *, gint);

gint secure_fprintf(SecureSaveInfo *, const gchar *, ...);
size_t secure_fwrite(gconstpointer ptr, size_t size, size_t nmemb, SecureSaveInfo *ssi);

gchar *secsave_strerror(SecureSaveErrno);

#endif /* SECURE_SAVE_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
