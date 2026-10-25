/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <functional>
#include <tuple>
#include <utility>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "actions.h"
#include "cache.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout-image.h"
#include "layout.h"
#include "options.h"
#include "thumb-standard.h"
#include "trash.h"
#include "ui-fileops.h"
#include "ui-tree-edit.h"
#include "view-file.h"
#include "view-file/view-file-icon.h"
#include "view-file/view-file-list.h"

namespace
{

class DeferredMarks : public ::testing::Test
{
protected:
	void SetUp() override
	{
		file_data_get_registered_mark_func(0, &saved_get, &saved_set, &saved_data);
		if (saved_get || saved_set || saved_data) GTEST_SKIP() << "Requires an unused mark";
		installed = true;
		file_data_register_mark_func(0, read_mark, nullptr, this, nullptr);
		vf.marks_enabled = TRUE;
	}

	void TearDown() override
	{
		vf_marks_load_cancel(&vf);
		if (installed) file_data_register_mark_func(0, nullptr, nullptr, nullptr, nullptr);
	}

	static gboolean read_mark(FileData *, gint, gpointer data)
	{
		auto *self = static_cast<DeferredMarks *>(data);
		++self->reads;
		return self->value;
	}

	ViewFile vf{};
	int reads = 0;
	bool installed = false;
	gboolean value = FALSE;
	FileData::GetMarkFunc saved_get = nullptr;
	FileData::SetMarkFunc saved_set = nullptr;
	gpointer saved_data = nullptr;
};

TEST_F(DeferredMarks, DisplayDefersAndDeduplicatesReads)
{
	auto first = FileData::new_simple("/tmp/geeqie-deferred-mark-first.jpg");
	auto second = FileData::new_simple("/tmp/geeqie-deferred-mark-second.jpg");
	EXPECT_FALSE(vf_mark_get_for_display(&vf, first, 0));
	EXPECT_FALSE(vf_mark_get_for_display(&vf, first, 0));
	EXPECT_FALSE(vf_mark_get_for_display(&vf, second, 0));
	EXPECT_EQ(reads, 0);
	ASSERT_NE(vf.marks_load_idle_id, 0U);
	EXPECT_EQ(g_queue_get_length(vf.marks_load_queue), 2U);
	while (reads == 0) g_main_context_iteration(nullptr, FALSE);
	EXPECT_EQ(reads, 1);
	while (vf.marks_load_idle_id) g_main_context_iteration(nullptr, FALSE);
	EXPECT_EQ(reads, 2);
	EXPECT_TRUE(first->valid_marks & 1);
	EXPECT_TRUE(second->valid_marks & 1);
	vf_mark_get_for_display(&vf, first, 0);
	EXPECT_EQ(vf.marks_load_idle_id, 0U);
}

TEST_F(DeferredMarks, ResolvedMarksUpdateListRow)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	value = TRUE;
	auto fd = FileData::new_simple("/tmp/geeqie-deferred-mark-row.jpg");
	vflist_new(&vf);
	auto *store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf.listview)));
	GtkTreeIter iter;
	gtk_tree_store_append(store, &iter, nullptr);
	gtk_tree_store_set(store, &iter, 0, file_data_ref(fd), -1);
	vf.list = g_list_append(vf.list, file_data_ref(fd));
	vflist_set_marks_fd(&vf, fd);
	gboolean active = TRUE;
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 12, &active, -1);
	EXPECT_FALSE(active);
	EXPECT_EQ(reads, 0);
	while (vf.marks_load_idle_id) g_main_context_iteration(nullptr, FALSE);
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 12, &active, -1);
	EXPECT_TRUE(active);
	EXPECT_EQ(reads, 1);
	file_data_set_mark(fd, 0, FALSE);
	vf_marks_load_cancel(&vf);
	vflist_destroy_cb(&vf);
	g_free(vf.info);
	vf.info = nullptr;
	vf.listview = nullptr;
}

TEST_F(DeferredMarks, CancellationDropsPendingReads)
{
	auto fd = FileData::new_simple("/tmp/geeqie-deferred-mark-cancel.jpg");
	vf_mark_get_for_display(&vf, fd, 0);
	vf_marks_load_cancel(&vf);
	EXPECT_EQ(reads, 0);
	EXPECT_EQ(vf.marks_load_idle_id, 0U);
	EXPECT_EQ(vf.marks_load_queue, nullptr);
	EXPECT_FALSE(fd->valid_marks & 1);
}

TEST_F(DeferredMarks, HiddenMarksDoNotReadMetadata)
{
	auto fd = FileData::new_simple("/tmp/geeqie-deferred-mark-hidden.jpg");
	vf.marks_enabled = FALSE;
	vf_mark_get_for_display(&vf, fd, 0);
	EXPECT_EQ(reads, 0);
	EXPECT_EQ(vf.marks_load_idle_id, 0U);
}

void check_startup_selection(bool show_details)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	ViewFile vf{};
	vflist_new(&vf);
	gtk_widget_set_visible(VFLIST(&vf)->details_scrolled, show_details);
	auto *store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf.listview)));
	FileData *selected = nullptr;
	GtkTreeIter selected_iter;
	for (int i = 0; i < 300; ++i)
		{
		g_autofree gchar *name = g_strdup_printf("/tmp/geeqie-startup-%03d.jpg", i);
		auto *fd = file_data_new_simple(name);
		vf.list = g_list_append(vf.list, fd);
		GtkTreeIter iter;
		gtk_tree_store_append(store, &iter, nullptr);
		gtk_tree_store_set(store, &iter, 0, fd, 3, fd->name, 4, fd->name, 5, fd->name, -1);
		if (i == 250)
			{
			selected = fd;
			selected_iter = iter;
			}
		}
	GtkWidget *window = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(window), 700, 400);
	gtk_window_set_child(GTK_WINDOW(window), vflist_get_view_widget(&vf));
	// Command-line selection happens before the window is presented.
	vflist_select_by_fd(&vf, selected);
	gtk_window_present(GTK_WINDOW(window));
	const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	while (g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
	EXPECT_TRUE(vflist_is_selected(&vf, selected));
	EXPECT_TRUE(tree_view_row_is_visible(GTK_TREE_VIEW(vf.listview), &selected_iter, TRUE));
	if (show_details)
		{
		EXPECT_TRUE(tree_view_row_is_visible(GTK_TREE_VIEW(VFLIST(&vf)->details_view), &selected_iter, TRUE));
		}
	gtk_window_destroy(GTK_WINDOW(window));
	vflist_destroy_cb(&vf);
	g_free(vf.info);
}

