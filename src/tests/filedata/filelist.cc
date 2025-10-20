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

#include <glib.h>

#include "filedata.h"
#include "filefilter.h"

namespace {

// For convenience.
namespace t = ::testing;


class FileDataSortTest : public t::Test
{
    protected:
	void SetUp() override
	{
		// We construct these so that fd_first < fd_middle < f_last in
		// every sortable attribute.
		fd_first = FileData::file_data_new_simple("/noexist/noexist/1_first.jpg", &context);
		fd_first->size = 11;
		fd_first->date = fd_first->cdate = 1111111111;
		fd_first->exifdate = fd_first->exifdate_digitized = 1111111111;
		fd_first->rating = 1;
		fd_first->format_class = FORMAT_CLASS_IMAGE;

		fd_middle = FileData::file_data_new_simple("/noexist/noexist/2_middle.jpg", &context);
		fd_middle->size = 222;
		fd_middle->date = fd_middle->cdate = 2222222222;
		fd_middle->exifdate = fd_middle->exifdate_digitized = 2222222222;
		fd_middle->rating = 2;
		fd_middle->format_class = FORMAT_CLASS_RAWIMAGE;

		fd_last = FileData::file_data_new_simple("/noexist/noexist/3_last.jpg", &context);
		fd_last->size = 3333;
		fd_last->date = fd_last->cdate = 3333333333;
		fd_last->exifdate = fd_last->exifdate_digitized = 3333333333;
		fd_last->rating = 3;
		fd_last->format_class = FORMAT_CLASS_META;
	}

	void TearDown() override
	{
		file_data_unref(fd_last);
		fd_last = nullptr;

		file_data_unref(fd_middle);
		fd_middle = nullptr;

		file_data_unref(fd_first);
		fd_first = nullptr;
	}

	FileData *fd_first = nullptr;
	FileData *fd_middle = nullptr;
	FileData *fd_last = nullptr;
	FileDataContext context;

