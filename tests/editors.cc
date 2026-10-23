/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "editors.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout-util.h"
#include "layout.h"
#include "main.h"
#include "options.h"
#include "toolbar.h"
#include "ui-menu.h"

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

TEST_F(PluginDiscovery, ToolbarChoosersIncludePluginsAndTheirIcons)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	const auto icon_path = folder() + "/icon with spaces.png";
	g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
	gdk_pixbuf_fill(pixels, 0x123456ff);
	ASSERT_TRUE(gdk_pixbuf_save(pixels, icon_path.c_str(), "png", nullptr, nullptr));
	paths.push_back(icon_path);
	write_plugin("themed.desktop", "Icon=action-unavailable-symbolic\n");
	write_plugin("absolute.desktop", ("Icon=" + icon_path + "\n").c_str());
	write_plugin("missing.desktop");
	write_plugin("unknown.desktop", "Icon=geeqie-test-nonexistent-icon\n");
	write_plugin("hidden.desktop", "Hidden=true\n");
	options->disabled_plugins.push_back(write_plugin("disabled.desktop"));
	install(editor_load_desktop_files({folder()}, nullptr));
	for (const auto bar : {TOOLBAR_MAIN, TOOLBAR_STATUS})
		{
		SCOPED_TRACE(bar);
		g_autoptr(GMenuModel) menu = toolbar_add_menu_new(bar);
		g_autoptr(GMenuModel) plugins = nullptr;
		for (int i = 0; i < g_menu_model_get_n_items(menu); ++i)
			{
			g_autofree gchar *label = nullptr;
			g_menu_model_get_item_attribute(menu, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
			if (g_strcmp0(label, "Plugins") == 0) plugins = g_menu_model_get_item_link(menu, i, G_MENU_LINK_SUBMENU);
			}
		ASSERT_NE(plugins, nullptr);
		ASSERT_EQ(g_menu_model_get_n_items(plugins), 4);
		for (int i = 0; i < 4; ++i)
			{
			g_autofree gchar *action = nullptr;
			g_autofree gchar *target = nullptr;
			g_autofree gchar *label = nullptr;
			g_menu_model_get_item_attribute(plugins, i, G_MENU_ATTRIBUTE_ACTION, "s", &action);
			g_menu_model_get_item_attribute(plugins, i, G_MENU_ATTRIBUTE_TARGET, "s", &target);
			g_menu_model_get_item_attribute(plugins, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
			EXPECT_STREQ(action, bar == TOOLBAR_MAIN ? "win.preferences-win-main-toolbarlist-add" : "win.preferences-win-status-toolbarlist-add");
			EXPECT_STREQ(label, "Test plugin");
			ASSERT_NE(target, nullptr);
			g_autoptr(GVariant) serialized = g_menu_model_get_item_attribute_value(plugins, i, G_MENU_ATTRIBUTE_ICON, nullptr);
			ASSERT_NE(serialized, nullptr);
			g_autoptr(GIcon) icon = g_icon_deserialize(serialized);
			ASSERT_NE(icon, nullptr);
			if (g_str_equal(target, "absolute.desktop"))
				{
				ASSERT_TRUE(G_IS_FILE_ICON(icon));
				g_autofree gchar *path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)));
				EXPECT_STREQ(path, icon_path.c_str());
				}
			else
				{
				ASSERT_TRUE(G_IS_THEMED_ICON(icon));
				const gchar * const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
				EXPECT_TRUE(g_strv_contains(names, "gq-fallback-system-run-symbolic"));
				}
			}
		}
}

struct DefaultApplicationRestore
{
	GApplication *previous = g_application_get_default();
	DefaultApplicationRestore()
	{
		if (previous) g_object_ref(previous);
	}
	~DefaultApplicationRestore()
	{
		g_application_set_default(previous);
		g_clear_object(&previous);
	}
};

