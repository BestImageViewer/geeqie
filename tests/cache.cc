/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <string>

#include <glib/gstdio.h>

#include "cache.h"
#include "options.h"
#include "ui-fileops.h"

namespace
{

TEST(SimCache, OriginalDateMigrationPreservesOtherData)
{
	if (!options) options = conf_options_new();
	const gboolean saved_local = options->thumbnails.cache_into_dirs;
	options->thumbnails.cache_into_dirs = TRUE;

	g_autofree gchar *directory = g_dir_make_tmp("geeqie-cache-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *source = g_build_filename(directory, "image.jpg", nullptr);
	ASSERT_TRUE(g_file_set_contents(source, "", 0, nullptr));
	g_autofree gchar *base = cache_create_location(CacheType::SIM, source);
	g_autofree gchar *path = cache_get_location(CacheType::SIM, source);
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

	g_remove(path);
	g_rmdir(base);
	g_remove(source);
	g_rmdir(directory);
	options->thumbnails.cache_into_dirs = saved_local;
}

} // namespace
