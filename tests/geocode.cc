/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "misc.h"

#include <glib.h>

#include "gtest/gtest.h"

#include "main.h"

namespace
{

class Geocode : public ::testing::Test
{
protected:
	void SetUp() override
	{
		saved_bindir = gq_bindir;
		gq_bindir = nullptr;
	}

	void TearDown() override
	{
		gq_bindir = saved_bindir;
	}

	gchar *saved_bindir = nullptr;
};

TEST_F(Geocode, DecodesGoogleMapsWithoutInstalledScript)
{
	const gchar *url = "https://www.google.com/maps/place/London+Bridge/@51.5079111,-0.0903079,591m/"
	                   "data=!3m2!1e3!4b1!4m6!3m5!1s0x4876035159bb13c5:0xa61e28267c3563ac!8m2!3d51.5078788!"
	                   "4d-0.0877321!16zL20vMHA3N2c?entry=ttu&g_ep=EgoyMDI2MTAxMy4wIKXMDSoASAFQAw%3D%3D";
	g_autofree gchar *location = decode_geo_parameters(url);
	EXPECT_STREQ(location, "51.5079111 -0.0903079");
}

TEST_F(Geocode, DecodesCoordinateText)
{
	g_autofree gchar *location = decode_geo_parameters("51.508215, -0.086582");
	gchar *longitude = nullptr;
	EXPECT_DOUBLE_EQ(g_ascii_strtod(location, &longitude), 51.508215);
	EXPECT_DOUBLE_EQ(g_ascii_strtod(longitude, nullptr), -0.086582);
}

TEST_F(Geocode, DecodesOpenStreetMap)
{
	g_autofree gchar *location = decode_geo_parameters("https://www.openstreetmap.org/#map=5/18.271/16.084");
	EXPECT_STREQ(location, "18.271 16.084");
}

TEST_F(Geocode, RejectsEmptyInput)
{
	g_autofree gchar *location = decode_geo_parameters("");
	EXPECT_TRUE(g_str_has_prefix(location, "Error:"));
}

} // namespace
