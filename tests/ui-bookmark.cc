/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <gtk/gtk.h>

#include "gtest/gtest.h"

#include "history-list.h"
#include "ui-bookmark.h"

namespace
{

TEST(BookmarkDestination, HighlightSurvivesRefreshAndModeChanges)
{
	if (!gtk_init_check() || !gdk_display_get_default())
		{
		GTEST_SKIP() << "Requires a working GTK display";
		}

	constexpr const gchar *collections_key = "test_collection_destination";
	constexpr const gchar *folders_key = "test_folder_destination";
	history_list_free_key(collections_key);
	history_list_free_key(folders_key);
	history_list_add_to_key(collections_key, "First[path]/tmp/first.gqv", 0);
	g_autoptr(GtkWidget) bookmarks = bookmark_list_new(collections_key, nullptr);
	g_object_ref_sink(bookmarks);
	bookmark_list_set_no_defaults(bookmarks, TRUE);
	history_list_free_key(collections_key);
	bookmark_list_add(bookmarks, "First", "/tmp/first.gqv");
	bookmark_list_add(bookmarks, "Second", "/tmp/second.gqv");

	EXPECT_EQ(bookmark_list_get_selected_path(bookmarks), nullptr);
	bookmark_list_set_selected_path(bookmarks, "/tmp/first.gqv");
	EXPECT_STREQ(bookmark_list_get_selected_path(bookmarks), "/tmp/first.gqv");

	// Adding another entry rebuilds the buttons without losing the destination.
	bookmark_list_add(bookmarks, "Third", "/tmp/third.gqv");
	GtkWidget *box = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(bookmarks));
	if (GTK_IS_VIEWPORT(box)) box = gtk_viewport_get_child(GTK_VIEWPORT(box));
	guint highlighted = 0;
	for (GtkWidget *child = gtk_widget_get_first_child(box); child; child = gtk_widget_get_next_sibling(child))
		{
		if (gtk_widget_has_css_class(child, "suggested-action")) highlighted++;
		}
	EXPECT_EQ(highlighted, 1U);
	EXPECT_STREQ(bookmark_list_get_selected_path(bookmarks), "/tmp/first.gqv");

	bookmark_list_set_selected_path(bookmarks, "/tmp/second.gqv");
	bookmark_list_set_key(bookmarks, folders_key);
	EXPECT_EQ(bookmark_list_get_selected_path(bookmarks), nullptr);
	bookmark_list_set_key(bookmarks, collections_key);
	EXPECT_STREQ(bookmark_list_get_selected_path(bookmarks), "/tmp/second.gqv");

	// A removed destination must no longer be usable by the shortcut.
	history_list_free_key(collections_key);
	bookmark_list_add(bookmarks, "First", "/tmp/first.gqv");
	EXPECT_EQ(bookmark_list_get_selected_path(bookmarks), nullptr);
	history_list_free_key(collections_key);
	history_list_free_key(folders_key);
}

