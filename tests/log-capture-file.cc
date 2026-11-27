/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

#include <glib/gstdio.h>

#include "log-capture-file.h"

namespace
{
class FaultIo
{
public:
	FaultIo()
	{
		fail_create = false;
		fail_write = false;
		fail_close = false;
		interrupts = 0;
		chunk_size = 0;
		closes = 0;
	}

	static LogCaptureFile::Io operations() { return {create, write, close}; }
	inline static bool fail_create;
	inline static bool fail_write;
	inline static bool fail_close;
	inline static int interrupts;
	inline static std::size_t chunk_size;
	inline static int closes;

private:
	static int create(gchar *path, int flags, int mode)
	{
		if (!fail_create) return g_mkstemp_full(path, flags, mode);
		errno = EACCES;
		return -1;
	}

	static ssize_t write(int fd, const void *text, std::size_t count)
	{
		if (interrupts > 0)
			{
			--interrupts;
			errno = EINTR;
			return -1;
			}
		if (fail_write)
			{
			errno = ENOSPC;
			return -1;
			}
		return ::write(fd, text, chunk_size ? std::min(chunk_size, count) : count);
	}

	static int close(int fd)
	{
		++closes;
		const int result = ::close(fd);
		if (!fail_close) return result;
		errno = EIO;
		return -1;
	}
};

class CaptureFile : public testing::Test
{
protected:
	void SetUp() override
	{
		directory = g_dir_make_tmp("geeqie-capture-file-test-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
	}

	void TearDown() override
	{
		if (!directory) return;
		g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
		if (dir)
			{
			while (const auto *name = g_dir_read_name(dir))
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				g_unlink(path);
				}
			}
		g_rmdir(directory);
		g_free(directory);
	}

	static std::string read(const std::string &path)
	{
		g_autofree gchar *text = nullptr;
		EXPECT_TRUE(g_file_get_contents(path.c_str(), &text, nullptr, nullptr));
		return text ? text : "";
	}

	void expect_empty_directory()
	{
		g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
		ASSERT_NE(dir, nullptr);
		EXPECT_EQ(g_dir_read_name(dir), nullptr);
	}

	gchar *directory = nullptr;
};

TEST_F(CaptureFile, SizeLimitIncludesTheHeaderAndAnExplicitStopNotice)
{
	LogCaptureFile file;
	ASSERT_TRUE(file.start(directory, "header\n", 128, nullptr));
	const std::string first = std::string(75, 'a') + "\n";
	EXPECT_TRUE(file.append(first.c_str()));
	EXPECT_FALSE(file.append("record refused\n"));
	EXPECT_FALSE(file.active());
	EXPECT_TRUE(file.limit_reached());
	EXPECT_EQ(file.error_code(), 0);
	EXPECT_TRUE(file.stop(nullptr));
	const auto contents = read(file.path());
	EXPECT_LE(contents.size(), 128U);
	EXPECT_NE(contents.find(first), std::string::npos);
	EXPECT_EQ(contents.find("record refused"), std::string::npos);
	EXPECT_NE(contents.find("# Capture stopped: size limit reached\n"), std::string::npos);
	const auto previous_path = file.path();
	ASSERT_TRUE(file.start(directory, "new header\n", 128, nullptr));
	EXPECT_NE(file.path(), previous_path);
	EXPECT_FALSE(file.limit_reached());
	EXPECT_TRUE(file.stop(nullptr));
	EXPECT_EQ(read(previous_path), contents);
}

TEST_F(CaptureFile, OversizedHeaderDoesNotCreateAFile)
{
	LogCaptureFile file;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file.start(directory, "header", 8, &error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->code, G_FILE_ERROR_NOSPC);
	EXPECT_FALSE(file.active());
	expect_empty_directory();
}

TEST_F(CaptureFile, CreateFailureLeavesNoFileOrActiveCapture)
{
	FaultIo faults;
	LogCaptureFile file(FaultIo::operations());
	FaultIo::fail_create = true;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file.start(directory, "header\n", 1024, &error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->code, G_FILE_ERROR_ACCES);
	EXPECT_FALSE(file.active());
	EXPECT_EQ(FaultIo::closes, 0);
	expect_empty_directory();
}

TEST_F(CaptureFile, HeaderWriteFailureClosesAndRemovesTheIncompleteFile)
{
	FaultIo faults;
	LogCaptureFile file(FaultIo::operations());
	FaultIo::fail_write = true;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file.start(directory, "header\n", 1024, &error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->code, G_FILE_ERROR_NOSPC);
	EXPECT_FALSE(file.active());
	EXPECT_EQ(FaultIo::closes, 1);
	expect_empty_directory();
}

TEST_F(CaptureFile, RecordWriteFailureStopsAndRemainsReportable)
{
	FaultIo faults;
	LogCaptureFile file(FaultIo::operations());
	ASSERT_TRUE(file.start(directory, "header\n", 1024, nullptr));
	FaultIo::fail_write = true;
	EXPECT_FALSE(file.append("record\n"));
	EXPECT_FALSE(file.active());
	EXPECT_EQ(file.error_code(), ENOSPC);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file.stop(&error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->code, G_FILE_ERROR_NOSPC);
	EXPECT_EQ(FaultIo::closes, 1);
	EXPECT_EQ(read(file.path()), "header\n");
}

TEST_F(CaptureFile, StopWriteFailurePreservesTheFirstErrorWhenCloseAlsoFails)
{
	FaultIo faults;
	LogCaptureFile file(FaultIo::operations());
	ASSERT_TRUE(file.start(directory, "header\n", 1024, nullptr));
	FaultIo::fail_write = true;
	FaultIo::fail_close = true;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file.stop(&error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->code, G_FILE_ERROR_NOSPC);
	EXPECT_FALSE(file.active());
	EXPECT_EQ(FaultIo::closes, 1);
}

TEST_F(CaptureFile, CloseFailureIsReportedWithoutRetryingTheDescriptor)
{
	FaultIo faults;
	LogCaptureFile file(FaultIo::operations());
	ASSERT_TRUE(file.start(directory, "header\n", 1024, nullptr));
	FaultIo::fail_close = true;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file.stop(&error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->code, G_FILE_ERROR_IO);
	EXPECT_FALSE(file.active());
	g_clear_error(&error);
	EXPECT_FALSE(file.stop(&error));
	EXPECT_EQ(FaultIo::closes, 1);
}

TEST_F(CaptureFile, ShortAndInterruptedWritesPreserveCompleteRecords)
{
	FaultIo faults;
	LogCaptureFile file(FaultIo::operations());
	FaultIo::interrupts = 1;
	FaultIo::chunk_size = 2;
	ASSERT_TRUE(file.start(directory, "header\n", 1024, nullptr));
	EXPECT_TRUE(file.append("record\n"));
	EXPECT_TRUE(file.stop(nullptr));
	EXPECT_EQ(read(file.path()), "header\nrecord\n\n# Capture stopped\n");
}

TEST_F(CaptureFile, FailedRestartPreservesTheLastSavedReport)
{
	FaultIo faults;
	LogCaptureFile file(FaultIo::operations());
	ASSERT_TRUE(file.start(directory, "header\n", 1024, nullptr));
	ASSERT_TRUE(file.stop(nullptr));
	const auto path = file.path();
	const auto contents = read(path);
	FaultIo::fail_write = true;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file.start(directory, "replacement\n", 1024, &error));
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(file.path(), path);
	EXPECT_EQ(file.error_code(), 0);
	EXPECT_EQ(read(path), contents);
}
} // namespace
