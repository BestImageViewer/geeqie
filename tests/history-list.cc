/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <map>
#include <string>

#include <glib.h>
#include <glib/gstdio.h>

#include "history-list.h"
#include "layout-util.h"
#include "options.h"

namespace
{

TEST(HistoryList, SaveKeepsNewestEntriesInOrder)
{
	if (!options) options = conf_options_new();
	const gint image_limit = options->recent_folder_image_list_maxsize;
	const gint path_limit = options->open_recent_list_maxsize;
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-history-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *filename = g_build_filename(directory, "history", nullptr);

	std::map<std::string, HistoryList> saved_lists;
	for (const gchar *key : {"image_list", "path_list"})
		{
		if (auto *items = history_list_find_by_key(key)) saved_lists[key] = *items;
		}

	for (const gchar *key : {"image_list", "path_list"})
		{
		for (gint limit : {0, 1, 2, 3, 4})
			{
			options->recent_folder_image_list_maxsize = limit;
			options->open_recent_list_maxsize = limit;
			history_list_free_key(key);
			for (const gchar *path : {"/old/image.jpg", "/middle/image.jpg", "/new/image.jpg"})
				{
				history_list_add_to_key(key, path, 0);
				}
			EXPECT_TRUE(history_list_save(filename));
			history_list_free_key(key);
			EXPECT_TRUE(history_list_load(filename));

			HistoryList expected;
			if (limit > 0) expected.emplace_back("/new/image.jpg");
			if (limit > 1) expected.emplace_back("/middle/image.jpg");
			if (limit > 2) expected.emplace_back("/old/image.jpg");
			auto *actual = history_list_find_by_key(key);
			EXPECT_EQ(actual ? *actual : HistoryList{}, expected) << key << " limit " << limit;
			}
		}

	for (const gchar *key : {"image_list", "path_list"})
		{
		history_list_free_key(key);
		const auto &saved_items = saved_lists[key];
		for (auto work = saved_items.crbegin(); work != saved_items.crend(); ++work)
			{
			history_list_add_to_key(key, work->c_str(), 0);
			}
		}

	options->recent_folder_image_list_maxsize = image_limit;
	options->open_recent_list_maxsize = path_limit;
	g_unlink(filename);
	g_rmdir(directory);
}

TEST(HistoryList, CollectionHistorySurvivesReloadWithIndependentLimit)
{
	if (!options) options = conf_options_new();
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-collection-history-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *filename = g_build_filename(directory, "history", nullptr);
	HistoryList saved_items;
	if (auto *items = history_list_find_by_key("collection_history")) saved_items = *items;
	const gint folder_limit = options->open_recent_list_maxsize;
	const gint collection_limit = options->recent_collections_list_maxsize;
	options->open_recent_list_maxsize = 1;
	options->recent_collections_list_maxsize = 100;
	history_list_free_key("collection_history");
	HistoryList expected;
	for (int i = 0; i < 105; ++i)
		{
		g_autofree gchar *name = g_strdup_printf("%s/collection-%03d.gqv", directory, i);
		EXPECT_TRUE(g_file_set_contents(name, "", 0, nullptr));
		layout_recent_add_path(name);
		expected.emplace_front(name);
		}
	expected.resize(100);
	EXPECT_TRUE(history_list_save(filename));
	history_list_free_key("collection_history");
	EXPECT_TRUE(history_list_load(filename));
	auto *actual = history_list_find_by_key("collection_history");
	EXPECT_EQ(actual ? *actual : HistoryList{}, expected);

	// Discard the obsolete recent section without changing collection history.
	g_autofree gchar *contents = nullptr;
	EXPECT_TRUE(g_file_get_contents(filename, &contents, nullptr, nullptr));
	std::string legacy_contents = contents;
	legacy_contents += "\n[recent]\n\"/obsolete/collection.gqv\"\n";
	EXPECT_TRUE(g_file_set_contents(filename, legacy_contents.c_str(), -1, nullptr));
	history_list_free_key("collection_history");
	EXPECT_TRUE(history_list_load(filename));
	actual = history_list_find_by_key("collection_history");
	EXPECT_EQ(actual ? *actual : HistoryList{}, expected);
	EXPECT_EQ(history_list_find_by_key("recent"), nullptr);

	// Lowering the preference must also limit the history saved for the next session.
	options->recent_collections_list_maxsize = 12;
	expected.resize(12);
	EXPECT_TRUE(history_list_save(filename));
	g_autofree gchar *saved_contents = nullptr;
	EXPECT_TRUE(g_file_get_contents(filename, &saved_contents, nullptr, nullptr));
	EXPECT_EQ(std::string(saved_contents).find("[recent]"), std::string::npos);
	history_list_free_key("collection_history");
	EXPECT_TRUE(history_list_load(filename));
	actual = history_list_find_by_key("collection_history");
	EXPECT_EQ(actual ? *actual : HistoryList{}, expected);

	history_list_free_key("collection_history");
	for (auto work = saved_items.crbegin(); work != saved_items.crend(); ++work)
		{
		history_list_add_to_key("collection_history", work->c_str(), 0);
		}
	options->open_recent_list_maxsize = folder_limit;
	options->recent_collections_list_maxsize = collection_limit;
	for (int i = 0; i < 105; ++i)
		{
		g_autofree gchar *name = g_strdup_printf("%s/collection-%03d.gqv", directory, i);
		g_unlink(name);
		}
	g_unlink(filename);
	g_rmdir(directory);
}

} // namespace
