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

#include <list>
#include <optional>

// From filedata.h
class FileData;
enum NotifyType : gint;

class FileCache {
    public:
	using ReleaseFunc = void (*)(FileData *);

	FileCache(ReleaseFunc release, size_t max_size);
	~FileCache();

	// Not copyable.
	FileCache(const FileCache &) = delete;
	FileCache &operator=(const FileCache &) = delete;

	// TODO[xsdg]: The name "get" here is really misleading.  Rename.
	bool get(FileData *fd);
	void put(FileData *fd, size_t size);
	void set_max_size(size_t size);

    private:
	struct Entry {
		Entry(FileData *fd, size_t size) : fd(fd), size(size) {}

		// Not copyable.
		Entry(const Entry &other) = delete;
		Entry &operator=(const Entry &other) = delete;

		FileData *fd;
		size_t size;
		bool checking_if_changed = false;
	};
	using ListIterT = std::list<Entry>::iterator;

	void dump();
	bool remove_entry(ListIterT entry_iter);
	std::optional<ListIterT> find_by_fd(FileData *fd);
	static void notify_cb(FileData *fd, NotifyType type, gpointer data);
	void shrink_to_max_size();

	ReleaseFunc release_;
	std::list<Entry> contents_;
	size_t max_size_;
	size_t size_ = 0;
};

using FileCacheReleaseFunc = FileCache::ReleaseFunc;

FileCache *file_cache_new(FileCacheReleaseFunc release, size_t max_size);
bool file_cache_get(FileCache *fc, FileData *fd);
void file_cache_put(FileCache *fc, FileData *fd, size_t size);
void file_cache_set_max_size(FileCache *fc, size_t size);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
