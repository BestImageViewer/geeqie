/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "dnd.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "gtest/gtest.h"

#include "filedata.h"
#include "filefilter.h"
#include "options.h"

namespace
{

TEST(DropAction, ModifiersOverrideTheConfiguredDefault)
{
	if (!options) options = conf_options_new();
	struct Restore
	{
		DnDAction previous = options->dnd_default_action;
		~Restore() { options->dnd_default_action = previous; }
	} restore;
	for (const auto setting : {DND_ACTION_ASK, DND_ACTION_COPY, DND_ACTION_MOVE})
		{
		options->dnd_default_action = setting;
		EXPECT_EQ(dnd_action_from_modifiers(GDK_CONTROL_MASK), GDK_ACTION_COPY);
		EXPECT_EQ(dnd_action_from_modifiers(GDK_SHIFT_MASK), GDK_ACTION_MOVE);
		EXPECT_EQ(dnd_action_from_modifiers(static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK)), GDK_ACTION_COPY);
		EXPECT_EQ(dnd_action_from_modifiers(GDK_NO_MODIFIER_MASK), setting == DND_ACTION_COPY ? GDK_ACTION_COPY : setting == DND_ACTION_MOVE ? GDK_ACTION_MOVE : GDK_ACTION_NONE);
		}
}

class GroupedDrop : public ::testing::TestWithParam<bool>
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		filter_add_defaults();
		filter_rebuild();
		sidecar_ext_parse(".jpg;.xmp");
		directory = g_dir_make_tmp("geeqie-grouped-drop-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		image_path = g_build_filename(directory, "image.jpg", nullptr);
		sidecar_path = g_build_filename(directory, "image.xmp", nullptr);
		destination = g_build_filename(directory, "copies", nullptr);
		ASSERT_TRUE(g_file_set_contents(image_path, "image", -1, nullptr));
		ASSERT_TRUE(g_file_set_contents(sidecar_path, "metadata", -1, nullptr));
		ASSERT_EQ(g_mkdir(destination, 0700), 0);
		image = file_data_new_group(image_path);
		ASSERT_NE(image->sidecar_files, nullptr);
		sidecar = static_cast<FileData *>(image->sidecar_files->data);
		ASSERT_STREQ(sidecar->path, sidecar_path);
	}

	void TearDown() override
	{
		if (image)
			{
			file_data_sc_free_ci(image);
			file_data_unref(image);
			}
		if (destination)
			{
			for (const gchar *name : {"image.jpg", "image.xmp"})
				{
				g_autofree gchar *path = g_build_filename(destination, name, nullptr);
				g_remove(path);
				}
			g_rmdir(destination);
			}
		if (image_path) g_remove(image_path);
		if (sidecar_path) g_remove(sidecar_path);
		if (directory) g_rmdir(directory);
		g_free(image_path);
		g_free(sidecar_path);
		g_free(destination);
		g_free(directory);
		sidecar_ext_parse(options->sidecar.ext);
	}

	GList *read_drop(bool whole_group)
	{
		g_autoptr(GFile) image_file = g_file_new_for_path(image_path);
		g_autoptr(GFile) sidecar_file = g_file_new_for_path(sidecar_path);
		if (GetParam())
			{
			GFile *files[] = {image_file, sidecar_file};
			GdkFileList *file_list = gdk_file_list_new_from_array(whole_group ? files : files + 1, whole_group ? 2 : 1);
			GList *result = dnd_file_list_from_files(file_list);
			g_boxed_free(GDK_TYPE_FILE_LIST, file_list);
			return result;
			}
		g_autofree gchar *image_uri = g_file_get_uri(image_file);
		g_autofree gchar *sidecar_uri = g_file_get_uri(sidecar_file);
		g_autofree gchar *text = whole_group ? g_strconcat(image_uri, "\r\n", sidecar_uri, "\r\n", nullptr)
		                                  : g_strconcat(sidecar_uri, "\r\n", nullptr);
		return dnd_file_list_from_uri_text(text);
	}

	void expect_grouped()
	{
		EXPECT_FALSE(image->disable_grouping);
		EXPECT_FALSE(sidecar->disable_grouping);
		EXPECT_EQ(sidecar->parent, image);
		ASSERT_NE(image->sidecar_files, nullptr);
		EXPECT_EQ(image->sidecar_files->data, sidecar);
	}

	gchar *directory = nullptr;
	gchar *image_path = nullptr;
	gchar *sidecar_path = nullptr;
	gchar *destination = nullptr;
	FileData *image = nullptr;
	FileData *sidecar = nullptr;
};

TEST_P(GroupedDrop, CopyKeepsSourceGrouped)
{
	g_autoptr(FileDataList) dropped = read_drop(true);
	ASSERT_EQ(g_list_length(dropped), 2U);
	expect_grouped();
	g_autoptr(FileDataList) ungrouped = nullptr;
	dropped = file_data_process_groups_in_selection(dropped, TRUE, &ungrouped);
	EXPECT_EQ(ungrouped, nullptr);
	ASSERT_EQ(g_list_length(dropped), 1U);
	ASSERT_TRUE(file_data_sc_add_ci_copy(image, destination));
	ASSERT_TRUE(file_data_sc_perform_ci(image));
	ASSERT_TRUE(file_data_sc_apply_ci(image));
	file_data_sc_free_ci(image);
	for (const gchar *name : {"image.jpg", "image.xmp"})
		{
		g_autofree gchar *path = g_build_filename(destination, name, nullptr);
		EXPECT_TRUE(g_file_test(path, G_FILE_TEST_IS_REGULAR));
		}
	expect_grouped();
}

TEST_P(GroupedDrop, ReadingSidecarAloneKeepsSourceGrouped)
{
	g_autoptr(FileDataList) dropped = read_drop(false);
	ASSERT_EQ(g_list_length(dropped), 1U);
	EXPECT_EQ(dropped->data, sidecar);
	expect_grouped();
}

INSTANTIATE_TEST_SUITE_P(Formats, GroupedDrop, ::testing::Bool());

} // namespace
