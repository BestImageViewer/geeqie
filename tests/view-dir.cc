/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout.h"
#include "options.h"
#include "view-dir.h"

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
			for (const auto *name : {"created", "new-folder"})
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
		drain_events();
		return find_entry(vd->widget);
	}

	LayoutWindow layout{};
	ViewDir *vd = nullptr;
	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	gchar *directory = nullptr;
};

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