TEST(FileList, AdditiveRangesPreserveOtherBlocksInBothViews)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	for (const bool details : {false, true})
		{
		SCOPED_TRACE(details);
		ViewFile vf{};
		vflist_new(&vf);
		g_autoptr(GtkWidget) filter = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		g_object_ref_sink(filter);
		gtk_widget_set_visible(filter, FALSE);
		vf.file_filter.control = filter;
		auto *store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vf.listview)));
		for (int i = 0; i < 12; ++i)
			{
			g_autofree gchar *name = g_strdup_printf("/tmp/geeqie-range-%02d.jpg", i);
			auto *fd = file_data_new_simple(name);
			vf.list = g_list_append(vf.list, fd);
			GtkTreeIter iter;
			gtk_tree_store_append(store, &iter, nullptr);
			gtk_tree_store_set(store, &iter, 0, file_data_ref(fd), 3, fd->name, 4, fd->name, 5, fd->name, -1);
			}
		auto *window = GTK_WINDOW(gtk_window_new());
		gtk_window_set_default_size(window, 700, 500);
		gtk_window_set_child(window, vflist_get_view_widget(&vf));
		gtk_window_present(window);
		for (int i = 0; i < 100; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
		auto *tree = GTK_TREE_VIEW(details ? VFLIST(&vf)->details_view : vf.listview);
		auto *selection = gtk_tree_view_get_selection(tree);
		const auto press = [&vf, tree](int row, GdkModifierType state)
			{
			g_autoptr(GtkTreePath) path = gtk_tree_path_new_from_indices(row, -1);
			GdkRectangle rect;
			gtk_tree_view_get_background_area(tree, path, nullptr, &rect);
			int x;
			int y;
			gtk_tree_view_convert_bin_window_to_widget_coords(tree, rect.x + 10, rect.y + (rect.height / 2), &x, &y);
			vflist_press_cb(&vf, {GTK_WIDGET(tree), GDK_BUTTON_PRIMARY, static_cast<double>(x), static_cast<double>(y), state, 1});
			};
		for (const auto range : {std::pair{6, 8}, std::pair{8, 6}, std::pair{1, 4}})
			{
			gtk_tree_selection_unselect_all(selection);
			g_autoptr(GtkTreePath) first = gtk_tree_path_new_from_indices(0, -1);
			g_autoptr(GtkTreePath) third = gtk_tree_path_new_from_indices(2, -1);
			gtk_tree_selection_select_range(selection, first, third);
			press(range.first, GDK_CONTROL_MASK);
			g_autoptr(GtkTreePath) anchor = gtk_tree_path_new_from_indices(range.first, -1);
			gtk_tree_selection_select_path(selection, anchor);
			press(range.second, static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK));
			EXPECT_TRUE(VFLIST(&vf)->range_selection);
			for (int i = 0; i < 12; ++i)
				{
				const bool expected = i <= 2 || (i >= std::min(range.first, range.second) && i <= std::max(range.first, range.second));
				auto *fd = static_cast<FileData *>(g_list_nth_data(vf.list, i));
				EXPECT_EQ(vflist_is_selected(&vf, fd), expected) << i;
				}
			}
		g_autofree gchar *directory = g_dir_make_tmp("geeqie-range-reset-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		FileData *dir_fd = file_data_new_dir(directory);
		EXPECT_TRUE(vflist_set_fd(&vf, dir_fd));
		EXPECT_EQ(VFLIST(&vf)->selection_anchor, nullptr);
		gtk_window_destroy(window);
		vflist_destroy_cb(&vf);
		file_data_unref(vf.dir_fd);
		file_data_unref(dir_fd);
		g_free(vf.info);
		g_rmdir(directory);
		}
}

TEST(FileList, StartupSelectionIsVisibleWithDetails)
{
	check_startup_selection(true);
}

TEST(FileList, StartupSelectionIsVisibleWithoutDetails)
{
	check_startup_selection(false);
}

class RepeatedDelete : public testing::TestWithParam<std::tuple<FileViewType, bool>>
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		filter_add_defaults();
		filter_rebuild();
		accel_map_load_merged();
		previous_app = g_application_get_default();
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.RepeatedDeleteTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		saved_file_ops = options->file_ops;
		directory = g_dir_make_tmp("geeqie-repeated-delete-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		options->file_ops.confirm_delete = FALSE;
		options->file_ops.confirm_move_to_trash = FALSE;
		options->file_ops.use_system_trash = FALSE;
		options->file_ops.no_trash = FALSE;
		options->file_ops.safe_delete_path = g_build_filename(directory, "trash", nullptr);
		options->file_ops.safe_delete_folder_maxsize = 0;
		for (int i = 0; i < 3; ++i)
			{
			g_autofree gchar *name = g_strdup_printf("%d.jpg", i);
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_TRUE(g_file_set_contents(path, "test", -1, nullptr));
			}
		g_autofree gchar *type = g_strdup_printf("%d", std::get<0>(GetParam()));
		g_autofree gchar *startup = g_strdup_printf("%d", STARTUP_PATH_HOME);
		const gchar *names[] = {"file_view_type", "home_path", "startup_path", nullptr};
		const gchar *values[] = {type, directory, startup, nullptr};
		layout = layout_new_from_config(names, values, FALSE);
		ASSERT_EQ(get_current_layout(), layout);
		vf_select_by_fd(layout->vf, vf_index_get_data(layout->vf, 0));
	}

	void TearDown() override
	{
		if (!directory) return;
		if (layout) layout_free(layout);
		if (app)
			{
			g_application_set_default(previous_app);
			g_object_unref(app);
			}
		file_util_trash_clear();
		for (const char *name : {"files", "info"})
			{
			g_autofree gchar *path = g_build_filename(options->file_ops.safe_delete_path, name, nullptr);
			g_rmdir(path);
			}
		g_rmdir(options->file_ops.safe_delete_path);
		g_free(options->file_ops.safe_delete_path);
		options->file_ops = saved_file_ops;
		for (int i = 0; i < 3; ++i)
			{
			g_autofree gchar *name = g_strdup_printf("%d.jpg", i);
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			g_remove(path);
			}
		g_rmdir(directory);
		g_free(directory);
	}

	LayoutWindow *layout = nullptr;
	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	decltype(options->file_ops) saved_file_ops{};
	gchar *directory = nullptr;
};

