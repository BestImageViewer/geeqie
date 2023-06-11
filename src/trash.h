/*
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
 */

#ifndef TRASH_H
#define TRASH_H

#include "ui-utildlg.h"

void file_util_trash_clear();
gboolean file_util_safe_unlink(const gchar *path);
gchar *file_util_safe_delete_status();

#endif /* TRASH_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
