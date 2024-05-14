/*
 * Copyright (C) 2024 The Geeqie Team
 *
 * Author: Omari Stephens
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * Unit tests for pixbuf-util.cc
 *
 */

#include "gtest/gtest.h"

#include "pixbuf-util.h"

namespace {

class ClipRegionTest : public ::testing::Test
{
    protected:
	gint rx{};
	gint ry{};
	gint rw{};
	gint rh{};
};

TEST_F(ClipRegionTest, RegionAContainsRegionB)
{
	ASSERT_TRUE(util_clip_region(0, 0, 1000, 1000,
	                             50, 50, 100, 100,
	                             rx, ry, rw, rh));

	ASSERT_EQ(50, rx);
	ASSERT_EQ(50, ry);
	ASSERT_EQ(100, rw);
	ASSERT_EQ(100, rh);
}

TEST_F(ClipRegionTest, RegionBContainsRegionA)
{
	ASSERT_TRUE(util_clip_region(50, 50, 100, 100,
	                             0, 0, 1000, 1000,
	                             rx, ry, rw, rh));

	ASSERT_EQ(50, rx);
	ASSERT_EQ(50, ry);
	ASSERT_EQ(100, rw);
	ASSERT_EQ(100, rh);
}

TEST_F(ClipRegionTest, PartialOverlapWithBAfterA)
{
	ASSERT_TRUE(util_clip_region(0, 0, 1000, 1000,
	                             500, 500, 1000, 1000,
	                             rx, ry, rw, rh));

	ASSERT_EQ(500, rx);
	ASSERT_EQ(500, ry);
	ASSERT_EQ(500, rw);
	ASSERT_EQ(500, rh);
}

TEST_F(ClipRegionTest, PartialOverlapWithAAfterB)
{
	ASSERT_TRUE(util_clip_region(500, 500, 1000, 1000,
	                             0, 0, 1000, 1000,
	                             rx, ry, rw, rh));

	ASSERT_EQ(500, rx);
	ASSERT_EQ(500, ry);
	ASSERT_EQ(500, rw);
	ASSERT_EQ(500, rh);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
