/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <unistd.h>

#include <initializer_list>
#include <string>

#include <glib/gstdio.h>

#include "cache.h"
#include "cache-loader.h"
#include "filedata.h"
#include "options.h"
#include "thumb.h"
#include "ui-fileops.h"

namespace
{

class SimCache : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved_size = options->thumbnails.size;
		saved_standard = options->thumbnails.spec_standard;
		saved_local = options->thumbnails.cache_into_dirs;
		saved_caching = options->thumbnails.enable_caching;
		saved_alternate = options->alternate_similarity_algorithm.enabled;
		options->thumbnails.cache_into_dirs = TRUE;
		options->thumbnails.enable_caching = TRUE;
		options->alternate_similarity_algorithm.enabled = FALSE;

		directory = g_dir_make_tmp("geeqie-cache-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		source = g_build_filename(directory, "image.png", nullptr);
		ASSERT_TRUE(g_file_set_contents(source, "", 0, nullptr));
		base = cache_create_location(CacheType::SIM, source);
		path = cache_get_location(CacheType::SIM, source);
	}

	void TearDown() override
	{
		if (path) g_remove(path);
		if (base) g_rmdir(base);
		if (source) g_remove(source);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(base);
		g_free(source);
		g_free(directory);
		options->thumbnails.size = saved_size;
		options->thumbnails.spec_standard = saved_standard;
		options->thumbnails.cache_into_dirs = saved_local;
		options->thumbnails.enable_caching = saved_caching;
		options->alternate_similarity_algorithm.enabled = saved_alternate;
	}

	gchar *directory = nullptr;
	gchar *source = nullptr;
	gchar *base = nullptr;
	gchar *path = nullptr;
	GqSize saved_size;
	gboolean saved_standard;
	gboolean saved_local;
	gboolean saved_caching;
	gboolean saved_alternate;
};

TEST_F(SimCache, PrivateThumbnailsAreCachedAtPreferenceSizeAndReusedForPanView)
{
	options->thumbnails.spec_standard = FALSE;
	options->thumbnails.size = {96, 96};
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 320, 160);
	gdk_pixbuf_fill(image, 0x804020ff);
	ASSERT_TRUE(gdk_pixbuf_save(image, source, "png", nullptr, nullptr));
	g_autofree gchar *thumbnail_path = cache_get_location(CacheType::THUMB, source);

	struct Completion
	{
		bool done = false;
		bool failed = false;
	};
	const auto load = [&](gint size)
		{
		Completion completion;
		auto *loader = thumb_loader_new(size, size);
		thumb_loader_set_reuse_cached_size(loader, TRUE);
		thumb_loader_set_callbacks(loader,
			[](ThumbLoader *, gpointer data) { static_cast<Completion *>(data)->done = true; },
			[](ThumbLoader *, gpointer data)
				{
				auto *result = static_cast<Completion *>(data);
				result->done = true;
				result->failed = true;
				}, nullptr, &completion);
		auto *fd = file_data_new_no_grouping(source);
		fd->format_class = FORMAT_CLASS_IMAGE;
		const bool started = thumb_loader_start(loader, fd);
		const gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
		while (started && !completion.done && g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
		EXPECT_TRUE(started);
		EXPECT_TRUE(completion.done);
		EXPECT_FALSE(completion.failed);
		if (completion.done && !completion.failed)
			{
			EXPECT_EQ(gdk_pixbuf_get_width(fd->thumb_pixbuf), size);
			EXPECT_EQ(gdk_pixbuf_get_height(fd->thumb_pixbuf), size / 2);
			}
		thumb_loader_free(loader);
		file_data_unref(fd);
		};

	load(48);
	g_autoptr(GdkPixbuf) cached = gdk_pixbuf_new_from_file(thumbnail_path, nullptr);
	ASSERT_NE(cached, nullptr);
	EXPECT_EQ(gdk_pixbuf_get_width(cached), 96);
	EXPECT_EQ(gdk_pixbuf_get_height(cached), 48);
	g_autofree gchar *contents = nullptr;
	gsize length = 0;
	ASSERT_TRUE(g_file_get_contents(thumbnail_path, &contents, &length, nullptr));

	// An undecodable source with an unchanged timestamp proves that the cache is reused.
	const time_t original_time = filetime(source);
	ASSERT_TRUE(g_file_set_contents(source, "invalid", -1, nullptr));
	filetime_set(source, original_time);
	load(192);
	g_autofree gchar *reloaded = nullptr;
	gsize reloaded_length = 0;
	ASSERT_TRUE(g_file_get_contents(thumbnail_path, &reloaded, &reloaded_length, nullptr));
	EXPECT_EQ(std::string(contents, length), std::string(reloaded, reloaded_length));
	g_remove(thumbnail_path);
}

