/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <string>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout.h"
#include "options.h"
#include "ui-menu.h"
#include "view-dir.h"
#include "view-dir-list.h"
#include "view-dir-tree.h"

namespace
{

GtkWidget *find_entry(GtkWidget *widget)
{
	if (GTK_IS_ENTRY(widget)) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *entry = find_entry(child)) return entry;
		}
	return nullptr;
}

void drain_events()
{
	for (int i = 0; i < 30; ++i)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
}

class ViewDirNewFolder : public testing::TestWithParam<DirViewType>
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		saved_descend = options->tree_descend_subdirs;
		options->tree_descend_subdirs = FALSE;
		setup_default_options(options);
		filter_add_defaults();
		filter_rebuild();
		accel_map_load_merged();
		previous_app = g_application_get_default();
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.InlineFolderTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		directory = g_dir_make_tmp("geeqie-inline-folder-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		layout.window = gtk_application_window_new(app);
		layout.options.dir_view_type = GetParam();
		layout.options.dir_view_list_sort = {SORT_NAME, TRUE, TRUE};
		layout.dir_fd = file_data_new_dir(directory);
		vd = vd_new(&layout);
		gtk_window_set_child(GTK_WINDOW(layout.window), vd->widget);
		gtk_window_set_default_size(GTK_WINDOW(layout.window), 600, 400);
		gtk_window_present(GTK_WINDOW(layout.window));
		drain_events();
	}

	void TearDown() override
	{
		if (!directory) return;
		options->tree_descend_subdirs = saved_descend;
		if (layout.window) gtk_window_destroy(GTK_WINDOW(layout.window));
		drain_events();
		if (layout.dir_fd) file_data_unref(layout.dir_fd);
		if (app)
			{
			g_application_set_default(previous_app);
			g_object_unref(app);
			}
		if (directory)
			{
			g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
			while (const gchar *name = g_dir_read_name(dir))
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				g_rmdir(path);
				}
			g_rmdir(directory);
			g_free(directory);
			}
	}

	GtkWidget *start()
	{
		vd_new_folder(vd, layout.dir_fd);
		// Opening the editor may need multiple frame-clock ticks to lay out the
		// placeholder, scroll it into view, and then map the popover.
		const gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			GtkWidget *entry = find_entry(vd->widget);
			if (entry && gtk_widget_get_mapped(entry) && gtk_widget_get_width(entry) > 0) return entry;
			g_usleep(1000);
			}
		ADD_FAILURE() << "The new-folder editor did not become visible";
		return nullptr;
	}

	LayoutWindow layout{};
	ViewDir *vd = nullptr;
	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	gchar *directory = nullptr;
	gboolean saved_descend = FALSE;
};

TEST_P(ViewDirNewFolder, KnownPathThroughExecuteOnlyAncestorSurvivesRefresh)
{
	if (GetParam() != DIRVIEW_TREE) GTEST_SKIP() << "Requires the folder tree";
	g_autofree gchar *child_path = g_build_filename(directory, "accessible", nullptr);
	g_autofree gchar *sibling_path = g_build_filename(directory, "unlisted", nullptr);
	ASSERT_EQ(g_mkdir(child_path, 0700), 0);
	ASSERT_EQ(g_mkdir(sibling_path, 0700), 0);
	struct RestorePermissions
	{
		const gchar *path;
		~RestorePermissions() { g_chmod(path, 0700); }
	} restore{directory};
	ASSERT_EQ(g_chmod(directory, 0100), 0);
	{
		g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
		if (dir) GTEST_SKIP() << "Requires directory read permissions to be enforced";
	}
	auto child = FileData::new_dir(child_path);
	FileData *child_fd = child;
	ASSERT_TRUE(vd_set_fd(vd, child_fd));
	drain_events();
	GtkTreeIter iter;
	EXPECT_TRUE(vd_find_row(vd, child_fd, &iter));
	auto sibling = FileData::new_dir(sibling_path);
	FileData *sibling_fd = sibling;
	EXPECT_FALSE(vd_find_row(vd, sibling_fd, &iter));
	vd_refresh(vd);
	drain_events();
	EXPECT_TRUE(vd_find_row(vd, child_fd, &iter));
	EXPECT_FALSE(vd_find_row(vd, sibling_fd, &iter));

	// Navigating to another known child must bypass the cached parent listing.
	ASSERT_TRUE(vd_set_fd(vd, sibling_fd));
	drain_events();
	EXPECT_TRUE(vd_find_row(vd, sibling_fd, &iter));
	EXPECT_EQ(vdtree_populate_path(vd, sibling_fd, TRUE, TRUE), sibling_fd);

	RestorePermissions restore_sibling{sibling_path};
	ASSERT_EQ(g_chmod(sibling_path, 0000), 0);
	ASSERT_TRUE(vd_set_fd(vd, child_fd));
	EXPECT_EQ(vdtree_populate_path(vd, child_fd, TRUE, TRUE), child_fd);
	EXPECT_EQ(vdtree_populate_path(vd, sibling_fd, TRUE, TRUE), nullptr);
	EXPECT_FALSE(vd_find_row(vd, sibling_fd, &iter));
}

