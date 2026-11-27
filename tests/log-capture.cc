/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <sys/stat.h>
#include <unistd.h>

#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "debug.h"
#include "intl.h"
#include "layout.h"
#include "log-capture.h"
#include "log.h"
#include "logwindow.h"
#include "options.h"

namespace
{
class LogCapture : public testing::Test
{
protected:
	void SetUp() override
	{
		ASSERT_FALSE(log_capture_active());
#ifdef DEBUG
		level = get_debug_level();
		domains = get_debug_domains();
		regexp = get_regexp();
		source = get_debug_source();
		timing = get_debug_timing();
		set_debug_level(3);
		set_debug_domains("all");
		set_regexp(nullptr);
#endif
	}

	void TearDown() override
	{
		log_capture_stop(nullptr);
		for (const auto &path : paths) g_unlink(path.c_str());
#ifdef DEBUG
		set_debug_level(level);
		set_debug_domains(domains);
		set_regexp(regexp);
		set_debug_source(source);
		set_debug_timing(timing);
		g_free(domains);
		g_free(regexp);
#endif
	}

	std::string start(const char *diagnostics = "Geeqie test diagnostics\ndistribution=test\n")
	{
		g_autoptr(GError) error = nullptr;
		const bool started = log_capture_start(diagnostics, &error);
		EXPECT_TRUE(started) << (error ? error->message : "");
		if (!started) return {};
		g_autofree gchar *path = log_capture_get_path();
		paths.emplace_back(path);
		return path;
	}

	static std::string read(const std::string &path)
	{
		g_autofree gchar *text = nullptr;
		if (!g_file_get_contents(path.c_str(), &text, nullptr, nullptr))
			{
			ADD_FAILURE() << "Cannot read capture";
			return {};
			}
		return text;
	}