TEST_F(SimCache, OriginalDateMigrationPreservesOtherData)
{
	const std::string grid(3072, '\x42');
	const std::string legacy = "SIMcache\nDate=[123456789]\nDimensions=[640 x 480]\n"
	                           "MD5sum=[0123456789abcdef0123456789abcdef]\n"
	                           "SimilarityGrid[32 x 32]=" + grid + "\n";
	ASSERT_TRUE(g_file_set_contents(path, legacy.data(), legacy.size(), nullptr));
	filetime_set(path, filetime(source));

	CacheData cache;
	ASSERT_TRUE(cache.load(source));
	EXPECT_FALSE(cache.date.has_value());
	ASSERT_TRUE(cache.dimensions.has_value());
	EXPECT_EQ(cache.dimensions->width, 640);
	EXPECT_EQ(cache.dimensions->height, 480);
	ASSERT_TRUE(cache.md5sum.has_value());
	ASSERT_NE(cache.similarity, nullptr);
	EXPECT_EQ(cache.similarity->avg_r[0], 0x42);
	EXPECT_EQ(cache.similarity->avg_b[1023], 0x42);

	cache.date = 987654321;
	cache.save(source);
	g_autofree gchar *contents = nullptr;
	gsize length = 0;
	ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
	const std::string updated(contents, length);
	EXPECT_EQ(updated.find("\nDate=["), std::string::npos);
	EXPECT_NE(updated.find("DateTimeOriginal=[987654321]\n"), std::string::npos);
	EXPECT_NE(updated.find("MD5sum=[0123456789abcdef0123456789abcdef]\n"), std::string::npos);
	EXPECT_NE(updated.find(grid), std::string::npos);

	CacheData reloaded;
	ASSERT_TRUE(reloaded.load(source));
	ASSERT_TRUE(reloaded.date.has_value());
	EXPECT_EQ(*reloaded.date, 987654321);
	cache.date = -1;
	cache.save(source);
	CacheData missing;
	ASSERT_TRUE(missing.load(source));
	ASSERT_TRUE(missing.date.has_value());
	EXPECT_EQ(*missing.date, -1);

}

TEST_F(SimCache, PerceptualHashesAreSavedAndRetainedWhenDisabled)
{
	ImageSimilarityData sim;
	sim.filled = true;
	for (guint i = 0; i < sim.avg_r.size(); i++)
		{
		sim.avg_r[i] = (i * 13 + i / 32) % 256;
		sim.avg_g[i] = (i * 17 + i / 32) % 256;
		sim.avg_b[i] = (i * 19 + i / 32) % 256;
		}
	CacheData cache;
	cache.set_similarity(sim);
	cache.set_dimensions({640, 480});
	cache.date = 123456789;
	cache.save(source);
	CacheData standard{source};
	ASSERT_NE(standard.similarity, nullptr);
	EXPECT_FALSE(standard.similarity->perceptual_hashes.has_value());

	options->alternate_similarity_algorithm.enabled = TRUE;
	cache.save(source);
	ASSERT_TRUE(sim.ensure_perceptual_hashes());
	CacheData hashed{source};
	ASSERT_NE(hashed.similarity, nullptr);
	EXPECT_EQ(hashed.similarity->perceptual_hashes, sim.perceptual_hashes);
	EXPECT_EQ(hashed.similarity->avg_r, sim.avg_r);
	EXPECT_EQ(hashed.similarity->avg_g, sim.avg_g);
	EXPECT_EQ(hashed.similarity->avg_b, sim.avg_b);
	EXPECT_EQ(hashed.date, cache.date);
	ASSERT_TRUE(hashed.dimensions.has_value());
	EXPECT_EQ(hashed.dimensions->width, 640);
	EXPECT_EQ(hashed.dimensions->height, 480);

	options->alternate_similarity_algorithm.enabled = FALSE;
	hashed.save(source);
	CacheData retained{source};
	ASSERT_NE(retained.similarity, nullptr);
	EXPECT_EQ(retained.similarity->perceptual_hashes, sim.perceptual_hashes);

	// Accept hashes before the binary grid as well as after it.
	g_autofree gchar *contents = nullptr;
	gsize length = 0;
	ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
	std::string reordered(contents, length);
	const auto hash_start = reordered.find("PerceptualHashV1=[");
	ASSERT_NE(hash_start, std::string::npos);
	const auto hash_length = reordered.find('\n', hash_start) + 1 - hash_start;
	const std::string hash_line = reordered.substr(hash_start, hash_length);
	reordered.erase(hash_start, hash_length);
	reordered.insert(9, hash_line);
	ASSERT_TRUE(g_file_set_contents(path, reordered.data(), reordered.size(), nullptr));
	filetime_set(path, filetime(source));
	CacheData hash_first{source};
	ASSERT_NE(hash_first.similarity, nullptr);
	EXPECT_EQ(hash_first.similarity->perceptual_hashes, sim.perceptual_hashes);
	EXPECT_EQ(hash_first.similarity->avg_r, sim.avg_r);
}

