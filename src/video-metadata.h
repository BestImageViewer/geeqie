/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef VIDEO_METADATA_H
#define VIDEO_METADATA_H

#include <optional>

struct VideoGPS
{
	double latitude;
	double longitude;
};

/** @brief Parse the signed decimal-degree ISO 6709 location used by phone videos. */
std::optional<VideoGPS> video_metadata_parse_location(const char *location);
std::optional<VideoGPS> video_metadata_read_gps(const char *path);

#endif /* VIDEO_METADATA_H */
