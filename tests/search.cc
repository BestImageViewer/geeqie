/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "search.h"

#include <initializer_list>

#include "gtest/gtest.h"

namespace {

constexpr double degrees_to_radians = 3.14159265358979323846 / 180.0;

TEST(SearchGps, IncludesOriginAndRadiusBoundary)
{
	const double radius = degrees_to_radians;
	const double limit_squared = radius * radius;
	EXPECT_TRUE(search_gps_within_range(0, 0, 0, 0, limit_squared));
	EXPECT_TRUE(search_gps_within_range(0, 0, 1, 0, limit_squared));
	EXPECT_TRUE(search_gps_within_range(0, 0, 0, 1, limit_squared));
	EXPECT_FALSE(search_gps_within_range(0, 0, 1.01, 0, limit_squared));
	EXPECT_FALSE(search_gps_within_range(0, 0, 0, 1.01, limit_squared));
	EXPECT_FALSE(search_gps_within_range(0, 0, 0.8, 0.8, limit_squared));
}

TEST(SearchGps, WrapsLongitudeAcrossDateLine)
{
	const double radius = degrees_to_radians;
	const double limit_squared = radius * radius;
	EXPECT_TRUE(search_gps_within_range(0, 179.9, 0, -179.9, limit_squared));
	EXPECT_TRUE(search_gps_within_range(0, -179.9, 0, 179.9, limit_squared));
	EXPECT_TRUE(search_gps_within_range(0, 180, 0, -180, 0));
	EXPECT_FALSE(search_gps_within_range(0, 179, 0, -179, limit_squared));
}

TEST(SearchGps, RadiusConversionSupportsAllUnits)
{
	for (const double earth_radius : {6371.0, 3959.0, 3440.0})
		{
		const double angular_radius = 10.0 / earth_radius;
		const double limit_squared = angular_radius * angular_radius;
		const double inside_longitude = 9.0 / earth_radius / degrees_to_radians;
		const double outside_longitude = 11.0 / earth_radius / degrees_to_radians;
		EXPECT_TRUE(search_gps_within_range(0, 0, 0, inside_longitude, limit_squared));
		EXPECT_FALSE(search_gps_within_range(0, 0, 0, outside_longitude, limit_squared));
		}
}

} // namespace
