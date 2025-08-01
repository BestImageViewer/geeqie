/*
 * Copyright (C) 2024 The Geeqie Team
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
 * Unit tests for filedata.cc
 *
 */

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <string>
#include <utility>
#include <vector>

#include <glib.h>

#include "filedata.h"

namespace {

// For convenience.
namespace t = ::testing;


class FileDataTest : public t::Test
{
    protected:
	void TearDown() override
	{
		if (fd != nullptr)
		{
			g_free(fd);
			fd = nullptr;
		}
		if (parent_fd != nullptr)
		{
			g_free(parent_fd);
			parent_fd = nullptr;
		}
	}

	FileData *fd = nullptr;
	FileData *parent_fd = nullptr;
	FileDataContext context;
};

TEST_F(FileDataTest, text_from_size_test)
{
	std::vector<std::pair<gint64, std::string>> test_cases = {
		{0, "0"},
		{1, "1"},
		{999, "999"},
		{1000, "1,000"},
		{1'000'000, "1,000,000"},
		{2'147'483'648, "2,147,483,648"}, // INT_MAX + 1
		{-1, "-1"},
		{-10, "-10"},
		{-100, "-100"},
		{-1000, "-1,000"},
		{-10'000, "-10,000"},
		{-100'000, "-100,000"},
	};

	for (const auto &test_case : test_cases)
	{
		g_autofree gchar *generated = FileData::text_from_size(test_case.first);
		ASSERT_EQ(test_case.second, std::string(generated));
	}
}

#ifdef DEBUG_FILEDATA
TEST_F(FileDataTest, FileDataNewSimpleAndFree)
{
	ASSERT_EQ(0, context.global_file_data_count);

	auto *_fd = FileData::file_data_new_simple("/does/not/exist.jpg", &context);
	ASSERT_NE(_fd, nullptr);
	ASSERT_EQ(1, context.global_file_data_count);
	ASSERT_EQ(1, _fd->ref);

	_fd->file_data_unref();
	_fd = nullptr;
	ASSERT_EQ(0, context.global_file_data_count);
}
#endif

#ifdef DEBUG_FILEDATA
TEST_F(FileDataTest, FileDataNewGroupAndFree)
{
	ASSERT_EQ(0, context.global_file_data_count);

	auto *_fd = FileData::file_data_new_group("/does/not/exist/file.jpg", &context);
	ASSERT_NE(_fd, nullptr);
	EXPECT_EQ(1, context.global_file_data_count);
	ASSERT_EQ(1, _fd->ref);

	_fd->file_data_unref();
	_fd = nullptr;
	ASSERT_EQ(0, context.global_file_data_count);
}
#endif

TEST_F(FileDataTest, BasicIncrementVersion)
{
	fd = g_new0(FileData, 1);
	fd->valid_marks = 0x4;

	file_data_increment_version(fd);

	ASSERT_EQ(1, fd->version);
	ASSERT_EQ(0x0, fd->valid_marks);
}

TEST_F(FileDataTest, BasicIncrementVersionWithParent)
{
	parent_fd = g_new0(FileData, 1);
	parent_fd->valid_marks = 0x8;
	fd = g_new0(FileData, 1);
	fd->valid_marks = 0x4;
	fd->parent = parent_fd;

	file_data_increment_version(fd);

	ASSERT_EQ(1, fd->version);
	ASSERT_EQ(0x0, fd->valid_marks);
	ASSERT_EQ(1, parent_fd->version);
	ASSERT_EQ(0x0, parent_fd->valid_marks);
}

TEST_F(FileDataTest, FileDataRef)
{
	fd = g_new0(FileData, 1);
	fd->magick = FD_MAGICK;
	// Avoids having the FileData object automatically freed when its
	// refcount drops back to zero.
	file_data_lock(fd);

	// Refcount is 0 outside of the FileDataRef scope.
	ASSERT_EQ(0, fd->ref);

	{
		// Refcount is 0 inside of the FileDataRef scope, but before it's defined.
		ASSERT_EQ(0, fd->ref);

		// Refcount should increase by 1 after the FileDataRef is created.
		FileDataRef fd_ref(*fd);
		ASSERT_EQ(1, fd->ref);

		// Refcount should increase by 1 more with the second FileDataRef.
		FileDataRef fd_ref2(*fd);
		ASSERT_EQ(2, fd->ref);
	}

	// And refcount drops back down to 0 after both of the FileDataRefs go out of scope.
	ASSERT_EQ(0, fd->ref);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
