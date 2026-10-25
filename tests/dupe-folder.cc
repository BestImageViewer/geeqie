/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <functional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "dupe.h"
#include "filedata.h"
#include "filefilter.h"
#include "options.h"

namespace
{

using Comparison = std::tuple<DupeMatchType, bool, bool>;

class DuplicateFolders : public testing::TestWithParam<Comparison>
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		saved = *options;
		configured = true;
		options->duplicates_match = std::get<0>(GetParam());
		options->duplicates_thumbnails = FALSE;
		options->duplicates_similarity_threshold = 99;
		options->alternate_similarity_algorithm.enabled = std::get<2>(GetParam());
		options->thumbnails.enable_caching = FALSE;
		options->external_preview.enable = FALSE;
		options->sort_totals = FALSE;
		filter_add_defaults();
		filter_rebuild();
		accel_map_load_merged();
		previous_app = g_application_get_default();
		static GtkApplication *app = nullptr;
		if (!app)
			{
			app = gtk_application_new("org.geeqie.DuplicateFoldersTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
			}
		g_application_set_default(G_APPLICATION(app));
		directory = g_dir_make_tmp("geeqie-dupe-folders-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		dw = dupe_window_new();
		ASSERT_NE(dw, nullptr);
		EXPECT_FALSE(dw->ignore_same_folder);
		EXPECT_FALSE(gtk_check_button_get_active(GTK_CHECK_BUTTON(dw->button_ignore_same_folder)));
		dw->second_set = std::get<1>(GetParam());
	}

	void TearDown() override
	{
		if (!configured) return;
		if (dw) dupe_window_close(dw);
		g_application_set_default(previous_app);
		for (const auto &path : paths) g_remove(path.c_str());
		for (const auto &path : folders) g_rmdir(path.c_str());
		if (directory) g_rmdir(directory);
		g_free(directory);
		options->duplicates_match = saved.duplicates_match;
		options->duplicates_thumbnails = saved.duplicates_thumbnails;
		options->duplicates_select_type = saved.duplicates_select_type;
		options->duplicates_similarity_threshold = saved.duplicates_similarity_threshold;
		options->alternate_similarity_algorithm = saved.alternate_similarity_algorithm;
		options->thumbnails.enable_caching = saved.thumbnails.enable_caching;
		options->external_preview.enable = saved.external_preview.enable;
		options->sort_totals = saved.sort_totals;
	}

	std::string create(const gchar *relative, bool patterned = false)
	{
		g_autofree gchar *path = g_build_filename(directory, relative, nullptr);
		g_autofree gchar *parent = g_path_get_dirname(path);
		if (g_mkdir_with_parents(parent, 0700) != 0) return {};
		std::string folder = parent;
		while (folder != directory)
			{
			folders.insert(folder);
			folder = folder.substr(0, folder.find_last_of('/'));
			}
		g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
		gdk_pixbuf_fill(image, 0x4080c0ff);
		if (patterned)
			{
			guchar *pixels = gdk_pixbuf_get_pixels(image);
			const gint stride = gdk_pixbuf_get_rowstride(image);
			for (gint y = 0; y < 32; ++y)
				{
				for (gint x = 0; x < 32; ++x)
					{
					auto *pixel = pixels + (y * stride) + (x * 3);
					pixel[0] = x * 7;
					pixel[1] = y * 7;
					pixel[2] = (x * 13 + y * 19) % 256;
					}
				}
			}
		if (!gdk_pixbuf_save(image, path, "png", nullptr, nullptr)) return {};
		paths.emplace_back(path);
		return path;
	}

	bool wait_finished()
	{
		const gint64 deadline = g_get_monotonic_time() + (10 * G_TIME_SPAN_SECOND);
		// Allow queued UI updates to settle after the tracked comparison callbacks finish.
		gint64 quiet_since = 0;
		while (g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			if (dupe_export_ready(dw))
				{
				if (!quiet_since) quiet_since = g_get_monotonic_time();
				if (g_get_monotonic_time() - quiet_since > 20 * G_TIME_SPAN_MILLISECOND) return true;
				}
			else quiet_since = 0;
			g_usleep(1000);
			}
		return false;
	}

	void add(const std::vector<std::string> &files, bool second = false)
	{
		g_autoptr(FileDataList) list = nullptr;
		for (const auto &path : files)
			{
			ASSERT_FALSE(path.empty());
			list = g_list_append(list, file_data_new_simple(path.c_str()));
			}
		dw->second_drop = second;
		dupe_window_add_files(dw, list, FALSE);
		ASSERT_TRUE(wait_finished());
	}

	void toggle(bool enabled)
	{
		gtk_check_button_set_active(GTK_CHECK_BUTTON(dw->button_ignore_same_folder), enabled);
		EXPECT_EQ(dw->ignore_same_folder, enabled);
		EXPECT_NE(dw->idle_id, 0U);
		ASSERT_TRUE(wait_finished());
	}

	static std::string parent(DupeItem *item)
	{
		g_autofree gchar *path = g_path_get_dirname(item->fd->path);
		return path;
	}

	void expect_cross_folder_results(const std::set<std::string> &expected)
	{
		std::set<std::string> shown;
		std::set<DupeItem *> representatives;
		for (GList *group = dw->dupes; group; group = group->next)
			{
			representatives.insert(static_cast<DupeItem *>(group->data));
			}
		for (GList *group = dw->dupes; group; group = group->next)
			{
			auto *representative = static_cast<DupeItem *>(group->data);
			EXPECT_FALSE(representative->second);
			shown.insert(representative->fd->path);
			ASSERT_NE(representative->group, nullptr);
			for (GList *member = representative->group; member; member = member->next)
				{
				auto *match = static_cast<DupeMatch *>(member->data);
				EXPECT_NE(parent(representative), parent(match->di));
				EXPECT_EQ(representatives.count(match->di), 0U);
				if (dw->second_set) EXPECT_TRUE(match->di->second);
				shown.insert(match->di->fd->path);
				}
			}
		EXPECT_EQ(shown, expected);
		// Check the actual comparison graph as well as the displayed groups.
		// All files in these cases have equal matching attributes.
		for (GList *first = dw->list; first; first = first->next)
			{
			auto *a = static_cast<DupeItem *>(first->data);
			for (GList *second = dw->second_set ? dw->second_list : first->next; second; second = second->next)
				{
				auto *b = static_cast<DupeItem *>(second->data);
				bool linked = false;
				for (GList *member = a->group; member; member = member->next)
					{
					auto *match = static_cast<DupeMatch *>(member->data);
					if (match->di == b) linked = true;
					}
				EXPECT_EQ(linked, parent(a) != parent(b)) << a->fd->path << " and " << b->fd->path;
				}
			}
	}

	DupeWindow *dw = nullptr;
	GApplication *previous_app = nullptr;
	ConfOptions saved{};
	bool configured = false;
	gchar *directory = nullptr;
	std::vector<std::string> paths;
	std::set<std::string, std::greater<>> folders;
};

TEST_P(DuplicateFolders, ToggleExcludesSameFolderAndRestoresMatches)
{
	const auto first = create("A/frame.png");
	const auto second = create("A/FRAME.PNG");
	if (dw->second_set)
		{
		add({first});
		add({second}, true);
		}
	else add({first, second});
	ASSERT_NE(dw->dupes, nullptr);
	toggle(true);
	EXPECT_EQ(dw->dupes, nullptr);
	toggle(false);
	EXPECT_NE(dw->dupes, nullptr);
}

TEST_P(DuplicateFolders, SubfoldersAreSeparateFolders)
{
	toggle(true);
	const auto first = create("A/frame.png");
	const auto second = create("A/sub/frame.png");
	if (dw->second_set)
		{
		add({first});
		add({second}, true);
		}
	else add({first, second});
	expect_cross_folder_results({first, second});
}

TEST_P(DuplicateFolders, MixedGroupsRetainEveryCrossFolderPair)
{
	toggle(true);
	const auto a = create("A/frame.png");
	const auto a2 = create("A/FRAME.PNG");
	const auto a3 = create("A/fRaMe.png");
	const auto b = create("B/frame.png");
	const auto b2 = create("B/FRAME.PNG");
	const auto b3 = create("B/fRaMe.png");
	if (dw->second_set)
		{
		add({a, a2, b});
		add({a3, b2, b3}, true);
		}
	else add({a, a2, a3, b, b2, b3});
	expect_cross_folder_results({a, a2, a3, b, b2, b3});
	ASSERT_TRUE(gtk_widget_activate_action(dw->window, "win.dupe-win-select-group2", nullptr));
	auto *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	GtkTreeModel *model = nullptr;
	g_autolist(GtkTreePath) rows = gtk_tree_selection_get_selected_rows(selection, &model);
	ASSERT_NE(rows, nullptr);
	for (GList *row = rows; row; row = row->next)
		{
		GtkTreeIter iter;
		ASSERT_TRUE(gtk_tree_model_get_iter(model, &iter, static_cast<GtkTreePath *>(row->data)));
		DupeItem *item = nullptr;
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		EXPECT_EQ(g_list_find(dw->dupes, item), nullptr);
		}
}

TEST_P(DuplicateFolders, EqualKeysInOneFolderDoNotHideOtherCandidates)
{
	toggle(true);
	const auto a = create("A/frame.png");
	const auto a2 = create("A/FRAME.PNG");
	const auto a3 = create("A/fRaMe.png");
	const auto b = create("B/frame.png");
	const auto c = create("C/frame.png");
	if (dw->second_set)
		{
		add({a, b, c});
		add({a2, a3}, true);
		// The A file in set 1 has no eligible match in set 2.
		expect_cross_folder_results({a2, a3, b, c});
		}
	else
		{
		add({a, a2, a3, b, c});
		expect_cross_folder_results({a, a2, a3, b, c});
		}
}

TEST_P(DuplicateFolders, RemovingSharedMatchDoesNotCreateSameFolderMatches)
{
	toggle(true);
	const auto a = create("A/frame.png");
	const auto a2 = create("A/FRAME.PNG");
	const auto b = create("B/frame.png");
	if (dw->second_set)
		{
		add({a, a2});
		add({b}, true);
		}
	else add({a, a2, b});
	expect_cross_folder_results({a, a2, b});
	auto *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_unselect_all(selection);
	auto *model = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
	GtkTreeIter iter;
	guint selected = 0;
	for (gboolean valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter))
		{
		DupeItem *item = nullptr;
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (b == item->fd->path)
			{
			gtk_tree_selection_select_iter(selection, &iter);
			++selected;
			}
		}
	EXPECT_EQ(selected, dw->second_set ? 2U : 1U);
	ASSERT_TRUE(gtk_widget_activate_action(dw->window, "win.dupe-win-remove", nullptr));
	ASSERT_TRUE(wait_finished());
	EXPECT_EQ(dw->dupes, nullptr);
	EXPECT_EQ(g_list_length(dw->list) + g_list_length(dw->second_list), 2U);
	for (const auto &path : paths) EXPECT_TRUE(g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR));
}

