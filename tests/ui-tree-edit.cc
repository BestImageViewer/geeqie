/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <gtk/gtk.h>

#include "ui-tree-edit.h"

namespace
{

void drain_events()
{
	while (g_main_context_iteration(nullptr, FALSE)) {}
}

void check_editor_close(bool activate)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	GtkWidget *window = gtk_window_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(GTK_WINDOW(window), box);
	g_autoptr(GtkListStore) store = gtk_list_store_new(1, G_TYPE_STRING);
	GtkTreeIter iter;
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, "original.jpg", -1);
	GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Name",
	                                           gtk_cell_renderer_text_new(), "text", 0, nullptr);
	gtk_box_append(GTK_BOX(box), tree);
	GtkWidget *outside = gtk_entry_new();
	gtk_box_append(GTK_BOX(box), outside);
	gtk_window_present(GTK_WINDOW(window));
	drain_events();

	int edits = 0;
	g_autoptr(GtkTreePath) path = gtk_tree_path_new_first();
	ASSERT_TRUE(tree_edit_by_path(GTK_TREE_VIEW(tree), path, 0, "original.jpg",
		[](TreeEditData *, const gchar *old_name, const gchar *new_name, gpointer data) -> gboolean
			{
			EXPECT_STREQ(old_name, "original.jpg");
			EXPECT_STREQ(new_name, "renamed.jpg");
			++*static_cast<int *>(data);
			return TRUE;
			}, &edits));
	drain_events();

	GtkWidget *popover = gtk_widget_get_first_child(tree);
	while (popover && !GTK_IS_POPOVER(popover)) popover = gtk_widget_get_next_sibling(popover);
	ASSERT_NE(popover, nullptr);
	g_object_ref(popover);
	GtkWidget *entry = gtk_popover_get_child(GTK_POPOVER(popover));
	gtk_editable_set_text(GTK_EDITABLE(entry), "renamed.jpg");
	const GLogLevelFlags old_fatal = g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));
	if (activate)
		{
		g_signal_emit_by_name(entry, "activate");
		// Repeated activation before idle cleanup must not commit twice.
		g_signal_emit_by_name(entry, "activate");
		}
	else
		{
		EXPECT_TRUE(gtk_widget_grab_focus(outside));
		}
	// The editor must remain parented until GTK finishes the focus change.
	EXPECT_EQ(gtk_widget_get_parent(popover), tree);
	drain_events();
	EXPECT_EQ(edits, 1);
	EXPECT_EQ(gtk_widget_get_parent(popover), nullptr);
	g_object_unref(popover);
	gtk_window_destroy(GTK_WINDOW(window));
	drain_events();
	g_log_set_always_fatal(old_fatal);
}

TEST(TreeEdit, FocusOutsideClosesEditor)
{
	check_editor_close(false);
}

TEST(TreeEdit, EntryActivationCommitsOnceAndClosesEditor)
{
	check_editor_close(true);
}

} // namespace
