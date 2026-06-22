/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "filedata.h"
#include "filedata/ref.h"

namespace {

// For convenience.
namespace t = ::testing;


class FileDataRefTest : public t::Test
{
    protected:
	void TearDown() override
	{
		g_clear_pointer(&fd, g_free);
		g_clear_pointer(&fd2, g_free);
	}

	FileDataContext context;
	FileData *fd = nullptr;
	FileData *fd2 = nullptr;
};

TEST_F(FileDataRefTest, NonOwningRefCount)
{
	fd = g_new0(FileData, 1);
	fd->magick = FD_MAGICK;
	// Avoids having the FileData object automatically freed when its
	// refcount drops back to zero.
	file_data_lock(fd);

	// Refcount is 0 outside of the FileDataRef scope.
	ASSERT_EQ(0, fd->ref);

	{
		// Refcount is 0 inside of the FileDataRef scope, but before it's defined.
		ASSERT_EQ(0, fd->ref);

		// Refcount should increase by 1 after the FileDataRef is created.
		FileDataRef fd_ref(fd);
		ASSERT_EQ(1, fd->ref);

		// Refcount should increase by 1 more with the second FileDataRef.
		FileDataRef fd_ref2(fd);
		ASSERT_EQ(2, fd->ref);
	}

	// And refcount drops back down to 0 after both of the FileDataRefs go out of scope.
	ASSERT_EQ(0, fd->ref);
}

TEST_F(FileDataRefTest, Reset)
{
	fd = g_new0(FileData, 1);
	fd->magick = FD_MAGICK;
	fd2 = g_new0(FileData, 1);
	fd2->magick = FD_MAGICK;

	// Avoids having the FileData objects automatically freed when its
	// refcount drops back to zero.
	file_data_lock(fd);
	file_data_lock(fd2);

	// Start off with no refs.
	ASSERT_EQ(0, fd->ref);
	ASSERT_EQ(0, fd2->ref);

	// This should ref fd.
	FileDataRef fd_ref(fd);

	ASSERT_EQ(1, fd->ref);
	ASSERT_EQ(0, fd2->ref);
	ASSERT_EQ(fd, static_cast<FileData *>(fd_ref));

	// This should ref fd2 and unref fd.
	fd_ref.reset(fd2);

	ASSERT_EQ(0, fd->ref);
	ASSERT_EQ(1, fd2->ref);
	ASSERT_EQ(fd2, static_cast<FileData *>(fd_ref));
}

TEST_F(FileDataRefTest, ResetToNull)
{
	fd = g_new0(FileData, 1);
	fd->magick = FD_MAGICK;

	// Avoids having the FileData objects automatically freed when its
	// refcount drops back to zero.
	file_data_lock(fd);

	FileDataRef fd_ref(fd);
	ASSERT_EQ(1, fd->ref);

	// Reset to nullptr shouldn't crash, and should unref the previously held fd.
	fd_ref.reset(nullptr);
	ASSERT_EQ(0, fd->ref);
	ASSERT_EQ(nullptr, static_cast<FileData *>(fd_ref));

	// And we should be able to re-ref the original fd without crashing.
	fd_ref.reset(fd);
	ASSERT_EQ(1, fd->ref);
	ASSERT_EQ(fd, static_cast<FileData *>(fd_ref));
}

TEST_F(FileDataRefTest, Release)
{
#ifdef DEBUG_FILEDATA
	ASSERT_EQ(0, context.global_file_data_count);
	FileData *fd_ptr = nullptr;

	{
		auto fd_ref = FileData::new_simple("/does/not/exist.jpg", &context);
		ASSERT_EQ(1, context.global_file_data_count);
		ASSERT_EQ(1, fd_ref->ref);

		fd_ptr = fd_ref.release();
		ASSERT_EQ(1, context.global_file_data_count);
		ASSERT_EQ(nullptr, *fd_ref);
		ASSERT_NE(nullptr, fd_ptr);
		ASSERT_EQ(1, fd_ptr->ref);
	}

	// fd_ref should have been empty, so it going out of scope should be a no-op.
	ASSERT_EQ(1, context.global_file_data_count);
	ASSERT_NE(nullptr, fd_ptr);
	ASSERT_EQ(1, fd_ptr->ref);

	fd_ptr->file_data_unref();
	ASSERT_EQ(0, context.global_file_data_count);
#else
	GTEST_SKIP() << "Test requires DEBUG_FILEDATA";
#endif
}

TEST_F(FileDataRefTest, AnonymousReturn)
{
	const auto &make_ref = []() -> FileDataRef {
		auto *fd = g_new0(FileData, 1);
		fd->magick = FD_MAGICK;
		// Avoids having the FileData objects automatically freed when its
		// refcount drops back to zero.
		file_data_lock(fd);

		return FileDataRef{fd};
	};

	// This ensures that the post-move anonymous FileDataRef can be destructed without crashing.
	FileDataRef fd_ref = make_ref();
	ASSERT_EQ(1, fd_ref->ref);

	// We need to manually free the FileData* since it doesn't have a context.
	g_free(fd_ref.release());
}

/**
 * Validates a common compatibility pattern for interacting with code that stores FileData*.
 */
TEST_F(FileDataRefTest, AnonymousReturnAndRelease)
{
	const auto &make_ref = []() -> FileDataRef {
		auto *_fd = g_new0(FileData, 1);
		_fd->magick = FD_MAGICK;
		// Avoids having the FileData objects automatically freed when its
		// refcount drops back to zero.
		file_data_lock(_fd);

		return FileDataRef{_fd};
	};

	// This ensures that the post-move anonymous FileDataRef can be destructed without crashing.
	fd = make_ref().release();
	ASSERT_NE(nullptr, fd);
	ASSERT_EQ(1, fd->ref);
}


/**
 * Ensures that equality comparisons are consistent and correct against either FileData* or FileDataRef.
 */
TEST_F(FileDataRefTest, FileDataEquality)
{
	fd = g_new0(FileData, 1);
	fd->magick = FD_MAGICK;

	// Avoids having the FileData objects automatically freed when its
	// refcount drops back to zero.
	file_data_lock(fd);
	FileData *null_fd = nullptr;

	FileDataRef fd_ref{fd};
	FileDataRef fd_ref2{fd};
	FileDataRef fd_null_ref{nullptr};

	// Using ASSERT_TRUE instead of ASSERT_EQ to ensure the macro isn't affecting implicit behavior.
	// We evaluate == and !=
	ASSERT_TRUE(fd_ref == fd);
	ASSERT_TRUE(fd_ref2 == fd);
	ASSERT_TRUE(fd_ref == fd_ref2);
	ASSERT_TRUE(*fd_ref == fd);
	ASSERT_TRUE(*fd_ref2 == fd);
	ASSERT_TRUE(*fd_ref == *fd_ref2);
	ASSERT_TRUE(fd_ref == fd_ref);
	ASSERT_TRUE(fd_ref2 == fd_ref2);

	ASSERT_TRUE(fd_null_ref == null_fd);
	ASSERT_TRUE(fd_null_ref == fd_null_ref);

	ASSERT_TRUE(fd_ref != null_fd);
	ASSERT_TRUE(fd_null_ref != fd);
	ASSERT_TRUE(fd_ref != fd_null_ref);
	ASSERT_TRUE(fd_null_ref != fd_ref2);

	ASSERT_FALSE(fd_ref == null_fd);
	ASSERT_FALSE(fd_null_ref == fd);
	ASSERT_FALSE(fd_ref == fd_null_ref);

	FileDataRef fd_ref3{nullptr};
	fd_ref3.reset(fd_ref.release());
	ASSERT_TRUE(fd_ref3 == fd);
	ASSERT_TRUE(fd_ref == null_fd);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
