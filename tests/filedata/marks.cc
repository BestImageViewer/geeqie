/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "filedata.h"

#include <string>

#include <glib/gstdio.h>

#include "gtest/gtest.h"

#include "options.h"

namespace
{

class SavedMarks : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		directory = g_dir_make_tmp("geeqie-saved-marks-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		backup = g_build_filename(directory, "backup", nullptr);
		marks = g_build_filename(directory, "marks", nullptr);
		image = g_build_filename(directory, "image,with,commas.jpg", nullptr);
		ASSERT_TRUE(marks_list_save(backup, TRUE));
		marks_clear_all();
	}

	void TearDown() override
	{
		if (!directory) return;
		marks_clear_all();
		marks_list_load(backup);
		for (const gchar *path : {backup, marks, image}) if (path) g_unlink(path);
		g_rmdir(directory);
		g_free(backup);
		g_free(marks);
		g_free(image);
		g_free(directory);
	}

	void write_marks(const char *mask = "513")
	{
		g_autofree gchar *contents = g_strdup_printf("#Marks lists\n%s,%s\n#end\n", image, mask);
		ASSERT_TRUE(g_file_set_contents(marks, contents, -1, nullptr));
	}

	std::string saved_contents()
	{
		g_autofree gchar *contents = nullptr;
		if (!g_file_get_contents(marks, &contents, nullptr, nullptr)) return {};
		return contents;
	}

	gchar *directory = nullptr;
	gchar *backup = nullptr;
	gchar *marks = nullptr;
	gchar *image = nullptr;
};

TEST_F(SavedMarks, MissingFileSurvivesRepeatedLoadsAndSavesAndReturnsWithMarks)
{
	write_marks();
	for (int i = 0; i < 3; ++i)
		{
		ASSERT_TRUE(marks_list_load(marks));
		ASSERT_TRUE(marks_list_save(marks, TRUE));
		EXPECT_NE(saved_contents().find(std::string(image) + ",513\n"), std::string::npos);
		marks_clear_all();
		}
	ASSERT_TRUE(marks_list_load(marks));
	ASSERT_TRUE(g_file_set_contents(image, "test", -1, nullptr));
	auto fd = FileData::new_no_grouping(image);
	EXPECT_TRUE(file_data_get_mark(fd, 0));
	EXPECT_TRUE(file_data_get_mark(fd, 9));
	EXPECT_FALSE(file_data_get_mark(fd, 1));
}

TEST_F(SavedMarks, FileDisappearingDuringSessionKeepsMarksAndExplicitClearRemovesThem)
{
	ASSERT_TRUE(g_file_set_contents(image, "test", -1, nullptr));
	auto fd = FileData::new_no_grouping(image);
	file_data_set_mark(fd, 1, TRUE);
	ASSERT_EQ(g_unlink(image), 0);
	ASSERT_TRUE(marks_list_save(marks, TRUE));
	EXPECT_NE(saved_contents().find(std::string(image) + ",2\n"), std::string::npos);
	marks_clear_all();
	EXPECT_EQ(fd->marks, 0U);
	ASSERT_TRUE(marks_list_save(marks, TRUE));
	EXPECT_EQ(saved_contents().find(image), std::string::npos);
}

TEST_F(SavedMarks, ClearingOneUnavailableMarkDoesNotRestoreItsOldValue)
{
	write_marks();
	ASSERT_TRUE(marks_list_load(marks));
	auto fd = FileData::new_no_grouping(image);
	file_data_set_mark(fd, 0, FALSE);
	ASSERT_TRUE(marks_list_save(marks, TRUE));
	EXPECT_NE(saved_contents().find(std::string(image) + ",512\n"), std::string::npos);
}

TEST_F(SavedMarks, ConfirmedDeletionDropsSavedMarks)
{
	ASSERT_TRUE(g_file_set_contents(image, "test", -1, nullptr));
	auto fd = FileData::new_no_grouping(image);
	file_data_set_mark(fd, 0, TRUE);
	ASSERT_TRUE(file_data_add_ci(fd, FILEDATA_CHANGE_DELETE, nullptr, nullptr));
	ASSERT_EQ(g_unlink(image), 0);
	EXPECT_TRUE(file_data_apply_ci(fd));
	file_data_free_ci(fd);
	EXPECT_EQ(fd->marks, 0U);
	ASSERT_TRUE(marks_list_save(marks, TRUE));
	EXPECT_EQ(saved_contents().find(image), std::string::npos);
}

TEST_F(SavedMarks, DisablingPersistenceWritesNoUnavailableEntries)
{
	write_marks();
	ASSERT_TRUE(marks_list_load(marks));
	ASSERT_TRUE(marks_list_save(marks, FALSE));
	EXPECT_EQ(saved_contents(), "#Marks lists\n#end\n");
}

TEST_F(SavedMarks, DeletingFolderDropsDescendantMarksButNotSiblingMarks)
{
	g_autofree gchar *folder = g_build_filename(directory, "photos", nullptr);
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	g_autofree gchar *child_path = g_build_filename(folder, "child.jpg", nullptr);
	ASSERT_TRUE(g_file_set_contents(child_path, "test", -1, nullptr));
	ASSERT_TRUE(g_file_set_contents(image, "test", -1, nullptr));
	auto child = FileData::new_no_grouping(child_path);
	auto sibling = FileData::new_no_grouping(image);
	auto parent = FileData::new_dir(folder);
	file_data_set_mark(child, 0, TRUE);
	file_data_set_mark(sibling, 1, TRUE);
	ASSERT_TRUE(file_data_add_ci(parent, FILEDATA_CHANGE_DELETE, nullptr, nullptr));
	EXPECT_EQ(g_unlink(child_path), 0);
	EXPECT_EQ(g_rmdir(folder), 0);
	EXPECT_TRUE(file_data_apply_ci(parent));
	file_data_free_ci(parent);
	EXPECT_EQ(child->marks, 0U);
	EXPECT_EQ(sibling->marks, 2U);
	ASSERT_TRUE(marks_list_save(marks, TRUE));
	EXPECT_EQ(saved_contents().find(child_path), std::string::npos);
	EXPECT_NE(saved_contents().find(std::string(image) + ",2\n"), std::string::npos);
}

TEST_F(SavedMarks, InvalidRecordsAreIgnored)
{
	for (const char *mask : {"", "text", "-1", "0", "1024", "18446744073709551616", "1extra"})
		{
		SCOPED_TRACE(mask);
		write_marks(mask);
		ASSERT_TRUE(marks_list_load(marks));
		ASSERT_TRUE(marks_list_save(marks, TRUE));
		EXPECT_EQ(saved_contents(), "#Marks lists\n#end\n");
		}
}

} // namespace
