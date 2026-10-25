/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <unistd.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "dupe.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout.h"
#include "options.h"
#include "view-file.h"

namespace
{

class DuplicateDeletion : public testing::TestWithParam<bool>
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		saved = *options;
		configured = true;
		g_autofree gchar *directory = g_dir_make_tmp("geeqie-dupe-delete-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		root = directory;
		options->duplicates_match = DUPE_MATCH_SIZE;
		options->duplicates_thumbnails = FALSE;
		options->duplicates_select_type = DUPE_SELECT_NONE;
		options->thumbnails.enable_caching = FALSE;
		options->external_preview.enable = FALSE;
		options->file_ops.confirm_delete = FALSE;
		options->file_ops.confirm_move_to_trash = FALSE;
		options->file_ops.no_trash = FALSE;
		options->file_ops.use_system_trash = FALSE;
		options->file_ops.safe_delete_enable = GetParam();
		options->file_ops.safe_delete_path = g_build_filename(directory, "trash", nullptr);
		options->file_ops.safe_delete_folder_maxsize = 0;
		filter_add_defaults();
		filter_rebuild();
		accel_map_load_merged();
		previous_app = g_application_get_default();
		static GtkApplication *app = nullptr;
		if (!app)
			{
			app = gtk_application_new("org.geeqie.DuplicateDeletionTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
			}
		g_application_set_default(G_APPLICATION(app));
	}

	void TearDown() override
	{
		if (!configured) return;
		if (layout) layout_free(layout);
		for (auto *window : windows) if (window) dupe_window_close(window);
		g_application_set_default(previous_app);
		g_free(options->file_ops.safe_delete_path);
		options->file_ops = saved.file_ops;
		options->duplicates_match = saved.duplicates_match;
		options->duplicates_thumbnails = saved.duplicates_thumbnails;
		options->duplicates_select_type = saved.duplicates_select_type;
		options->thumbnails.enable_caching = saved.thumbnails.enable_caching;
		options->external_preview.enable = saved.external_preview.enable;
		std::error_code error;
		if (!root.empty()) std::filesystem::remove_all(root, error);
	}

	std::string create(const std::string &relative)
	{
		const auto path = root / relative;
		if (g_mkdir_with_parents(path.parent_path().c_str(), 0700) != 0) return {};
		g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 8, 8);
		gdk_pixbuf_fill(image, 0x4080c0ff);
		if (!gdk_pixbuf_save(image, path.c_str(), "png", nullptr, nullptr)) return {};
		return path.native();
	}

	static bool wait_for(const std::function<bool()> &finished)
	{
		const gint64 deadline = g_get_monotonic_time() + (15 * G_TIME_SPAN_SECOND);
		while (g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			if (finished()) return true;
			g_usleep(1000);
			}
		return false;
	}

	DupeWindow *window(const std::vector<std::string> &files)
	{
		auto *dw = dupe_window_new();
		windows.push_back(dw);
		add(dw, files);
		return dw;
	}

	static void add(DupeWindow *dw, const std::vector<std::string> &files, bool second = false)
	{
		g_autoptr(FileDataList) list = nullptr;
		for (const auto &path : files)
			{
			ASSERT_FALSE(path.empty());
			list = g_list_prepend(list, file_data_new_simple(path.c_str()));
			}
		dw->second_drop = second;
		dupe_window_add_files(dw, list, FALSE);
		ASSERT_TRUE(wait_for([dw]() { return dupe_export_ready(dw); }));
	}

	static void select(DupeWindow *dw, const std::string &path)
	{
		auto *model = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
		auto *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
		gtk_tree_selection_unselect_all(selection);
		GtkTreeIter iter;
		bool found = false;
		for (gboolean valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter))
			{
			DupeItem *item = nullptr;
			gtk_tree_model_get(model, &iter, 0, &item, -1);
			if (path == item->fd->path)
				{
				gtk_tree_selection_select_iter(selection, &iter);
				found = true;
				}
			}
		ASSERT_TRUE(found);
	}

	static void expect_count(DupeWindow *dw, guint count)
	{
		ASSERT_TRUE(wait_for([dw, count]()
			{
			return dupe_export_ready(dw) && g_list_length(dw->list) + g_list_length(dw->second_list) == count;
			}));
	}

	static void erase(const std::string &path)
	{
		auto fd = FileData::new_simple(path.c_str());
		ASSERT_TRUE(file_data_add_ci(fd, FILEDATA_CHANGE_DELETE, nullptr, nullptr));
		const bool success = file_data_perform_ci(fd);
		if (success)
			{
			EXPECT_TRUE(file_data_apply_ci(fd));
			}
		file_data_free_ci(fd);
		ASSERT_TRUE(success);
	}

	void close(DupeWindow *dw)
	{
		for (auto *&entry : windows) if (entry == dw) entry = nullptr;
		dupe_window_close(dw);
	}

	std::filesystem::path root;
	std::vector<DupeWindow *> windows;
	LayoutWindow *layout = nullptr;
	GApplication *previous_app = nullptr;
	ConfOptions saved{};
	bool configured = false;
};

