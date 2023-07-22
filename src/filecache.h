/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#ifndef FILECACHE_H
#define FILECACHE_H

#include <glib.h>

struct FileCacheData;
struct FileData;

using FileCacheReleaseFunc = void (*)(FileData *);

FileCacheData *file_cache_new(FileCacheReleaseFunc release, gulong max_size);
gboolean file_cache_get(FileCacheData *fc, FileData *fd);
void file_cache_put(FileCacheData *fc, FileData *fd, gulong size);
void file_cache_dump(FileCacheData *fc);
void file_cache_set_size(FileCacheData *fc, gulong size);
gulong file_cache_get_size(FileCacheData *fc);
void file_cache_set_max_size(FileCacheData *fc, gulong size);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
