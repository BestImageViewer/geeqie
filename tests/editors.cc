/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <future>
#include <string>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>

#include "editors.h"
#include "layout-util.h"
#include "main.h"
#include "options.h"

namespace
{

class PluginDiscovery : public ::testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		directory = g_dir_make_tmp("geeqie-plugins-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (const char *name : {"applications", "fallback"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_EQ(g_mkdir(path, 0700), 0);
			}
		saved_disabled = options->disabled_plugins;
		editor_table_clear();
	}

	std::string folder(const char *name = "applications") const
	{
		return std::string(directory) + "/" + name;
	}

	std::string write_plugin(const char *name, const char *extra = "", const char *subdir = "applications")
	{
		const auto path = folder(subdir) + "/" + name;
		const std::string contents = std::string("[Desktop Entry]\nType=Application\nName=Test plugin\n"
		                                       "Categories=X-Geeqie;\nExec=/bin/true %f\n") + extra;
		EXPECT_TRUE(g_file_set_contents(path.c_str(), contents.c_str(), -1, nullptr));
		paths.push_back(path);
		return path;
	}

	void install(const EditorDesktopFiles &files)
	{
		for (const auto &file : files) editor_add_desktop_file(file);
		editor_table_finish();
	}

	void TearDown() override
	{
		layout_editors_reload_finish();
		options->disabled_plugins = saved_disabled;
		editor_table_clear();
		for (const auto &path : paths) g_unlink(path.c_str());
		for (const char *name : {"applications", "fallback"}) g_rmdir(folder(name).c_str());
		g_rmdir(directory);
		g_free(directory);
	}

	gchar *directory = nullptr;
	std::vector<std::string> paths;
	std::vector<std::string> saved_disabled;
};

TEST_F(PluginDiscovery, WorkerOnlyLoadsDataAndRegistrationDoesNotRereadFiles)
{
	const auto path = write_plugin("worker.desktop", "X-Geeqie-File-Extensions=.jpg;.png\n");
	auto worker = std::async(std::launch::async, [this]
		{
		return editor_load_desktop_files({folder()}, nullptr);
		});
	const auto files = worker.get();
	ASSERT_EQ(files.size(), 1U);
	EXPECT_EQ(get_editor_by_command("worker.desktop"), nullptr);
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(desktop_file_list)), 0U);
	ASSERT_EQ(g_unlink(path.c_str()), 0);
	install(files);
	const auto *editor = get_editor_by_command("worker.desktop");
	ASSERT_NE(editor, nullptr);
	EXPECT_STREQ(editor->name, "Test plugin");
	EXPECT_EQ(g_list_length(editor->ext_list), 2U);
}

TEST_F(PluginDiscovery, UserFilesTakePrecedenceAndHiddenFilesMaskFallbacks)
{
	const auto preferred = write_plugin("override.desktop");
	write_plugin("override.desktop", "", "fallback");
	write_plugin("hidden.desktop", "Hidden=true\n");
	write_plugin("hidden.desktop", "", "fallback");
	write_plugin("invalid.desktop", "Type=Link\n");
	const auto fallback = write_plugin("invalid.desktop", "", "fallback");
	install(editor_load_desktop_files({folder(), folder("fallback")}, nullptr));
	ASSERT_NE(get_editor_by_command("override.desktop"), nullptr);
	EXPECT_STREQ(get_editor_by_command("override.desktop")->file, preferred.c_str());
	EXPECT_EQ(get_editor_by_command("hidden.desktop"), nullptr);
	ASSERT_NE(get_editor_by_command("invalid.desktop"), nullptr);
	EXPECT_STREQ(get_editor_by_command("invalid.desktop")->file, fallback.c_str());
}

TEST_F(PluginDiscovery, ExecutableChecksAndDisabledPluginsArePreserved)
{
	const auto disabled = write_plugin("disabled.desktop", "TryExec=/bin/true\n");
	write_plugin("missing.desktop", "TryExec=/nonexistent/geeqie-plugin-test\n");
	options->disabled_plugins.push_back(disabled);
	install(editor_load_desktop_files({folder()}, nullptr));
	ASSERT_NE(get_editor_by_command("disabled.desktop"), nullptr);
	EXPECT_TRUE(get_editor_by_command("disabled.desktop")->disabled);
	EXPECT_EQ(get_editor_by_command("missing.desktop"), nullptr);
	EXPECT_TRUE(editor_list_get().empty());
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(desktop_file_list)), 2U);
}

TEST_F(PluginDiscovery, EmptyAndCancelledDiscovery)
{
	EXPECT_TRUE(editor_load_desktop_files({folder(), folder("missing")}, nullptr).empty());
	write_plugin("cancelled.desktop");
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	g_cancellable_cancel(cancellable);
	EXPECT_TRUE(editor_load_desktop_files({folder()}, cancellable).empty());
	install({});
	EXPECT_TRUE(editor_list_get().empty());
}

TEST_F(PluginDiscovery, ReloadFinishConsumesWorkerResultsOnlyOnce)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	write_plugin("geeqie-worker-reload-test.desktop");
	g_clear_object(&desktop_file_list);
	// Discovery snapshots this path, so restoring it does not affect a running worker.
	gchar *saved_appdir = gq_appdir;
	gq_appdir = directory;
	layout_editors_reload_start();
	EXPECT_NE(desktop_file_list, nullptr);
	layout_editors_reload_start();
	gq_appdir = saved_appdir;
	layout_editors_reload_finish();
	EXPECT_NE(get_editor_by_command("geeqie-worker-reload-test.desktop"), nullptr);
	const guint count = g_list_model_get_n_items(G_LIST_MODEL(desktop_file_list));
	// Pending task completion callbacks must not install a consumed or superseded result.
	while (g_main_context_iteration(nullptr, FALSE)) {}
	EXPECT_NE(get_editor_by_command("geeqie-worker-reload-test.desktop"), nullptr);
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(desktop_file_list)), count);
	layout_editors_reload_finish();
}

TEST_F(PluginDiscovery, AsyncReloadPublishesOnMainThread)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	write_plugin("geeqie-worker-async-test.desktop");
	const gulong handler = g_signal_connect(desktop_file_list, "items-changed",
		G_CALLBACK(+[](GListModel *, guint, guint, guint, gpointer main_thread)
			{
			EXPECT_EQ(g_thread_self(), main_thread);
			}), g_thread_self());
	gchar *saved_appdir = gq_appdir;
	gq_appdir = directory;
	layout_editors_reload_start();
	gq_appdir = saved_appdir;
	EXPECT_EQ(get_editor_by_command("geeqie-worker-async-test.desktop"), nullptr);
	bool found = false;
	const gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
	while (!found && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		const auto plugins = editor_list_get();
		found = std::any_of(plugins.begin(), plugins.end(), [](const EditorDescription *editor)
			{
			return g_str_equal(editor->key, "geeqie-worker-async-test.desktop");
			});
		if (!found) g_usleep(1000);
		}
	g_signal_handler_disconnect(desktop_file_list, handler);
	EXPECT_TRUE(found);
	layout_editors_reload_finish();
}

} // namespace
