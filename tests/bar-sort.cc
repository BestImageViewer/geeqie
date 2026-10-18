/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <glib/gstdio.h>

#include "bar-sort.h"
#include "collect.h"
#include "collect-io.h"
#include "filedata.h"
#include "history-list.h"
#include "options.h"

namespace
{

class SortCollection : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		collect_manager_flush();
		if (auto *history = history_list_find_by_key(key)) saved_history = *history;
		if (auto *recent = history_list_find_by_key("collection_history")) saved_recent = *recent;
		history_list_free_key(key);
		directory = g_dir_make_tmp("geeqie-sort-collection-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "external.gqv", nullptr);
		image_path = g_build_filename(directory, "image.svg", nullptr);
	}

	void TearDown() override
	{
		collect_manager_flush();
		history_list_free_key(key);
		for (auto it = saved_history.crbegin(); it != saved_history.crend(); ++it)
			history_list_add_to_key(key, it->c_str(), 0);
		history_list_free_key("collection_history");
		for (auto it = saved_recent.crbegin(); it != saved_recent.crend(); ++it)
			history_list_add_to_key("collection_history", it->c_str(), 0);
		if (path) g_unlink(path);
		if (image_path) g_unlink(image_path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(image_path);
		g_free(directory);
	}

	static constexpr const gchar *key = "sort_manager_external_collections";
	HistoryList saved_history;
	HistoryList saved_recent;
	gchar *directory = nullptr;
	gchar *path = nullptr;
	gchar *image_path = nullptr;
};

TEST_F(SortCollection, CreatesNewAndPreservesExistingCollection)
{
	ASSERT_TRUE(bar_sort_collection_add(path));
	g_autofree gchar *original = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &original, nullptr, nullptr));
	ASSERT_TRUE(bar_sort_collection_add(path));
	g_autofree gchar *after = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &after, nullptr, nullptr));
	EXPECT_STREQ(after, original);
	const HistoryList *history = history_list_find_by_key(key);
	ASSERT_NE(history, nullptr);
	EXPECT_EQ(history->size(), 1U);
	EXPECT_EQ(history->front(), path);

	// External collections must receive queued additions independently of recent history.
	ASSERT_TRUE(g_file_set_contents(image_path, "<svg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/>", -1, nullptr));
	history_list_free_key("collection_history");
	FileDataRef image = FileData::new_group(image_path);
	collect_manager_add(image, path);
	collect_manager_flush();
	g_autoptr(CollectionData) collection = collection_new(path);
	ASSERT_TRUE(collection_load(collection, path, COLLECTION_LOAD_NONE));
	EXPECT_EQ(g_list_length(collection->list), 1U);
}

TEST_F(SortCollection, RejectsDirectoriesWrongExtensionsAndUnwritableDestinations)
{
	ASSERT_EQ(g_mkdir(path, 0700), 0);
	EXPECT_FALSE(bar_sort_collection_add(path));
	EXPECT_EQ(g_rmdir(path), 0);
	EXPECT_FALSE(bar_sort_collection_add(image_path));
	g_autofree gchar *missing = g_build_filename(directory, "missing", "new.gqv", nullptr);
	EXPECT_FALSE(bar_sort_collection_add(missing));
	EXPECT_EQ(history_list_find_by_key(key), nullptr);
}

} // namespace
