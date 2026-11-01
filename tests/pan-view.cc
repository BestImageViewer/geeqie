/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <unistd.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "filedata.h"
#include "filefilter.h"
#include "image.h"
#include "options.h"
#include "pan-view.h"
#include "pan-view/pan-index.h"
#include "pan-view/pan-types.h"
#include "pan-view/pan-view.h"

namespace
{

bool wait_pan(const std::function<bool()> &ready, gint seconds = 20)
{
	const auto deadline = g_get_monotonic_time() + (seconds * G_TIME_SPAN_SECOND);
	while (!ready() && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		g_usleep(1000);
		}
	return ready();
}

class PanIndexing : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display"; // NOTRANSLATE: Test requirement.
		if (!gtk_init_check()) GTEST_SKIP() << "Requires a display"; // NOTRANSLATE: Test requirement.
		if (!options) options = conf_options_new();
		saved_caching = options->thumbnails.enable_caching;
		saved_hidden = options->file_filter.show_hidden_files;
		saved_dot_hidden = options->file_filter.dot_prefix_hidden_files;
		options->thumbnails.enable_caching = TRUE;
		options->file_filter.show_hidden_files = FALSE;
		options->file_filter.dot_prefix_hidden_files = TRUE;
		changed_options = true;
		filter_add_defaults();
		filter_rebuild();
		sidecar_ext_parse(".dng;.jpg;.xmp");
		accel_map_load_merged();
		previous = g_application_get_default();
		if (previous) g_object_ref(previous);
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.PanIndexTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		g_application_set_default(G_APPLICATION(test_app));
		directory = g_dir_make_tmp("geeqie-pan-index-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		g_autofree gchar *seed = g_build_filename(directory, "seed.png", nullptr);
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
		gdk_pixbuf_fill(pixels, 0x123456ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, seed, "png", nullptr, nullptr));
		paths.emplace_back(seed);
	}

	void TearDown() override
	{
		close();
		while (g_main_context_iteration(nullptr, FALSE)) {}
		if (directory)
			{
			for (const auto &path : paths) g_unlink(path.c_str());
			for (auto it = folders.rbegin(); it != folders.rend(); ++it) g_rmdir(it->c_str());
			g_rmdir(directory);
			g_free(directory);
			}
		if (changed_options)
			{
			options->thumbnails.enable_caching = saved_caching;
			options->file_filter.show_hidden_files = saved_hidden;
			options->file_filter.dot_prefix_hidden_files = saved_dot_hidden;
			sidecar_ext_parse(options->sidecar.ext);
			g_application_set_default(previous);
			if (previous) g_object_unref(previous);
			}
	}

	void tree(guint directories, guint images)
	{
		const std::string seed = std::string(directory) + "/seed.png";
		for (guint d = 0; d < directories; ++d)
			{
			g_autofree gchar *folder = g_strdup_printf("%s/folder-%04u", directory, d);
			ASSERT_EQ(g_mkdir(folder, 0700), 0);
			folders.emplace_back(folder);
			for (guint i = 0; i < images; ++i)
				{
				g_autofree gchar *path = g_strdup_printf("%s/image-%06u.png", folder, i);
				ASSERT_EQ(link(seed.c_str(), path), 0);
				paths.emplace_back(path);
				}
			}
	}

	PanWindow *open()
	{
		auto fd = FileData::new_dir(directory);
		pan_window_new(fd);
		auto *windows = gtk_window_get_toplevels();
		for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
			{
			g_autoptr(GtkWindow) candidate = GTK_WINDOW(g_list_model_get_item(windows, i));
			if (auto *pw = static_cast<PanWindow *>(g_object_get_data(G_OBJECT(candidate), "pan-window")))
				{
				window = candidate;
				pw->size = PAN_IMAGE_SIZE_THUMB_NONE;
				pw->exif_date_enable = FALSE;
				return pw;
				}
			}
		return nullptr;
	}

	void close()
	{
		if (window) gtk_window_close(std::exchange(window, nullptr));
	}

	GtkWindow *window = nullptr;
	GApplication *previous = nullptr;
	gchar *directory = nullptr;
	std::vector<std::string> paths, folders;
	bool changed_options = false;
	gboolean saved_caching = FALSE, saved_hidden = FALSE, saved_dot_hidden = FALSE;
};

