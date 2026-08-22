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

TEST(PixbufFromCairoSurface, ConvertsPremultipliedArgbToRgba)
{
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 0.25, 0.5, 0.75, 0.5);
	cairo_paint(cr);
	cairo_destroy(cr);

	g_autoptr(GdkPixbuf) pixbuf = pixbuf_from_cairo_surface(surface);
	cairo_surface_destroy(surface);

	ASSERT_NE(pixbuf, nullptr);
	const guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
	EXPECT_NEAR(pixels[0], 64, 1);
	EXPECT_NEAR(pixels[1], 128, 1);
	EXPECT_NEAR(pixels[2], 191, 1);
	EXPECT_NEAR(pixels[3], 128, 1);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