TEST_P(DuplicateDeletion, MainWindowDeletionUpdatesEveryDuplicatesWindow)
{
	const auto first = create("0.png");
	const auto second = create("1.png");
	const auto third = create("2.png");
	auto *a = window({first, second, third});
	auto *b = window({first, second, third});
	g_autofree gchar *startup = g_strdup_printf("%d", STARTUP_PATH_HOME);
	const gchar *names[] = {"home_path", "startup_path", nullptr};
	const gchar *values[] = {root.c_str(), startup, nullptr};
	layout = layout_new_from_config(names, values, FALSE);
	ASSERT_NE(layout, nullptr);
	auto fd = FileData::new_simple(first.c_str());
	vf_select_by_fd(layout->vf, fd);
	ASSERT_TRUE(gtk_widget_activate_action(layout->window, GetParam() ? "win.main-win-delete" : "win.main-win-delete-permanent", nullptr));
	expect_count(a, 2);
	expect_count(b, 2);
	EXPECT_FALSE(g_file_test(first.c_str(), G_FILE_TEST_EXISTS));
	EXPECT_NE(a->dupes, nullptr);
	EXPECT_NE(b->dupes, nullptr);
}

TEST_P(DuplicateDeletion, ChangingSelectionDoesNotRemoveUndeletedFiles)
{
	const auto first = create("0.png");
	const auto second = create("1.png");
	const auto third = create("2.png");
	auto *a = window({first, second, third});
	auto *b = window({first, second, third});
	select(a, first);
	ASSERT_TRUE(gtk_widget_activate_action(a->window, GetParam() ? "win.dupe-win-delete" : "win.dupe-win-delete-permanent", nullptr));
	select(a, third);
	expect_count(a, 2);
	expect_count(b, 2);
	EXPECT_FALSE(g_file_test(first.c_str(), G_FILE_TEST_EXISTS));
	EXPECT_TRUE(g_file_test(third.c_str(), G_FILE_TEST_EXISTS));
	auto *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(a->listview));
	EXPECT_EQ(gtk_tree_selection_count_selected_rows(selection), 1);
}

TEST_P(DuplicateDeletion, DeletingRepresentativeRetainsSurvivingMatches)
{
	const auto first = create("0.png");
	const auto second = create("1.png");
	const auto third = create("2.png");
	auto *dw = window({first, second, third});
	ASSERT_NE(dw->dupes, nullptr);
	auto *parent = static_cast<DupeItem *>(dw->dupes->data);
	const std::string path = parent->fd->path;
	erase(path);
	expect_count(dw, 2);
	ASSERT_NE(dw->dupes, nullptr);
	auto *replacement = static_cast<DupeItem *>(dw->dupes->data);
	EXPECT_NE(path, replacement->fd->path);
	EXPECT_EQ(g_list_length(replacement->group), 1U);
}

TEST_P(DuplicateDeletion, FailedDeletionLeavesItsEntryAfterOtherFilesSucceed)
{
	if (geteuid() == 0) GTEST_SKIP() << "Requires ordinary filesystem permissions";
	const auto first = create("0.png");
	const auto failed = create("1.png");
	const auto third = create("2.png");
	auto *dw = window({first, failed, third});
	erase(first);
	options->file_ops.safe_delete_enable = FALSE;
	auto fd = FileData::new_simple(failed.c_str());
	ASSERT_TRUE(file_data_add_ci(fd, FILEDATA_CHANGE_DELETE, nullptr, nullptr));
	ASSERT_EQ(g_chmod(root.c_str(), 0500), 0);
	const bool success = file_data_perform_ci(fd);
	EXPECT_EQ(g_chmod(root.c_str(), 0700), 0);
	file_data_free_ci(fd);
	EXPECT_FALSE(success);
	expect_count(dw, 2);
	EXPECT_TRUE(g_file_test(failed.c_str(), G_FILE_TEST_EXISTS));
	select(dw, failed);
}