TEST_F(PluginDiscovery, AddingPluginsImmediatelyShowsTheirNamesAndIconsOnBothBars)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	DefaultApplicationRestore restore;
	g_autoptr(GtkApplication) app = gtk_application_new("org.geeqie.ToolbarPluginTest", G_APPLICATION_NON_UNIQUE);
	ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
	g_application_set_default(G_APPLICATION(app));
	accel_map_load_merged();
	write_plugin("toolbar.desktop", "Icon=action-unavailable-symbolic\n");
	write_plugin("no-icon.desktop");
	install(editor_load_desktop_files({folder()}, nullptr));
	std::unique_ptr<GtkWindow, decltype(&gtk_window_destroy)> window(GTK_WINDOW(gtk_application_window_new(app)), gtk_window_destroy);
	auto *pages = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(window.get(), pages);
	LayoutWindow layout{};
	for (const auto bar : {TOOLBAR_MAIN, TOOLBAR_STATUS})
		{
		SCOPED_TRACE(bar);
		auto *page = toolbar_select_new(&layout, GTK_WIDGET(window.get()), bar);
		gtk_box_append(GTK_BOX(pages), page);
		auto *popover = toolbar_add_popover_new(bar);
		auto *menu_button = gtk_menu_button_new();
		gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), popover);
		gtk_box_append(GTK_BOX(page), menu_button);
		int visible_icon_rows = 0;
		std::function<void(GtkWidget *)> inspect = [&](GtkWidget *widget)
			{
			if (GTK_IS_BUTTON(widget))
				{
				auto *box = gtk_button_get_child(GTK_BUTTON(widget));
				if (GTK_IS_BOX(box))
					{
					auto *image = gtk_widget_get_first_child(box);
					auto *label = image ? gtk_widget_get_next_sibling(image) : nullptr;
					if (GTK_IS_IMAGE(image) && GTK_IS_LABEL(label))
						{
						EXPECT_TRUE(gtk_widget_get_visible(image));
						EXPECT_TRUE(gtk_widget_get_visible(label));
						++visible_icon_rows;
						}
					}
				}
			for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) inspect(child);
			};
		inspect(popover);
		EXPECT_GT(visible_icon_rows, 2);
		for (const char *key : {"toolbar.desktop", "no-icon.desktop"})
			{
			std::function<GtkWidget *(GtkWidget *)> find_menu_row = [&](GtkWidget *widget) -> GtkWidget *
				{
				if (GTK_IS_BUTTON(widget))
					{
					GVariant *target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(widget));
					if (target && g_variant_is_of_type(target, G_VARIANT_TYPE_STRING) &&
					    g_str_equal(g_variant_get_string(target, nullptr), key)) return widget;
					}
				for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
					{
					if (auto *found = find_menu_row(child)) return found;
					}
				return nullptr;
				};
			auto *row = find_menu_row(popover);
			ASSERT_NE(row, nullptr);
			g_signal_emit_by_name(row, "clicked");
			std::function<GtkWidget *(GtkWidget *)> find = [&](GtkWidget *widget) -> GtkWidget *
				{
				if (g_strcmp0(static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "action_name")), key) == 0) return widget;
				for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
					{
					if (auto *found = find(child)) return found;
					}
				return nullptr;
				};
			auto *button = find(page);
			ASSERT_NE(button, nullptr);
			auto *box = gtk_button_get_child(GTK_BUTTON(button));
			auto *label = gtk_widget_get_last_child(box);
			ASSERT_TRUE(GTK_IS_LABEL(label));
			EXPECT_STREQ(gtk_label_get_text(GTK_LABEL(label)), "Test plugin");
			EXPECT_TRUE(GTK_IS_IMAGE(gtk_widget_get_first_child(box)));
			}
		}
}

GtkWidget *find_plugin_menu_row(GtkWidget *widget, const char *key)
{
	if (GTK_IS_BUTTON(widget))
		{
		GVariant *target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(widget));
		if (target && g_variant_is_of_type(target, G_VARIANT_TYPE_STRING) &&
		    g_str_equal(g_variant_get_string(target, nullptr), key)) return widget;
		}
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_plugin_menu_row(child, key)) return found;
		}
	return nullptr;
}