TEST_P(RepeatedDelete, RebuildThumbnailsOnlyInvalidatesSelectedFiles)
{
	auto *vf = layout->vf;
	vf_thumb_stop(vf);
	auto *first = vf_index_get_data(vf, 0);
	auto *second = vf_index_get_data(vf, 1);
	auto *third = vf_index_get_data(vf, 2);
	ASSERT_NE(third, nullptr);
	g_autoptr(FileDataList) selection = nullptr;
	selection = g_list_append(selection, file_data_ref(first));
	selection = g_list_append(selection, file_data_ref(second));
	vf_select_list(vf, selection);
	vf->click_fd = first;
	g_autofree gchar *local_dir = g_build_filename(directory, GQ_CACHE_LOCAL_THUMB, nullptr);
	ASSERT_EQ(g_mkdir_with_parents(local_dir, 0700), 0);
	g_autofree gchar *similarity = g_build_filename(local_dir, "0.jpg.sim", nullptr);
	ASSERT_TRUE(g_file_set_contents(similarity, "similarity", -1, nullptr));
	const auto standard_path = [](FileData *fd)
		{
		g_autofree gchar *uri = g_filename_to_uri(fd->path, nullptr, nullptr);
		g_autofree gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_MD5, uri, -1);
		g_autofree gchar *name = g_strconcat(hash, ".png", nullptr);
		return g_build_filename(get_thumbnails_standard_cache_dir(), THUMB_FOLDER_NORMAL, name, nullptr);
		};
	for (auto *fd : {first, second, third})
		{
		g_autofree gchar *name = g_strconcat(filename_from_path(fd->path), GQ_CACHE_EXT_THUMB, nullptr);
		g_autofree gchar *local = g_build_filename(local_dir, name, nullptr);
		ASSERT_TRUE(g_file_set_contents(local, "stale thumbnail", -1, nullptr));
		g_autofree gchar *shared_name = g_strconcat(fd->path, GQ_CACHE_EXT_THUMB, nullptr);
		g_autofree gchar *shared = g_build_filename(get_thumbnails_cache_dir(), shared_name, nullptr);
		g_autofree gchar *parent = g_path_get_dirname(shared);
		ASSERT_EQ(g_mkdir_with_parents(parent, 0700), 0);
		ASSERT_TRUE(g_file_set_contents(shared, "stale thumbnail", -1, nullptr));
		g_autofree gchar *standard = standard_path(fd);
		g_autofree gchar *standard_parent = g_path_get_dirname(standard);
		ASSERT_EQ(g_mkdir_with_parents(standard_parent, 0700), 0);
		ASSERT_TRUE(g_file_set_contents(standard, "stale thumbnail", -1, nullptr));
		g_clear_object(&fd->thumb_pixbuf);
		fd->thumb_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
		}
	ASSERT_TRUE(gtk_widget_activate_action(layout->window, "win.view-file-rebuild-thumbnails", nullptr));
	EXPECT_EQ(first->thumb_pixbuf, nullptr);
	EXPECT_EQ(second->thumb_pixbuf, nullptr);
	EXPECT_NE(third->thumb_pixbuf, nullptr);
	EXPECT_NE(vf->refresh_idle_id, 0U);
	for (auto *fd : {first, second, third})
		{
		g_autofree gchar *name = g_strconcat(filename_from_path(fd->path), GQ_CACHE_EXT_THUMB, nullptr);
		g_autofree gchar *local = g_build_filename(local_dir, name, nullptr);
		g_autofree gchar *shared_name = g_strconcat(fd->path, GQ_CACHE_EXT_THUMB, nullptr);
		g_autofree gchar *shared = g_build_filename(get_thumbnails_cache_dir(), shared_name, nullptr);
		EXPECT_EQ(isfile(local), fd == third);
		EXPECT_EQ(isfile(shared), fd == third);
		g_autofree gchar *standard = standard_path(fd);
		EXPECT_EQ(isfile(standard), fd == third);
		EXPECT_TRUE(isfile(fd->path));
		g_remove(local);
		g_remove(shared);
		g_remove(standard);
		}
	EXPECT_TRUE(isfile(similarity));
	g_remove(similarity);
	g_rmdir(local_dir);
	// Right-clicking an unselected file rebuilds only that file.
	vf->click_fd = third;
	ASSERT_TRUE(gtk_widget_activate_action(layout->window, "win.view-file-rebuild-thumbnails", nullptr));
	EXPECT_EQ(third->thumb_pixbuf, nullptr);
}

TEST_P(RepeatedDelete, SelectedImageNavigationPreservesOrderAndWrapping)
{
	auto *vf = layout->vf;
	const auto saved_circular = options->circular_selection_lists;
	for (const bool all : {false, true})
		{
		g_autoptr(FileDataList) selected = nullptr;
		const std::vector<int> chosen = all ? std::vector<int>{2, 0, 1} : std::vector<int>{2, 0};
		for (gint index : chosen) selected = g_list_append(selected, file_data_ref(vf_index_get_data(vf, index)));
		vf_select_none(vf);
		vf_select_list(vf, selected);
		auto expected = chosen;
		if (vf->type == FILEVIEW_LIST) std::sort(expected.begin(), expected.end());
		EXPECT_EQ(vf_selection_get_list_by_index(vf), expected);
		for (const bool circular : {false, true})
			{
			options->circular_selection_lists = circular;
			layout_image_set_index(layout, expected.front());
			layout_image_prev(layout);
			EXPECT_EQ(layout_image_get_fd(layout), vf_index_get_data(vf, circular ? expected.back() : expected.front()));
			layout_image_set_index(layout, expected.front());
			for (guint i = 1; i < expected.size(); ++i)
				{
				layout_image_next(layout);
				EXPECT_EQ(layout_image_get_fd(layout), vf_index_get_data(vf, expected[i]));
				}
			layout_image_next(layout);
			EXPECT_EQ(layout_image_get_fd(layout), vf_index_get_data(vf, circular ? expected.front() : expected.back()));
			layout_image_set_index(layout, expected.back());
			for (gint i = static_cast<gint>(expected.size()) - 2; i >= 0; --i)
				{
				layout_image_prev(layout);
				EXPECT_EQ(layout_image_get_fd(layout), vf_index_get_data(vf, expected[i]));
				}
			}
		}
	options->circular_selection_lists = saved_circular;
}

TEST_P(RepeatedDelete, SelectingMarksSynchronizesDisplayedImage)
{
	g_autofree gchar *xdotool = g_find_program_in_path("xdotool");
	const bool keyboard = std::get<1>(GetParam());
	if (keyboard && (!xdotool || !g_getenv("DISPLAY"))) GTEST_SKIP() << "Requires xdotool and X11";
	gtk_window_set_title(GTK_WINDOW(layout->window), "Geeqie select marks test");
	gtk_window_present(GTK_WINDOW(layout->window));
	const auto settle = []()
		{
		const auto deadline = g_get_monotonic_time() + 150 * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
		};
	settle();
	auto *first = vf_index_get_data(layout->vf, 0);
	auto *second = vf_index_get_data(layout->vf, 1);
	auto *third = vf_index_get_data(layout->vf, 2);
	ASSERT_NE(third, nullptr);
	const auto select_marks = [&]()
		{
		if (keyboard)
			{
			gtk_window_set_title(GTK_WINDOW(layout->window), "Geeqie select marks test");
			gtk_widget_grab_focus(layout->vf->listview);
			settle();
			const gchar *args[] = {xdotool, "search", "--name", "^Geeqie select marks test$",
			                       "windowfocus", "--sync", "key", "--clearmodifiers", "ctrl+1", nullptr};
			gint status = 0;
			ASSERT_TRUE(g_spawn_sync(nullptr, const_cast<gchar **>(args), nullptr, G_SPAWN_DEFAULT,
			                         nullptr, nullptr, nullptr, nullptr, &status, nullptr));
			ASSERT_TRUE(g_spawn_check_wait_status(status, nullptr));
			}
		else
			{
			ASSERT_TRUE(gtk_widget_activate_action(layout->window, "win.main-win-select-mark-1", nullptr));
			}
		settle();
		};
	file_data_set_mark(second, 0, TRUE);
	file_data_set_mark(third, 0, TRUE);
	layout_image_set_fd(layout, first);
	settle();
	select_marks();
	EXPECT_EQ(vf_selection_count(layout->vf, nullptr), 2U);
	EXPECT_EQ(layout_image_get_fd(layout), second);

	layout_image_set_fd(layout, third);
	select_marks();
	EXPECT_EQ(vf_selection_count(layout->vf, nullptr), 2U);
	EXPECT_EQ(layout_image_get_fd(layout), third);

	file_data_set_mark(second, 0, FALSE);
	file_data_set_mark(third, 0, FALSE);
	select_marks();
	EXPECT_EQ(vf_selection_count(layout->vf, nullptr), 0U);
	EXPECT_EQ(layout_image_get_fd(layout), third);
}