TEST_F(PanIndexing, LargeTreeBuildsAllLayoutsWithoutBlockingTheMainLoop)
{
	tree(12, 500);
	auto *pw = open();
	ASSERT_NE(pw, nullptr);
	for (auto layout : {PAN_LAYOUT_GRID, PAN_LAYOUT_TIMELINE, PAN_LAYOUT_CALENDAR, PAN_LAYOUT_FOLDERS_LINEAR, PAN_LAYOUT_FOLDERS_FLOWER})
		{
		SCOPED_TRACE(layout);
		pw->layout = layout;
		pan_layout_update(pw);
		struct Heartbeat
		{
			gint64 previous = g_get_monotonic_time();
			gint64 maximum = 0;
			unsigned int ticks = 0;
			unsigned int layout_ticks = 0;
			unsigned int grid_ticks = 0;
			PanWindow *window;
		} heartbeat{g_get_monotonic_time(), 0, 0, 0, 0, pw};
		const auto source = g_timeout_add(5, [](gpointer value) -> gboolean
			{
			auto &beat = *static_cast<Heartbeat *>(value);
			const auto now = g_get_monotonic_time();
			beat.maximum = std::max(beat.maximum, now - beat.previous);
			beat.previous = now;
			++beat.ticks;
			if (beat.window->build_phase == PanBuildPhase::Layout) ++beat.layout_ticks;
			if (beat.window->build_phase == PanBuildPhase::Grid) ++beat.grid_ticks;
			return G_SOURCE_CONTINUE;
			}, &heartbeat);
		const bool ready = wait_pan([&]() { return pw->build_phase == PanBuildPhase::Complete; });
		g_source_remove(source);
		ASSERT_TRUE(ready);
		EXPECT_GT(heartbeat.ticks, 5U);
		// Check that both stages yield, without relying on wall-clock limits on busy CI hosts.
		EXPECT_GT(heartbeat.layout_ticks, 0U);
		EXPECT_GT(heartbeat.grid_ticks, 0U);
		RecordProperty("layout_" + std::to_string(layout) + "_max_gap_us", std::to_string(heartbeat.maximum));
		std::unordered_set<std::string> images;
		for (const auto *item : pw->list_static)
			if (item->fd && (item->type == PAN_ITEM_THUMB || item->type == PAN_ITEM_IMAGE || item->key == PanKey::Dot)) images.insert(item->fd->path);
		EXPECT_EQ(images.size(), 6001U);
		if (layout == PAN_LAYOUT_GRID)
			{
			ASSERT_NE(pw->list_grid, nullptr);
			for (gint y = 0; y < pw->layout_height; y += std::max(1, pw->layout_height / 7))
				for (gint x = 0; x < pw->layout_width; x += std::max(1, pw->layout_width / 7))
					{
					const GdkRectangle rect{x, y, 373, 269};
					std::set<const PanItem *> expected;
					for (const auto *item : pw->list_static)
						{
						const GdkRectangle bounds{item->x, item->y, item->width, item->height};
						if (gdk_rectangle_intersect(&rect, &bounds, nullptr)) expected.insert(item);
						}
					const auto hits = pan_layout_intersect(pw, x, y, rect.width, rect.height);
					EXPECT_EQ((std::set<const PanItem *>(hits.begin(), hits.end())), expected);
					}
			}

		EXPECT_FALSE(gtk_widget_get_visible(pw->cancel_button));
		EXPECT_TRUE(gtk_widget_get_sensitive(pw->imd->widget));
		}
}

TEST_F(PanIndexing, CancelCloseAndRestartDiscardPendingWork)
{
	tree(8, 400);
	const auto baseline = g_hash_table_size(GlobalFileDataContext::get_instance().context().file_data_pool);
	auto *pw = open();
	ASSERT_NE(pw, nullptr);
	std::weak_ptr<PanIndex> old = pw->index;
	g_signal_emit_by_name(pw->cancel_button, "clicked");
	EXPECT_EQ(pw->build_phase, PanBuildPhase::Cancelled);
	ASSERT_TRUE(wait_pan([&]() { return old.expired(); }));
	for (auto layout : {PAN_LAYOUT_FOLDERS_FLOWER, PAN_LAYOUT_CALENDAR, PAN_LAYOUT_FOLDERS_LINEAR, PAN_LAYOUT_TIMELINE, PAN_LAYOUT_GRID})
		{
		SCOPED_TRACE(layout);
		pw->layout = layout;
		pan_layout_update(pw);
		ASSERT_TRUE(wait_pan([&]() { return pw->build_phase == PanBuildPhase::Layout; }));
		pan_layout_cancel(pw);
		EXPECT_TRUE(pw->list.empty());
		EXPECT_TRUE(pw->list_static.empty());
		}
	pan_layout_update(pw);
	old = pw->index;
	pan_layout_update(pw);
	ASSERT_TRUE(wait_pan([&]() { return old.expired(); }));
	old = pw->index;
	close();
	EXPECT_TRUE(wait_pan([&]() { return old.expired(); }));
	EXPECT_EQ(g_hash_table_size(GlobalFileDataContext::get_instance().context().file_data_pool), baseline);
}

