/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <csignal>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glib/gstdio.h>

#include "filedata.h"
#include "image-load.h"
#include "options.h"

namespace
{

using Loader = std::unique_ptr<ImageLoader, decltype(&image_loader_free)>;

class ExternalPreview : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved = options->external_preview;
		directory = g_dir_make_tmp("geeqie-external-preview-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
		gdk_pixbuf_fill(pixbuf, 0x204060ff);
		g_autofree gchar *path = g_build_filename(directory, "preview.png", nullptr);
		ASSERT_TRUE(gdk_pixbuf_save(pixbuf, path, "png", nullptr, nullptr));
		g_autofree gchar *contents = nullptr;
		gsize length = 0;
		ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
		for (int i = 0; i < 5; ++i)
			{
			g_autofree gchar *name = g_strdup_printf("image %d'$(literal).gpr", i);
			g_autofree gchar *input = g_build_filename(directory, name, nullptr);
			ASSERT_TRUE(g_file_set_contents(input, contents, length, nullptr));
			paths.emplace_back(input);
			}
		options->external_preview.enable = TRUE;
		options->external_preview.select = g_build_filename(directory, "select.sh", nullptr);
		options->external_preview.extract = g_build_filename(directory, "extract.sh", nullptr);
		configured = true;
		ASSERT_TRUE(g_file_set_contents(options->external_preview.select,
			"#!/bin/sh\n"
			"dir=$(dirname \"$0\")\n"
			"printf 'select:%s\\n' \"$1\" >> \"$dir/launches\"\n"
			"if [ -f \"$dir/no-match\" ]; then exit 1; fi\n"
			"if [ -f \"$dir/slow-select\" ]; then\n"
			"  sleep 30 &\n"
			"  printf '%s' \"$!\" > \"$dir/selector-child\"\n"
			"  wait\n"
			"fi\n"
			"exit 0\n", -1, nullptr));
		ASSERT_TRUE(g_file_set_contents(options->external_preview.extract,
			"#!/bin/sh\n"
			"dir=$(dirname \"$0\")\n"
			"mkdir \"$dir/active\" || { printf overlap > \"$dir/overlap\"; exit 1; }\n"
			"trap 'rmdir \"$dir/active\"' 0\n"
			"trap 'exit 143' TERM\n"
			"printf 'extract:%s\\n' \"$1\" >> \"$dir/launches\"\n"
			"printf '%s' \"$2\" > \"$dir/output-path\"\n"
			"if [ -f \"$dir/slow-extract\" ]; then\n"
			"  sleep 30 &\n"
			"  printf '%s' \"$!\" > \"$dir/extractor-child\"\n"
			"  wait\n"
			"fi\n"
			"if [ -f \"$dir/fail\" ]; then exit 1; fi\n"
			"if [ -f \"$dir/empty\" ]; then exit 0; fi\n"
			"cp \"$dir/preview.png\" \"$2\" || exit 1\n"
			"if [ -f \"$dir/nonzero\" ]; then exit 1; fi\n", -1, nullptr));
		ASSERT_EQ(g_chmod(options->external_preview.select, 0700), 0);
		ASSERT_EQ(g_chmod(options->external_preview.extract, 0700), 0);
	}