TEST_P(ViewDirNewFolder, KnownHiddenPathSurvivesRefreshWithoutDuplicates)
{
	if (GetParam() != DIRVIEW_TREE) GTEST_SKIP() << "Requires the folder tree";
	struct RestoreHiddenFilter
	{
		gboolean saved = options->file_filter.show_hidden_files;
		~RestoreHiddenFilter() { options->file_filter.show_hidden_files = saved; }
	} restore;
	options->file_filter.show_hidden_files = FALSE;
	g_autofree gchar *path = g_build_filename(directory, ".known", nullptr);
	g_autofree gchar *other_path = g_build_filename(directory, ".other", nullptr);
	ASSERT_EQ(g_mkdir(path, 0700), 0);
	ASSERT_EQ(g_mkdir(other_path, 0700), 0);
	auto child = FileData::new_dir(path);
	auto other = FileData::new_dir(other_path);
	ASSERT_TRUE(vd_set_fd(vd, child));
	for (int pass = 0; pass < 2; ++pass)
		{
		EXPECT_NE(vdtree_populate_path(vd, child, TRUE, TRUE), nullptr);
		GtkTreeIter iter;
		EXPECT_TRUE(vd_find_row(vd, child, &iter));
		EXPECT_FALSE(vd_find_row(vd, other, &iter));
		ASSERT_TRUE(vd_find_row(vd, layout.dir_fd, &iter));
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
		GtkTreeIter row;
		ASSERT_TRUE(gtk_tree_model_iter_children(model, &row, &iter));
		int matches = 0;
		do
			{
			g_autofree gchar *name = nullptr;
			gtk_tree_model_get(model, &row, DIR_COLUMN_NAME, &name, -1);
			if (g_strcmp0(name, ".known") == 0) ++matches;
			} while (gtk_tree_model_iter_next(model, &row));
		EXPECT_EQ(matches, 1);
		vd_refresh(vd);
		drain_events();
		}
}

TEST_P(ViewDirNewFolder, DescendChecksExistingUnexpandedSubfolders)
{
	if (GetParam() != DIRVIEW_TREE) GTEST_SKIP() << "Requires the folder tree";
	vd_set_fd(vd, layout.dir_fd);
	drain_events();
	g_autofree gchar *path = g_build_filename(directory, "created", nullptr);
	ASSERT_EQ(g_mkdir(path, 0700), 0);
	vd_refresh(vd);
	drain_events();
	auto *fd = file_data_new_dir(path);
	GtkTreeIter iter;
	ASSERT_TRUE(vd_find_row(vd, fd, &iter));
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	GtkTreeIter parent;
	ASSERT_TRUE(vd_find_row(vd, layout.dir_fd, &parent));
	g_autoptr(GtkTreePath) parent_path = gtk_tree_model_get_path(model, &parent);
	gtk_tree_view_expand_row(GTK_TREE_VIEW(vd->view), parent_path, FALSE);
	EXPECT_TRUE(gtk_tree_model_iter_has_child(model, &iter));

	options->tree_descend_subdirs = TRUE;
	vd_refresh(vd);
	drain_events();
	ASSERT_TRUE(vd_find_row(vd, fd, &iter));
	EXPECT_FALSE(gtk_tree_model_iter_has_child(model, &iter));
	file_data_unref(fd);
}

