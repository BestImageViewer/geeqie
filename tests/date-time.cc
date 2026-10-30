/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "date-time.h"

#include <cstring>
#include <memory>
#include <string>

#include <config.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#if HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include "gtest/gtest.h"

#include "exif.h"
#include "filedata.h"
#include "options.h"
#include "rcfile.h"

namespace
{

class DateTimeFormat : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved_format = options->date_time_format;
		options->date_time_format = g_strdup("");
		ASSERT_NE(strptime("2024:02:29 02:30:45", "%Y:%m:%d %H:%M:%S", &fields), nullptr);
	}

	void TearDown() override
	{
		g_free(options->date_time_format);
		options->date_time_format = saved_format;
	}

	static void set_format(const char *format)
	{
		g_free(options->date_time_format);
		options->date_time_format = g_strdup(format);
	}

	std::tm fields{};
	gchar *saved_format = nullptr;
};

TEST_F(DateTimeFormat, EmptyUsesLocaleAndInvalidFallsBack)
{
	gchar buffer[512];
	ASSERT_GT(std::strftime(buffer, sizeof(buffer), "%x %X", &fields), 0U);
	g_autofree gchar *expected = g_locale_to_utf8(buffer, -1, nullptr, nullptr, nullptr);
	for (const char *format : {"", "%", "%Q", "%EA", "%Oa", "%-d", "\xff"})
		{
		SCOPED_TRACE(format);
		set_format(format);
		g_autofree gchar *actual = date_time_format(fields);
		EXPECT_STREQ(actual, expected);
		EXPECT_EQ(date_time_format_is_valid(format), !*format);
		}
	EXPECT_TRUE(date_time_format_is_valid(nullptr));
}

TEST_F(DateTimeFormat, CustomFormatPreservesWallClockAndUtf8Literals)
{
	set_format("撮影 %Y-%m-%d · %H:%M:%S %%");
	g_autofree gchar *actual = date_time_format(fields);
	EXPECT_STREQ(actual, "撮影 2024-02-29 · 02:30:45 %");
	g_autofree gchar *explicit_format = date_time_format(fields, "%d/%m/%Y");
	EXPECT_STREQ(explicit_format, "29/02/2024");
	std::string long_format(1024, 'a');
	long_format += "%Y";
	g_autofree gchar *long_result = date_time_format(fields, long_format.c_str());
	EXPECT_EQ(std::string(long_result), std::string(1024, 'a') + "2024");
}

TEST_F(DateTimeFormat, FractionsFollowSecondsAndAreOmittedForDateOnly)
{
	g_autofree gchar *time_first = date_time_format(fields, "%H:%M:%S on %Y-%m-%d", "0123");
	EXPECT_STREQ(time_first, "02:30:45.0123 on 2024-02-29");
	g_autofree gchar *date_only = date_time_format(fields, "%Y-%m-%d", "0123");
	EXPECT_STREQ(date_only, "2024-02-29");
	g_autofree gchar *literal = date_time_format(fields, "%%S", "0123");
	EXPECT_STREQ(literal, "%S");
	g_autofree gchar *locale = date_time_format(fields, "");
	g_autofree gchar *with_fraction = date_time_format(fields, "", "0123");
	g_autofree gchar *expected = g_strconcat(locale, ".0123", nullptr);
	EXPECT_STREQ(with_fraction, expected);
}

TEST_F(DateTimeFormat, FileDatesUseConfiguredFormat)
{
	set_format("%Y-%m-%d %H:%M:%S");
	const time_t timestamp = 1700000000;
	std::tm local{};
	ASSERT_NE(localtime_r(&timestamp, &local), nullptr);
	g_autofree gchar *expected = date_time_format(local);
	EXPECT_STREQ(text_from_time(timestamp), expected);
	g_autofree gchar *direct = date_time_format(timestamp);
	EXPECT_STREQ(direct, expected);
}

TEST_F(DateTimeFormat, ConfigurationEscapesAndRestoresFormat)
{
	const gchar fragment[] = "<gq><global date_time_format='%Y-%m-%d &amp; %H:%M:%S'/></gq>";
	ASSERT_TRUE(load_config_from_buf(fragment, sizeof(fragment) - 1, FALSE));
	EXPECT_STREQ(options->date_time_format, "%Y-%m-%d & %H:%M:%S");
}

#if HAVE_EXIV2
TEST_F(DateTimeFormat, FormattedExifUsesCustomFormatWithoutChangingRawMetadata)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-date-format-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "image.jpg", nullptr);
	struct Cleanup
	{
		const gchar *path;
		const gchar *directory;
		~Cleanup() { g_unlink(path); g_rmdir(directory); }
	} cleanup{path, directory};
	g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 4, 4);
	gdk_pixbuf_fill(pixels, 0x123456ff);
	ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "jpeg", nullptr, nullptr));
	auto image = Exiv2::ImageFactory::open(path);
	image->readMetadata();
	image->exifData()["Exif.Photo.DateTimeOriginal"] = "2024:03:31 02:30:45"; // NOTRANSLATE: EXIF keys and test metadata values.
	image->exifData()["Exif.Photo.SubSecTimeOriginal"] = "0123"; // NOTRANSLATE: EXIF keys and test metadata values.
	image->exifData()["Exif.Photo.DateTimeDigitized"] = "2024:03:31 02:30:46"; // NOTRANSLATE: EXIF keys and test metadata values.
	image->exifData()["Exif.Photo.SubSecTimeDigitized"] = "0567"; // NOTRANSLATE: EXIF keys and test metadata values.
	image->writeMetadata();
	std::unique_ptr<ExifData, decltype(&exif_free)> data(exif_read(path, nullptr, nullptr), exif_free);
	ASSERT_NE(data, nullptr);
	set_format("%Y-%m-%d %H:%M:%S");
	g_autofree gchar *original = exif_get_data_as_text(data.get(), "formatted.DateTime");
	g_autofree gchar *digitized = exif_get_data_as_text(data.get(), "formatted.DateTimeDigitized");
	g_autofree gchar *raw = exif_get_data_as_text(data.get(), "Exif.Photo.DateTimeOriginal"); // NOTRANSLATE: EXIF keys and test metadata values.
	EXPECT_STREQ(original, "2024-03-31 02:30:45.0123");
	EXPECT_STREQ(digitized, "2024-03-31 02:30:46.0567");
	EXPECT_STREQ(raw, "2024:03:31 02:30:45");
	set_format("%Y-%m-%d %H:%M:%S [%z %Z]");
	g_autofree gchar *without_zone = exif_get_data_as_text(data.get(), "formatted.DateTime");
	EXPECT_STREQ(without_zone, "2024-03-31 02:30:45.0123 [ ]");
	image->readMetadata();
	EXPECT_EQ(image->exifData()["Exif.Photo.DateTimeOriginal"].toString(), "2024:03:31 02:30:45"); // NOTRANSLATE: EXIF keys and test metadata values.
}
#endif

} // namespace