	void TearDown() override
	{
		if (!directory) return;
		if (configured)
			{
			g_free(options->external_preview.select);
			g_free(options->external_preview.extract);
			options->external_preview = saved;
			}
		g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
		if (dir)
			{
			while (const auto *name = g_dir_read_name(dir))
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				if (g_file_test(path, G_FILE_TEST_IS_DIR)) g_rmdir(path);
				else g_remove(path);
				}
			}
		g_rmdir(directory);
		g_free(directory);
	}

	Loader loader(int index)
	{
		auto *fd = file_data_new_simple(paths[index].c_str());
		Loader result(image_loader_new(fd), image_loader_free);
		file_data_unref(fd);
		return result;
	}

	static bool wait_for(const std::function<bool()> &ready, gint64 timeout = 3 * G_TIME_SPAN_SECOND)
	{
		const auto deadline = g_get_monotonic_time() + timeout;
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (ready()) return true;
			g_usleep(1000);
			}
		return ready();
	}

	std::string read(const char *name)
	{
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		g_autofree gchar *contents = nullptr;
		return g_file_get_contents(path, &contents, nullptr, nullptr) ? contents : "";
	}

	void flag(const char *name)
	{
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		ASSERT_TRUE(g_file_set_contents(path, "", 0, nullptr));
	}

	bool exists(const char *name)
	{
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		return g_file_test(path, G_FILE_TEST_EXISTS);
	}

	decltype(options->external_preview) saved{};
	gchar *directory = nullptr;
	std::vector<std::string> paths;
	bool configured = false;
};

TEST_F(ExternalPreview, RapidNavigationOnlyExtractsTheFinalImage)
{
	for (int i = 0; i < 4; ++i)
		{
		auto current = loader(i);
		ASSERT_TRUE(image_loader_start_deferred(current.get()));
		struct Navigation
		{
			Loader *current;
			bool handled = false;
			bool started = false;
		} navigation{&current};
		// Input has priority over deferred loading even if a busy runner wakes late.
		const guint id = g_timeout_add_full(G_PRIORITY_DEFAULT, 20, [](gpointer data) -> gboolean
			{
			auto *navigation = static_cast<Navigation *>(data);
			navigation->started = (*navigation->current)->thread;
			navigation->current->reset();
			navigation->handled = true;
			return G_SOURCE_REMOVE;
			}, &navigation, nullptr);
		const bool handled = wait_for([&]() { return navigation.handled; });
		if (!handled) g_source_remove(id);
		ASSERT_TRUE(handled);
		EXPECT_FALSE(navigation.started);
		}
	EXPECT_FALSE(exists("launches"));
	auto final = loader(4);
	ASSERT_TRUE(image_loader_start_deferred(final.get()));
	ASSERT_TRUE(wait_for([&]() { return image_loader_get_is_done(final.get()); }));
	ASSERT_NE(image_loader_get_pixbuf(final.get()), nullptr);
	const std::string expected = "select:" + paths[4] + "\nextract:" + paths[4] + "\n";
	EXPECT_EQ(read("launches"), expected);
	EXPECT_FALSE(exists("overlap"));
}

TEST_F(ExternalPreview, CancellingExtractionStopsDescendantsAndDeletesTheTemporaryFile)
{
	flag("slow-extract");
	auto current = loader(0);
	ASSERT_TRUE(image_loader_start(current.get()));
	ASSERT_TRUE(wait_for([&]() { return !read("extractor-child").empty(); }));
	const auto child = static_cast<pid_t>(g_ascii_strtoll(read("extractor-child").c_str(), nullptr, 10));
	const auto output = read("output-path");
	const auto start = g_get_monotonic_time();
	current.reset();
	EXPECT_LT(g_get_monotonic_time() - start, G_TIME_SPAN_SECOND);
	EXPECT_FALSE(g_file_test(output.c_str(), G_FILE_TEST_EXISTS));
#ifdef __linux__
	ASSERT_TRUE(wait_for([&]()
		{
		g_autofree gchar *path = g_strdup_printf("/proc/%ld/stat", static_cast<long>(child));
		g_autofree gchar *contents = nullptr;
		if (!g_file_get_contents(path, &contents, nullptr, nullptr)) return true;
		const auto *end = strrchr(contents, ')');
		return end && end[1] == ' ' && end[2] == 'Z';
		}));
#else
	EXPECT_TRUE(wait_for([&]() { return kill(child, 0) != 0; }));
#endif
}

