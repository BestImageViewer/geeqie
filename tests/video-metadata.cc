/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>

#if HAVE_VIDEO_METADATA
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}
#endif

#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "metadata.h"
#include "options.h"
#include "video-metadata.h"

namespace
{

TEST(VideoMetadata, ParsePhoneLocations)
{
	struct Location
	{
		const char *text;
		double latitude;
		double longitude;
	};
	const Location locations[] = {
		{"+54.123456-004.765432/", 54.123456, -4.765432},
		{"-33.865143+151.209900+012.345/", -33.865143, 151.209900},
		{"+00.0000+000.0000/", 0.0, 0.0},
		{"-90-180", -90.0, -180.0},
		{"+90+180+0/Place", 90.0, 180.0},
		{"+51.5-000.1-12.5/", 51.5, -0.1}
	};
	for (const auto &location : locations)
		{
		SCOPED_TRACE(location.text);
		const auto gps = video_metadata_parse_location(location.text);
		ASSERT_TRUE(gps);
		EXPECT_NEAR(gps->latitude, location.latitude, 1e-9);
		EXPECT_NEAR(gps->longitude, location.longitude, 1e-9);
		}
}

TEST(VideoMetadata, RejectMalformedLocations)
{
	EXPECT_FALSE(video_metadata_parse_location(nullptr));
	for (const char *location : {"", "London", "54.1-004.2/", "+54.1", "+91+0/", "+0-181/",
	                             "+nan+0/", "+0+inf/", "+1e1+2/", "+1.+2/", "+1+2garbage",
	                             "+1+2+/", "+1+2+nan/", "+1 +2/", "+1+2.3.4/"})
		{
		EXPECT_FALSE(video_metadata_parse_location(location)) << location;
		}
}

#if HAVE_VIDEO_METADATA
class VideoMetadataFile : public ::testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		filter_add_defaults();
		filter_rebuild();
		directory = g_dir_make_tmp("geeqie-video-metadata-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "video.mov", nullptr);
		sidecar = g_build_filename(directory, "video.xmp", nullptr);
	}

	void make_video(const char *key, const char *location, bool apple = false)
	{
		ASSERT_GE(avformat_alloc_output_context2(&output, nullptr, "mov", path), 0);
		ASSERT_NE(output, nullptr);
		auto *stream = avformat_new_stream(output, nullptr);
		ASSERT_NE(stream, nullptr);
		stream->time_base = {1, 25};
		stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
		stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
		stream->codecpar->format = AV_PIX_FMT_RGB24;
		stream->codecpar->width = 2;
		stream->codecpar->height = 2;
		if (key)
			{
			ASSERT_GE(av_dict_set(&output->metadata, key, location, 0), 0);
			}
		ASSERT_GE(avio_open(&output->pb, path, AVIO_FLAG_WRITE), 0);
		AVDictionary *mux_options = nullptr;
		if (apple) av_dict_set(&mux_options, "movflags", "use_metadata_tags", 0);
		const int result = avformat_write_header(output, &mux_options);
		av_dict_free(&mux_options);
		ASSERT_GE(result, 0);
		unsigned char pixels[12] = {};
		AVPacket packet{};
		packet.data = pixels;
		packet.size = sizeof(pixels);
		packet.duration = 1;
		packet.flags = AV_PKT_FLAG_KEY;
		ASSERT_GE(av_write_frame(output, &packet), 0);
		ASSERT_GE(av_write_trailer(output), 0);
		ASSERT_GE(avio_closep(&output->pb), 0);
		avformat_free_context(output);
		output = nullptr;
	}

	void expect_map_coordinates(double latitude, double longitude)
	{
		FileData *fd = file_data_new_simple(path);
		ASSERT_NE(fd, nullptr);
		EXPECT_EQ(fd->format_class, FORMAT_CLASS_VIDEO);
		const auto lat = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude");
		const auto lon = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude");
		file_data_unref(fd);
		ASSERT_TRUE(lat);
		ASSERT_TRUE(lon);
		EXPECT_NEAR(*lat, latitude, 1e-6);
		EXPECT_NEAR(*lon, longitude, 1e-6);
	}

	void TearDown() override
	{
		if (output)
			{
			if (output->pb) avio_closep(&output->pb);
			avformat_free_context(output);
			}
		if (path) g_unlink(path);
		if (sidecar) g_unlink(sidecar);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(sidecar);
		g_free(directory);
	}

	gchar *directory = nullptr;
	gchar *path = nullptr;
	gchar *sidecar = nullptr;
	AVFormatContext *output = nullptr;
};

TEST_F(VideoMetadataFile, QuickTimeLocationReachesMap)
{
	make_video("location", "+54.123456-004.765432/");
	expect_map_coordinates(54.123456, -4.765432);
}