TEST_P(DuplicateFolders, ClosingWindowCancelsDeferredComparison)
{
	const auto first = create("A/frame.png");
	g_autoptr(FileDataList) files = g_list_append(nullptr, file_data_new_simple(first.c_str()));
	dupe_window_add_files(dw, files, FALSE);
	const gint64 deadline = g_get_monotonic_time() + (10 * G_TIME_SPAN_SECOND);
	while (!dw->recompare_id && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		g_usleep(1000);
		}
	ASSERT_NE(dw->recompare_id, 0U);
	const guint pending = dw->recompare_id;
	EXPECT_FALSE(dupe_export_ready(dw));
	dupe_window_close(dw);
	dw = nullptr;
	EXPECT_EQ(g_main_context_find_source_by_id(nullptr, pending), nullptr);
}

TEST_P(DuplicateFolders, DifferentFoldersStillRequireMatchingAttributes)
{
	toggle(true);
	const auto first = create("A/frame.png");
	const auto second = create("B/unrelated.png", true);
	if (dw->second_set)
		{
		add({first});
		add({second}, true);
		}
	else add({first, second});
	EXPECT_EQ(dw->dupes, nullptr);
}

INSTANTIATE_TEST_SUITE_P(ComparisonMethods, DuplicateFolders, testing::Values(
	Comparison{DUPE_MATCH_NAME_CI, false, false}, Comparison{DUPE_MATCH_NAME_CI, true, false},
	Comparison{DUPE_MATCH_SIZE, false, false}, Comparison{DUPE_MATCH_SIZE, true, false},
	Comparison{DUPE_MATCH_SUM, false, false}, Comparison{DUPE_MATCH_SUM, true, false},
	Comparison{DUPE_MATCH_SIM_CUSTOM, false, false}, Comparison{DUPE_MATCH_SIM_CUSTOM, true, false},
	Comparison{DUPE_MATCH_SIM_CUSTOM, false, true}, Comparison{DUPE_MATCH_SIM_CUSTOM, true, true}));

} // namespace
