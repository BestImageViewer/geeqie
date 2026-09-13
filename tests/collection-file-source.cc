/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <string>
#include <vector>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "collect.h"
#include "filedata.h"
#include "filefilter.h"
#include "options.h"
#include "sort-type.h"
#include "ui-fileops.h"
#include "view-dir.h"
#include "view-file.h"

namespace
{

class CollectionFileSource : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		setup_default_options(options);
		filter_add_defaults();
		filter_rebuild();
		directory = g_dir_make_tmp("geeqie-collection-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (const char *name : {"a", "b"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_EQ(g_mkdir(path, 0700), 0);
			}
		cd = collection_new(nullptr);
		first = make_file("a", "same.svg");
		second = make_file("b", "same.svg");
		unlisted = make_file("a", "unlisted.svg");
		ASSERT_TRUE(collection_add(cd, second, FALSE));
		ASSERT_TRUE(collection_add(cd, first, FALSE));
		view.source = FileViewSource::COLLECTION;
		view.collection = cd;
		view.collection_order = g_hash_table_new(g_direct_hash, g_direct_equal);
		view.sort = {SORT_NONE, TRUE, TRUE};
	}

	FileData *make_file(const char *folder, const char *name)
	{
		g_autofree gchar *path = g_build_filename(directory, folder, name, nullptr);
		constexpr char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'><rect width='8' height='8' fill='red'/></svg>";
		if (!g_file_set_contents(path, svg, -1, nullptr)) return nullptr;
		paths.emplace_back(path);
		return file_data_new_group(path);
	}

	void TearDown() override
	{
		file_data_list_free(files);
		for (GList *work = view.monitored_files; work; work = work->next)
			file_data_unregister_real_time_monitor(static_cast<FileData *>(work->data));
		g_list_free(view.monitored_files);
		g_clear_pointer(&view.collection_order, g_hash_table_destroy);
		if (cd) collection_unref(cd);
		file_data_unref(first);
		file_data_unref(second);
		file_data_unref(unlisted);
		for (const auto &path : paths) g_remove(path.c_str());
		for (const char *name : {"a", "b"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			g_rmdir(path);
			}
		g_rmdir(directory);
		g_free(directory);
	}

	gchar *directory = nullptr;
	CollectionData *cd = nullptr;
	FileData *first = nullptr;
	FileData *second = nullptr;
	FileData *unlisted = nullptr;
	ViewFile view{};
	GList *files = nullptr;
	std::vector<std::string> paths;
};

TEST_F(CollectionFileSource, ReadsOnlyMembersAndKeepsCollectionOrder)
{
	ASSERT_TRUE(vf_read_source(&view, &files));
	ASSERT_EQ(g_list_length(files), 2U);
	EXPECT_EQ(files->data, second);
	EXPECT_EQ(files->next->data, first);
	EXPECT_EQ(g_list_find(files, unlisted), nullptr);
	view.sort.method = SORT_NAME;
	files = vf_filelist_sort(&view, files);
	EXPECT_NE(vf_filelist_compare(&view, first, second), 0);
	view.sort.method = SORT_NONE;
	files = vf_filelist_sort(&view, files);
	EXPECT_EQ(files->data, second);
	EXPECT_EQ(cd->list->data, collection_list_find_fd(cd->list, second));
}

TEST_F(CollectionFileSource, NotifiesAllSubscribersAndRemovalLeavesFileIntact)
{
	gint first_count = 0;
	gint second_count = 0;
	const auto changed = [](CollectionData *, gpointer data) { ++*static_cast<gint *>(data); };
	collection_add_listener(cd, changed, &first_count);
	collection_add_listener(cd, changed, &second_count);
	EXPECT_TRUE(collection_remove(cd, first));
	EXPECT_TRUE(g_file_test(first->path, G_FILE_TEST_IS_REGULAR));
	EXPECT_EQ(first_count, 1);
	EXPECT_EQ(second_count, 1);
	collection_remove_listener(cd, changed, &first_count);
	EXPECT_TRUE(collection_add(cd, first, FALSE));
	EXPECT_EQ(first_count, 1);
	EXPECT_EQ(second_count, 2);
	collection_remove_listener(cd, changed, &second_count);
}

TEST_F(CollectionFileSource, MissingMemberDoesNotAddOtherFilesOrChangeMembership)
{
	ASSERT_EQ(g_remove(first->path), 0);
	ASSERT_TRUE(vf_read_source(&view, &files));
	ASSERT_EQ(g_list_length(files), 1U);
	EXPECT_EQ(files->data, second);
	EXPECT_EQ(g_list_length(cd->list), 2U);
}