TEST_P(ViewDirNewFolder, LongNamesDoNotHideNewFolderText)
{
	if (GetParam() != DIRVIEW_LIST) GTEST_SKIP() << "Requires the folder list";
	for (int i = 0; i < 20; ++i)
		{
		const std::string name(220, 'a' + i);
		g_autofree gchar *path = g_build_filename(directory, name.c_str(), nullptr);
		ASSERT_EQ(g_mkdir(path, 0700), 0);
		}
	vd_set_fd(vd, layout.dir_fd);
	vd_refresh(vd);
	drain_events();
	auto *horizontal = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(vd->widget));
	ASSERT_GT(gtk_adjustment_get_upper(horizontal), gtk_adjustment_get_page_size(horizontal));

	for (bool scroll_right : {false, true})
		{
		gtk_adjustment_set_value(horizontal, scroll_right ? gtk_adjustment_get_upper(horizontal) : 0);
		drain_events();
		auto *entry = start();
		ASSERT_NE(entry, nullptr);
		const auto check_bounds = [&]()
			{
			graphene_rect_t bounds;
			ASSERT_TRUE(gtk_widget_compute_bounds(entry, vd->widget, &bounds));
			EXPECT_GE(bounds.origin.x, 0);
			EXPECT_LE(bounds.origin.x + bounds.size.width, gtk_widget_get_width(vd->widget));
			EXPECT_TRUE(gtk_widget_get_mapped(entry));
			};
		check_bounds();
		gtk_editable_set_text(GTK_EDITABLE(entry), "typed-test");
		gtk_editable_set_position(GTK_EDITABLE(entry), -1);
		gtk_widget_grab_focus(entry);
		drain_events();
		check_bounds();
		vd_set_fd(vd, layout.dir_fd);
		drain_events();
		}
}

TEST_P(ViewDirNewFolder, CreatesOnlyAfterReturnAndRetainsInvalidNames)
{
	auto *entry = start();
	ASSERT_NE(entry, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "created", nullptr);
	EXPECT_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
	gtk_editable_set_text(GTK_EDITABLE(entry), "../invalid");
	g_signal_emit_by_name(entry, "activate");
	drain_events();
	EXPECT_NE(vd->new_folder, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "created");
	g_signal_emit_by_name(entry, "activate");
	drain_events();
	EXPECT_TRUE(g_file_test(path, G_FILE_TEST_IS_DIR));
	EXPECT_EQ(vd->new_folder, nullptr);

	entry = start();
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "created");
	g_signal_emit_by_name(entry, "activate");
	drain_events();
	EXPECT_NE(vd->new_folder, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "new-folder");
	g_signal_emit_by_name(entry, "activate");
	drain_events();
	g_autofree gchar *default_path = g_build_filename(directory, "new-folder", nullptr);
	EXPECT_TRUE(g_file_test(default_path, G_FILE_TEST_IS_DIR));
}

TEST_P(ViewDirNewFolder, FocusMovesToCreatedFolderWithoutEnteringIt)
{
	auto *entry = start();
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "created");
	g_signal_emit_by_name(entry, "activate");
	drain_events();
	drain_events();
	ASSERT_EQ(vd->new_folder, nullptr);
	EXPECT_STREQ(vd->dir_fd->path, directory);

	if (GetParam() == DIRVIEW_LIST)
		{
		auto *focus = gtk_root_get_focus(GTK_ROOT(layout.window));
		ASSERT_TRUE(GTK_IS_BUTTON(focus));
		auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(focus), "vdlist-fd"));
		ASSERT_NE(fd, nullptr);
		EXPECT_STREQ(fd->name, "created");
		}
	else
		{
		EXPECT_TRUE(gtk_widget_has_focus(vd->view));
		auto *tree = GTK_TREE_VIEW(vd->view);
		g_autoptr(GtkTreePath) path = nullptr;
		gtk_tree_view_get_cursor(tree, &path, nullptr);
		ASSERT_NE(path, nullptr);
		auto *model = gtk_tree_view_get_model(tree);
		GtkTreeIter iter;
		ASSERT_TRUE(gtk_tree_model_get_iter(model, &iter, path));
		g_autofree gchar *name = nullptr;
		gtk_tree_model_get(model, &iter, DIR_COLUMN_NAME, &name, -1);
		EXPECT_STREQ(name, "created");
		}
}