TEST_F(ExternalPreview, CancellingSelectorDoesNotWaitForTheCommand)
{
	flag("slow-select");
	auto current = loader(0);
	ASSERT_TRUE(image_loader_start(current.get()));
	ASSERT_TRUE(wait_for([&]() { return !read("selector-child").empty(); }));
	const auto start = g_get_monotonic_time();
	current.reset();
	EXPECT_LT(g_get_monotonic_time() - start, G_TIME_SPAN_SECOND);
	EXPECT_EQ(read("launches").find("extract:"), std::string::npos);
}

TEST_F(ExternalPreview, CancellingQueuedImageAndThumbnailDoesNotLaunchCommands)
{
	flag("slow-extract");
	auto active = loader(0);
	ASSERT_TRUE(image_loader_start(active.get()));
	ASSERT_TRUE(wait_for([&]() { return !read("extractor-child").empty(); }));
	for (const int priority : {G_PRIORITY_DEFAULT_IDLE, G_PRIORITY_LOW})
		{
		auto queued = loader(1);
		image_loader_set_priority(queued.get(), priority);
		ASSERT_TRUE(image_loader_start(queued.get()));
		wait_for([]() { return false; }, 20 * G_TIME_SPAN_MILLISECOND);
		const auto start = g_get_monotonic_time();
		queued.reset();
		EXPECT_LT(g_get_monotonic_time() - start, G_TIME_SPAN_SECOND);
		}
	EXPECT_EQ(read("launches").find(paths[1]), std::string::npos);
	EXPECT_FALSE(exists("overlap"));
}

TEST_F(ExternalPreview, ConcurrentRequestsAreSerializedAndFailureReportsAnError)
{
	auto first = loader(0);
	auto second = loader(1);
	ASSERT_TRUE(image_loader_start(first.get()));
	ASSERT_TRUE(image_loader_start(second.get()));
	ASSERT_TRUE(wait_for([&]() { return image_loader_get_is_done(first.get()) && image_loader_get_is_done(second.get()); }));
	EXPECT_FALSE(exists("overlap"));
	for (const char *name : {"fail", "empty"})
		{
		flag(name);
		auto failed = loader(2);
		bool error = false;
		g_signal_connect(failed.get(), "error", G_CALLBACK(+[](ImageLoader *, gpointer data)
			{
			*static_cast<bool *>(data) = true;
			}), &error);
		ASSERT_TRUE(image_loader_start(failed.get()));
		ASSERT_TRUE(wait_for([&]() { return error; }));
		EXPECT_EQ(image_loader_get_pixbuf(failed.get()), nullptr);
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		g_remove(path);
		}
}

TEST_F(ExternalPreview, NonmatchingFilesUseTheNativeDecoder)
{
	flag("no-match");
	auto current = loader(0);
	ASSERT_TRUE(image_loader_start(current.get()));
	ASSERT_TRUE(wait_for([&]() { return image_loader_get_is_done(current.get()); }));
	EXPECT_NE(image_loader_get_pixbuf(current.get()), nullptr);
	EXPECT_EQ(read("launches"), "select:" + paths[0] + "\n");
}

TEST_F(ExternalPreview, UsableOutputIsAcceptedOnNonzeroExit)
{
	flag("nonzero");
	auto current = loader(0);
	ASSERT_TRUE(image_loader_start(current.get()));
	ASSERT_TRUE(wait_for([&]() { return image_loader_get_is_done(current.get()); }));
	EXPECT_NE(image_loader_get_pixbuf(current.get()), nullptr);
}

TEST_F(ExternalPreview, PendingLoadKeepsItsOwnCommandSettings)
{
	auto current = loader(0);
	g_free(options->external_preview.select);
	g_free(options->external_preview.extract);
	options->external_preview.select = g_strdup("/nonexistent/changed-selector");
	options->external_preview.extract = g_strdup("/nonexistent/changed-extractor");
	ASSERT_TRUE(image_loader_start_deferred(current.get()));
	ASSERT_TRUE(wait_for([&]() { return image_loader_get_is_done(current.get()); }));
	EXPECT_NE(image_loader_get_pixbuf(current.get()), nullptr);
}

} // namespace