TEST_F(SimCache, MalformedHashesDoNotDiscardTheGrid)
{
	const std::string grid(3072, '\x42');
	for (const std::string hash_line : {
	         "PerceptualHashV1=[0]\n",
	         "PerceptualHashV1=[0\n",
	         "PerceptualHashV1=[zzzzzzzzzzzzzzzz]\n",
	         "PerceptualHashV1=[0000000000000001]\n",
	         "PerceptualHashV2=[0000000000000000]\n"})
		{
		SCOPED_TRACE(hash_line);
		const std::string contents = "SIMcache\n" + hash_line + "SimilarityGrid[32 x 32]=" + grid + "\n";
		ASSERT_TRUE(g_file_set_contents(path, contents.data(), contents.size(), nullptr));
		filetime_set(path, filetime(source));
		CacheData loaded{source};
		ASSERT_NE(loaded.similarity, nullptr);
		EXPECT_TRUE(loaded.similarity->filled);
		EXPECT_FALSE(loaded.similarity->perceptual_hashes.has_value());
		EXPECT_EQ(loaded.similarity->avg_r[0], 0x42);
		EXPECT_EQ(loaded.similarity->avg_b[1023], 0x42);
		}
}

TEST_F(SimCache, CacheLoaderUpgradesGridWithoutDecodingSource)
{
	ImageSimilarityData sim;
	sim.avg_r.fill(66);
	sim.avg_g.fill(77);
	sim.avg_b.fill(88);
	sim.filled = true;
	CacheData cache;
	cache.set_similarity(sim);
	cache.date = 123456789;
	cache.save(source);

	// The source is empty, so successfully adding hashes must reuse the cached grid.
	options->alternate_similarity_algorithm.enabled = TRUE;
	FileData *fd = file_data_new_group(source);
	bool done = false;
	CacheLoader *loader = cache_loader_new(fd, CACHE_LOADER_SIMILARITY,
		[](CacheLoader *, gint, gpointer data) { *static_cast<bool *>(data) = true; }, &done);
	file_data_unref(fd);
	ASSERT_NE(loader, nullptr);
	EXPECT_EQ(loader->il, nullptr);
	const gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (!done && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		g_usleep(1000);
		}
	EXPECT_TRUE(done);
	EXPECT_FALSE(loader->error);
	EXPECT_NE(loader->done_mask & CACHE_LOADER_SIMILARITY, 0);
	cache_loader_free(loader);

	CacheData upgraded{source};
	ASSERT_NE(upgraded.similarity, nullptr);
	ASSERT_TRUE(upgraded.similarity->perceptual_hashes.has_value());
	ASSERT_TRUE(sim.ensure_perceptual_hashes());
	EXPECT_EQ(upgraded.similarity->perceptual_hashes, sim.perceptual_hashes);
	EXPECT_EQ(upgraded.similarity->avg_r, sim.avg_r);
	EXPECT_EQ(upgraded.date, cache.date);
}

