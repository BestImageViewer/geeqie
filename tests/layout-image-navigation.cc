/*
 * Copyright (C) 2026 The Geeqie Team
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
 * Unit tests for layout-image folder navigation
 *
 */

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <string>
#include <utility>
#include <vector>

#include <gio/gio.h>
#include <glib.h>

#include "filedata.h"
#include "filefilter.h"
#include "layout.h"
#include "options.h"

namespace {

// For convenience.
namespace t = ::testing;

class LayoutImageNavigationTest : public t::Test
{
    protected:
    void SetUp() override
    {
        // Initialize global options - required by FileData::FileList::read_list_real()
        // which accesses options->file_filter.show_hidden_files and
        // is_hidden_file() which accesses options->file_filter.dot_prefix_hidden_files
        if (!options) {
            options = conf_options_new();
        }
        setup_default_options(options);
        filter_add_defaults();
        filter_rebuild();

        // Create a temporary directory structure for testing
        // Use g_mkdtemp for reliable temp directory creation.
        // Note: g_mkdtemp modifies the template in place and returns it,
        // so we must NOT free the template after the call.
        gchar *tmp_template = g_build_filename(g_get_tmp_dir(), "geeqie_test_XXXXXX", nullptr);
        test_dir = g_mkdtemp(tmp_template);
        // tmp_template and test_dir now point to the same memory - do not free tmp_template
        ASSERT_NE(test_dir, nullptr);
        
        // Create parent directory
        parent_dir = g_build_filename(test_dir, "parent", nullptr);
        g_mkdir_with_parents(parent_dir, 0755);
        
        // Create subdirectories with images
        dir_a = g_build_filename(parent_dir, "folder_a", nullptr);
        g_mkdir_with_parents(dir_a, 0755);
        
        dir_b = g_build_filename(parent_dir, "folder_b", nullptr);
        g_mkdir_with_parents(dir_b, 0755);
        
        dir_c = g_build_filename(parent_dir, "folder_c", nullptr);
        g_mkdir_with_parents(dir_c, 0755);
        
        // Create empty directory (no images)
        dir_empty = g_build_filename(parent_dir, "folder_empty", nullptr);
        g_mkdir_with_parents(dir_empty, 0755);
        
        // Create test image files
        create_test_image(g_build_filename(dir_a, "image1.jpg", nullptr));
        create_test_image(g_build_filename(dir_a, "image2.jpg", nullptr));
        create_test_image(g_build_filename(dir_b, "image1.jpg", nullptr));
        create_test_image(g_build_filename(dir_c, "image1.jpg", nullptr));
        create_test_image(g_build_filename(dir_c, "image2.jpg", nullptr));
        create_test_image(g_build_filename(dir_c, "image3.jpg", nullptr));
    }
    
    void TearDown() override
    {
        if (test_dir)
        {
            g_file_delete(g_file_new_for_path(test_dir), nullptr, nullptr);
            g_free(test_dir);
        }

        // Free global options allocated in SetUp()
        filter_reset();
        g_free(options);
        options = nullptr;
    }
    
    static void create_test_image(const gchar *path)
    {
        GFile *file = g_file_new_for_path(path);
        GFileOutputStream *stream = g_file_create(file, G_FILE_CREATE_NONE, nullptr, nullptr);
        if (stream)
        {
            // Write a minimal JPEG header
            const guint8 jpeg_header[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00};
            g_output_stream_write(G_OUTPUT_STREAM(stream), jpeg_header, sizeof(jpeg_header), nullptr, nullptr);
            g_output_stream_close(G_OUTPUT_STREAM(stream), nullptr, nullptr);
            g_object_unref(stream);
        }
        g_object_unref(file);
    }
    
