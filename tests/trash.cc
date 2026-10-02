/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <glib/gstdio.h>

#include "filedata.h"
#include "options.h"
#include "trash.h"
#include "ui-fileops.h"

namespace
{

class EmptyFolderDelete : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved_enable = options->file_ops.safe_delete_enable;
		saved_system = options->file_ops.use_system_trash;
		saved_no_trash = options->file_ops.no_trash;
		saved_path = options->file_ops.safe_delete_path;
		saved_limit = options->file_ops.safe_delete_folder_maxsize;
		root = g_dir_make_tmp("geeqie-empty-folder-XXXXXX", nullptr);
		ASSERT_NE(root, nullptr);
		folder = g_build_filename(root, "folder", nullptr);
		ASSERT_EQ(g_mkdir(folder, 0700), 0);
		options->file_ops.safe_delete_enable = TRUE;
		options->file_ops.use_system_trash = FALSE;
		options->file_ops.no_trash = FALSE;
		options->file_ops.safe_delete_path = g_build_filename(root, "trash", nullptr);
		options->file_ops.safe_delete_folder_maxsize = 0;
		trashed = g_build_filename(options->file_ops.safe_delete_path, "files", "folder", nullptr);
	}

	void TearDown() override
	{
		file_util_trash_clear();
		g_autofree gchar *files = g_build_filename(options->file_ops.safe_delete_path, "files", nullptr);
		g_autofree gchar *info = g_build_filename(options->file_ops.safe_delete_path, "info", nullptr);
		g_rmdir(files);
		g_rmdir(info);
		g_rmdir(options->file_ops.safe_delete_path);
		g_free(options->file_ops.safe_delete_path);
		options->file_ops.safe_delete_enable = saved_enable;
		options->file_ops.use_system_trash = saved_system;
		options->file_ops.no_trash = saved_no_trash;
		options->file_ops.safe_delete_path = saved_path;
		options->file_ops.safe_delete_folder_maxsize = saved_limit;
		g_rmdir(folder);
		g_rmdir(root);
		g_free(trashed);
		g_free(folder);
		g_free(root);
	}

	bool delete_folder()
	{
		auto fd = FileData::new_dir(folder);
		if (!file_data_sc_add_ci_delete(fd)) return false;
		const bool result = file_data_sc_perform_ci(fd);
		file_data_sc_free_ci(fd);
		return result;
	}

	gchar *root = nullptr;
	gchar *folder = nullptr;
	gchar *trashed = nullptr;
	gchar *saved_path = nullptr;
	gboolean saved_enable = FALSE;
	gboolean saved_system = FALSE;
	gboolean saved_no_trash = FALSE;
	gint saved_limit = 0;
};

TEST_F(EmptyFolderDelete, PrivateTrashPreservesFolderAndOriginalPath)
{
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	ASSERT_TRUE(isdir(trashed));
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_STREQ(original, folder);
	ASSERT_TRUE(file_util_safe_trash_restore(trashed, TRUE, nullptr));
	EXPECT_TRUE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
}

TEST_F(EmptyFolderDelete, ClearingTrashRemovesEmptyFoldersAndMetadata)
{
	ASSERT_TRUE(delete_folder());
	file_util_trash_clear();
	EXPECT_FALSE(isdir(trashed));
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_EQ(original, nullptr);
}

TEST_F(EmptyFolderDelete, SystemTrashPreservesFolderAndOriginalPath)
{
	// Only touch the system trash inside the test runner's disposable home.
	if (!g_str_has_prefix(g_get_home_dir(), "/tmp/tmp.") ||
	    !g_str_has_prefix(g_get_user_data_dir(), g_get_home_dir()))
		GTEST_SKIP() << "Requires an isolated temporary home";
	options->file_ops.use_system_trash = TRUE;
	ASSERT_EQ(g_rmdir(folder), 0);
	g_free(folder);
	folder = g_build_filename(root, filename_from_path(root), nullptr);
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	g_autofree gchar *system_folder = g_build_filename(g_get_user_data_dir(), "Trash", "files", filename_from_path(folder), nullptr);
	ASSERT_TRUE(isdir(system_folder));
	g_autofree gchar *info_name = g_strconcat(filename_from_path(folder), ".trashinfo", nullptr);
	g_autofree gchar *info_path = g_build_filename(g_get_user_data_dir(), "Trash", "info", info_name, nullptr);
	g_autoptr(GKeyFile) info = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(info, info_path, G_KEY_FILE_NONE, nullptr));
	g_autofree gchar *escaped = g_key_file_get_string(info, "Trash Info", "Path", nullptr);
	g_autofree gchar *original = g_uri_unescape_string(escaped, nullptr);
	EXPECT_STREQ(original, folder);
	EXPECT_TRUE(move_file(system_folder, folder));
	g_unlink(info_path);
}

TEST_F(EmptyFolderDelete, DuplicateFolderNamesKeepBothTrashEntries)
{
	ASSERT_TRUE(delete_folder());
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	ASSERT_TRUE(delete_folder());
	g_autofree gchar *second = g_build_filename(options->file_ops.safe_delete_path, "files", "folder.2", nullptr);
	EXPECT_TRUE(isdir(trashed));
	EXPECT_TRUE(isdir(second));
}

TEST_F(EmptyFolderDelete, DisabledTrashPermanentlyRemovesFolder)
{
	options->file_ops.safe_delete_enable = FALSE;
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
}

TEST_F(EmptyFolderDelete, PermanentDeleteOverrideRemovesFolder)
{
	options->file_ops.no_trash = TRUE;
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
}

TEST_F(EmptyFolderDelete, NonemptyFolderIsLeftIntact)
{
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	EXPECT_FALSE(delete_folder());
	EXPECT_TRUE(isdir(folder));
	EXPECT_TRUE(isfile(child));
	g_unlink(child);
}

} // namespace