TEST(BookmarkDestination, NumbersMatchVisibleOrderAndStopAtNine)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	constexpr const gchar *key = "test_numbered_destinations";
	history_list_free_key(key);
	history_list_add_to_key(key, "Seed[path]/tmp/numbered-seed", 0);
	g_autoptr(GtkWidget) bookmarks = bookmark_list_new(key, nullptr);
	g_object_ref_sink(bookmarks);
	bookmark_list_set_no_defaults(bookmarks, TRUE);
	history_list_free_key(key);
	bookmark_list_set_numbered(bookmarks, TRUE);
	for (guint i = 1; i <= 10; ++i)
		{
		g_autofree gchar *name = g_strdup_printf("Folder %u", i);
		g_autofree gchar *path = g_strdup_printf("/tmp/numbered-destination-%u", i);
		bookmark_list_add(bookmarks, name, path);
		}
	auto *box = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(bookmarks));
	if (GTK_IS_VIEWPORT(box)) box = gtk_viewport_get_child(GTK_VIEWPORT(box));
	guint position = 0;
	for (auto *button = gtk_widget_get_first_child(box); button; button = gtk_widget_get_next_sibling(button))
		{
		++position;
		auto *contents = gtk_button_get_child(GTK_BUTTON(button));
		auto *label = gtk_widget_get_next_sibling(gtk_widget_get_first_child(contents));
		g_autofree gchar *expected = position <= 9 ? g_strdup_printf("%u", position) : g_strdup_printf("Folder %u", position);
		ASSERT_TRUE(GTK_IS_LABEL(label));
		EXPECT_STREQ(gtk_label_get_text(GTK_LABEL(label)), expected);
		g_autofree gchar *path = g_strdup_printf("/tmp/numbered-destination-%u", position);
		EXPECT_STREQ(bookmark_list_get_path_at_index(bookmarks, position - 1), path);
		}
	EXPECT_EQ(position, 10U);
	EXPECT_EQ(bookmark_list_get_path_at_index(bookmarks, 10), nullptr);
	// Reordering history changes both the visible numbers and indexed targets.
	history_list_free_key(key);
	bookmark_list_add(bookmarks, "Last first", "/tmp/numbered-destination-10");
	EXPECT_STREQ(bookmark_list_get_path_at_index(bookmarks, 0), "/tmp/numbered-destination-10");
	history_list_free_key(key);
}

TEST(BookmarkDestination, CtrlClickSelectsWithoutActivating)
{
	g_autofree gchar *xdotool = g_find_program_in_path("xdotool");
	if (!xdotool || !g_getenv("DISPLAY") || !gtk_init_check())
		{
		GTEST_SKIP() << "Requires xdotool and an X display";
		}

	constexpr const gchar *key = "test_collection_ctrl_click";
	history_list_free_key(key);
	history_list_add_to_key(key, "Collection[path]/tmp/click.gqv", 0);
	guint activations = 0;
	GtkWidget *bookmarks = bookmark_list_new(key, [&activations](const gchar *) { activations++; });
	bookmark_list_set_select_only_on_ctrl(bookmarks, TRUE);
	GtkWidget *window = gtk_window_new();
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_title(GTK_WINDOW(window), "Geeqie bookmark Ctrl click test");
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 150);
	gtk_window_set_child(GTK_WINDOW(window), bookmarks);
	gtk_window_present(GTK_WINDOW(window));

	const auto drain_events = []()
	{
		const gint64 deadline = g_get_monotonic_time() + 200 * G_TIME_SPAN_MILLISECOND;
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
	};
	drain_events();

	const auto click = [&](gboolean control)
	{
		const gchar *plain_args[] = {xdotool, "search", "--name", "^Geeqie bookmark Ctrl click test$",
		                             "mousemove", "--window", "%1", "40", "15", "click", "1", nullptr};
		const gchar *control_args[] = {xdotool, "search", "--name", "^Geeqie bookmark Ctrl click test$",
		                               "mousemove", "--window", "%1", "40", "15", "keydown", "ctrl",
		                               "click", "1", "keyup", "ctrl", nullptr};
		gint status = 0;
		EXPECT_TRUE(g_spawn_sync(nullptr, const_cast<gchar **>(control ? control_args : plain_args),
		                         nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr, &status, nullptr));
		EXPECT_TRUE(g_spawn_check_wait_status(status, nullptr));
		drain_events();
	};

	click(TRUE);
	EXPECT_STREQ(bookmark_list_get_selected_path(bookmarks), "/tmp/click.gqv");
	EXPECT_EQ(activations, 0U);
	click(FALSE);
	EXPECT_EQ(activations, 1U);

	// Folder bookmarks retain their ordinary Ctrl+click activation behavior.
	bookmark_list_set_select_only_on_ctrl(bookmarks, FALSE);
	click(TRUE);
	EXPECT_EQ(activations, 2U);
	gtk_window_destroy(GTK_WINDOW(window));
	history_list_free_key(key);
}

} // namespace
