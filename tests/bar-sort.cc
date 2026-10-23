/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include <glib/gstdio.h>

#include "bar-sort.h"
#include "collect.h"
#include "collect-io.h"
#include "filedata.h"
#include "history-list.h"
#include "layout.h"
#include "options.h"
#include "ui-bookmark.h"

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

GtkWidget *find_sort_widget(GtkWidget *widget, GType type)
{
	if (G_TYPE_CHECK_INSTANCE_TYPE(widget, type)) return widget;
	/* Do not inspect GTK's internal dropdown menu widgets. */
	if (GTK_IS_DROP_DOWN(widget)) return nullptr;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_sort_widget(child, type)) return found;
		}
	return nullptr;
}

class SortFolderDestinations : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		directory = g_dir_make_tmp("geeqie-sort-folders-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (const char *name : {"Downloads", "Sports", "Other", "Downloads/Nested"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_EQ(g_mkdir(path, 0700), 0);
			g_autofree gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path, -1);
			const std::string key = std::string("sort_manager_folder_") + digest;
			keys.push_back(key);
			keys.push_back(key + "_enabled");
			}
		for (const char *key : {"sort_manager", "sort_manager_selected_folder", "sort_manager_selected_collection", "sort_manager_collections"})
			{
			keys.emplace_back(key);
			if (auto *history = history_list_find_by_key(key)) saved[key] = *history;
			history_list_free_key(key);
			}
		history_list_add_to_key("sort_manager", "Shared[path]/tmp/shared-sort-destination", 0);
		layout.dir_fd = file_data_new_dir(directory);
		bar = bar_sort_new_default(&layout);
		ASSERT_NE(bar, nullptr);
		g_object_ref_sink(bar);
		bookmarks = find_sort_widget(bar, GTK_TYPE_SCROLLED_WINDOW);
		ASSERT_NE(bookmarks, nullptr);
		folder_specific = GTK_CHECK_BUTTON(find_sort_widget(bar, GTK_TYPE_CHECK_BUTTON));
		ASSERT_NE(folder_specific, nullptr);
		navigate("Downloads");
	}

	void TearDown() override
	{
		g_clear_object(&bar);
		file_data_unref(layout.dir_fd);
		for (const auto &key : keys) history_list_free_key(key.c_str());
		for (const auto &[key, history] : saved)
			for (auto item = history.crbegin(); item != history.crend(); ++item)
				history_list_add_to_key(key.c_str(), item->c_str(), 0);
		if (directory)
			{
			for (const char *name : {"Downloads/Nested", "Downloads", "Sports", "Other", "history"})
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				if (g_rmdir(path) != 0) g_unlink(path);
				}
			g_rmdir(directory);
			}
		g_free(directory);
	}

	void navigate(const char *name)
	{
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		file_data_unref(layout.dir_fd);
		layout.dir_fd = file_data_new_dir(path);
		bar_sort_update(bar);
	}

	void expect_destinations(std::initializer_list<const char *> expected)
	{
		GtkWidget *box = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(bookmarks));
		if (GTK_IS_VIEWPORT(box)) box = gtk_viewport_get_child(GTK_VIEWPORT(box));
		guint count = 0;
		for (auto *child = gtk_widget_get_first_child(box); child; child = gtk_widget_get_next_sibling(child)) ++count;
		EXPECT_EQ(count, expected.size());
		for (const char *path : expected)
			{
			bookmark_list_set_selected_path(bookmarks, path);
			EXPECT_STREQ(bookmark_list_get_selected_path(bookmarks), path);
			}
	}

	LayoutWindow layout{};
	GtkWidget *bar = nullptr;
	GtkWidget *bookmarks = nullptr;
	GtkCheckButton *folder_specific = nullptr;
	gchar *directory = nullptr;
	std::vector<std::string> keys;
	std::map<std::string, HistoryList> saved;
};