TEST_P(DuplicateDeletion, BothInputSetsAndSharedFilteredRowsAreUpdated)
{
	for (bool filtered : {false, true})
		{
		const std::string prefix = filtered ? "filtered/" : "ordinary/";
		const auto first = create(prefix + "A/0.png");
		const auto first_other = create(prefix + "A/1.png");
		const auto second = create(prefix + "B/0.png");
		const auto second_other = create(prefix + "B/1.png");
		auto *dw = window({first, first_other});
		dw->second_set = TRUE;
		if (filtered) gtk_check_button_set_active(GTK_CHECK_BUTTON(dw->button_ignore_same_folder), TRUE);
		add(dw, {second, second_other}, true);
		erase(first);
		expect_count(dw, 3);
		EXPECT_NE(dw->dupes, nullptr);
		erase(second);
		expect_count(dw, 2);
		EXPECT_NE(dw->dupes, nullptr);
		EXPECT_EQ(g_list_length(dw->list), 1U);
		EXPECT_EQ(g_list_length(dw->second_list), 1U);
		}
}

TEST_P(DuplicateDeletion, ClosingWindowCancelsItsPendingBatchOnly)
{
	const auto first = create("0.png");
	const auto second = create("1.png");
	auto *a = window({first, second});
	auto *b = window({first, second});
	erase(first);
	const guint pending = a->delete_update_id;
	ASSERT_NE(pending, 0U);
	EXPECT_FALSE(dupe_export_ready(a));
	close(a);
	EXPECT_EQ(g_main_context_find_source_by_id(nullptr, pending), nullptr);
	expect_count(b, 1);
}

TEST_P(DuplicateDeletion, ClosingOriginWhileDeletionRunsUpdatesOtherWindows)
{
	const auto first = create("0.png");
	const auto second = create("1.png");
	auto *a = window({first, second});
	auto *b = window({first, second});
	select(a, first);
	ASSERT_TRUE(gtk_widget_activate_action(a->window, GetParam() ? "win.dupe-win-delete" : "win.dupe-win-delete-permanent", nullptr));
	close(a);
	expect_count(b, 1);
	EXPECT_FALSE(g_file_test(first.c_str(), G_FILE_TEST_EXISTS));
}

TEST_P(DuplicateDeletion, DeletionDuringFileLoadingKeepsOtherQueuedFiles)
{
	std::vector<std::string> paths;
	paths.reserve(10);
	for (guint i = 0; i < 10; ++i) paths.push_back(create(std::to_string(i) + ".png"));
	auto *dw = dupe_window_new();
	windows.push_back(dw);
	g_autoptr(FileDataList) files = nullptr;
	for (const auto &path : paths) files = g_list_prepend(files, file_data_new_simple(path.c_str()));
	dupe_window_add_files(dw, files, FALSE);
	erase(paths.front());
	ASSERT_NE(dw->delete_update_id, 0U);
	GSource *batch = g_main_context_find_source_by_id(nullptr, dw->delete_update_id);
	ASSERT_NE(batch, nullptr);
	g_source_set_ready_time(batch, 0);
	g_main_context_iteration(nullptr, FALSE);
	EXPECT_NE(dw->add_files_queue_id, 0U);
	EXPECT_NE(dw->pending_deletions, nullptr);
	expect_count(dw, 9);
}

TEST_P(DuplicateDeletion, LargeBurstUsesOnePendingBatchAndOneModelClear)
{
	std::vector<std::string> files;
	files.reserve(1200);
	for (guint i = 0; i < 1200; ++i) files.push_back(create(std::to_string(i) + ".png"));
	auto *dw = window(files);
	auto *model = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
	const gint original_rows = gtk_tree_model_iter_n_children(model, nullptr);
	guint removed_rows = 0;
	const gulong handler = g_signal_connect(model, "row-deleted", G_CALLBACK(+[](GtkTreeModel *, GtkTreePath *, gpointer data)
		{
		++*static_cast<guint *>(data);
		}), &removed_rows);
	guint pending = 0;
	for (guint i = 0; i < 100; ++i)
		{
		erase(files[i]);
		if (!pending) pending = dw->delete_update_id;
		EXPECT_EQ(dw->delete_update_id, pending);
		}
	ASSERT_NE(pending, 0U);
	EXPECT_EQ(g_hash_table_size(dw->pending_deletions), 100U);
	EXPECT_EQ(removed_rows, 0U);
	expect_count(dw, 1100);
	EXPECT_EQ(removed_rows, static_cast<guint>(original_rows));
	g_signal_handler_disconnect(model, handler);
}

INSTANTIATE_TEST_SUITE_P(TrashAndPermanent, DuplicateDeletion, testing::Bool());

} // namespace
