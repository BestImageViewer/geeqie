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

#endif
} // namespace
