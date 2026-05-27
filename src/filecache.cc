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

#include <algorithm>
#include <config.h>
#include <list>
#include <optional>

#include "filedata.h"

/* this implements a simple LRU algorithm */

#ifdef DEBUG
constexpr bool debug_file_cache = false; /* Set to true to add file cache dumps to the debug output */

void FileCache::dump()
{
	if (!debug_file_cache) return;

	DEBUG_1("cache dump: fc=%p max size:%lu size:%lu", (void *)this, max_size_, size_);

	size_t n = 0;
	for (const auto &entry : contents_)
		{
		DEBUG_1("cache entry: fc=%p [%lu] %s %lu", (void *)this, ++n, entry.fd->path, entry.size);
		}
}
#else
// TODO[xsdg]: Make this a no-op again.
void FileCache::dump() {}
// #  define file_cache_dump(fc)
#endif

bool FileCache::remove_entry(FileCache::ListIterT entry_iter)
{
	auto &entry = *entry_iter;

	// Avoid evicting a FileCacheEntry that implicitly triggered this removal attempt.
	if (entry.checking_if_changed)
		{
		DEBUG_1("deferring cache remove: fc=%p %s", (void *)this, entry.fd->path);
		return false;
		}

	DEBUG_1("cache remove: fc=%p %s", (void *)this, entry.fd->path);

	size_ -= entry.size;
	release_(entry.fd);
	file_data_unref(entry.fd);
	contents_.erase(entry_iter);

	return true;
}

std::optional<FileCache::ListIterT> FileCache::find_by_fd(FileData *fd)
{
	const auto entry_iter = std::find_if(
		contents_.begin(),
		contents_.end(),
		[fd](const Entry &entry) { return entry.fd == fd; });

	if (entry_iter != contents_.end()) return entry_iter;
	return std::nullopt;
}

// static
void FileCache::notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	/* invalidate the entry on each file change */
	if (!(type & (NOTIFY_REREAD | NOTIFY_CHANGE))) return;

	DEBUG_1("Notify cache: %s %04x", fd->path, type);

	auto *fc = static_cast<FileCache *>(data);
	fc->dump();

	const auto maybe_iter = fc->find_by_fd(fd);
	if (!maybe_iter) return;

	fc->remove_entry(*maybe_iter);
}

void FileCache::shrink_to_max_size()
{
	dump();

	g_assert((size_ == 0) == contents_.empty());  // Assert that size is consistent with emptiness.

	if (contents_.empty()) return;
	auto entry_iter = std::prev(contents_.end());
	while(size_ > max_size_ && entry_iter != contents_.begin())
		{
		// This is valid since the list is guaranteed non-empty.
		auto evict_iter = entry_iter;
		--entry_iter;

		// This may fail to remove the specified entry if this resize was implicitly
		// triggered during a file_cache_get call.  Any file_cache_put after the
		// file_cache_get will re-trigger the shrink and correct the cache size, if needed.
		remove_entry(evict_iter);
		}

	g_assert((size_ == 0) == contents_.empty());  // Assert that size is consistent with emptiness.

	// At this point, the loop won't have been able to evict the begin() entry.  Maybe do so now.
	if (size_ > max_size_ && !contents_.empty())
		{
		remove_entry(contents_.begin());
		}

	g_assert((size_ == 0) == contents_.empty());  // Assert that size is consistent with emptiness.
}

FileCache::FileCache(ReleaseFunc release, size_t max_size) : release_(release), max_size_(max_size)
{
	file_data_register_notify_func(FileCache::notify_cb, this, NOTIFY_PRIORITY_HIGH);
}

FileCache::~FileCache()
{
	file_data_unregister_notify_func(FileCache::notify_cb, this);
}

bool FileCache::get(FileData *fd)
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

	g_assert(fd);

	// We assume that file_data_check_changed_files may invalidate fc iterators.  So we create
	// a "before" scope, so that the iters will be undefined by the time we make the
	// invalidating call
	{
		const auto maybe_entry_iter = find_by_fd(fd);
		if (!maybe_entry_iter)
			{
			DEBUG_2("cache miss: fc=%p %s", (void *)this, fd->path);
			return FALSE;
			}
		auto entry_iter = *maybe_entry_iter;

		// Entry exists.
		DEBUG_2("cache hit: fc=%p %s", (void *)this, fd->path);

		// Move it to the beginning, if needed.
		if (entry_iter != contents_.begin())
			{
			DEBUG_2("cache move to front: fc=%p %s", (void *)this, fd->path);
			// Moves entry_iter from somewhere in contents_ to before contents_.begin();
			contents_.splice(contents_.begin(), contents_, entry_iter);
			}

		// Most of the following code is defending against the case where
		// file_data_check_changed_files triggers a re-entrant call back into this file_cache_get.
		if (entry_iter->checking_if_changed) return true;  // Avoid infinite recursion.
		entry_iter->checking_if_changed = TRUE;
	}

	// We assume that file_data_check_changed_files may invalidate fc iterators.
	const bool fd_changed = file_data_check_changed_files(fd);

	// Now we re-acquire entry_iter to take the appropriate action, if it still exists.
	const auto maybe_entry_iter = find_by_fd(fd);
	if (!maybe_entry_iter) return false;
	auto &entry_iter = *maybe_entry_iter;

	// Doing this here for correctness, even though we might immediately evict the entry.
	entry_iter->checking_if_changed = false;

	if (fd_changed)
		{
		// Underlying file has been changed.  Evict the cache entry.
		dump();
		remove_entry(entry_iter);
		return false;
		}

	dump();
	return true;
}

void FileCache::put(FileData *fd, size_t size)
{
	if (get(fd)) return;

	DEBUG_2("cache add: fc=%p %s", (void *)this, fd->path);
	contents_.emplace_front(file_data_ref(fd), size);
	size_ += size;

	shrink_to_max_size();
}

void FileCache::set_max_size(size_t size)
{
	max_size_ = size;
	shrink_to_max_size();
}

// Trampoline implementation of C-style API.
FileCacheData *file_cache_new(FileCacheReleaseFunc release, size_t max_size)
{
	return new FileCache(release, max_size);
}

bool file_cache_get(FileCacheData *fc, FileData *fd)
{
	return fc->get(fd);
}

void file_cache_put(FileCacheData *fc, FileData *fd, size_t size)
{
	fc->put(fd, size);
}

void file_cache_set_max_size(FileCacheData *fc, size_t size)
{
	fc->set_max_size(size);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