TEST_P(ViewDirNewFolder, RefreshRetainsCreatedFolderFocusAndScroll)
{
	if (GetParam() != DIRVIEW_LIST) GTEST_SKIP() << "Requires the folder list";
	for (int i = 0; i < 60; ++i)
		{
		g_autofree gchar *path = g_strdup_printf("%s/folder-%02d", directory, i);
		ASSERT_EQ(g_mkdir(path, 0700), 0);
		}
	vd_refresh(vd);
	drain_events();
	auto *entry = start();
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "zzz-created");
	g_signal_emit_by_name(entry, "activate");
	drain_events();
	drain_events();
	auto *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(vd->widget));
	ASSERT_GT(gtk_adjustment_get_value(adjustment), 0);
	// Simulate delayed filesystem notifications after the initial focus settles.
	for (int i = 0; i < 3; ++i)
		{
		vd_refresh(vd);
		drain_events();
		drain_events();
		auto *focus = gtk_root_get_focus(GTK_ROOT(layout.window));
		ASSERT_TRUE(GTK_IS_BUTTON(focus));
		auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(focus), "vdlist-fd"));
		ASSERT_NE(fd, nullptr);
		EXPECT_STREQ(fd->name, "zzz-created");
		EXPECT_GT(gtk_adjustment_get_value(adjustment), 0);
		}
}

TEST_P(ViewDirNewFolder, RefreshPreservesOpenContextMenuAndTarget)
{
	if (GetParam() != DIRVIEW_LIST) GTEST_SKIP() << "Requires the folder list";
	g_autofree gchar *path = g_build_filename(directory, "target", nullptr);
	ASSERT_EQ(g_mkdir(path, 0700), 0);
	vd_refresh(vd);
	drain_events();
	vd->click_fd = vdlist_row_by_path(vd, path, nullptr);
	ASSERT_NE(vd->click_fd, nullptr);
	g_autoptr(GMenu) menu = g_menu_new();
	g_menu_append(menu, "Test item", "win.test");
	auto *popover = popup_menu_at(menu, vd->view, 10, 10);
	g_object_ref(popover);
	drain_events();
	ASSERT_TRUE(gtk_widget_get_mapped(popover));
	for (int i = 0; i < 3; ++i)
		{
		vd_refresh(vd);
		drain_events();
		EXPECT_EQ(gtk_widget_get_parent(popover), vd->view);
		EXPECT_TRUE(gtk_widget_get_mapped(popover));
		ASSERT_NE(vd->click_fd, nullptr);
		EXPECT_STREQ(vd->click_fd->path, path);
		}
	gtk_popover_popdown(GTK_POPOVER(popover));
	drain_events();
	g_object_unref(popover);
}

TEST_P(ViewDirNewFolder, EscapeCancelsWithoutCreatingDirectory)
{
	auto *entry = start();
	ASSERT_NE(entry, nullptr);
	g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(entry);
	for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
		{
		g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
		if (GTK_IS_EVENT_CONTROLLER_KEY(controller))
			{
			gboolean handled = FALSE;
			g_signal_emit_by_name(controller, "key-pressed", GDK_KEY_Escape, 0, static_cast<GdkModifierType>(0), &handled);
			}
		}
	drain_events();
	EXPECT_EQ(vd->new_folder, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "new-folder", nullptr);
	EXPECT_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
}

TEST_P(ViewDirNewFolder, RefreshPreservesEditorAndNavigationCancels)
{
	ASSERT_NE(start(), nullptr);
	vd_refresh(vd);
	EXPECT_NE(vd->new_folder, nullptr);
	vd_set_fd(vd, layout.dir_fd);
	drain_events();
	EXPECT_EQ(vd->new_folder, nullptr);
	ASSERT_NE(start(), nullptr);
	// TearDown also exercises destruction with an active editor.
}

INSTANTIATE_TEST_SUITE_P(DirectoryViews, ViewDirNewFolder, testing::Values(DIRVIEW_LIST, DIRVIEW_TREE));

} // namespace