    gchar *test_dir = nullptr;
    gchar *parent_dir = nullptr;
    gchar *dir_a = nullptr;
    gchar *dir_b = nullptr;
    gchar *dir_c = nullptr;
    gchar *dir_empty = nullptr;
};

TEST_F(LayoutImageNavigationTest, FileListReadListReturnsDirectories)
{
    FileData *parent_fd = file_data_new_dir(parent_dir);
    ASSERT_NE(parent_fd, nullptr);
    
    GList *files = nullptr;
    GList *dirs = nullptr;
    
    gboolean result = FileData::FileList::read_list(parent_fd, &files, &dirs);
    ASSERT_TRUE(result);
    
    // Should find 4 directories (folder_a, folder_b, folder_c, folder_empty)
    guint dir_count = g_list_length(dirs);
    EXPECT_EQ(4U, dir_count);
    
    // Verify directory names
    std::vector<std::string> dir_names;
    for (GList *work = dirs; work; work = work->next)
    {
        auto *fd = static_cast<FileData *>(work->data);
        g_autofree gchar *name = g_path_get_basename(fd->path);
        dir_names.emplace_back(name);
    }
    
    EXPECT_TRUE(std::find(dir_names.begin(), dir_names.end(), "folder_a") != dir_names.end());
    EXPECT_TRUE(std::find(dir_names.begin(), dir_names.end(), "folder_b") != dir_names.end());
    EXPECT_TRUE(std::find(dir_names.begin(), dir_names.end(), "folder_c") != dir_names.end());
    EXPECT_TRUE(std::find(dir_names.begin(), dir_names.end(), "folder_empty") != dir_names.end());
    
    // Clean up
    g_list_free_full(dirs, reinterpret_cast<GDestroyNotify>(file_data_unref));
    if (files) g_list_free(files);
    file_data_unref(parent_fd);
}

TEST_F(LayoutImageNavigationTest, FileListSortByName)
{
    FileData *parent_fd = file_data_new_dir(parent_dir);
    ASSERT_NE(parent_fd, nullptr);
    
    GList *files = nullptr;
    GList *dirs = nullptr;
    
    gboolean result = FileData::FileList::read_list(parent_fd, &files, &dirs);
    ASSERT_TRUE(result);
    
    FileData::FileList::SortSettings sort_settings;
    sort_settings.method = SORT_NAME;
    sort_settings.ascending = TRUE;
    sort_settings.case_sensitive = FALSE;
    
    dirs = FileData::FileList::sort(dirs, sort_settings);
    
    // Verify alphabetical order
    std::vector<std::string> dir_names;
    for (GList *work = dirs; work; work = work->next)
    {
        auto *fd = static_cast<FileData *>(work->data);
        g_autofree gchar *name = g_path_get_basename(fd->path);
        dir_names.emplace_back(name);
    }
    
    EXPECT_EQ("folder_a", dir_names[0]);
    EXPECT_EQ("folder_b", dir_names[1]);
    EXPECT_EQ("folder_c", dir_names[2]);
    EXPECT_EQ("folder_empty", dir_names[3]);
    
    // Clean up
    g_list_free_full(dirs, reinterpret_cast<GDestroyNotify>(file_data_unref));
    if (files) g_list_free(files);
    file_data_unref(parent_fd);
}

TEST_F(LayoutImageNavigationTest, DirectoryWithImagesHasFiles)
{
    FileData *dir_a_fd = file_data_new_dir(dir_a);
    ASSERT_NE(dir_a_fd, nullptr);
    
    GList *files = nullptr;
    GList *dirs = nullptr;
    
    gboolean result = FileData::FileList::read_list(dir_a_fd, &files, &dirs);
    ASSERT_TRUE(result);
    
    // Should find 2 image files
    guint file_count = g_list_length(files);
    EXPECT_EQ(2U, file_count);
    
    // Clean up
    g_list_free_full(files, reinterpret_cast<GDestroyNotify>(file_data_unref));
    if (dirs) g_list_free(dirs);
    file_data_unref(dir_a_fd);
}

TEST_F(LayoutImageNavigationTest, EmptyDirectoryHasNoFiles)
{
    FileData *dir_empty_fd = file_data_new_dir(dir_empty);
    ASSERT_NE(dir_empty_fd, nullptr);
    
    GList *files = nullptr;
    GList *dirs = nullptr;
    
    gboolean result = FileData::FileList::read_list(dir_empty_fd, &files, &dirs);
    ASSERT_TRUE(result);
    
    // Should find 0 image files
    guint file_count = g_list_length(files);
    EXPECT_EQ(0U, file_count);
    
    // Clean up
    if (files) g_list_free(files);
    if (dirs) g_list_free(dirs);
    file_data_unref(dir_empty_fd);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
