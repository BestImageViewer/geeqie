/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <cstring>
#include <filesystem>
#include <functional>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "cache-maint.h"
#include "cache.h"
#include "options.h"

namespace
{

bool wait_for(const std::function<bool()> &done)
{
	const gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		if (done()) return true;
		g_usleep(1000);
		}
	return false;
}

GtkWidget *find_size_label(GtkWidget *widget, const gchar *path)
{
	if (g_strcmp0(static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "cache-size-path")), path) == 0) return widget;
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_size_label(child, path)) return found;
		}
	return nullptr;
}

class CacheMaintenanceSize : public ::testing::Test
{
protected:
	void SetUp() override
	{
		if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		previous_app = g_application_get_default();
		if (previous_app) g_object_ref(previous_app);
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.CacheSizeTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		directory = g_dir_make_tmp("geeqie-cache-size-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		saved_cache = options->thumbnails.cache_dir;
		options->thumbnails.cache_dir = g_strdup(directory);
		path = g_strdup(get_thumbnails_cache_dir());
	}

	void TearDown() override
	{
		if (window)
			{
			gtk_window_close(window);
			g_object_unref(window);
			}
		if (directory)
			{
			g_free(options->thumbnails.cache_dir);
			options->thumbnails.cache_dir = saved_cache;
			std::filesystem::remove_all(directory);
			g_free(directory);
			g_free(path);
			}
		if (app)
			{
			g_application_set_default(previous_app);
			g_object_unref(app);
			}
		if (previous_app) g_object_unref(previous_app);
	}

	GtkWidget *open()
	{
		cache_manager_show();
		GListModel *windows = gtk_window_get_toplevels();
		for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
			{
			g_autoptr(GtkWindow) candidate = GTK_WINDOW(g_list_model_get_item(windows, i));
			if (auto *label = find_size_label(GTK_WIDGET(candidate), path))
				{
				window = GTK_WINDOW(g_object_ref(candidate));
				return label;
				}
			}
		return nullptr;
	}

	static bool finished(GtkWidget *label)
	{
		return strstr(gtk_label_get_text(GTK_LABEL(label)), "Calculating") == nullptr;
	}

	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	GtkWindow *window = nullptr;
	gchar *directory = nullptr;
	gchar *path = nullptr;
	gchar *saved_cache = nullptr;
};

TEST_F(CacheMaintenanceSize, CountsInBackgroundAndRefreshesWithoutFollowingLinks)
{
	ASSERT_EQ(g_mkdir(path, 0700), 0);
	g_autofree gchar *file = g_build_filename(path, "data", nullptr);
	ASSERT_TRUE(g_file_set_contents(file, "1234", 4, nullptr));
	g_autofree gchar *outside = g_build_filename(directory, "outside", nullptr);
	ASSERT_EQ(g_mkdir(outside, 0700), 0);
	g_autofree gchar *external = g_build_filename(outside, "large", nullptr);
	ASSERT_TRUE(g_file_set_contents(external, "external content", -1, nullptr));
	g_autofree gchar *link = g_build_filename(path, "link", nullptr);
	std::filesystem::create_directory_symlink(outside, link);
	auto *label = open();
	ASSERT_NE(label, nullptr);
	EXPECT_FALSE(finished(label));
	ASSERT_TRUE(wait_for([&]() { return finished(label); }));
	g_autofree gchar *size = g_format_size_full(4 + strlen(outside), G_FORMAT_SIZE_IEC_UNITS);
	EXPECT_NE(strstr(gtk_label_get_text(GTK_LABEL(label)), size), nullptr);
	g_autoptr(GCancellable) previous = G_CANCELLABLE(g_object_ref(g_object_get_data(G_OBJECT(label), "cache-size-cancellable")));
	ASSERT_TRUE(g_file_set_contents(file, "1234567890", 10, nullptr));
	cache_manager_show();
	EXPECT_TRUE(g_cancellable_is_cancelled(previous));
	ASSERT_TRUE(wait_for([&]() { return finished(label); }));
	g_autofree gchar *updated = g_format_size_full(10 + strlen(outside), G_FORMAT_SIZE_IEC_UNITS);
	EXPECT_NE(strstr(gtk_label_get_text(GTK_LABEL(label)), updated), nullptr);
}

TEST_F(CacheMaintenanceSize, DistinguishesMissingFromUnusableDirectory)
{
	auto *label = open();
	ASSERT_NE(label, nullptr);
	ASSERT_TRUE(wait_for([&]() { return finished(label); }));
	EXPECT_NE(strstr(gtk_label_get_text(GTK_LABEL(label)), "not created"), nullptr);
	ASSERT_TRUE(g_file_set_contents(path, "file", -1, nullptr));
	cache_manager_show();
	ASSERT_TRUE(wait_for([&]() { return finished(label); }));
	EXPECT_NE(strstr(gtk_label_get_text(GTK_LABEL(label)), "unable to calculate"), nullptr);
}

TEST_F(CacheMaintenanceSize, ClosingCancelsPendingWork)
{
	ASSERT_EQ(g_mkdir(path, 0700), 0);
	auto *label = open();
	ASSERT_NE(label, nullptr);
	g_autoptr(GCancellable) cancellable = G_CANCELLABLE(g_object_ref(g_object_get_data(G_OBJECT(label), "cache-size-cancellable")));
	gtk_window_close(window);
	EXPECT_TRUE(g_cancellable_is_cancelled(cancellable));
	g_clear_object(&window);
	// A late completion must not access the closed dialog or a newly opened one.
	auto *new_label = open();
	ASSERT_NE(new_label, nullptr);
	ASSERT_TRUE(wait_for([&]() { return finished(new_label); }));
	EXPECT_NE(strstr(gtk_label_get_text(GTK_LABEL(new_label)), "0 bytes"), nullptr);
}

} // namespace
