/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <glib/gstdio.h>

#include "filedata.h"
#include "options.h"
#include "ui-fileops.h"

namespace
{

class SymlinkTransfer : public testing::TestWithParam<bool>
{
protected:
	void SetUp() override
	{
		g_autofree gchar *directory = g_dir_make_tmp("geeqie-symlink-transfer-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		root = directory;
		ASSERT_EQ(g_mkdir((root / "src").c_str(), 0700), 0);
		ASSERT_EQ(g_mkdir((root / "dest").c_str(), 0700), 0);
		ASSERT_TRUE(g_file_set_contents((root / "image.jpg").c_str(), "original", -1, nullptr));
	}

	void TearDown() override
	{
		std::error_code error;
		if (!extra.empty()) std::filesystem::remove_all(extra, error);
		if (!root.empty()) std::filesystem::remove_all(root, error);
	}

	static void transfer(const std::filesystem::path &source, const std::filesystem::path &destination)
	{
		ASSERT_TRUE(GetParam() ? move_file(source.c_str(), destination.c_str()) : copy_file(source.c_str(), destination.c_str()));
		struct stat st;
		EXPECT_EQ(lstat(source.c_str(), &st) == 0, !GetParam());
		ASSERT_EQ(lstat(destination.c_str(), &st), 0);
		EXPECT_TRUE(S_ISLNK(st.st_mode));
	}

	static std::string target(const std::filesystem::path &path)
	{
		g_autofree gchar *text = g_file_read_link(path.c_str(), nullptr);
		return text ? text : "";
	}

	static void expect_contents(const std::filesystem::path &path, const gchar *expected)
	{
		g_autofree gchar *contents = nullptr;
		ASSERT_TRUE(g_file_get_contents(path.c_str(), &contents, nullptr, nullptr));
		EXPECT_STREQ(contents, expected);
	}

	std::filesystem::path root;
	std::filesystem::path extra;
};

TEST_P(SymlinkTransfer, RelativeLinkIntoChildFolderKeepsItsTarget)
{
	ASSERT_EQ(symlink("../image.jpg", (root / "src/link.jpg").c_str()), 0);
	ASSERT_EQ(g_mkdir((root / "src/child").c_str(), 0700), 0);
	transfer(root / "src/link.jpg", root / "src/child/link.jpg");
	EXPECT_EQ(target(root / "src/child/link.jpg"), "../../image.jpg");
	expect_contents(root / "src/child/link.jpg", "original");
}

TEST_P(SymlinkTransfer, RelativeLinkIntoSiblingFolderStaysRelative)
{
	ASSERT_EQ(symlink("../image.jpg", (root / "src/link.jpg").c_str()), 0);
	transfer(root / "src/link.jpg", root / "dest/link.jpg");
	const auto text = target(root / "dest/link.jpg");
	EXPECT_FALSE(text.empty());
	EXPECT_FALSE(g_path_is_absolute(text.c_str()));
	EXPECT_EQ(text, "../image.jpg");
	if (!GetParam())
		{
		ASSERT_EQ(g_remove((root / "src/link.jpg").c_str()), 0);
		}
	ASSERT_EQ(g_rmdir((root / "src").c_str()), 0);
	expect_contents(root / "dest/link.jpg", "original");
}

TEST_P(SymlinkTransfer, AbsoluteLinkTextIsPreserved)
{
	const auto absolute = root / "image.jpg";
	ASSERT_EQ(symlink(absolute.c_str(), (root / "src/link.jpg").c_str()), 0);
	transfer(root / "src/link.jpg", root / "dest/link.jpg");
	EXPECT_EQ(target(root / "dest/link.jpg"), absolute.native());
	expect_contents(root / "dest/link.jpg", "original");
}

TEST_P(SymlinkTransfer, DanglingRelativeLinkStillPointsToItsOriginalLocation)
{
	ASSERT_EQ(symlink("../missing.jpg", (root / "src/link.jpg").c_str()), 0);
	transfer(root / "src/link.jpg", root / "dest/link.jpg");
	EXPECT_FALSE(g_path_is_absolute(target(root / "dest/link.jpg").c_str()));
	EXPECT_FALSE(g_file_test((root / "dest/link.jpg").c_str(), G_FILE_TEST_EXISTS));
	ASSERT_TRUE(g_file_set_contents((root / "missing.jpg").c_str(), "appeared later", -1, nullptr));
	expect_contents(root / "dest/link.jpg", "appeared later");
}

TEST_P(SymlinkTransfer, OperationValidationAcceptsDanglingLinks)
{
	if (!options) options = conf_options_new();
	ASSERT_EQ(symlink("../missing.jpg", (root / "src/link.jpg").c_str()), 0);
	auto fd = FileData::new_simple((root / "src/link.jpg").c_str());
	for (FileDataChangeType type : {GetParam() ? FILEDATA_CHANGE_MOVE : FILEDATA_CHANGE_COPY, FILEDATA_CHANGE_RENAME})
		{
		ASSERT_TRUE(file_data_add_ci(fd, type, fd->path, (root / "dest/link.jpg").c_str()));
		EXPECT_EQ(file_data_verify_ci(fd, nullptr) & CHANGE_ERROR_MASK, 0);
		file_data_free_ci(fd);
		}
}

TEST_P(SymlinkTransfer, SameDirectoryRenamePreservesLinkText)
{
	ASSERT_EQ(symlink("./../image.jpg", (root / "src/link.jpg").c_str()), 0);
	transfer(root / "src/link.jpg", root / "src/renamed.jpg");
	EXPECT_EQ(target(root / "src/renamed.jpg"), "./../image.jpg");
}

TEST_P(SymlinkTransfer, SymlinkedParentDirectoriesAreResolvedCorrectly)
{
	ASSERT_EQ(symlink("src", (root / "source-alias").c_str()), 0);
	ASSERT_EQ(symlink("dest", (root / "destination-alias").c_str()), 0);
	ASSERT_EQ(symlink("../image.jpg", (root / "src/link.jpg").c_str()), 0);
	transfer(root / "source-alias/link.jpg", root / "destination-alias/link.jpg");
	expect_contents(root / "dest/link.jpg", "original");
}

TEST_P(SymlinkTransfer, SameEntryThroughParentAliasIsANoop)
{
	ASSERT_EQ(symlink("src", (root / "alias").c_str()), 0);
	ASSERT_EQ(symlink("../image.jpg", (root / "src/link.jpg").c_str()), 0);
	EXPECT_TRUE(GetParam() ? move_file((root / "src/link.jpg").c_str(), (root / "alias/link.jpg").c_str()) :
	                        copy_file((root / "src/link.jpg").c_str(), (root / "alias/link.jpg").c_str()));
	EXPECT_EQ(target(root / "src/link.jpg"), "../image.jpg");
	expect_contents(root / "src/link.jpg", "original");
}

TEST_P(SymlinkTransfer, TargetComponentsAreNotCollapsedAcrossAnotherSymlink)
{
	ASSERT_EQ(g_mkdir_with_parents((root / "branch/deep").c_str(), 0700), 0);
	ASSERT_TRUE(g_file_set_contents((root / "branch/wanted").c_str(), "correct", -1, nullptr));
	ASSERT_TRUE(g_file_set_contents((root / "src/wanted").c_str(), "wrong", -1, nullptr));
	ASSERT_EQ(symlink("../branch/deep", (root / "src/chain").c_str()), 0);
	ASSERT_EQ(symlink("chain/../wanted", (root / "src/link.jpg").c_str()), 0);
	transfer(root / "src/link.jpg", root / "dest/link.jpg");
	expect_contents(root / "dest/link.jpg", "correct");
}

TEST_P(SymlinkTransfer, ExistingDestinationLinkIsReplacedWithoutWritingThroughIt)
{
	ASSERT_TRUE(g_file_set_contents((root / "unrelated.jpg").c_str(), "unrelated", -1, nullptr));
	ASSERT_EQ(symlink("../image.jpg", (root / "src/link.jpg").c_str()), 0);
	ASSERT_EQ(symlink("../unrelated.jpg", (root / "dest/link.jpg").c_str()), 0);
	transfer(root / "src/link.jpg", root / "dest/link.jpg");
	expect_contents(root / "dest/link.jpg", "original");
	expect_contents(root / "unrelated.jpg", "unrelated");
}

TEST_P(SymlinkTransfer, LongDestinationNameDoesNotPreventLinkCreation)
{
	ASSERT_EQ(symlink("../image.jpg", (root / "src/link.jpg").c_str()), 0);
	const auto destination = root / "dest" / (std::string(251, 'x') + ".jpg");
	transfer(root / "src/link.jpg", destination);
	expect_contents(destination, "original");
}

TEST_P(SymlinkTransfer, FailureKeepsSourceAndDestinationIntact)
{
	ASSERT_EQ(symlink("../image.jpg", (root / "src/link.jpg").c_str()), 0);
	ASSERT_TRUE(g_file_set_contents((root / "dest/existing.jpg").c_str(), "existing", -1, nullptr));
	EXPECT_FALSE(GetParam() ? move_file((root / "src/link.jpg").c_str(), (root / "dest").c_str()) :
	                         copy_file((root / "src/link.jpg").c_str(), (root / "dest").c_str()));
	EXPECT_EQ(target(root / "src/link.jpg"), "../image.jpg");
	EXPECT_TRUE(g_file_test((root / "dest").c_str(), G_FILE_TEST_IS_DIR));
	guint entries = 0;
	for (const auto &entry : std::filesystem::directory_iterator(root))
		{
		EXPECT_NE(entry.path().filename().native().find(".geeqie-link-"), 0U);
		++entries;
		}
	EXPECT_EQ(entries, 3U);
	expect_contents(root / "src/link.jpg", "original");
	expect_contents(root / "dest/existing.jpg", "existing");
}

TEST_P(SymlinkTransfer, CrossFilesystemLinksPreserveRelativeAndAbsoluteTargets)
{
	const gchar *second_root = g_getenv("GQ_TEST_CROSS_FILESYSTEM_ROOT");
	if (!second_root) GTEST_SKIP() << "Set GQ_TEST_CROSS_FILESYSTEM_ROOT to a writable second filesystem";
	g_autofree gchar *pattern = g_build_filename(second_root, "geeqie-symlink-XXXXXX", nullptr);
	ASSERT_NE(g_mkdtemp(pattern), nullptr);
	extra = pattern;
	struct stat first_stat;
	struct stat second_stat;
	ASSERT_EQ(stat(root.c_str(), &first_stat), 0);
	ASSERT_EQ(stat(extra.c_str(), &second_stat), 0);
	if (first_stat.st_dev == second_stat.st_dev) GTEST_SKIP() << "Requires different filesystems";
	ASSERT_EQ(symlink("../image.jpg", (root / "src/relative.jpg").c_str()), 0);
	ASSERT_EQ(symlink((root / "image.jpg").c_str(), (root / "src/absolute.jpg").c_str()), 0);
	transfer(root / "src/relative.jpg", extra / "relative.jpg");
	transfer(root / "src/absolute.jpg", extra / "absolute.jpg");
	EXPECT_FALSE(g_path_is_absolute(target(extra / "relative.jpg").c_str()));
	EXPECT_EQ(target(extra / "absolute.jpg"), (root / "image.jpg").native());
	expect_contents(extra / "relative.jpg", "original");
	expect_contents(extra / "absolute.jpg", "original");
}

TEST_P(SymlinkTransfer, RegularFileTransfersStillWork)
{
	EXPECT_TRUE(GetParam() ? move_file((root / "image.jpg").c_str(), (root / "dest/image.jpg").c_str()) :
	                        copy_file((root / "image.jpg").c_str(), (root / "dest/image.jpg").c_str()));
	expect_contents(root / "dest/image.jpg", "original");
	EXPECT_EQ(g_file_test((root / "image.jpg").c_str(), G_FILE_TEST_EXISTS), !GetParam());
}

INSTANTIATE_TEST_SUITE_P(CopyAndMove, SymlinkTransfer, testing::Bool());

class LongFilenameCopy : public testing::TestWithParam<bool>
{
protected:
	void SetUp() override
	{
		g_autofree gchar *directory = g_dir_make_tmp("geeqie-long-filename-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		root = directory;
		ASSERT_EQ(g_mkdir((root / "destination").c_str(), 0700), 0);
	}

	void TearDown() override
	{
		std::error_code error;
		if (!root.empty()) std::filesystem::remove_all(root, error);
	}

	std::filesystem::path root;
};

TEST_P(LongFilenameCopy, PreservesMaximumLengthNameAndContents)
{
	const long limit = pathconf(root.c_str(), _PC_NAME_MAX);
	if (limit < 32 || limit > 4096) GTEST_SKIP() << "Requires a bounded filename length limit";
	std::string name;
	if (GetParam())
		{
		// Use complete UTF-8 characters while filling the byte limit exactly.
		while (name.size() + 2 <= static_cast<size_t>(limit) - 4) name += "é";
		}
	name.append(static_cast<size_t>(limit) - 4 - name.size(), 'a');
	name += ".jpg";
	const auto source = root / name;
	const auto destination = root / "destination" / name;
	std::ofstream stream(source, std::ios::binary);
	stream << "original contents";
	stream.close();
	ASSERT_TRUE(stream.good());
	ASSERT_TRUE(copy_file(source.c_str(), destination.c_str()));
	for (const auto &path : {source, destination})
		{
		g_autofree gchar *contents = nullptr;
		ASSERT_TRUE(g_file_get_contents(path.c_str(), &contents, nullptr, nullptr));
		EXPECT_STREQ(contents, "original contents");
		}
	guint entries = 0;
	for (const auto &entry : std::filesystem::directory_iterator(root / "destination"))
		{
		EXPECT_EQ(entry.path().filename().native(), name);
		++entries;
		}
	EXPECT_EQ(entries, 1U);
}

INSTANTIATE_TEST_SUITE_P(AsciiAndUtf8, LongFilenameCopy, testing::Bool());

} // namespace
