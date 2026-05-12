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
		g_clear_pointer(&fd, g_free);
		g_clear_pointer(&fd2, g_free);
	}

	static void cache_release(FileData *fd)
	{
		std::cerr << "released " << static_cast<void *>(fd) << " (" << fd->path << ")\n";
	}

	FileData *fd = nullptr;
	FileData *fd2 = nullptr;
	FileDataContext context;
};


TEST_F(FileCacheTest, BasicLifecycle)
{
	// TODO[xsdg]: Create a mock FileData that acts like the underlying file exists.
	fd = FileData::file_data_new_simple("/does/not/exist.jpg", &context);
	fd2 = FileData::file_data_new_simple("/does/not/exist2.jpg", &context);
	FileCacheData *fc = file_cache_new(&FileCacheTest::cache_release, /*max_size=*/5);

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

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