TEST_F(SortFolderDestinations, NavigationSwitchesListsAndClearsPreviousDestination)
{
	expect_destinations({"/tmp/shared-sort-destination"});
	EXPECT_FALSE(gtk_check_button_get_active(folder_specific));
	gtk_check_button_set_active(folder_specific, TRUE);
	EXPECT_EQ(bookmark_list_get_selected_path(bookmarks), nullptr);
	expect_destinations({});
	bookmark_list_add(bookmarks, "Nature", "/tmp/nature-sort-destination");
	expect_destinations({"/tmp/nature-sort-destination"});
	navigate("Sports");
	EXPECT_FALSE(gtk_check_button_get_active(folder_specific));
	EXPECT_EQ(bookmark_list_get_selected_path(bookmarks), nullptr);
	gtk_check_button_set_active(folder_specific, TRUE);
	expect_destinations({});
	bookmark_list_add(bookmarks, "Cycling", "/tmp/cycling-sort-destination");
	expect_destinations({"/tmp/cycling-sort-destination"});
	navigate("Downloads");
	EXPECT_TRUE(gtk_check_button_get_active(folder_specific));
	EXPECT_EQ(bookmark_list_get_selected_path(bookmarks), nullptr);
	expect_destinations({"/tmp/nature-sort-destination"});
	gtk_check_button_set_active(folder_specific, FALSE);
	EXPECT_EQ(bookmark_list_get_selected_path(bookmarks), nullptr);
	expect_destinations({"/tmp/shared-sort-destination"});
	gtk_check_button_set_active(folder_specific, TRUE);
	expect_destinations({"/tmp/nature-sort-destination"});
}

TEST_F(SortFolderDestinations, AssociationsAndDestinationsSurviveHistoryReload)
{
	gtk_check_button_set_active(folder_specific, TRUE);
	bookmark_list_add(bookmarks, "Nature", "/tmp/nature-sort-destination");
	navigate("Sports");
	gtk_check_button_set_active(folder_specific, TRUE);
	navigate("Downloads");
	g_autofree gchar *path = g_build_filename(directory, "history", nullptr);
	ASSERT_TRUE(history_list_save(path));
	for (const auto &key : keys) history_list_free_key(key.c_str());
	ASSERT_TRUE(history_list_load(path));
	g_clear_object(&bar);
	bar = bar_sort_new_default(&layout);
	g_object_ref_sink(bar);
	bookmarks = find_sort_widget(bar, GTK_TYPE_SCROLLED_WINDOW);
	folder_specific = GTK_CHECK_BUTTON(find_sort_widget(bar, GTK_TYPE_CHECK_BUTTON));
	EXPECT_TRUE(gtk_check_button_get_active(folder_specific));
	expect_destinations({"/tmp/nature-sort-destination"});
	navigate("Sports");
	EXPECT_TRUE(gtk_check_button_get_active(folder_specific));
	expect_destinations({});
	navigate("Other");
	EXPECT_FALSE(gtk_check_button_get_active(folder_specific));
	expect_destinations({"/tmp/shared-sort-destination"});
}

TEST_F(SortFolderDestinations, SharedListsRemainSharedAndSubfoldersDoNotInherit)
{
	bookmark_list_add(bookmarks, "Another shared folder", "/tmp/another-shared-sort-destination");
	navigate("Other");
	expect_destinations({"/tmp/shared-sort-destination", "/tmp/another-shared-sort-destination"});
	navigate("Downloads");
	gtk_check_button_set_active(folder_specific, TRUE);
	bookmark_list_add(bookmarks, "Nature", "/tmp/nature-sort-destination");
	navigate("Downloads/Nested");
	EXPECT_FALSE(gtk_check_button_get_active(folder_specific));
	expect_destinations({"/tmp/shared-sort-destination", "/tmp/another-shared-sort-destination"});
}

TEST_F(SortFolderDestinations, ModeSwitchUsesCurrentFolderAndPreservesCollectionMode)
{
	gtk_check_button_set_active(folder_specific, TRUE);
	bookmark_list_add(bookmarks, "Nature", "/tmp/nature-sort-destination");
	auto *mode = GTK_DROP_DOWN(find_sort_widget(bar, GTK_TYPE_DROP_DOWN));
	ASSERT_NE(mode, nullptr);
	gtk_drop_down_set_selected(mode, BarSort::MODE_COLLECTION);
	EXPECT_FALSE(gtk_widget_is_visible(GTK_WIDGET(folder_specific)));
	const HistoryList *collections = history_list_find_by_key("sort_manager_collections");
	const HistoryList saved_collections = collections ? *collections : HistoryList{};
	navigate("Sports");
	collections = history_list_find_by_key("sort_manager_collections");
	EXPECT_EQ(collections ? *collections : HistoryList{}, saved_collections);
	gtk_drop_down_set_selected(mode, BarSort::MODE_FOLDER);
	EXPECT_FALSE(gtk_check_button_get_active(folder_specific));
	expect_destinations({"/tmp/shared-sort-destination"});
	navigate("Downloads");
	EXPECT_TRUE(gtk_check_button_get_active(folder_specific));
	expect_destinations({"/tmp/nature-sort-destination"});
}

} // namespace