TEST_F(CollectionFileSource, DefaultCollectionDirectoryHasVirtualFolders)
{
	const gchar *collection_directory = get_collections_dir();
	if (!g_str_has_prefix(collection_directory, "/tmp/")) GTEST_SKIP() << "Requires an isolated home directory";
	ASSERT_EQ(g_mkdir_with_parents(collection_directory, 0700), 0);
	g_autofree gchar *unique = g_uuid_string_random();
	g_autofree gchar *name = g_strconcat(unique, ".gqv", nullptr);
	g_autofree gchar *path = g_build_filename(collection_directory, name, nullptr);
	ASSERT_TRUE(g_file_set_contents(path, "#Geeqie collection\n#end\n", -1, nullptr));
	paths.emplace_back(path);
	FileData *collection_file = file_data_new_simple(path);
	EXPECT_TRUE(vd_is_collection(collection_file));
	EXPECT_FALSE(vd_is_collection(first));

	FileData *folder = file_data_new_dir(collection_directory);
	GList *directories = nullptr;
	ASSERT_TRUE(vd_read_directories(folder, &directories));
	EXPECT_NE(g_list_find(directories, collection_file), nullptr);
	file_data_list_free(directories);

	ViewFile directory_view{};
	directory_view.source = FileViewSource::DIRECTORY;
	directory_view.dir_fd = folder;
	GList *ordinary_files = nullptr;
	ASSERT_TRUE(vf_read_source(&directory_view, &ordinary_files));
	EXPECT_EQ(g_list_find(ordinary_files, collection_file), nullptr);
	file_data_list_free(ordinary_files);

	g_autofree gchar *outside = g_build_filename(directory, name, nullptr);
	ASSERT_TRUE(g_file_set_contents(outside, "#Geeqie collection\n#end\n", -1, nullptr));
	paths.emplace_back(outside);
	FileData *ordinary_collection = file_data_new_simple(outside);
	EXPECT_FALSE(vd_is_collection(ordinary_collection));
	file_data_unref(ordinary_collection);
	file_data_unref(folder);
	file_data_unref(collection_file);
}

TEST_F(CollectionFileSource, MovingIconsPreservesSelectionAndCollectionOrder)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	ASSERT_TRUE(collection_add(cd, unlisted, FALSE));
	GtkWidget *window = gtk_window_new();
	auto *pane = vf_new(FILEVIEW_ICON, nullptr);
	gtk_window_set_child(GTK_WINDOW(window), pane->widget);
	ASSERT_TRUE(vf_set_collection(pane, cd));
	vf_sort_set(pane, {SORT_NAME, TRUE, TRUE});
	GList *moving = g_list_append(nullptr, second);
	moving = g_list_append(moving, first);
	vf_select_list(pane, moving);
	vf_collection_move(pane, moving, nullptr);
	ASSERT_TRUE(vf_refresh(pane));
	EXPECT_EQ(pane->sort.method, SORT_NONE);
	EXPECT_EQ(pane->list->data, unlisted);
	EXPECT_EQ(pane->list->next->data, second);
	EXPECT_EQ(pane->list->next->next->data, first);
	EXPECT_EQ(vf_selection_count(pane, nullptr), 2U);
	EXPECT_TRUE(cd->changed);
	vf_collection_move(pane, moving, unlisted);
	ASSERT_TRUE(vf_refresh(pane));
	EXPECT_EQ(pane->list->data, second);
	EXPECT_EQ(pane->list->next->data, first);
	EXPECT_EQ(pane->list->next->next->data, unlisted);
	vf_collection_move(pane, moving, first);
	ASSERT_TRUE(vf_refresh(pane));
	EXPECT_EQ(pane->list->data, second);
	EXPECT_EQ(g_list_length(cd->list), 3U);
	g_list_free(moving);
	cd->changed = FALSE;
	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_F(CollectionFileSource, BothPaneTypesRefreshAndReturnToDirectory)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	for (FileViewType type : {FILEVIEW_LIST, FILEVIEW_ICON})
		{
		GtkWidget *window = gtk_window_new();
		auto *pane = vf_new(type, nullptr);
		gtk_window_set_child(GTK_WINDOW(window), pane->widget);
		ASSERT_TRUE(vf_set_collection(pane, cd));
		EXPECT_EQ(vf_count(pane), 2U);
		vf_select_all(pane);
		EXPECT_EQ(vf_selection_count(pane, nullptr), 2U);
		g_autoptr(FileDataList) selected = vf_selection_get_list(pane);
		EXPECT_EQ(g_list_length(selected), 2U);
		EXPECT_NE(g_list_find(selected, first), nullptr);
		EXPECT_NE(g_list_find(selected, second), nullptr);
		EXPECT_TRUE(collection_add(cd, unlisted, FALSE));
		EXPECT_NE(pane->refresh_idle_id, 0U);
		EXPECT_TRUE(vf_refresh(pane));
		EXPECT_EQ(vf_count(pane), 3U);
		EXPECT_TRUE(collection_remove(cd, unlisted));
		EXPECT_TRUE(vf_refresh(pane));
		EXPECT_EQ(vf_count(pane), 2U);
		cd->changed = FALSE;
		FileDataRef folder(file_data_new_dir(directory));
		file_data_unref(folder);
		ASSERT_TRUE(vf_set_fd(pane, folder));
		EXPECT_EQ(pane->source, FileViewSource::DIRECTORY);
		EXPECT_EQ(pane->collection, nullptr);
		EXPECT_EQ(vf_count(pane), 0U);
		gtk_window_destroy(GTK_WINDOW(window));
		}
}

} // namespace
