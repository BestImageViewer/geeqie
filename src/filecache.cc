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

#include "filecache.h"

#include <config.h>
#include <algorithm>
#include <list>
#include <optional>

#include "filedata.h"

/* this implements a simple LRU algorithm */

namespace
{

struct FileCacheEntry {
	FileData *fd;
	gulong size;
	gboolean checking_if_changed;
};

}  // namespace

struct FileCacheData {
	using ListIterT = std::list<FileCacheEntry>::iterator;

	// TODO[xsdg]: turn file_cache_new into a c++ constructor.
	FileCacheReleaseFunc release;
	std::list<FileCacheEntry> *list;
	gulong max_size;
	gulong size;
};

namespace
{

#ifdef DEBUG
constexpr bool debug_file_cache = false; /* Set to true to add file cache dumps to the debug output */

void file_cache_dump(FileCacheData *fc)
{
	if (!debug_file_cache) return;

	DEBUG_1("cache dump: fc=%p max size:%lu size:%lu", (void *)fc, fc->max_size, fc->size);

	gulong n = 0;
	for (const auto &entry : *fc->list)
		{
		DEBUG_1("cache entry: fc=%p [%lu] %s %lu", (void *)fc, ++n, entry.fd->path, entry.size);
		}
}
#else
#  define file_cache_dump(fc)
#endif

gboolean file_cache_remove_entry(FileCacheData *fc, FileCacheData::ListIterT entry_iter)
{
	auto &entry = *entry_iter;

	// Avoid evicting a FileCacheEntry that implicitly triggered this removal attempt.
	if (entry.checking_if_changed)
		{
		DEBUG_1("deferring cache remove: fc=%p %s", (void *)fc, entry.fd->path);
		return FALSE;
		}

	DEBUG_1("cache remove: fc=%p %s", (void *)fc, entry.fd->path);

	fc->size -= entry.size;
	fc->release(entry.fd);
	file_data_unref(entry.fd);
	fc->list->erase(entry_iter);

	return TRUE;
}

std::optional<FileCacheData::ListIterT> file_cache_find_by_fd(FileCacheData *fc, FileData *fd)
{
	const auto entry_iter = std::find_if(
		fc->list->begin(),
		fc->list->end(),
		[fd](const FileCacheEntry &entry) { return entry.fd == fd; });

	if (entry_iter != fc->list->end()) return entry_iter;
	return std::nullopt;
}

void file_cache_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	/* invalidate the entry on each file change */
	if (!(type & (NOTIFY_REREAD | NOTIFY_CHANGE))) return;

	DEBUG_1("Notify cache: %s %04x", fd->path, type);

	auto *fc = static_cast<FileCacheData *>(data);
	file_cache_dump(fc);

	const auto maybe_iter = file_cache_find_by_fd(fc, fd);
	if (!maybe_iter) return;

	file_cache_remove_entry(fc, *maybe_iter);
}

void file_cache_shrink_to_max_size(FileCacheData *fc)
{
	file_cache_dump(fc);

	g_assert((fc->size == 0) == fc->list->empty());  // Assert that size is consistent with emptiness.

	if (fc->list->empty()) return;
	auto entry_iter = std::prev(fc->list->end());
	while(fc->size > fc->max_size && entry_iter != fc->list->begin())
		{
		// This is valid since the list is guaranteed non-empty.
		auto evict_iter = entry_iter;
		--entry_iter;

		// This may fail to remove the specified entry if this resize was implicitly
		// triggered during a file_cache_get call.  Any file_cache_put after the
		// file_cache_get will re-trigger the shrink and correct the cache size, if needed.
		file_cache_remove_entry(fc, evict_iter);
		}

	g_assert((fc->size == 0) == fc->list->empty());  // Assert that size is consistent with emptiness.

	// At this point, the loop won't have been able to evict the begin() entry.  Maybe do so now.
	if (fc->size > fc->max_size && !fc->list->empty())
		{
		file_cache_remove_entry(fc, fc->list->begin());
		}

	g_assert((fc->size == 0) == fc->list->empty());  // Assert that size is consistent with emptiness.
}

} // namespace

