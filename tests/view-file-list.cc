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
