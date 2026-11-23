/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "signal-handler.h"

namespace
{

class FatalSignal : public testing::TestWithParam<int>
{
protected:
	void SetUp() override
	{
		// Re-exec the child: integration tests may already have GTK worker threads.
		testing::FLAGS_gtest_death_test_style = "threadsafe";
	}
};

void prepare_crash()
{
	// These tests must neither generate core files nor invoke normal exit handlers.
	const struct rlimit limit{0, 0};
	if (setrlimit(RLIMIT_CORE, &limit) != 0) _exit(98);
	if (std::atexit([]() { _exit(99); }) != 0) _exit(98);
	setup_sig_handler();
}

TEST_P(FatalSignal, MainThreadRetainsSignalAndSkipsExitHandlers)
{
	const int signo = GetParam();
	EXPECT_EXIT({ prepare_crash(); raise(signo); }, testing::KilledBySignal(signo), "Geeqie fatal error\nSignal:");
}

TEST_P(FatalSignal, WorkerThreadRetainsSignalAndSkipsExitHandlers)
{
	const int signo = GetParam();
	EXPECT_EXIT(
		{
		prepare_crash();
		std::thread worker([signo]() { raise(signo); });
		worker.join();
		}, testing::KilledBySignal(signo), "Geeqie fatal error\nSignal:");
}

TEST_P(FatalSignal, DeveloperBacktraceRetainsSignal)
{
	const int signo = GetParam();
#if defined(DEBUG) && HAVE_EXECINFO_H
	const char *expected = "Best-effort developer backtrace";
#else
	const char *expected = "Crash report:";
#endif
	EXPECT_EXIT(
		{
		setenv("GQ_CRASH_BACKTRACE", "1", 1);
		prepare_crash();
		raise(signo);
		}, testing::KilledBySignal(signo), expected);
}

TEST_P(FatalSignal, UsesLatestPreparedSnapshot)
{
	const int signo = GetParam();
	EXPECT_EXIT(
		{
		prepare_crash();
		if (!signal_handler_set_snapshot("Old settings snapshot\n")) _exit(98);
		if (!signal_handler_set_snapshot("Latest settings: file_view=icons\n")) _exit(98);
		raise(signo);
		}, testing::KilledBySignal(signo), "Latest settings: file_view=icons");
}

INSTANTIATE_TEST_SUITE_P(Signals, FatalSignal, testing::Values(SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV));

TEST(CrashReport, SavesDiagnosticsInPrivateFile)
{
	setup_sig_handler();
	const char *path = signal_handler_report_path();
	ASSERT_NE(path, nullptr);
	struct stat status{};
	ASSERT_EQ(stat(path, &status), 0);
	EXPECT_EQ(status.st_mode & 0777, 0600);
	const pid_t child = fork();
	ASSERT_GE(child, 0);
	if (child == 0)
		{
		const struct rlimit limit{0, 0};
		if (setrlimit(RLIMIT_CORE, &limit) != 0) _exit(98);
		raise(SIGABRT);
		_exit(98);
		}
	int result;
	ASSERT_EQ(waitpid(child, &result, 0), child);
	ASSERT_TRUE(WIFSIGNALED(result));
	EXPECT_EQ(WTERMSIG(result), SIGABRT);
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
	EXPECT_NE(strstr(contents, "GTK="), nullptr);
	EXPECT_NE(strstr(contents, "Settings snapshot (UTC):"), nullptr);
	EXPECT_NE(strstr(contents, "Geeqie fatal error\nSignal: Abort"), nullptr);
}

TEST(CrashReport, DiscoveryIgnoresActiveIncompleteAcknowledgedAndLinkedFiles)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-report-discovery-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	const char *names[] = {"geeqie-crash-aaaaaa.txt", "geeqie-crash-bbbbbb.txt", "geeqie-crash-cccccc.txt",
	                       "geeqie-crash-dddddd.txt", "geeqie-crash-eeeeee.txt"};
	const char *crash = "Geeqie test\nGeeqie fatal error\nSignal: Abort\n";
	for (int i = 0; i < 4; ++i)
		{
		g_autofree gchar *path = g_build_filename(directory, names[i], nullptr);
		ASSERT_TRUE(g_file_set_contents(path, i == 1 ? "Geeqie reserved\n" : crash, -1, nullptr));
		}
	g_autofree gchar *active = g_build_filename(directory, names[2], nullptr);
	const int fd = open(active, O_RDWR);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(flock(fd, LOCK_EX | LOCK_NB), 0);
	g_autofree gchar *acknowledged_path = g_build_filename(directory, names[3], nullptr);
	ASSERT_TRUE(g_file_set_contents(acknowledged_path, "Geeqie test\nGeeqie fatal error\n\nCrash notification acknowledged\n", -1, nullptr));
	g_autofree gchar *pending = g_build_filename(directory, names[0], nullptr);
	g_autofree gchar *link = g_build_filename(directory, names[4], nullptr);
	ASSERT_EQ(symlink(pending, link), 0);
	const auto reports = crash_reports_pending(directory);
	ASSERT_EQ(reports.size(), 1);
	EXPECT_EQ(reports.front(), pending);
	close(fd);
	for (const auto *name : names)
		{
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		g_unlink(path);
		}
	g_rmdir(directory);
}

} // namespace