GtkWidget *find_rename_entry(GtkWidget *widget)
{
	if (GTK_IS_ENTRY(widget)) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *entry = find_rename_entry(child)) return entry;
		}
	return nullptr;
}

void check_main_rename(LayoutWindow *layout, gboolean in_place, gboolean multiple)
{
	gtk_window_present(GTK_WINDOW(layout->window));
	const gint64 deadline = g_get_monotonic_time() + 200 * G_TIME_SPAN_MILLISECOND;
	while (g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
	options->file_ops.enable_in_place_rename = in_place;
	if (multiple) vf_select_all(layout->vf);
	// A stale context-menu target must not override the current selection.
	layout->vf->click_fd = vf_index_get_data(layout->vf, 2);
	const ActionDef *action = get_main_actions();
	while (action->action_name && g_strcmp0(action->action_name, "win.main-win-rename") != 0) ++action;
	ASSERT_NE(action->action_name, nullptr);
	action->callback(nullptr, nullptr, nullptr);
	while (g_main_context_iteration(nullptr, FALSE)) {}

	GtkWidget *entry = find_rename_entry(layout->vf->listview);
	const gboolean expect_inline = in_place && !multiple && layout->vf->type == FILEVIEW_LIST;
	EXPECT_EQ(entry != nullptr, expect_inline != FALSE);
	if (entry)
		{
		EXPECT_STREQ(gtk_editable_get_text(GTK_EDITABLE(entry)), "0.jpg");
		g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(entry);
		for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
			{
			g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
			if (!GTK_IS_EVENT_CONTROLLER_KEY(controller)) continue;
			gboolean handled = FALSE;
			g_signal_emit_by_name(controller, "key-pressed", GDK_KEY_Escape, 0, GDK_NO_MODIFIER_MASK, &handled);
			}
		}
	gboolean found_dialog = FALSE;
	GListModel *windows = gtk_window_get_toplevels();
	for (guint i = g_list_model_get_n_items(windows); i > 0; --i)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i - 1));
		const gchar *title = gtk_window_get_title(window);
		if (title && g_str_has_prefix(title, "Rename"))
			{
			found_dialog = TRUE;
			gtk_window_close(window);
			}
		}
	EXPECT_EQ(found_dialog != FALSE, expect_inline == FALSE);
	while (g_main_context_iteration(nullptr, FALSE)) {}
}

TEST_P(RepeatedDelete, MainRenameHonorsInPlaceSetting)
{
	check_main_rename(layout, std::get<1>(GetParam()), FALSE);
}

TEST_P(RepeatedDelete, MainRenameUsesDialogForMultipleFiles)
{
	check_main_rename(layout, std::get<1>(GetParam()), TRUE);
}

TEST_P(RepeatedDelete, RefreshesSelectionBeforeTheNextDelete)
{
	const char *name = std::get<1>(GetParam()) ? "win.main-win-delete" : "win.main-win-delete-permanent";
	const ActionDef *action = get_main_actions();
	while (action->action_name && g_strcmp0(action->action_name, name) != 0) ++action;
	ASSERT_NE(action->action_name, nullptr);
	for (int i = 0; i < 3; ++i)
		{
		g_autofree gchar *filename = g_strdup_printf("%d.jpg", i);
		g_autofree gchar *path = g_build_filename(directory, filename, nullptr);
		action->callback(nullptr, nullptr, nullptr);
		const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
		while (isname(path) && g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
		ASSERT_FALSE(isname(path));
		// Repeat Delete before the lower-priority files-pane refresh runs.
		ASSERT_NE(layout->vf->refresh_idle_id, 0U);
		}
	action->callback(nullptr, nullptr, nullptr);
	EXPECT_EQ(vf_selection_count(layout->vf, nullptr), 0U);
	EXPECT_EQ(layout->vf->refresh_idle_id, 0U);
}

TEST_P(RepeatedDelete, ShiftArrowsExtendAndShrinkImmediatelyAfterAnchorRemoval)
{
	if (layout->vf->type != FILEVIEW_ICON) GTEST_SKIP() << "Requires icon view";
	auto *vf = layout->vf;
	const gboolean saved_rectangular = options->collections.rectangular_selection;
	options->collections.rectangular_selection = std::get<1>(GetParam());
	// Establish the first image as the keyboard anchor.
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Right, static_cast<GdkModifierType>(0));
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Left, static_cast<GdkModifierType>(0));
	EXPECT_EQ(vf->click_fd, vf_index_get_data(vf, 0));
	g_autofree gchar *path = g_build_filename(directory, "0.jpg", nullptr);
	g_autofree gchar *moved = g_build_filename(options->file_ops.safe_delete_path, "moved.jpg", nullptr);
	if (std::get<1>(GetParam()))
		{
		EXPECT_EQ(g_mkdir(options->file_ops.safe_delete_path, 0700), 0);
		EXPECT_EQ(g_rename(path, moved), 0);
		}
	else
		{
		EXPECT_EQ(g_unlink(path), 0);
		}
	vf_refresh(vf);
	EXPECT_EQ(vf_selection_count(vf, nullptr), 1U);
	EXPECT_EQ(vf->click_fd, vf_index_get_data(vf, 0));
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Right, GDK_SHIFT_MASK);
	EXPECT_EQ(vf_selection_count(vf, nullptr), 2U);
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Left, GDK_SHIFT_MASK);
	EXPECT_EQ(vf_selection_count(vf, nullptr), 1U);
	EXPECT_TRUE(vficon_is_selected(vf, vf_index_get_data(vf, 0)));
	options->collections.rectangular_selection = saved_rectangular;
	if (std::get<1>(GetParam()))
		{
		EXPECT_EQ(g_unlink(moved), 0);
		EXPECT_EQ(g_rmdir(options->file_ops.safe_delete_path), 0);
		}
}

