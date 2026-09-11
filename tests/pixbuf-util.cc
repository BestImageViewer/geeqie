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

#include <pango/pangocairo.h>

#include "gq-color.h"
#include "pixbuf-util.h"

namespace {

TEST(PixbufToCairoSurface, PreservesColorsAndAlpha)
{
	for (bool has_alpha : {false, true})
		{
		g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, has_alpha, 8, 3, 2);
		gdk_pixbuf_fill(pixbuf, 0x4080c080);
		cairo_surface_t *surface = pixbuf_to_cairo_surface(pixbuf);
		ASSERT_NE(surface, nullptr);
		g_autoptr(GdkPixbuf) result = pixbuf_from_cairo_surface(surface);
		cairo_surface_destroy(surface);
		ASSERT_NE(result, nullptr);
		for (gint y = 0; y < 2; y++)
			{
			for (gint x = 0; x < 3; x++)
				{
				const guchar *pixel = gdk_pixbuf_get_pixels(result) + y * gdk_pixbuf_get_rowstride(result) + x * 4;
				EXPECT_NEAR(pixel[0], 64, 1);
				EXPECT_NEAR(pixel[1], 128, 1);
				EXPECT_NEAR(pixel[2], 192, 1);
				EXPECT_EQ(pixel[3], has_alpha ? 128 : 255);
				}
			}
		}
}

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

TEST(PixbufDrawLayout, KeepsAntialiasedTextColorOnTransparentBackground)
{
	PangoContext *context = pango_font_map_create_context(pango_cairo_font_map_get_default());
	g_autoptr(PangoLayout) layout = pango_layout_new(context);
	g_object_unref(context);
	pango_layout_set_markup(layout, "<span font_desc='Sans 16'>Overlay</span>", -1);
	g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 200, 60);
	gdk_pixbuf_fill(pixbuf, 0);

	pixbuf_draw_layout(pixbuf, layout, 2, 2, {255, 255, 255, 128});

	gboolean found_edge = FALSE;
	const guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
	const gint stride = gdk_pixbuf_get_rowstride(pixbuf);
	for (gint y = 0; y < 60; y++)
		{
		for (gint x = 0; x < 200; x++)
			{
			const guchar *pixel = pixels + (y * stride) + (x * 4);
			EXPECT_LE(pixel[3], 128);
			if (pixel[3] > 0 && pixel[3] < 128)
				{
				found_edge = TRUE;
				EXPECT_EQ(pixel[0], 255);
				EXPECT_EQ(pixel[1], 255);
				EXPECT_EQ(pixel[2], 255);
				}
			}
		}
	EXPECT_TRUE(found_edge);
}

}  // anonymous namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
