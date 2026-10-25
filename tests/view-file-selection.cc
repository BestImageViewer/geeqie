/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <numeric>
#include <vector>

#include <gtk/gtk.h>

#include "filedata.h"
#include "options.h"
#include "view-file.h"
#include "view-file/view-file-icon.h"
#include "view-file/view-file-list.h"

namespace
{

class SelectedIndices : public testing::TestWithParam<FileViewType>
{
protected:
	void SetUp() override
	{
		vf.type = GetParam();
		if (vf.type == FILEVIEW_LIST && (!gtk_init_check() || !gdk_display_get_default())) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		vf.info = &icons;
		files.reserve(6000);
		for (gint i = 0; i < 6000; ++i)
			{
			g_autofree gchar *path = g_strdup_printf("/tmp/geeqie-selection-index-%04d.jpg", i);
			auto *fd = file_data_new_simple(path);
			vf.list = g_list_prepend(vf.list, fd);
			files.push_back(fd);
			}
		vf.list = g_list_reverse(vf.list);
		if (vf.type == FILEVIEW_LIST)
			{
			store = gtk_tree_store_new(1, G_TYPE_POINTER);
			for (auto *fd : files)
				{
				GtkTreeIter iter;
				gtk_tree_store_append(store, &iter, nullptr);
				gtk_tree_store_set(store, &iter, 0, fd, -1);
				}
			vf.listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
			g_object_ref_sink(vf.listview);
			gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(vf.listview)), GTK_SELECTION_MULTIPLE);
			}
	}

	void TearDown() override
	{
		g_clear_object(&vf.listview);
		g_clear_object(&store);
		g_list_free(icons.selection);
		file_data_list_free(vf.list);
	}

	void select(const std::vector<int> &indices)
	{
		if (vf.type == FILEVIEW_ICON)
			{
			g_clear_pointer(&icons.selection, g_list_free);
			for (gint index : indices) icons.selection = g_list_prepend(icons.selection, files[index]);
			icons.selection = g_list_reverse(icons.selection);
			}
		else
			{
			auto *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vf.listview));
			gtk_tree_selection_unselect_all(selection);
			for (gint index : indices)
				{
				g_autoptr(GtkTreePath) path = gtk_tree_path_new_from_indices(index, -1);
				gtk_tree_selection_select_path(selection, path);
				}
			}
	}

	ViewFile vf{};
	ViewFileInfoIcon icons{};
	GtkTreeStore *store = nullptr;
	std::vector<FileData *> files;
};

TEST_P(SelectedIndices, EmptySelectionReturnsNoIndices)
{
	EXPECT_TRUE(vf_selection_get_list_by_index(&vf).empty());
}

TEST_P(SelectedIndices, SparseSelectionPreservesEachViewsOrder)
{
	select({5999, 1, 3500});
	const std::vector<int> expected = vf.type == FILEVIEW_ICON ? std::vector<int>{5999, 1, 3500} : std::vector<int>{1, 3500, 5999};
	EXPECT_EQ(vf_selection_get_list_by_index(&vf), expected);
}

TEST_P(SelectedIndices, AllSixThousandSelectedFilesHaveCorrectIndices)
{
	std::vector<int> expected(6000);
	std::iota(expected.begin(), expected.end(), 0);
	select(expected);
	EXPECT_EQ(vf_selection_get_list_by_index(&vf), expected);
}

TEST_P(SelectedIndices, FileOrderCanDifferFromSelectionAndModelOrder)
{
	select({0, 3, 5999});
	vf.list = g_list_reverse(vf.list);
	EXPECT_EQ(vf_selection_get_list_by_index(&vf), (std::vector<int>{5999, 5996, 0}));
}

TEST_P(SelectedIndices, MissingSelectedFileRetainsInvalidIndex)
{
	FileDataRef missing{files[42]};
	vf.list = g_list_remove(vf.list, files[42]);
	file_data_unref(files[42]);
	select({42});
	EXPECT_EQ(vf_selection_get_list_by_index(&vf), (std::vector<int>{-1}));
}

TEST_P(SelectedIndices, ExpandedChildRowsPreserveTheirSelectionPosition)
{
	if (vf.type != FILEVIEW_LIST) GTEST_SKIP() << "Requires a tree model";
	auto child_fd = FileData::new_simple("/tmp/geeqie-selection-child.xmp");
	GtkTreeIter parent;
	GtkTreeIter child;
	ASSERT_TRUE(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &parent));
	gtk_tree_store_append(store, &child, &parent);
	gtk_tree_store_set(store, &child, 0, static_cast<FileData *>(child_fd), -1);
	g_autoptr(GtkTreePath) parent_path = gtk_tree_path_new_from_indices(0, -1);
	gtk_tree_view_expand_row(GTK_TREE_VIEW(vf.listview), parent_path, FALSE);
	select({5999});
	g_autoptr(GtkTreePath) child_path = gtk_tree_path_new_from_string("0:0");
	gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(vf.listview)), child_path);
	EXPECT_EQ(vf_selection_get_list_by_index(&vf), (std::vector<int>{-1, 5999}));
}

TEST_P(SelectedIndices, DISABLED_BenchmarkSixThousandSelectedFiles)
{
	std::vector<int> expected(6000);
	std::iota(expected.begin(), expected.end(), 0);
	select(expected);
	EXPECT_EQ(vf_selection_get_list_by_index(&vf), expected);
	const gint64 old_start = g_get_monotonic_time();
	for (gint repeat = 0; repeat < 5; ++repeat)
		{
		std::vector<int> reference;
		reference.reserve(files.size());
		for (auto *fd : files) reference.push_back(g_list_index(vf.list, fd));
		EXPECT_EQ(reference, expected);
		}
	const gint64 old_time = g_get_monotonic_time() - old_start;
	const gint64 new_start = g_get_monotonic_time();
	for (gint repeat = 0; repeat < 5; ++repeat) EXPECT_EQ(vf_selection_get_list_by_index(&vf), expected);
	const gint64 new_time = g_get_monotonic_time() - new_start;
	g_print("%s: repeated list searches %.3f ms, indexed selection %.3f ms per call\n",
	        vf.type == FILEVIEW_ICON ? "Icons" : "List", old_time / 5000.0, new_time / 5000.0);
}

INSTANTIATE_TEST_SUITE_P(FileViews, SelectedIndices, testing::Values(FILEVIEW_LIST, FILEVIEW_ICON));

} // namespace