TEST_F(SimCache, CacheLoaderCreatesAllHashesForNewImage)
{
	g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
	ASSERT_NE(pixbuf, nullptr);
	guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
	const gint stride = gdk_pixbuf_get_rowstride(pixbuf);
	for (guint y = 0; y < 32; y++)
		{
		for (guint x = 0; x < 32; x++)
			{
			guchar *pixel = pixels + y * stride + x * 3;
			pixel[0] = (x * 13 + y * 3) % 256;
			pixel[1] = (x * 17 + y * 7) % 256;
			pixel[2] = (x * 19 + y * 11) % 256;
			}
		}
	ASSERT_TRUE(gdk_pixbuf_save(pixbuf, source, "png", nullptr, nullptr));
	options->alternate_similarity_algorithm.enabled = TRUE;
	ImageSimilarityData expected{pixbuf};
	FileData *fd = file_data_new_group(source);
	bool done = false;
	CacheLoader *loader = cache_loader_new(fd, CACHE_LOADER_SIMILARITY,
		[](CacheLoader *, gint, gpointer data) { *static_cast<bool *>(data) = true; }, &done);
	file_data_unref(fd);
	ASSERT_NE(loader, nullptr);
	const gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
	while (!done && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		g_usleep(1000);
		}
	EXPECT_TRUE(done);
	EXPECT_FALSE(loader->error);
	cache_loader_free(loader);

	CacheData created{source};
	ASSERT_NE(created.similarity, nullptr);
	ASSERT_TRUE(created.similarity->perceptual_hashes.has_value());
	EXPECT_EQ(created.similarity->perceptual_hashes, expected.perceptual_hashes);
	EXPECT_EQ(created.similarity->avg_r, expected.avg_r);
	EXPECT_EQ(created.similarity->avg_g, expected.avg_g);
	EXPECT_EQ(created.similarity->avg_b, expected.avg_b);
}

TEST(ThumbnailCacheSource, ExcludesAllCacheLocationsWithDirectoryBoundaries)
{
	for (const gchar *root : {get_thumbnails_cache_dir(), get_thumbnails_standard_cache_dir()})
		{
		g_autofree gchar *source = g_build_filename(root, "nested", "image.png", nullptr);
		EXPECT_TRUE(cache_source_is_thumbnail(source));
		EXPECT_EQ(cache_create_location(CacheType::THUMB, source), nullptr);
		EXPECT_EQ(cache_create_location(CacheType::SIM, source), nullptr);
		g_autofree gchar *neighbor = g_strconcat(root, "-images/image.png", nullptr);
		EXPECT_FALSE(cache_source_is_thumbnail(neighbor));
		}
	EXPECT_TRUE(cache_source_is_thumbnail("/photos/.thumbnails/image.png"));
	EXPECT_TRUE(cache_source_is_thumbnail("/photos/.thumbnails/nested/image.png"));
	EXPECT_TRUE(cache_source_is_thumbnail("/photos/.thumblocal/normal/image.png"));
	EXPECT_FALSE(cache_source_is_thumbnail("/photos/.thumbnails-extra/image.png"));
	EXPECT_FALSE(cache_source_is_thumbnail("/photos/image.png"));
}

TEST_F(SimCache, DoesNotSaveSimilarityForLocalCacheSourcesOrAliases)
{
	g_autofree gchar *cached_source = g_build_filename(base, "cached.png", nullptr);
	ASSERT_TRUE(g_file_set_contents(cached_source, "", 0, nullptr));
	g_autofree gchar *alias = g_build_filename(directory, "alias.png", nullptr);
	ASSERT_EQ(symlink(cached_source, alias), 0);
	EXPECT_TRUE(cache_source_is_thumbnail(alias));

	CacheData data;
	data.set_dimensions({32, 32});
	data.save(cached_source);
	data.save(alias);
	g_autofree gchar *nested_cache = g_build_filename(base, ".thumbnails", nullptr);
	g_autofree gchar *alias_cache = cache_get_location(CacheType::SIM, alias);
	EXPECT_FALSE(isdir(nested_cache));
	EXPECT_FALSE(isfile(alias_cache));
	g_remove(alias);
	g_remove(cached_source);
}

TEST_F(SimCache, StandardThumbnailsKeepSimilarityDataInGeeqieCache)
{
	options->thumbnails.spec_standard = TRUE;
	options->thumbnails.cache_into_dirs = FALSE;
	g_autofree gchar *sim_path = cache_get_location(CacheType::SIM, source);
	g_autofree gchar *prefix = g_strconcat(get_thumbnails_cache_dir(), G_DIR_SEPARATOR_S, nullptr);
	EXPECT_TRUE(g_str_has_prefix(sim_path, prefix));
}

} // namespace
