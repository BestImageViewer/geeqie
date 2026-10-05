/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <tuple>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "actions.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout.h"
#include "options.h"
#include "trash.h"
#include "ui-fileops.h"
#include "ui-tree-edit.h"
#include "view-file.h"
#include "view-file/view-file-list.h"

namespace
{

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

INSTANTIATE_TEST_SUITE_P(FileViews, RepeatedDelete,
                        testing::Combine(testing::Values(FILEVIEW_LIST, FILEVIEW_ICON), testing::Bool()));

} // namespace