	FileData::FileList::SortSettings default_sort = {SORT_NAME, TRUE, TRUE};
	FileData::FileList::SortSettings reverse_sort = {SORT_NAME, FALSE, TRUE};
};

TEST_F(FileDataSortTest, BasicCompare)
{
	// Convenience alias.
	auto &sort_compare_filedata = FileData::FileList::sort_compare_filedata;

	// Expect natural sort, and reverse_sort option inverts result.
	EXPECT_LT(sort_compare_filedata(fd_first, fd_middle, &default_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_middle, fd_last, &default_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_first, fd_last, &default_sort), 0);

	EXPECT_GT(sort_compare_filedata(fd_first, fd_middle, &reverse_sort), 0);
	EXPECT_GT(sort_compare_filedata(fd_middle, fd_last, &reverse_sort), 0);
	EXPECT_GT(sort_compare_filedata(fd_first, fd_last, &reverse_sort), 0);

	// Swapping argument order should give the opposite results compared to above.
	EXPECT_GT(sort_compare_filedata(fd_middle, fd_first, &default_sort), 0);
	EXPECT_GT(sort_compare_filedata(fd_last, fd_middle, &default_sort), 0);
	EXPECT_GT(sort_compare_filedata(fd_last, fd_first, &default_sort), 0);

	EXPECT_LT(sort_compare_filedata(fd_middle, fd_first, &reverse_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_last, fd_middle, &reverse_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_last, fd_first, &reverse_sort), 0);

	// Each should compare equal to itself, regardless of sort direction.
	EXPECT_EQ(sort_compare_filedata(fd_first, fd_first, &default_sort), 0);
	EXPECT_EQ(sort_compare_filedata(fd_first, fd_first, &reverse_sort), 0);
	EXPECT_EQ(sort_compare_filedata(fd_middle, fd_middle, &default_sort), 0);
	EXPECT_EQ(sort_compare_filedata(fd_middle, fd_middle, &reverse_sort), 0);
	EXPECT_EQ(sort_compare_filedata(fd_last, fd_last, &default_sort), 0);
	EXPECT_EQ(sort_compare_filedata(fd_last, fd_last, &reverse_sort), 0);
}

TEST_F(FileDataSortTest, CompareByEachNonPathTrait)
{
	// Convenience alias.
	auto &sort_compare_filedata = FileData::FileList::sort_compare_filedata;

	// In order to ensure that we're getting a result from the specified
	// trait, we set the collate_key_name, collate_key_name_nocase, AND
	// original_path values to the same value.
	g_free(fd_middle->collate_key_name);
	fd_middle->collate_key_name = g_strdup(fd_first->collate_key_name);
	g_free(fd_middle->collate_key_name_nocase);
	fd_middle->collate_key_name_nocase = g_strdup(fd_first->collate_key_name_nocase);
	g_free(fd_middle->original_path);
	fd_middle->original_path = g_strdup(fd_first->original_path);

	g_free(fd_last->collate_key_name);
	fd_last->collate_key_name = g_strdup(fd_first->collate_key_name);
	g_free(fd_last->collate_key_name_nocase);
	fd_last->collate_key_name_nocase = g_strdup(fd_first->collate_key_name_nocase);
	g_free(fd_last->original_path);
	fd_last->original_path = g_strdup(fd_first->original_path);

	// Sorting by things that aren't name, so excluding SORT_NONE, SORT_NAME,
	// SORT_NUMBER, and SORT_PATH.
	for (const auto &sort_type : {SORT_SIZE, SORT_TIME, SORT_CTIME, SORT_NUMBER,
				      SORT_EXIFTIME, SORT_EXIFTIMEDIGITIZED, SORT_RATING,
				      SORT_CLASS})
		{
		// This shows the sort_type in any assertion failure messages.
		SCOPED_TRACE(std::to_string(sort_type));

		FileData::FileList::SortSettings normal_sort = {sort_type, TRUE, TRUE};
		FileData::FileList::SortSettings reverse_sort = {sort_type, FALSE, TRUE};

		EXPECT_LT(sort_compare_filedata(fd_first, fd_middle, &normal_sort), 0);
		EXPECT_LT(sort_compare_filedata(fd_middle, fd_last, &normal_sort), 0);
		EXPECT_LT(sort_compare_filedata(fd_first, fd_last, &normal_sort), 0);

		EXPECT_GT(sort_compare_filedata(fd_first, fd_middle, &reverse_sort), 0);
		EXPECT_GT(sort_compare_filedata(fd_middle, fd_last, &reverse_sort), 0);
		EXPECT_GT(sort_compare_filedata(fd_first, fd_last, &reverse_sort), 0);
		}
}

TEST_F(FileDataSortTest, NumberSort)
{
	// Convenience alias.
	auto &sort_compare_filedata = FileData::FileList::sort_compare_filedata;

	// We create multiple filedatas which only differ in the path name (plus
	// ref holders that will clean them up when they go out of scope)
	FileData *fd_1 = FileData::file_data_new_simple("/noexist/noexist/1_image.jpg", &context);
	FileDataRef fd_1_ref(*fd_1, /*skip_ref=*/TRUE);
	FileData *fd_5 = FileData::file_data_new_simple("/noexist/noexist/5_image.jpg", &context);
	FileDataRef fd_5_ref(*fd_5, /*skip_ref=*/TRUE);
	FileData *fd_10 = FileData::file_data_new_simple("/noexist/noexist/10_image.jpg", &context);
	FileDataRef fd_10_ref(*fd_10, /*skip_ref=*/TRUE);
	FileData *fd_50 = FileData::file_data_new_simple("/noexist/noexist/50_image.jpg", &context);
	FileDataRef fd_50_ref(*fd_50, /*skip_ref=*/TRUE);

	FileData::FileList::SortSettings number_sort = {SORT_NUMBER, TRUE, TRUE};

	EXPECT_LT(sort_compare_filedata(fd_1, fd_5, &default_sort), 0);
	// ASCII '0' == 0x30.  ASCII '_' == 0x5F.  So with default sort, we expect
	// "1_image.jpg" to sort _later_ than (greater-than) "10_image.jpg".  But we
	// expect that filenames of the same length will sort numerically.
	EXPECT_GT(sort_compare_filedata(fd_1, fd_10, &default_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_1, fd_50, &default_sort), 0);

	// '5' > '1', so "5_image.jpg" also sorts later than "10_image.jpg".
	EXPECT_GT(sort_compare_filedata(fd_5, fd_10, &default_sort), 0);
	EXPECT_GT(sort_compare_filedata(fd_5, fd_50, &default_sort), 0);

	EXPECT_LT(sort_compare_filedata(fd_10, fd_50, &default_sort), 0);

	// However, number sort should consider the entire numerical part all
	// together, which should sort "1_image.jpg" earlier than (less-than)
	// "10_image.jpg".
	EXPECT_LT(sort_compare_filedata(fd_1, fd_5, &number_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_1, fd_10, &number_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_1, fd_50, &number_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_5, fd_10, &number_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_5, fd_50, &number_sort), 0);
	EXPECT_LT(sort_compare_filedata(fd_10, fd_50, &number_sort), 0);
}

TEST_F(FileDataSortTest, TieBreakerFallbackBehavior)
{
	// Convenience alias.
	auto &sort_compare_filedata = FileData::FileList::sort_compare_filedata;

	// Create a FileData that is identical to fd_middle except for original_path.
	FileData *fd_other_middle = FileData::file_data_new_simple("/noexist/otherdir/2_middle.jpg", &context);
	FileDataRef fd_other_middle_ref(*fd_other_middle, /*skip_ref=*/TRUE);
	fd_other_middle->size = fd_middle->size;
	fd_other_middle->date = fd_middle->date;
	fd_other_middle->cdate = fd_middle->cdate;
	fd_other_middle->exifdate = fd_middle->exifdate;
	fd_other_middle->exifdate_digitized = fd_middle->exifdate_digitized;
	fd_other_middle->rating = fd_middle->rating;
	fd_other_middle->format_class = fd_middle->format_class;

	// "noexist" < "otherdir", so we expect fd_middle < fd_other_middle in all
	// cases, since original_path is the last fallback.  But we still expect
	// fd_first < fd_other_middle and fd_other_middle < fd_last, since pathname
	// shouldn't be considered except when filenames are identical.
	//
	// Sorting by things that aren't name, so excluding SORT_NONE, SORT_NAME,
	// SORT_NUMBER, and SORT_PATH.
	for (const auto &sort_type : {SORT_SIZE, SORT_TIME, SORT_CTIME, SORT_NUMBER,
				      SORT_EXIFTIME, SORT_EXIFTIMEDIGITIZED, SORT_RATING,
				      SORT_CLASS})
		{
		// This shows the sort_type in any assertion failure messages.
		SCOPED_TRACE(std::to_string(sort_type));

		FileData::FileList::SortSettings settings = {sort_type, TRUE, TRUE};
		EXPECT_LT(sort_compare_filedata(fd_first, fd_other_middle, &settings), 0);
		EXPECT_LT(sort_compare_filedata(fd_middle, fd_other_middle, &settings), 0);
		EXPECT_LT(sort_compare_filedata(fd_other_middle, fd_last, &settings), 0);
		}
}

TEST_F(FileDataSortTest, CaseSensitivity)
{
	// Convenience aliases.
	auto &sort_compare_filedata = FileData::FileList::sort_compare_filedata;
	using SortSettings = FileData::FileList::SortSettings;

	FileData *fd_lower_1 = FileData::file_data_new_simple("/noexist/noexist/1_image.jpg", &context);
	FileDataRef fd_lower_1_ref(*fd_lower_1, /*skip_ref=*/TRUE);
	FileData *fd_upper_1 = FileData::file_data_new_simple("/noexist/noexist/1_IMAGE.JPG", &context);
	FileDataRef fd_upper_1_ref(*fd_upper_1, /*skip_ref=*/TRUE);
	FileData *fd_lower_10 = FileData::file_data_new_simple("/noexist/noexist/10_image.jpg", &context);
	FileDataRef fd_lower_10_ref(*fd_lower_10, /*skip_ref=*/TRUE);
	FileData *fd_upper_10 = FileData::file_data_new_simple("/noexist/noexist/10_IMAGE.JPG", &context);
	FileDataRef fd_upper_10_ref(*fd_upper_10, /*skip_ref=*/TRUE);

	// To avoid inadvertently relying on the original_path fallthrough behavior,
	// we set all of the original_paths to be identical.
	g_free(fd_upper_1->original_path);
	fd_upper_1->original_path = g_strdup(fd_lower_1->original_path);
	g_free(fd_lower_10->original_path);
	fd_lower_10->original_path = g_strdup(fd_lower_1->original_path);
	g_free(fd_upper_10->original_path);
	fd_upper_10->original_path = g_strdup(fd_lower_1->original_path);

	// Since SORT_NUMBER depends on the filename, we also check for
	// interactions between case_sensitive and SORT_NUMBER/SORT_NAME
	SortSettings sort_by_name_with_case = {SORT_NAME, TRUE, TRUE};
	SortSettings sort_by_name_without_case = {SORT_NAME, TRUE, FALSE};
	SortSettings sort_by_number_with_case = {SORT_NUMBER, TRUE, TRUE};
	SortSettings sort_by_number_without_case = {SORT_NUMBER, TRUE, FALSE};

	// Comparing upper- vs. lower-case with the same number.
	// Note that ASCII 'A' = 0x41, but ASCII 'a' = 0x61, so we expect the
	// upper-case versions to sort earlier-than (less-than) the lower-case
	// versions when case is considered.
	EXPECT_EQ(sort_compare_filedata(fd_upper_1, fd_lower_1, &sort_by_name_without_case), 0);
	// BUG[xsdg]: The following expectation fails, because SORT_NUMBER disregards
	// the case_sensitive setting.
	EXPECT_EQ(sort_compare_filedata(fd_upper_1, fd_lower_1, &sort_by_number_without_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_upper_1, fd_lower_1, &sort_by_name_with_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_upper_1, fd_lower_1, &sort_by_number_with_case), 0);

	//// We only expect case to matter when the numbers are the same.  So below,
	//// we expect with/without case results to match.

	// Comparing same case with different numbers.
	EXPECT_GT(sort_compare_filedata(fd_upper_1, fd_upper_10, &sort_by_name_without_case), 0);
	EXPECT_GT(sort_compare_filedata(fd_upper_1, fd_upper_10, &sort_by_name_with_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_upper_1, fd_upper_10, &sort_by_number_without_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_upper_1, fd_upper_10, &sort_by_number_with_case), 0);

	// Comparing cross-case with different numbers (both ways).
	EXPECT_GT(sort_compare_filedata(fd_lower_1, fd_upper_10, &sort_by_name_without_case), 0);
	EXPECT_GT(sort_compare_filedata(fd_lower_1, fd_upper_10, &sort_by_name_with_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_lower_1, fd_upper_10, &sort_by_number_without_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_lower_1, fd_upper_10, &sort_by_number_with_case), 0);

	EXPECT_GT(sort_compare_filedata(fd_upper_1, fd_lower_10, &sort_by_name_without_case), 0);
	EXPECT_GT(sort_compare_filedata(fd_upper_1, fd_lower_10, &sort_by_name_with_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_upper_1, fd_lower_10, &sort_by_number_without_case), 0);
	EXPECT_LT(sort_compare_filedata(fd_upper_1, fd_lower_10, &sort_by_number_with_case), 0);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
