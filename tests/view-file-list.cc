/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <gtk/gtk.h>

#include "filedata.h"
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

} // namespace