TEST_P(RepeatedDelete, RefreshPreservesSurvivingKeyboardAnchor)
{
	if (layout->vf->type != FILEVIEW_ICON) GTEST_SKIP() << "Requires icon view";
	auto *vf = layout->vf;
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Right, static_cast<GdkModifierType>(0));
	FileDataRef anchor(vf->click_fd);
	g_autofree gchar *path = g_build_filename(directory, "0.jpg", nullptr);
	EXPECT_EQ(g_unlink(path), 0);
	vf_refresh(vf);
	EXPECT_EQ(vf->click_fd, *anchor);
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Right, GDK_SHIFT_MASK);
	EXPECT_EQ(vf_selection_count(vf, nullptr), 2U);
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Left, GDK_SHIFT_MASK);
	EXPECT_EQ(vf_selection_count(vf, nullptr), 1U);
}

TEST_P(RepeatedDelete, ViewSwitchWaitsForMenuActionToReturn)
{
	auto *previous = layout->vf;
	const auto target = previous->type == FILEVIEW_LIST ? FILEVIEW_ICON : FILEVIEW_LIST;
	layout_views_set(layout, layout->options.dir_view_type, target);
	EXPECT_EQ(layout->vf, previous);
	EXPECT_NE(layout->views_idle_id, 0U);
	const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	while (layout->views_idle_id && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		g_usleep(1000);
		}
	ASSERT_EQ(layout->views_idle_id, 0U);
	EXPECT_EQ(layout->vf->type, target);
	EXPECT_EQ(vf_count(layout->vf), 3U);
}

TEST_P(RepeatedDelete, ViewSwitchClosesContextMenu)
{
	gtk_window_present(GTK_WINDOW(layout->window));
	const gint64 map_deadline = g_get_monotonic_time() + (5 * G_TIME_SPAN_SECOND);
	while (!gtk_widget_get_mapped(layout->vf->listview) && g_get_monotonic_time() < map_deadline)
		g_main_context_iteration(nullptr, FALSE);
	ASSERT_TRUE(gtk_widget_get_mapped(layout->vf->listview));

	auto *previous_view = layout->vf->listview;
	g_object_add_weak_pointer(G_OBJECT(previous_view), reinterpret_cast<gpointer *>(&previous_view));
	layout->vf->popup = vf_pop_menu(layout->vf, previous_view, 10, 10);
	g_autoptr(GtkWidget) menu = GTK_WIDGET(g_object_ref(layout->vf->popup));
	gtk_popover_popdown(GTK_POPOVER(menu));
	const auto target = layout->vf->type == FILEVIEW_LIST ? FILEVIEW_ICON : FILEVIEW_LIST;
	layout_views_set(layout, layout->options.dir_view_type, target);
	const gint64 deadline = g_get_monotonic_time() + (5 * G_TIME_SPAN_SECOND);
	while (layout->views_idle_id && g_get_monotonic_time() < deadline)
		g_main_context_iteration(nullptr, FALSE);
	EXPECT_EQ(layout->views_idle_id, 0U);
	EXPECT_EQ(gtk_widget_get_parent(menu), nullptr);
	EXPECT_EQ(previous_view, nullptr);
	if (previous_view) g_object_remove_weak_pointer(G_OBJECT(previous_view), reinterpret_cast<gpointer *>(&previous_view));
	EXPECT_EQ(layout->vf->type, target);
	EXPECT_EQ(vf_count(layout->vf), 3U);
}

TEST_P(RepeatedDelete, ClosingWindowCancelsPendingViewSwitch)
{
	const auto target = layout->vf->type == FILEVIEW_LIST ? FILEVIEW_ICON : FILEVIEW_LIST;
	layout_views_set(layout, layout->options.dir_view_type, target);
	const guint source_id = layout->views_idle_id;
	ASSERT_NE(source_id, 0U);
	layout_free(layout);
	layout = nullptr;
	EXPECT_EQ(g_main_context_find_source_by_id(nullptr, source_id), nullptr);
}

INSTANTIATE_TEST_SUITE_P(FileViews, RepeatedDelete,
                        testing::Combine(testing::Values(FILEVIEW_LIST, FILEVIEW_ICON), testing::Bool()));

GtkWidget *find_file_widget(GtkWidget *widget, FileData *fd)
{
	if (g_object_get_data(G_OBJECT(widget), "view-file-fd") == fd) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_file_widget(child, fd)) return found;
		}
	return nullptr;
}

class RevealFile : public testing::TestWithParam<std::tuple<FileViewType, bool>>
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		setup_default_options(options);
		saved_thumbnail_size = options->thumbnails.size;
		saved_rectangular = options->collections.rectangular_selection;
		options->thumbnails.size = {128, 96};
		g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &saved_animations, nullptr);
		g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);
		filter_add_defaults();
		filter_rebuild();
		directory = g_dir_make_tmp("geeqie-reveal-file-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (int i = 0; i < 150; ++i)
			{
			g_autofree gchar *name = g_strdup_printf("%03d.svg", i);
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_TRUE(g_file_set_contents(path, "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'/>", -1, nullptr));
			}
		auto *dir_fd = file_data_new_dir(directory);
		vf = vf_new(std::get<0>(GetParam()), dir_fd);
		file_data_unref(dir_fd);
		if (vf->type == FILEVIEW_LIST) gtk_widget_set_visible(VFLIST(vf)->details_scrolled, std::get<1>(GetParam()));
		window = gtk_window_new();
		auto *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		entry = gtk_entry_new();
		gtk_box_append(GTK_BOX(box), entry);
		gtk_box_append(GTK_BOX(box), vf->widget);
		gtk_window_set_child(GTK_WINDOW(window), box);
		gtk_window_set_default_size(GTK_WINDOW(window), 600, 300);
		gtk_window_present(GTK_WINDOW(window));
		settle();
	}

	void TearDown() override
	{
		if (window) gtk_window_destroy(GTK_WINDOW(window));
		if (!directory) return;
		options->thumbnails.size = saved_thumbnail_size;
		options->collections.rectangular_selection = saved_rectangular;
		g_object_set(gtk_settings_get_default(), "gtk-enable-animations", saved_animations, nullptr);
		for (int i = 0; i < 150; ++i)
			{
			g_autofree gchar *name = g_strdup_printf("%03d.svg", i);
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			g_remove(path);
			}
		g_rmdir(directory);
		g_free(directory);
	}

	static void settle()
	{
		const gint64 deadline = g_get_monotonic_time() + (350 * G_TIME_SPAN_MILLISECOND);
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
	}

	ViewFile *vf = nullptr;
	GtkWidget *window = nullptr;
	GtkWidget *entry = nullptr;
	GqSize saved_thumbnail_size{};
	gboolean saved_rectangular = FALSE;
	gboolean saved_animations = TRUE;
	gchar *directory = nullptr;
};