TEST_F(PanIndexing, ImageDimensionsAndExifCacheResumeTheBuild)
{
	tree(2, 10);
	auto *pw = open();
	ASSERT_NE(pw, nullptr);
	pw->size = PAN_IMAGE_SIZE_50;
	pw->exif_date_enable = TRUE;
	pw->layout = PAN_LAYOUT_TIMELINE;
	pan_layout_update(pw);
	ASSERT_TRUE(wait_pan([&]() { return pw->build_phase == PanBuildPhase::Complete; }));
	guint count = 0;
	for (const auto *item : pw->list_static)
		if (item->type == PAN_ITEM_IMAGE)
			{
			++count;
			EXPECT_EQ(item->width, 1);
			EXPECT_EQ(item->height, 1);
			}
	EXPECT_EQ(count, 21U);
	EXPECT_TRUE(pw->cache_by_file.empty());
}

TEST_F(PanIndexing, SnapshotReusesDirectoryContentsAndPreservesGroupingAndHiddenRules)
{
	tree(2, 3);
	for (const auto *name : {"group.dng", "group.jpg", "group.xmp"})
		{
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		ASSERT_EQ(link(paths.front().c_str(), path), 0);
		paths.emplace_back(path);
		}

	g_autofree gchar *hidden = g_build_filename(directory, ".hidden.png", nullptr);
	ASSERT_EQ(link(paths.front().c_str(), hidden), 0);
	paths.emplace_back(hidden);
	g_autofree gchar *cycle = g_build_filename(folders.front().c_str(), "cycle", nullptr);
	ASSERT_EQ(symlink(directory, cycle), 0);
	paths.emplace_back(cycle);
	auto *pw = open();
	ASSERT_NE(pw, nullptr);
	pw->layout = PAN_LAYOUT_GRID;
	pan_layout_update(pw);
	ASSERT_TRUE(wait_pan([&]() { return pw->build_phase == PanBuildPhase::Complete; }));
	g_autoptr(FileDataList) snapshot = pw->index->files(SORT_NAME);
	EXPECT_EQ(g_list_length(snapshot), 8U);
	g_autoptr(FileDataList) root_files = nullptr;
	g_autoptr(FileDataList) root_dirs = nullptr;
	ASSERT_TRUE(pw->index->directory(directory, &root_files, &root_dirs));
	const auto groups = [](GList *files)
		{
		std::map<std::string, std::set<std::string>> result;
		for (auto *work = files; work; work = work->next)
			{
			auto *fd = static_cast<FileData *>(work->data);
			auto &children = result[fd->path];
			for (auto *sidecar = fd->sidecar_files; sidecar; sidecar = sidecar->next)
				children.insert(static_cast<FileData *>(sidecar->data)->path);
			}
		return result;
		};
	const auto actual_groups = groups(root_files);
	g_autoptr(FileDataList) legacy_files = nullptr;
	g_autoptr(FileDataList) legacy_dirs = nullptr;
	ASSERT_TRUE(filelist_read(pw->dir_fd, &legacy_files, &legacy_dirs));
	EXPECT_EQ(actual_groups, groups(legacy_files));
	EXPECT_EQ(actual_groups.at(std::string(directory) + "/group.dng").size(), 2U);
	EXPECT_EQ(g_unlink(paths.back().c_str()), 0);
	for (const auto &path : paths) g_unlink(path.c_str());
	for (const auto &folder : folders) g_rmdir(folder.c_str());
	g_autoptr(FileDataList) retained = pw->index->files(SORT_NAME);
	EXPECT_EQ(g_list_length(retained), 8U);
	g_autoptr(FileDataList) files = nullptr;
	g_autoptr(FileDataList) dirs = nullptr;
	EXPECT_TRUE(pw->index->directory(folders.front().c_str(), &files, &dirs));
	EXPECT_EQ(g_list_length(files), 3U);
}

} // namespace
