/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "video-metadata.h"

#include <cmath>
#include <initializer_list>

#include <config.h>

#include <glib.h>

#if HAVE_VIDEO_METADATA
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}
#endif

#include "ui-fileops.h"

namespace
{

bool read_coordinate(const char *&text, double &value)
{
	if (*text != '+' && *text != '-') return false;
	const char *start = text++;
	if (!g_ascii_isdigit(*text)) return false;
	while (g_ascii_isdigit(*text)) ++text;
	if (*text == '.')
		{
		++text;
		if (!g_ascii_isdigit(*text)) return false;
		while (g_ascii_isdigit(*text)) ++text;
		}
	value = g_ascii_strtod(start, nullptr);
	return std::isfinite(value);
}

#if HAVE_VIDEO_METADATA
std::optional<VideoGPS> read_location(AVDictionary *metadata)
{
	for (const char *key : {"com.apple.quicktime.location.ISO6709", "location"})
		{
		auto *entry = av_dict_get(metadata, key, nullptr, 0);
		if (!entry) continue;
		if (auto gps = video_metadata_parse_location(entry->value)) return gps;
		}
	return {};
}
#endif

} // namespace

std::optional<VideoGPS> video_metadata_parse_location(const char *location)
{
	if (!location) return {};
	VideoGPS gps{};
	if (!read_coordinate(location, gps.latitude) || std::abs(gps.latitude) > 90.0 ||
	    !read_coordinate(location, gps.longitude) || std::abs(gps.longitude) > 180.0) return {};

	if (*location == '+' || *location == '-')
		{
		double altitude;
		if (!read_coordinate(location, altitude)) return {};
		}
	// FFmpeg can append a place name after the ISO 6709 terminator.
	if (*location != '\0' && *location != '/') return {};
	return gps;
}

std::optional<VideoGPS> video_metadata_read_gps(const char *path)
{
#if HAVE_VIDEO_METADATA
	g_autofree gchar *path_local = path_from_utf8(path);
	AVFormatContext *context = nullptr;
	AVDictionary *options = nullptr;
	// Only allow local file access while reading metadata.
	av_dict_set(&options, "protocol_whitelist", "file", 0);
	const int result = avformat_open_input(&context, path_local, nullptr, &options);
	av_dict_free(&options);
	if (result < 0) return {};

	// The container header provides location tags without decoding video frames.
	auto gps = read_location(context->metadata);
	for (unsigned int i = 0; !gps && i < context->nb_streams; ++i)
		{
		gps = read_location(context->streams[i]->metadata);
		}
	avformat_close_input(&context);
	return gps;
#else
	(void)path;
	return {};
#endif
}