TEST_P(RevealFile, NavigationAfterDragMoveReplacesSelection)
{
	if (vf->type != FILEVIEW_LIST) GTEST_SKIP() << "Requires list view";
	vf_select_by_fd(vf, vf_index_get_data(vf, 0));
	g_autoptr(FileDataList) selected = nullptr;
	selected = g_list_append(selected, file_data_ref(vf_index_get_data(vf, 0)));
	selected = g_list_append(selected, file_data_ref(vf_index_get_data(vf, 1)));
	vf_select_list(vf, selected);
	// Pressing a selected row preserves the group until the drag finishes.
	vf->preserve_selection = TRUE;
	vf->drag_started = TRUE;
	vf->drag_selection = filelist_copy(selected);

	g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(vf->listview);
	bool drag_ended = false;
	for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
		{
		g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
		if (!GTK_IS_DRAG_SOURCE(controller)) continue;
		g_signal_emit_by_name(controller, "drag-end", nullptr, FALSE);
		drag_ended = true;
		}
	ASSERT_TRUE(drag_ended);

	// Source rows disappear after the move; the following file is selected.
	for (GList *work = selected; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		ASSERT_EQ(g_remove(fd->path), 0);
		}
	vf_refresh(vf);
	ASSERT_EQ(vf_selection_count(vf, nullptr), 1U);
	ASSERT_TRUE(vflist_is_selected(vf, vf_index_get_data(vf, 0)));

	// Image-navigation shortcuts select the next file through this API.
	auto *next = vf_index_get_data(vf, 1);
	vf_select_by_fd(vf, next);
	EXPECT_EQ(vf_selection_count(vf, nullptr), 1U);
	EXPECT_TRUE(vflist_is_selected(vf, next));
}

TEST_P(RevealFile, ScrollsWithoutChangingSelectionOrFocus)
{
	auto *first = vf_index_get_data(vf, 0);
	auto *second = vf_index_get_data(vf, 1);
	auto *target = vf_index_get_data(vf, 120);
	ASSERT_NE(target, nullptr);
	vf_select_none(vf);
	g_autoptr(FileDataList) selection = g_list_append(nullptr, file_data_ref(first));
	selection = g_list_append(selection, file_data_ref(second));
	vf_select_list(vf, selection);
	gtk_widget_grab_focus(entry);
	settle();
	auto *focus = gtk_window_get_focus(GTK_WINDOW(window));
	auto *scrolled = vf->type == FILEVIEW_LIST ? VFLIST(vf)->name_scrolled : vf->scrolled;
	auto *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled));
	gtk_adjustment_set_value(adjustment, 0);
	settle();
	EXPECT_EQ(gtk_adjustment_get_value(adjustment), 0);
	vf_scroll_to_file(vf, target);
	settle();
	EXPECT_GT(gtk_adjustment_get_value(adjustment), 0);
	EXPECT_EQ(gtk_window_get_focus(GTK_WINDOW(window)), focus);
	g_autoptr(FileDataList) after = vf_selection_get_list(vf);
	EXPECT_EQ(g_list_length(after), 2U);
	EXPECT_NE(g_list_find(after, first), nullptr);
	EXPECT_NE(g_list_find(after, second), nullptr);
	EXPECT_EQ(g_list_find(after, target), nullptr);
	if (vf->type == FILEVIEW_LIST)
		{
		auto *model = gtk_tree_view_get_model(GTK_TREE_VIEW(vf->listview));
		GtkTreeIter iter;
		ASSERT_TRUE(gtk_tree_model_iter_nth_child(model, &iter, nullptr, 120));
		EXPECT_TRUE(tree_view_row_is_visible(GTK_TREE_VIEW(vf->listview), &iter, TRUE));
		}
	else
		{
		auto *item = find_file_widget(vf->listview, target);
		ASSERT_NE(item, nullptr);
		graphene_rect_t bounds;
		ASSERT_TRUE(gtk_widget_compute_bounds(item, vf->scrolled, &bounds));
		EXPECT_GE(bounds.origin.y, 0);
		EXPECT_LE(bounds.origin.y + bounds.size.height, gtk_widget_get_height(vf->scrolled));
		}
}

TEST_P(RevealFile, DownMovesOneDisplayedIconRowAfterSelectNone)
{
	if (vf->type != FILEVIEW_ICON) GTEST_SKIP() << "Requires icon view";
	g_autoptr(GtkCssProvider) css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, ".view-file-grid-item { min-width: 350px; }");
	auto *display = gtk_widget_get_display(window);
	gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	for (const int width : {600, 1000})
		{
		SCOPED_TRACE(width);
		gtk_window_set_default_size(GTK_WINDOW(window), width, 300);
		settle();
		auto *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(vf->scrolled));
		gtk_adjustment_set_value(adjustment, 0);
		settle();
		auto *first = vf_index_get_data(vf, 0);
		auto *first_widget = find_file_widget(vf->listview, first);
		EXPECT_NE(first_widget, nullptr);
		if (!first_widget) continue;
		graphene_rect_t first_bounds{};
		EXPECT_TRUE(gtk_widget_compute_bounds(first_widget, vf->listview, &first_bounds));
		int next_row = 0;
		for (int i = 1; i < 12; ++i)
			{
			auto *item = find_file_widget(vf->listview, vf_index_get_data(vf, i));
			graphene_rect_t bounds{};
			if (item && gtk_widget_compute_bounds(item, vf->listview, &bounds) && bounds.origin.y > first_bounds.origin.y + 1)
				{
				next_row = i;
				break;
				}
			}
		EXPECT_GT(next_row, 0);
		if (!next_row) continue;
		auto *below = vf_index_get_data(vf, next_row);
		vf_select_none(vf);
		const ViewFileMouseButtonEvent click{vf->listview, GDK_BUTTON_PRIMARY,
		                                     first_bounds.origin.x + first_bounds.size.width / 2,
		                                     first_bounds.origin.y + 10, static_cast<GdkModifierType>(0), 1};
		vficon_press_cb(vf, click);
		vficon_release_cb(vf, click);
		EXPECT_TRUE(vficon_is_selected(vf, first));
		vficon_press_key_cb(vf, vf->listview, GDK_KEY_Down, static_cast<GdkModifierType>(0));
		EXPECT_EQ(VFICON(vf)->columns, next_row);
		EXPECT_EQ(VFICON(vf)->focus_fd, below);
		EXPECT_EQ(vf_selection_count(vf, nullptr), 1U);
		EXPECT_TRUE(vficon_is_selected(vf, below));
		vficon_press_key_cb(vf, vf->listview, GDK_KEY_Up, static_cast<GdkModifierType>(0));
		EXPECT_EQ(VFICON(vf)->focus_fd, first);
		vficon_press_key_cb(vf, vf->listview, GDK_KEY_Down, GDK_CONTROL_MASK);
		EXPECT_EQ(VFICON(vf)->focus_fd, below);
		EXPECT_TRUE(vficon_is_selected(vf, first));
		vficon_press_key_cb(vf, vf->listview, GDK_KEY_Down, GDK_CONTROL_MASK);
		EXPECT_EQ(VFICON(vf)->focus_fd, vf_index_get_data(vf, next_row * 2));
		EXPECT_EQ(vf_selection_count(vf, nullptr), 1U);
		EXPECT_TRUE(vficon_is_selected(vf, first));
		}
	gtk_style_context_remove_provider_for_display(display, GTK_STYLE_PROVIDER(css));
}