FileCacheData *file_cache_new(FileCacheReleaseFunc release, gulong max_size)
{
	auto fc = g_new(FileCacheData, 1);

	fc->release = release;
	fc->list = new std::list<FileCacheEntry>();  // TODO[xsdg]: This is never freed.
	fc->max_size = max_size;
	fc->size = 0;

	file_data_register_notify_func(file_cache_notify_cb, fc, NOTIFY_PRIORITY_HIGH);

	return fc;
}

gboolean file_cache_get(FileCacheData *fc, FileData *fd)
{
	/* Operating theory of this function:
	 * This function must be re-entrant, which means it must specifically be implemented in a
	 * way that remains correct even when a re-entrant call happens.
	 *
	 * In particular, the file_data_check_changed_files function call may trigger another call
	 * into this function.  In order to handle that case correctly, we establish that if the
	 * FileData has changed (in which case we plan to evict it and return FALSE), any
	 * re-entrant calls into the function will return TRUE _without_ checking for changes.
	 * Then we will evict the FileData from the cache (if that hasn't happened already), and
	 * then we will return FALSE.
	 *
	 * That said, because it's also possible for a re-entrant call to target a _different_
	 * FileData than the one that we plan to evict, we stash a checking_if_changed bool in
	 * every FileCacheEntry.  That is, we need to handle the case where
	 * file_cache_get(fc, fd_A) triggers file_cache_get(fc, fd_B), which in turn triggers
	 * file_cache_get(fc, fd_A) again.
	 */

	g_assert(fc && fd);

	// We assume that file_data_check_changed_files may invalidate fc iterators.  So we create
	// a "before" scope, so that the iters will be undefined by the time we make the
	// invalidating call
	{
		const auto maybe_entry_iter = file_cache_find_by_fd(fc, fd);
		if (!maybe_entry_iter)
			{
			DEBUG_2("cache miss: fc=%p %s", (void *)fc, fd->path);
			return FALSE;
			}
		auto entry_iter = *maybe_entry_iter;

		// Entry exists.
		DEBUG_2("cache hit: fc=%p %s", (void *)fc, fd->path);

		// Move it to the beginning, if needed.
		if (entry_iter != fc->list->begin())
			{
			DEBUG_2("cache move to front: fc=%p %s", (void *)fc, fd->path);
			// Moves entry_iter from fc->list to before fc->list->begin();
			fc->list->splice(fc->list->begin(), *fc->list, entry_iter);
			}

		// Most of the following code is defending against the case where
		// file_data_check_changed_files triggers a re-entrant call back into this file_cache_get.
		if (entry_iter->checking_if_changed) return TRUE;  // Avoid infinite recursion.
		entry_iter->checking_if_changed = TRUE;
	}

	// We assume that file_data_check_changed_files may invalidate fc iterators.
	const gboolean fd_changed = file_data_check_changed_files(fd);

	// Now we re-acquire entry_iter to take the appropriate action, if it still exists.
	const auto maybe_entry_iter = file_cache_find_by_fd(fc, fd);
	if (!maybe_entry_iter) return FALSE;
	auto &entry_iter = *maybe_entry_iter;

	// Doing this here for correctness, even though we might immediately evict the entry.
	entry_iter->checking_if_changed = FALSE;

	if (fd_changed)
		{
		// Underlying file has been changed.  Evict the cache entry.
		file_cache_dump(fc);
		file_cache_remove_entry(fc, entry_iter);
		return FALSE;
		}

	file_cache_dump(fc);
	return TRUE;
}

void file_cache_put(FileCacheData *fc, FileData *fd, gulong size)
{
	if (file_cache_get(fc, fd)) return;

	DEBUG_2("cache add: fc=%p %s", (void *)fc, fd->path);
	// TODO[xsdg]: Switch to an stl container and do this initialization in a constructor.
	FileCacheEntry entry;
	entry.fd = file_data_ref(fd);
	entry.size = size;
	entry.checking_if_changed = FALSE;
	fc->list->push_front(std::move(entry));
	fc->size += size;

	file_cache_shrink_to_max_size(fc);
}

void file_cache_set_max_size(FileCacheData *fc, gulong size)
{
	fc->max_size = size;
	file_cache_shrink_to_max_size(fc);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