TEST_F(VideoMetadataFile, AppleLocationReachesMap)
{
	make_video("com.apple.quicktime.location.ISO6709", "-33.865143+151.209900+012.345/", true);
	expect_map_coordinates(-33.865143, 151.209900);
}

TEST_F(VideoMetadataFile, ZeroCoordinatesReachesMap)
{
	make_video("location", "+00.0000+000.0000/");
	expect_map_coordinates(0.0, 0.0);
}

TEST_F(VideoMetadataFile, MissingLocationHasNoCoordinates)
{
	make_video(nullptr, nullptr);
	EXPECT_FALSE(video_metadata_read_gps(path));
	FileData *fd = file_data_new_simple(path);
	EXPECT_FALSE(metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude"));
	EXPECT_FALSE(metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude"));
	file_data_unref(fd);
}

TEST_F(VideoMetadataFile, MalformedLocationHasNoCoordinates)
{
	make_video("com.apple.quicktime.location.ISO6709", "+99.9+004.5/", true);
	EXPECT_FALSE(video_metadata_read_gps(path));
}

TEST_F(VideoMetadataFile, InvalidFileHasNoCoordinates)
{
	ASSERT_TRUE(g_file_set_contents(path, "not a video", -1, nullptr));
	EXPECT_FALSE(video_metadata_read_gps(path));
}

TEST_F(VideoMetadataFile, SidecarCoordinatesOverrideVideo)
{
	make_video("location", "+54.123456-004.765432/");
	const char *xmp = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
	                  "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
	                  "<rdf:Description rdf:about='' xmlns:exif='http://ns.adobe.com/exif/1.0/' "
	                  "exif:GPSLatitude='12,30S' exif:GPSLongitude='45,15E'/>"
	                  "</rdf:RDF></x:xmpmeta>";
	ASSERT_TRUE(g_file_set_contents(sidecar, xmp, -1, nullptr));
	ExifData *exif = exif_read(path, sidecar, nullptr);
	ASSERT_NE(exif, nullptr);
	GList *lat = exif_get_metadata(exif, "Xmp.exif.GPSLatitude", METADATA_PLAIN);
	GList *lon = exif_get_metadata(exif, "Xmp.exif.GPSLongitude", METADATA_PLAIN);
	ASSERT_NE(lat, nullptr);
	ASSERT_NE(lon, nullptr);
	EXPECT_STREQ(static_cast<const char *>(lat->data), "12,30S");
	EXPECT_STREQ(static_cast<const char *>(lon->data), "45,15E");
	g_list_free_full(lat, g_free);
	g_list_free_full(lon, g_free);
	exif_free(exif);
}

TEST_F(VideoMetadataFile, PendingEditsOverrideVideo)
{
	make_video("location", "+54.123456-004.765432/");
	g_autoptr(GHashTable) modified = g_hash_table_new(g_str_hash, g_str_equal);
	GList latitude{const_cast<char *>("12,30S"), nullptr, nullptr};
	g_hash_table_insert(modified, const_cast<char *>("Xmp.exif.GPSLatitude"), &latitude);
	ExifData *exif = exif_read(path, nullptr, modified);
	ASSERT_NE(exif, nullptr);
	GList *lat = exif_get_metadata(exif, "Xmp.exif.GPSLatitude", METADATA_PLAIN);
	ASSERT_NE(lat, nullptr);
	EXPECT_STREQ(static_cast<const char *>(lat->data), "12,30S");
	g_list_free_full(lat, g_free);
	exif_free(exif);
}

TEST_F(VideoMetadataFile, SidecarWithoutGPSKeepsVideoCoordinates)
{
	make_video("location", "+54.123456-004.765432/");
	const char *xmp = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
	                  "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
	                  "<rdf:Description rdf:about=''/></rdf:RDF></x:xmpmeta>";
	ASSERT_TRUE(g_file_set_contents(sidecar, xmp, -1, nullptr));
	ExifData *embedded = exif_read(path, nullptr, nullptr);
	ExifData *combined = exif_read(path, sidecar, nullptr);
	ASSERT_NE(embedded, nullptr);
	ASSERT_NE(combined, nullptr);
	for (const char *key : {"Xmp.exif.GPSLatitude", "Xmp.exif.GPSLongitude"})
		{
		GList *original = exif_get_metadata(embedded, key, METADATA_PLAIN);
		GList *merged = exif_get_metadata(combined, key, METADATA_PLAIN);
		ASSERT_NE(original, nullptr);
		ASSERT_NE(merged, nullptr);
		EXPECT_STREQ(static_cast<const char *>(original->data), static_cast<const char *>(merged->data));
		g_list_free_full(original, g_free);
		g_list_free_full(merged, g_free);
		}
	exif_free(embedded);
	exif_free(combined);
}
#endif

} // namespace
