/*
 * Copyright (C) 2026 The Geeqie Team
 *
 * Author: Omari Stephens
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
 *
 * Unit tests for filecache.cc
 *
 */

#include "gtest/gtest.h"

#include <glib.h>

#include "filecache.h"
#include "filedata.h"

namespace {

// For convenience.
namespace t = ::testing;

class FileCacheTest : public t::Test
{
    protected:
	void TearDown() override
	{
		// Free Refs before cleaning up fd_funcs
		fd.reset(nullptr);
		fd2.reset(nullptr);

		// We have to clean this up by hand since the notify funcs are stored in a global.
		for (const auto notify_func_pair : unregister_fd_funcs_at_teardown)
			{
			file_data_unregister_notify_func(notify_func_pair.first, notify_func_pair.second);
			}
	}

	void register_notify_func_with_cleanup(FileData::NotifyFunc func, gpointer data, NotifyPriority priority)
	{
		unregister_fd_funcs_at_teardown.emplace_back(func, data);
		file_data_register_notify_func(func, data, priority);
	}

	static void cache_release(FileData *fd)
	{
		std::cerr << "released " << static_cast<void *>(fd) << " (" << fd->path << ")\n";
	}

	FileDataContext context;  // Needs to be constructed before Refs.
	FileDataRef fd{nullptr};
	FileDataRef fd2{nullptr};
	std::vector<std::pair<FileData::NotifyFunc, gpointer>> unregister_fd_funcs_at_teardown;
};


TEST_F(FileCacheTest, BasicLifecycle)
{
	// TODO[xsdg]: Create a mock FileData that acts like the underlying file exists.
	fd = FileData::new_simple("/does/not/exist.jpg", &context);
	fd2 = FileData::new_simple("/does/not/exist2.jpg", &context);
	FileCache *fc = file_cache_new(&FileCacheTest::cache_release, /*max_size=*/5);

	ASSERT_FALSE(file_cache_get(fc, fd));
	ASSERT_FALSE(file_cache_get(fc, fd2));

	// Because the FileDatas point to files that don't exist, they will get evicted from the
	// cache during file_cache_get.
	file_cache_put(fc, fd, /*size=*/1);
	ASSERT_FALSE(file_cache_get(fc, fd));
	ASSERT_FALSE(file_cache_get(fc, fd2));

	file_cache_put(fc, fd2, /*size=*/1);
	ASSERT_FALSE(file_cache_get(fc, fd));
	ASSERT_FALSE(file_cache_get(fc, fd2));
}

struct CacheAndFds
{
	FileCache *fc;
	FileData *fd;
	FileData *fd2;
	int trigger_count = 0;
};
void reentrant_cache_notify_cb(FileData *fd, NotifyType, gpointer data)
{
	auto *cache_and_fds = static_cast<CacheAndFds *>(data);
	std::cerr << "re-calling file_cache_get(fc, " << static_cast<void *>(fd) << ")\n";
	++cache_and_fds->trigger_count;
	file_cache_get(cache_and_fds->fc, fd);

	/* TODO[xsdg] switch to this version of the test once FileData can persist in the cache.

	if (fd == cache_and_fds->fd2)
	{
		// Recurse from fd2 to fd.
		file_cache_get(cache_and_fds->fc, cache_and_fds->fd);
	} else {
		// Recurse from fd to fd2.
		file_cache_get(cache_and_fds->fc, cache_and_fds->fd2);
	}
	*/
}

/**
 * Ensures that file_cache_get behaves correctly (and avoids infinite recursion) with reentrant
 * calls through the notification system.
 **/
TEST_F(FileCacheTest, ReentrantCacheGet)
{
	// TODO[xsdg]: Create a mock FileData that acts like the underlying file exists.
	fd = FileData::new_simple("/does/not/exist.jpg", &context);
	fd2 = FileData::new_simple("/does/not/exist2.jpg", &context);
	FileCache *fc = file_cache_new(&FileCacheTest::cache_release, /*max_size=*/5);

	CacheAndFds cache_and_fds = {fc, fd, fd2};
	register_notify_func_with_cleanup(
		reentrant_cache_notify_cb, &cache_and_fds, NOTIFY_PRIORITY_HIGH);

	ASSERT_FALSE(file_cache_get(fc, fd));
	file_cache_put(fc, fd, /*size=*/1);
	ASSERT_FALSE(file_cache_get(fc, fd));
	ASSERT_EQ(1, cache_and_fds.trigger_count);
}

void notify_put_get_cache_notify_cb(FileData *fd, NotifyType, gpointer data)
{
	auto *cache_and_fds = static_cast<CacheAndFds *>(data);
	std::cerr << "triggering eviction with set_max_size\n";
	file_cache_set_max_size(cache_and_fds->fc, 0);
	file_cache_set_max_size(cache_and_fds->fc, 5);

	std::cerr << "triggering file_cache_put(fc, " << static_cast<void *>(fd) << ")\n";
	file_cache_put(cache_and_fds->fc, fd, /*size=*/1);

	std::cerr << "re-calling file_cache_get(fc, " << static_cast<void *>(fd) << ")\n";
	file_cache_get(cache_and_fds->fc, fd);

	++cache_and_fds->trigger_count;
}

/**
 * Ensures that file_cache_get behaves correctly (and avoids infinite recursion) with reentrant
 * calls to cache_notify_cb, put, and get through the notification system.
 **/
TEST_F(FileCacheTest, ReentrantNotifyPutGet)
{
	// TODO[xsdg]: Create a mock FileData that acts like the underlying file exists.
	fd = FileData::new_simple("/does/not/exist.jpg", &context);
	fd2 = FileData::new_simple("/does/not/exist2.jpg", &context);
	FileCache *fc = file_cache_new(&FileCacheTest::cache_release, /*max_size=*/5);

	CacheAndFds cache_and_fds = {fc, fd, fd2};
	register_notify_func_with_cleanup(
		notify_put_get_cache_notify_cb, &cache_and_fds, NOTIFY_PRIORITY_HIGH);

	ASSERT_FALSE(file_cache_get(fc, fd));
	file_cache_put(fc, fd, /*size=*/1);
	ASSERT_FALSE(file_cache_get(fc, fd));
	ASSERT_EQ(1, cache_and_fds.trigger_count);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