	std::vector<std::string> paths;
#ifdef DEBUG
	gint level = 0;
	gchar *domains = nullptr;
	gchar *regexp = nullptr;
	bool source = false;
	bool timing = false;
#endif
};

TEST_F(LogCapture, UniquePrivateFilesRetainHeadersAndStopAtTheRequestedPoint)
{
	const auto first = start();
	ASSERT_FALSE(first.empty());
	struct stat info{};
	ASSERT_EQ(stat(first.c_str(), &info), 0);
	EXPECT_EQ(info.st_mode & 077, 0U);
	log_capture_write("first marker\n");
	ASSERT_TRUE(log_capture_stop(nullptr));
	const auto before = read(first);
	EXPECT_NE(before.find("Geeqie debug capture\nstarted_utc="), std::string::npos);
	EXPECT_NE(before.find("distribution=test\n"), std::string::npos);
	EXPECT_NE(before.find("size_limit_bytes=" + std::to_string(log_capture_max_bytes) + "\n"), std::string::npos);
	EXPECT_NE(before.find("first marker\n"), std::string::npos);
	EXPECT_NE(before.find("# Capture stopped"), std::string::npos);
	log_capture_write("after stop\n");
	EXPECT_EQ(read(first), before);
	const auto second = start();
	ASSERT_FALSE(second.empty());
	EXPECT_NE(first, second);
	EXPECT_EQ(read(first), before);
	g_autofree gchar *error = log_capture_get_error();
	EXPECT_EQ(error, nullptr);
}

TEST_F(LogCapture, DuplicateStartLeavesTheCurrentCaptureIntact)
{
	const auto path = start();
	ASSERT_FALSE(path.empty());
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(log_capture_start("replacement", &error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->domain, G_FILE_ERROR);
	EXPECT_EQ(error->code, G_FILE_ERROR_EXIST);
	EXPECT_TRUE(log_capture_active());
	g_autofree gchar *current = log_capture_get_path();
	EXPECT_EQ(std::string(current), path);
	EXPECT_EQ(read(path).find("replacement"), std::string::npos);
}

TEST_F(LogCapture, ConcurrentRecordsAreCompleteAndWrittenOnce)
{
	const auto path = start();
	ASSERT_FALSE(path.empty());
	std::vector<std::thread> workers;
	for (int i = 0; i < 4; ++i)
		{
		workers.emplace_back([i]()
			{
			for (int j = 0; j < 200; ++j)
				{
				const auto text = "record-" + std::to_string(i) + "-" + std::to_string(j) + "\n";
				log_capture_write(text.c_str());
				}
			});
		}
	for (auto &worker : workers) worker.join();
	ASSERT_TRUE(log_capture_stop(nullptr));
	std::istringstream input(read(path));
	std::string line;
	std::set<std::string> records;
	int count = 0;
	while (std::getline(input, line))
		{
		if (line.find("record-") != 0) continue;
		++count;
		records.insert(line);
		}
	EXPECT_EQ(count, 800);
	EXPECT_EQ(records.size(), 800U);
}

TEST_F(LogCapture, CapturingPreservesTheCommandLineLogDestination)
{
	CommandLine settings{};
	struct RestoreCommandLine
	{
		CommandLine *previous = command_line;
		~RestoreCommandLine() { g_free(command_line->log_file); command_line = previous; }
	} restore;
	command_line = &settings;
	g_autofree gchar *existing = nullptr;
	const int fd = g_file_open_tmp("geeqie-existing-log-XXXXXX.txt", &existing, nullptr);
	ASSERT_GE(fd, 0);
	close(fd);
	paths.emplace_back(existing);
	log_set_file(existing);
	const auto capture = start();
	ASSERT_FALSE(capture.empty());
	log_info("explicit capture marker");
	log_printf("legacy capture marker");
	EXPECT_STREQ(command_line->log_file, existing);
	ASSERT_TRUE(log_capture_stop(nullptr));
	log_info("after capture marker");
	EXPECT_STREQ(command_line->log_file, existing);
	log_set_file(nullptr);
	const auto text = read(capture);
	EXPECT_NE(text.find("explicit capture marker"), std::string::npos);
	EXPECT_NE(text.find("legacy capture marker"), std::string::npos);
	EXPECT_EQ(text.find("after capture marker"), std::string::npos);
	EXPECT_NE(read(existing).find("after capture marker"), std::string::npos);
}

#ifdef DEBUG
TEST_F(LogCapture, RapidSettingsChangesAreRecordedWithoutGuiPolling)
{
	set_debug_source(false);
	set_debug_timing(true);
	const auto path = start();
	ASSERT_FALSE(path.empty());
	const auto offset = read(path).size();
	set_debug_domains("none");
	set_debug_domains("image");
	set_debug_level(1);
	set_debug_level(4);
	set_debug_source(true);
	set_debug_source(false);
	set_debug_timing(false);
	set_debug_timing(true);
	set_regexp("first\nsecond");
	set_regexp(nullptr);
	set_debug_level(4);
	set_debug_domains("image");
	set_regexp(nullptr);
	EXPECT_FALSE(set_debug_domains("invalid"));
	const auto text = read(path).substr(offset);
	std::size_t count = 0;
	std::size_t position = 0;
	while ((position = text.find("# Logging settings changed\n", position)) != std::string::npos)
		{
		++count;
		++position;
		}
	EXPECT_EQ(count, 10U);
	EXPECT_LT(text.find("debug_domains=none\n"), text.find("debug_domains=image\n"));
	EXPECT_LT(text.find("debug_level=1\n"), text.find("debug_level=4\n"));
	EXPECT_LT(text.find("debug_source=1\n"), text.find("debug_source=0\n"));
	EXPECT_LT(text.find("debug_timing_delta=0\n"), text.find("debug_timing_delta=1\n"));
	EXPECT_NE(text.find("debug_regexp=first\\nsecond\n"), std::string::npos);
	EXPECT_NE(text.find("changed_utc="), std::string::npos);
}

TEST_F(LogCapture, HeaderRecordsTheSelectedLoggingSettings)
{
	set_debug_level(4);
	set_debug_domains("files,image");
	set_debug_source(true);
	set_debug_timing(false);
	set_regexp("capture-filter");
	const auto path = start();
	ASSERT_FALSE(path.empty());
	const auto text = read(path);
	EXPECT_NE(text.find("debug_level=4\n"), std::string::npos);
	EXPECT_NE(text.find("debug_domains=image,files\n"), std::string::npos);
	EXPECT_NE(text.find("debug_source=1\n"), std::string::npos);
	EXPECT_NE(text.find("debug_timing_delta=0\n"), std::string::npos);
	EXPECT_NE(text.find("debug_regexp=capture-filter\n"), std::string::npos);
	EXPECT_STREQ(get_debug_category_name(DebugCategory::IMAGE), "image");
	EXPECT_EQ(get_debug_category_name(DebugCategory::COUNT), nullptr);
}

GtkWidget *find_widget(GtkWidget *root, const std::function<bool(GtkWidget *)> &match)
{
	if (match(root)) return root;
	for (auto *child = gtk_widget_get_first_child(root); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_widget(child, match)) return found;
		}
	return nullptr;
}

TEST_F(LogCapture, WindowControlsSelectSubsystemsAndCaptureWithoutCommandLineOptions)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	accel_map_load_merged();
	static GtkApplication *app = gtk_application_new("org.geeqie.LogCaptureTest", G_APPLICATION_NON_UNIQUE);
	ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
	struct RestoreApplication
	{
		GApplication *previous = g_application_get_default();
		~RestoreApplication() { g_application_set_default(previous); }
	} restore;
	g_application_set_default(G_APPLICATION(app));
	LayoutWindow layout{};
	layout.options.log_window = {0, 0, 700, 600};
	set_debug_level(0);
	log_window_new(&layout);
	g_autoptr(GtkWindow) log_window = nullptr;
	auto *windows = gtk_window_get_toplevels();
	for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
		{
		g_autoptr(GtkWindow) candidate = GTK_WINDOW(g_list_model_get_item(windows, i));
		if (G_IS_ACTION_MAP(candidate) && g_action_map_lookup_action(G_ACTION_MAP(candidate), "log-win-run-command"))
			{
			log_window = g_steal_pointer(&candidate);
			break;
			}
		}
	auto *window = log_window ? GTK_WIDGET(log_window) : nullptr;
	ASSERT_NE(window, nullptr);
	struct HideWindow
	{
		GtkWidget *window;
		~HideWindow() { gtk_widget_set_visible(window, false); }
	} hide{window};
	const auto button = [window](const char *label)
		{
		return find_widget(window, [label](GtkWidget *widget)
			{
			return GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0;
			});
		};
	auto *none = button(_("None"));
	ASSERT_NE(none, nullptr);
	g_signal_emit_by_name(none, "clicked");
	g_autofree gchar *selected = get_debug_domains();
	EXPECT_STREQ(selected, "none");
	auto *images = find_widget(window, [](GtkWidget *widget)
		{
		return GTK_IS_CHECK_BUTTON(widget) && g_strcmp0(gtk_widget_get_tooltip_text(widget), "image") == 0;
		});
	ASSERT_NE(images, nullptr);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(images), true);
	g_clear_pointer(&selected, g_free);
	selected = get_debug_domains();
	EXPECT_STREQ(selected, "image");
	auto *all = button(_("All"));
	ASSERT_NE(all, nullptr);
	g_signal_emit_by_name(all, "clicked");
	auto *capture = button(_("Start capture"));
	ASSERT_NE(capture, nullptr);
	g_signal_emit_by_name(capture, "clicked");
	ASSERT_TRUE(log_capture_active());
	EXPECT_EQ(get_debug_level(), 2);
	g_autofree gchar *path = log_capture_get_path();
	paths.emplace_back(path);
	log_info("window capture marker");
	g_signal_emit_by_name(none, "clicked");
	EXPECT_STREQ(gtk_button_get_label(GTK_BUTTON(capture)), _("Stop capture"));
	g_signal_emit_by_name(capture, "clicked");
	EXPECT_FALSE(log_capture_active());
	const auto text = read(path);
	EXPECT_NE(text.find("Diagnostic settings report"), std::string::npos);
	EXPECT_NE(text.find("window capture marker"), std::string::npos);
	EXPECT_NE(text.find("# Logging settings changed\n"), std::string::npos);
	EXPECT_NE(text.find("debug_domains=none\n"), std::string::npos);
	gtk_widget_set_visible(window, false);
}
#endif
} // namespace