TEST_F(PluginDiscovery, MainPluginMenusShowIconsShortcutsAndReloadSafely)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	DefaultApplicationRestore restore;
	g_autoptr(GtkApplication) app = gtk_application_new("org.geeqie.PluginMenuIconTest", G_APPLICATION_NON_UNIQUE);
	ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
	g_application_set_default(G_APPLICATION(app));
	std::unique_ptr<GtkWindow, decltype(&gtk_window_destroy)> window(GTK_WINDOW(gtk_application_window_new(app)), gtk_window_destroy);
	auto *pages = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(window.get(), pages);
	std::string activated;
	g_autoptr(GSimpleAction) action = g_simple_action_new("main-win-plugin-run", G_VARIANT_TYPE_STRING);
	g_signal_connect(action, "activate", G_CALLBACK(+[](GSimpleAction *, GVariant *parameter, gpointer data)
		{
		*static_cast<std::string *>(data) = g_variant_get_string(parameter, nullptr);
		}), &activated);
	g_action_map_add_action(G_ACTION_MAP(window.get()), G_ACTION(action));
	write_plugin("menu-icons.desktop", "Icon=action-unavailable-symbolic\n");
	install(editor_load_desktop_files({folder()}, nullptr));
	const gchar *first[] = {"<Control>p", nullptr};
	gtk_application_set_accels_for_action(app, "win.main-win-plugin-run::menu-icons.desktop", first);
	for (bool menubar : {true, false})
		{
		SCOPED_TRACE(menubar);
		g_autoptr(GMenu) root = g_menu_new();
		g_autoptr(GMenu) plugins = g_menu_new();
		plugins_menu_populate(plugins, "win.main-win-plugin-run", nullptr, true);
		g_menu_append_submenu(root, "Plugins", G_MENU_MODEL(plugins));
		GtkWidget *menu;
		if (menubar)
			{
			menu = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(root));
			gtk_box_append(GTK_BOX(pages), menu);
			}
		else
			{
			menu = gtk_popover_menu_new_from_model(G_MENU_MODEL(root));
			auto *button = gtk_menu_button_new();
			gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), menu);
			gtk_box_append(GTK_BOX(pages), button);
			}
		menu_icon_rows_refresh(menu, G_MENU_MODEL(root));
		auto *row = find_plugin_menu_row(menu, "menu-icons.desktop");
		ASSERT_NE(row, nullptr);
		auto *box = gtk_button_get_child(GTK_BUTTON(row));
		auto *image = gtk_widget_get_first_child(box);
		auto *label = gtk_widget_get_next_sibling(image);
		auto *shortcut = gtk_widget_get_next_sibling(label);
		ASSERT_TRUE(GTK_IS_IMAGE(image));
		ASSERT_TRUE(GTK_IS_LABEL(label));
		EXPECT_TRUE(gtk_widget_get_visible(image));
		EXPECT_TRUE(gtk_widget_get_visible(label));
		EXPECT_STREQ(gtk_label_get_text(GTK_LABEL(label)), "Test plugin");
		ASSERT_TRUE(GTK_IS_SHORTCUT_LABEL(shortcut));
		EXPECT_STREQ(gtk_shortcut_label_get_accelerator(GTK_SHORTCUT_LABEL(shortcut)), "<Control>p");
		activated.clear();
		g_signal_emit_by_name(row, "clicked");
		EXPECT_EQ(activated, "menu-icons.desktop");
		// Reloading replaces custom slots as well as their widgets.
		g_menu_remove_all(plugins);
		plugins_menu_populate(plugins, "win.main-win-plugin-run", nullptr, true);
		menu_icon_rows_refresh(menu, G_MENU_MODEL(root));
		row = find_plugin_menu_row(menu, "menu-icons.desktop");
		ASSERT_NE(row, nullptr);
		activated.clear();
		g_signal_emit_by_name(row, "clicked");
		EXPECT_EQ(activated, "menu-icons.desktop");
		}
}

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

TEST_F(PluginDiscovery, MimeTypesUseSharedDatabaseExtensionsAndAliases)
{
	filter_add_defaults();
	filter_rebuild();
	write_plugin("jpeg.desktop", "MimeType=image/jpeg;\n");
	write_plugin("bitmap.desktop", "MimeType=image/x-bmp;\n");
	write_plugin("pdf.desktop", "MimeType=application/pdf;\n");
	install(editor_load_desktop_files({folder()}, nullptr));
	for (const auto &entry : {std::make_pair("jpeg.desktop", ".jpe"),
	                          std::make_pair("bitmap.desktop", ".bmp"),
	                          std::make_pair("pdf.desktop", ".pdf")})
		{
		const auto *editor = get_editor_by_command(entry.first);
		ASSERT_NE(editor, nullptr);
		EXPECT_FALSE(editor->hidden);
		EXPECT_NE(g_list_find_custom(editor->ext_list, entry.second,
		                            reinterpret_cast<GCompareFunc>(g_strcmp0)), nullptr);
		}
}

TEST_F(PluginDiscovery, MimeWildcardDoesNotAcceptUnrelatedFiles)
{
	filter_add_defaults();
	filter_rebuild();
	write_plugin("images.desktop", "MimeType=image/*;\n");
	install(editor_load_desktop_files({folder()}, nullptr));
	const auto *editor = get_editor_by_command("images.desktop");
	ASSERT_NE(editor, nullptr);
	EXPECT_FALSE(editor->hidden);
	for (const auto &entry : {std::make_pair("/nonexistent/test.jpg", true),
	                          std::make_pair("/nonexistent/test.pdf", false)})
		{
		auto *fd = file_data_new_simple(entry.first);
		GList *files = g_list_append(nullptr, fd);
		const auto flags = editor_command_parse(editor, files, FALSE, nullptr);
		EXPECT_EQ(editor_errors(flags) == 0, entry.second);
		g_list_free(files);
		file_data_unref(fd);
		}
}

TEST_F(PluginDiscovery, ExplicitExtensionsOverrideMimeTypes)
{
	write_plugin("override-mime.desktop", "MimeType=image/jpeg;\nX-Geeqie-File-Extensions=.png\n");
	write_plugin("unknown-mime.desktop", "MimeType=application/x-geeqie-nonexistent-type;\n");
	install(editor_load_desktop_files({folder()}, nullptr));
	const auto *editor = get_editor_by_command("override-mime.desktop");
	ASSERT_NE(editor, nullptr);
	ASSERT_EQ(g_list_length(editor->ext_list), 1U);
	EXPECT_STREQ(static_cast<const gchar *>(editor->ext_list->data), ".png");
	EXPECT_EQ(get_editor_by_command("unknown-mime.desktop"), nullptr);
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