TEST_P(RevealFile, CtrlDownMovesIconFocusWithoutChangingSelection)
{
	if (vf->type != FILEVIEW_ICON) GTEST_SKIP() << "Requires icon view";
	g_autofree gchar *xdotool = g_find_program_in_path("xdotool");
	if (!xdotool || !g_getenv("DISPLAY")) GTEST_SKIP() << "Requires xdotool and X11";
	gtk_window_set_title(GTK_WINDOW(window), "Geeqie icon keyboard navigation test");
	auto *first = vf_index_get_data(vf, 0);
	vf_select_none(vf);
	vf_select_by_fd(vf, first);
	gtk_widget_grab_focus(vf->listview);
	settle();
	for (int i = 1; i <= 2; ++i)
		{
		const gchar *args[] = {xdotool, "search", "--name", "^Geeqie icon keyboard navigation test$",
		                       "windowfocus", "--sync", "key", "--clearmodifiers", "ctrl+Down", nullptr};
		gint status = 0;
		ASSERT_TRUE(g_spawn_sync(nullptr, const_cast<gchar **>(args), nullptr, G_SPAWN_DEFAULT,
		                         nullptr, nullptr, nullptr, nullptr, &status, nullptr));
		ASSERT_TRUE(g_spawn_check_wait_status(status, nullptr));
		settle();
		EXPECT_EQ(VFICON(vf)->focus_fd, vf_index_get_data(vf, VFICON(vf)->columns * i));
		EXPECT_EQ(vf_selection_count(vf, nullptr), 1U);
		EXPECT_TRUE(vficon_is_selected(vf, first));
		auto *focused = find_file_widget(vf->listview, VFICON(vf)->focus_fd);
		ASSERT_NE(focused, nullptr);
		EXPECT_TRUE(gtk_widget_has_css_class(focused, "view-file-grid-focus"));
		guint outlined = 0;
		for (GList *work = vf->list; work; work = work->next)
			{
			auto *item = find_file_widget(vf->listview, static_cast<FileData *>(work->data));
			if (item && gtk_widget_has_css_class(item, "view-file-grid-focus")) ++outlined;
			}
		EXPECT_EQ(outlined, 1U);
		}
	auto *focused = find_file_widget(vf->listview, VFICON(vf)->focus_fd);
	ASSERT_NE(focused, nullptr);
	gtk_widget_grab_focus(entry);
	settle();
	EXPECT_FALSE(gtk_widget_has_css_class(focused, "view-file-grid-focus"));
}

TEST_P(RevealFile, IconKeyboardInputSelectsRangesTogglesAndPages)
{
	if (vf->type != FILEVIEW_ICON) GTEST_SKIP() << "Requires icon view";
	g_autofree gchar *xdotool = g_find_program_in_path("xdotool");
	if (!xdotool || !g_getenv("DISPLAY")) GTEST_SKIP() << "Requires xdotool and X11";
	gtk_window_set_title(GTK_WINDOW(window), "Geeqie icon input combinations test");
	vf_select_none(vf);
	vf_select_by_fd(vf, vf_index_get_data(vf, 0));
	vf->click_fd = vf_index_get_data(vf, 0);
	gtk_widget_grab_focus(vf->listview);
	settle();
	const auto send_key = [&](const char *key, const std::function<bool()> &ready)
		{
		const gchar *args[] = {xdotool, "search", "--name", "^Geeqie icon input combinations test$",
		                       "windowfocus", "--sync", "key", "--clearmodifiers", key, nullptr};
		gint status = 0;
		if (!g_spawn_sync(nullptr, const_cast<gchar **>(args), nullptr, G_SPAWN_DEFAULT,
		                  nullptr, nullptr, nullptr, nullptr, &status, nullptr) || !g_spawn_check_wait_status(status, nullptr)) return false;
		const gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
		do
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (ready()) return true;
			g_usleep(1000);
			} while (g_get_monotonic_time() < deadline);
		auto *item = find_file_widget(vf->listview, VFICON(vf)->focus_fd);
		graphene_rect_t bounds{};
		if (item) gtk_widget_compute_bounds(item, vf->scrolled, &bounds);
		ADD_FAILURE() << "Keyboard state did not settle after " << key
		              << "; focus=" << vf_index_by_fd(vf, VFICON(vf)->focus_fd)
		              << "; selected=" << vf_selection_count(vf, nullptr)
		              << "; outlined=" << (item && gtk_widget_has_css_class(item, "view-file-grid-focus"))
		              << "; y=" << bounds.origin.y << "; height=" << bounds.size.height
		              << "; viewport=" << gtk_widget_get_height(vf->scrolled);
		return false;
		};
	const auto at = [&](int index) { return VFICON(vf)->focus_fd == vf_index_get_data(vf, index); };
	const auto only = [&](int index)
		{
		return vf_selection_count(vf, nullptr) == 1 && vficon_is_selected(vf, vf_index_get_data(vf, index));
		};
	const auto single_visible_outline = [&]()
		{
		guint outlines = 0;
		for (GList *work = vf->list; work; work = work->next)
			{
			auto *item = find_file_widget(vf->listview, static_cast<FileData *>(work->data));
			if (item && gtk_widget_has_css_class(item, "view-file-grid-focus")) ++outlines;
			}
		auto *item = find_file_widget(vf->listview, VFICON(vf)->focus_fd);
		graphene_rect_t bounds{};
		return outlines == 1 && item && gtk_widget_has_css_class(item, "view-file-grid-focus") &&
		       gtk_widget_compute_bounds(item, vf->scrolled, &bounds) && bounds.origin.y >= 0 &&
		       bounds.origin.y + bounds.size.height <= gtk_widget_get_height(vf->scrolled);
		};
	ASSERT_TRUE(send_key("Home", [&]() { return at(0) && only(0) && single_visible_outline(); }));
	ASSERT_TRUE(send_key("Right", [&]() { return at(1) && only(1); }));
	ASSERT_TRUE(send_key("Left", [&]() { return at(0) && only(0); }));
	const int columns = VFICON(vf)->columns;
	ASSERT_TRUE(send_key("Down", [&]() { return at(columns) && only(columns); }));
	ASSERT_TRUE(send_key("Up", [&]() { return at(0) && only(0); }));
	ASSERT_TRUE(send_key("ctrl+Right", [&]() { return at(1) && only(0); }));
	ASSERT_TRUE(send_key("ctrl+space", [&]() { return vf_selection_count(vf, nullptr) == 2; }));
	EXPECT_TRUE(vficon_is_selected(vf, vf_index_get_data(vf, 0)));
	EXPECT_TRUE(vficon_is_selected(vf, vf_index_get_data(vf, 1)));
	ASSERT_TRUE(send_key("ctrl+space", [&]() { return at(1) && only(0); }));
	ASSERT_TRUE(send_key("space", [&]() { return at(1) && only(1); }));

	for (const bool rectangular : {false, true})
		{
		SCOPED_TRACE(rectangular);
		options->collections.rectangular_selection = rectangular;
		ASSERT_TRUE(send_key("Home", [&]() { return at(0) && only(0); }));
		const auto range_selected = [&]()
			{
			if (!at(columns) || vf_selection_count(vf, nullptr) != (rectangular ? 2U : columns + 1U)) return false;
			for (int i = 0; i <= columns; ++i)
				if (vficon_is_selected(vf, vf_index_get_data(vf, i)) != (!rectangular || i == 0 || i == columns)) return false;
			return true;
			};
		ASSERT_TRUE(send_key("shift+Down", range_selected));
		ASSERT_TRUE(send_key("shift+Up", [&]() { return at(0) && only(0); }));
		ASSERT_TRUE(send_key("ctrl+shift+Down", range_selected));
		ASSERT_TRUE(send_key("ctrl+shift+Up", [&]() { return at(0) && only(0); }));
		}
	options->collections.rectangular_selection = FALSE;
	const int last = vf_count(vf) - 1;
	ASSERT_TRUE(send_key("ctrl+End", [&]() { return at(last) && only(0) && single_visible_outline(); }));
	ASSERT_TRUE(send_key("ctrl+Home", [&]() { return at(0) && only(0) && single_visible_outline(); }));
	ASSERT_TRUE(send_key("shift+End", [&]() { return at(last) && vf_selection_count(vf, nullptr) == vf_count(vf); }));
	ASSERT_TRUE(send_key("shift+Home", [&]() { return at(0) && only(0); }));
	ASSERT_TRUE(send_key("End", [&]() { return at(last) && only(last); }));
	ASSERT_TRUE(send_key("Home", [&]() { return at(0) && only(0); }));
	ASSERT_TRUE(send_key("ctrl+Next", [&]() { return !at(0) && only(0) && single_visible_outline(); }));
	const int page_target = vf_index_by_fd(vf, VFICON(vf)->focus_fd);
	EXPECT_GT(page_target, 0);
	EXPECT_EQ(page_target % columns, 0);
	ASSERT_TRUE(send_key("ctrl+Prior", [&]() { return at(0) && only(0); }));
	ASSERT_TRUE(send_key("Next", [&]() { return at(page_target) && only(page_target); }));
	ASSERT_TRUE(send_key("Prior", [&]() { return at(0) && only(0); }));
	ASSERT_TRUE(send_key("shift+Next", [&]() { return at(page_target) && vf_selection_count(vf, nullptr) == page_target + 1U; }));
	ASSERT_TRUE(send_key("shift+Prior", [&]() { return at(0) && only(0); }));

	// Let GTK bind the replacement rows, then continue without a calibration key.
	g_autofree gchar *removed = g_build_filename(directory, "000.svg", nullptr);
	ASSERT_EQ(g_unlink(removed), 0);
	vf_refresh(vf);
	settle();
	ASSERT_TRUE(send_key("Right", [&]() { return at(1) && only(1) && single_visible_outline(); }));
}

