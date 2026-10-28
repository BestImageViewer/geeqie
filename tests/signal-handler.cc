/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <sys/resource.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <thread>

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

INSTANTIATE_TEST_SUITE_P(Signals, FatalSignal, testing::Values(SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV));

} // namespace
