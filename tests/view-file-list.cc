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
#include "view-file/view-file-icon.h"
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

	void settle()
	{
		const gint64 deadline = g_get_monotonic_time() + 350 * G_TIME_SPAN_MILLISECOND;
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
	gboolean saved_animations = TRUE;
	gchar *directory = nullptr;
};

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

INSTANTIATE_TEST_SUITE_P(FileViews, RevealFile,
                        testing::Values(std::make_tuple(FILEVIEW_LIST, false),
                                        std::make_tuple(FILEVIEW_LIST, true),
                                        std::make_tuple(FILEVIEW_ICON, false)));

} // namespace