TEST_P(RevealFile, IconNavigationModifierMatrix)
{
	if (vf->type != FILEVIEW_ICON) GTEST_SKIP() << "Requires icon view";
	gtk_widget_grab_focus(vf->listview);
	settle();
	vficon_press_key_cb(vf, vf->listview, GDK_KEY_Home, static_cast<GdkModifierType>(0));
	const int columns = VFICON(vf)->columns;
	ASSERT_GT(columns, 1);
	const int anchor = columns * 3 + 1;
	const std::pair<guint, int> destinations[] = {
		{GDK_KEY_Left, anchor - 1}, {GDK_KEY_Right, anchor + 1},
		{GDK_KEY_Up, anchor - columns}, {GDK_KEY_Down, anchor + columns},
		{GDK_KEY_Home, 0}, {GDK_KEY_End, static_cast<int>(vf_count(vf)) - 1}};
	const GdkModifierType modifiers_list[] = {static_cast<GdkModifierType>(0), GDK_CONTROL_MASK, GDK_SHIFT_MASK,
	                                          static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK)};
	for (const bool rectangular : {false, true})
		{
		options->collections.rectangular_selection = rectangular;
		for (const auto modifiers : modifiers_list)
			{
			for (const auto &destination : destinations)
				{
				SCOPED_TRACE(testing::Message() << "rectangular=" << rectangular << " modifiers=" << modifiers << " key=" << destination.first);
				vf_select_none(vf);
				vf_select_by_fd(vf, vf_index_get_data(vf, anchor));
				vf->click_fd = vf_index_get_data(vf, anchor);
				vficon_press_key_cb(vf, vf->listview, destination.first, static_cast<GdkModifierType>(modifiers));
				EXPECT_EQ(VFICON(vf)->focus_fd, vf_index_get_data(vf, destination.second));
				for (int i = 0; i < static_cast<int>(vf_count(vf)); ++i)
					{
					bool selected = i == (modifiers & GDK_CONTROL_MASK ? anchor : destination.second);
					if (modifiers & GDK_SHIFT_MASK)
						{
						selected = rectangular
						         ? i / columns >= std::min(anchor / columns, destination.second / columns) &&
						           i / columns <= std::max(anchor / columns, destination.second / columns) &&
						           i % columns >= std::min(anchor % columns, destination.second % columns) &&
						           i % columns <= std::max(anchor % columns, destination.second % columns)
						         : i >= std::min(anchor, destination.second) && i <= std::max(anchor, destination.second);
						}
					EXPECT_EQ(vficon_is_selected(vf, vf_index_get_data(vf, i)), selected) << i;
					}
				}
			}
		}
}

TEST_P(RevealFile, IconResizeKeepsModelItemsAndDisplaysFiles)
{
	if (vf->type != FILEVIEW_ICON) GTEST_SKIP() << "Requires icon view";
	g_autoptr(GObject) first = static_cast<GObject *>(g_list_model_get_item(G_LIST_MODEL(VFICON(vf)->store), 0));
	ASSERT_NE(first, nullptr);
	const gint old_columns = VFICON(vf)->columns;
	gtk_window_set_default_size(GTK_WINDOW(window), 250, 300);
	settle();
	EXPECT_NE(VFICON(vf)->columns, old_columns);
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(VFICON(vf)->store)), 150U);
	g_autoptr(GObject) after = static_cast<GObject *>(g_list_model_get_item(G_LIST_MODEL(VFICON(vf)->store), 0));
	EXPECT_EQ(first, after);
	auto *visible = find_file_widget(vf->listview, vf_index_get_data(vf, 0));
	ASSERT_NE(visible, nullptr);
	EXPECT_GT(gtk_widget_get_width(visible), 0);
	EXPECT_GT(gtk_widget_get_height(visible), 0);
}

INSTANTIATE_TEST_SUITE_P(FileViews, RevealFile,
                        testing::Values(std::make_tuple(FILEVIEW_LIST, false),
                                        std::make_tuple(FILEVIEW_LIST, true),
                                        std::make_tuple(FILEVIEW_ICON, false)));

} // namespace
