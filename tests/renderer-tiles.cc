/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include <cairo.h>

#include "renderer-tiles.h"
#include "options.h"
#include "pixbuf-renderer.h"

namespace
{

TEST(RendererTilesBirdseye, FinalizeWithActiveOverlay)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *pr = pixbuf_renderer_new();
	g_object_ref_sink(pr);
	g_autoptr(GdkPixbuf) overview = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 160, 120);
	gdk_pixbuf_fill(overview, 0x123456ff);
	pr->birdseye_pixbuf = GDK_PIXBUF(g_object_ref(overview));
	pr->birdseye_overlay = pixbuf_renderer_overlay_add(pr, overview, 0, 0, OVL_RELATIVE);
	ASSERT_NE(pr->birdseye_overlay, -1);

	// Finalization must remove the overview before freeing its renderer.
	g_object_unref(pr);
	EXPECT_EQ(G_OBJECT(overview)->ref_count, 1U);
}

void check_texture_reuse_and_pixels(double display_scale)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	gtk_window_set_default_size(window, 600, 400);
	pr->tile_cache_max = 1;
	pr->zoom_quality = GDK_INTERP_NEAREST;
	pr->zoom_2pass = TRUE;
	gtk_window_set_child(window, GTK_WIDGET(pr));
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 800, 600);
	for (int row = 0; row < 600; ++row)
		{
		for (int col = 0; col < 800; ++col)
			{
			auto *pixel = gdk_pixbuf_get_pixels(image) + (row * gdk_pixbuf_get_rowstride(image)) + (col * 3);
			pixel[0] = col % 256;
			pixel[1] = row % 256;
			pixel[2] = 0x56;
			}
		}
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	gtk_window_present(window);
	auto finish_rendering = [pr]()
		{
		for (int i = 0; i < 1000; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (pr->complete && gtk_widget_get_mapped(GTK_WIDGET(pr))) return true;
			g_usleep(1000);
			}
		return false;
		};
	ASSERT_TRUE(finish_rendering());
	auto viewport_texture = [pr, display_scale]()
		{
		auto *snapshot = gtk_snapshot_new();
		renderer_tiles_snapshot(pr->renderer, snapshot, display_scale);
		auto *node = gtk_snapshot_free_to_node(snapshot);
		if (!node) return static_cast<GdkTexture *>(nullptr);
		std::function<GdkTexture *(GskRenderNode *)> find_texture;
		find_texture = [&find_texture](GskRenderNode *child) -> GdkTexture *
			{
			switch (gsk_render_node_get_node_type(child))
				{
				case GSK_TEXTURE_NODE:
					return gsk_texture_node_get_texture(child);
				case GSK_CLIP_NODE:
					return find_texture(gsk_clip_node_get_child(child));
				case GSK_CONTAINER_NODE:
					for (guint i = 0; i < gsk_container_node_get_n_children(child); ++i)
						{
						if (auto *texture = find_texture(gsk_container_node_get_child(child, i))) return texture;
						}
					return nullptr;
				default:
					return nullptr;
				}
			};
		auto *found = find_texture(node);
		auto *texture = found ? GDK_TEXTURE(g_object_ref(found)) : nullptr;
		gsk_render_node_unref(node);
		return texture;
		};
	g_autoptr(GdkTexture) first = viewport_texture();
	g_autoptr(GdkTexture) second = viewport_texture();
	ASSERT_NE(first, nullptr);
	EXPECT_EQ(first, second);
	g_autoptr(GdkPixbuf) overlay = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 2, 2);
	gdk_pixbuf_fill(overlay, 0xffffffff);
	const int overlay_id = pr->renderer->overlay_add(pr->renderer, overlay, 0, 0, OVL_NORMAL);
	g_autoptr(GdkTexture) with_overlay = viewport_texture();
	EXPECT_EQ(first, with_overlay);
	pr->renderer->overlay_set(pr->renderer, overlay_id, nullptr, 0, 0);
	g_autoptr(GdkTexture) without_overlay = viewport_texture();
	EXPECT_EQ(first, without_overlay);

	// Verify tile placement and viewport clipping against the source pixels.
	// The visible tile footprint exceeds the configured cache limit.
	auto verify_pixels = [pr, &image, display_scale]()
		{
		auto *snapshot = gtk_snapshot_new();
		renderer_tiles_snapshot(pr->renderer, snapshot, display_scale);
		auto *node = gtk_snapshot_free_to_node(snapshot);
		ASSERT_NE(node, nullptr);
		auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		                                          gtk_widget_get_width(GTK_WIDGET(pr)),
		                                          gtk_widget_get_height(GTK_WIDGET(pr)));
		auto *cr = cairo_create(surface);
		gsk_render_node_draw(node, cr);
		cairo_destroy(cr);
		cairo_surface_flush(surface);
		g_autoptr(GdkPixbuf) reference = gdk_pixbuf_scale_simple(image, pr->width, pr->height, pr->zoom_quality);
		for (int row = 0; row < pr->vis_height; row += 29)
			{
			for (int col = 0; col < pr->vis_width; col += 37)
				{
				uint32_t pixel;
				const auto *address = cairo_image_surface_get_data(surface) +
				                      ((row + pr->y_offset) * cairo_image_surface_get_stride(surface)) +
				                      ((col + pr->x_offset) * 4);
				std::memcpy(&pixel, address, sizeof(pixel));
				const auto *source = gdk_pixbuf_get_pixels(reference) +
				                     ((row + pr->y_scroll) * gdk_pixbuf_get_rowstride(reference)) +
				                     ((col + pr->x_scroll) * 3);
				const uint32_t expected = 0xff000000U | (source[0] << 16) | (source[1] << 8) | source[2];
				EXPECT_EQ(pixel, expected) << col << ", " << row;
				}
			}
		cairo_surface_destroy(surface);
		gsk_render_node_unref(node);
		};
	verify_pixels();
	pixbuf_renderer_scroll(pr, 31, 27);
	ASSERT_TRUE(finish_rendering());
	verify_pixels();
	pixbuf_renderer_scroll(pr, -17, -19);
	ASSERT_TRUE(finish_rendering());
	verify_pixels();

	pr->zoom_quality = GDK_INTERP_BILINEAR;
	g_autoptr(GdkTexture) before_zoom = viewport_texture();
	pixbuf_renderer_zoom_set(pr, 2.0);
	// Snapshot before dispatching render work: retain the complete previous image.
	g_autoptr(GdkTexture) pending_zoom = viewport_texture();
	EXPECT_EQ(before_zoom, pending_zoom);
	pixbuf_renderer_zoom_set(pr, 3.0);
	g_autoptr(GdkTexture) repeated_zoom = viewport_texture();
	EXPECT_EQ(before_zoom, repeated_zoom);
	pixbuf_renderer_zoom_set(pr, 2.0);
	ASSERT_TRUE(finish_rendering());
	verify_pixels();
	pixbuf_renderer_zoom_set(pr, -2.0);
	ASSERT_TRUE(finish_rendering());
	verify_pixels();

	gdk_pixbuf_fill(image, 0xabcdefFF);
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) changed = viewport_texture();
	EXPECT_NE(first, changed);
	pixbuf_renderer_zoom_set(pr, 0.0);
	ASSERT_TRUE(finish_rendering());
	verify_pixels();
	// Resize without dispatching tiles: the retained preview must already fit.
	g_signal_emit_by_name(pr, "resize", 500, 350);
	verify_pixels();
	g_signal_emit_by_name(pr, "resize", 550, 375);
	verify_pixels();
	ASSERT_TRUE(finish_rendering());
	verify_pixels();
	pixbuf_renderer_set_pixbuf(pr, nullptr, 1.0);
	g_autoptr(GdkTexture) cleared = viewport_texture();
	EXPECT_EQ(cleared, nullptr);
	gtk_window_destroy(window);
}

TEST(RendererTilesTexture, ReusesTilesAndPreservesPixelsAcrossPanningAndZoom)
{
	check_texture_reuse_and_pixels(1.0);
}

TEST(RendererTilesTexture, FractionalScaleReusesTextureAndPreservesPixelsAcrossPanningAndZoom)
{
	check_texture_reuse_and_pixels(1.25);
}

TEST(RendererTilesTexture, StereoModes)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	gtk_window_set_default_size(window, 600, 400);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 400, 100);
	gdk_pixbuf_fill(image, 0x204060ff);
	g_autoptr(GdkPixbuf) right = gdk_pixbuf_new_subpixbuf(image, 200, 0, 200, 100);
	gdk_pixbuf_fill(right, 0x8090a0ff);
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	pixbuf_renderer_set_stereo_data(pr, STEREO_PIXBUF_SBS);
	gtk_window_present(window);
	for (const int mode : std::initializer_list<int>{PR_STEREO_NONE, PR_STEREO_HORIZ, PR_STEREO_VERT, PR_STEREO_ANAGLYPH_RC, PR_STEREO_HORIZ | PR_STEREO_SWAP})
		{
		SCOPED_TRACE(mode);
		pixbuf_renderer_stereo_set(pr, mode);
		for (int i = 0; i < 300; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
		auto *snapshot = gtk_snapshot_new();
		GTK_WIDGET_GET_CLASS(pr)->snapshot(GTK_WIDGET(pr), snapshot);
		auto *node = gtk_snapshot_free_to_node(snapshot);
		ASSERT_NE(node, nullptr);
		auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 600, 400);
		auto *cr = cairo_create(surface);
		gsk_render_node_draw(node, cr);
		cairo_destroy(cr);
		cairo_surface_flush(surface);
		auto pixel_at = [surface](int x, int y)
			{
			uint32_t pixel;
			std::memcpy(&pixel, cairo_image_surface_get_data(surface) + y * cairo_image_surface_get_stride(surface) + x * 4, 4);
			return pixel;
			};
		const bool swap = mode & PR_STEREO_SWAP;
		EXPECT_EQ(pixel_at(pr->x_offset + 50, pr->y_offset + 50),
		          mode == PR_STEREO_ANAGLYPH_RC ? 0xff804060U : swap ? 0xff8090a0U : 0xff204060U);
		if (pr->renderer2)
			{
			EXPECT_EQ(pixel_at(pr->x_offset + 50 + ((mode & PR_STEREO_HORIZ) ? pr->viewport_width : 0),
			                   pr->y_offset + 50 + ((mode & PR_STEREO_VERT) ? pr->viewport_height : 0)),
			          swap ? 0xff204060U : 0xff8090a0U);
			}
		cairo_surface_destroy(surface);
		gsk_render_node_unref(node);
		}
	gtk_window_destroy(window);
}

TEST(RendererTilesTexture, FractionalDisplayScaleHasNoTileSeams)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	gtk_window_set_default_size(window, 600, 400);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 800, 600);
	for (int y = 0; y < 600; ++y)
		{
		for (int x = 0; x < 800; ++x)
			{
			auto *pixel = gdk_pixbuf_get_pixels(image) + y * gdk_pixbuf_get_rowstride(image) + x * 3;
			pixel[0] = 0x20 + x / 4;
			pixel[1] = 0x40 + y / 4;
			pixel[2] = 0x60;
			}
		}
	g_autoptr(GBytes) bytes = g_bytes_new(gdk_pixbuf_read_pixels(image), gdk_pixbuf_get_byte_length(image));
	g_autoptr(GdkTexture) reference = gdk_memory_texture_new(800, 600, GDK_MEMORY_R8G8B8, bytes, gdk_pixbuf_get_rowstride(image));
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	gtk_window_present(window);
	auto finish_rendering = [pr]()
		{
		for (int i = 0; i < 1000; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (pr->complete && gtk_widget_get_mapped(GTK_WIDGET(pr))) return true;
			g_usleep(1000);
			}
		return false;
		};
	ASSERT_TRUE(finish_rendering());
	auto *renderer = gtk_native_get_renderer(GTK_NATIVE(window));
	for (const double scale : {1.0, 1.25, 1.5, 1.75, 2.0})
		{
		SCOPED_TRACE(scale);
		for (const int scroll : {0, 31, -17})
			{
			SCOPED_TRACE(scroll);
			pixbuf_renderer_scroll(pr, scroll, scroll);
			ASSERT_TRUE(finish_rendering());
			auto *snapshot = gtk_snapshot_new();
			gtk_snapshot_scale(snapshot, scale, scale);
			// An odd widget position puts tile boundaries between device pixels.
			graphene_point_t position{1, 1};
			gtk_snapshot_translate(snapshot, &position);
			renderer_tiles_snapshot(pr->renderer, snapshot, scale);
			auto *node = gtk_snapshot_free_to_node(snapshot);
			ASSERT_NE(node, nullptr);
			graphene_rect_t viewport;
			graphene_rect_init(&viewport, 0, 0, (pr->viewport_width + 2) * scale, (pr->viewport_height + 2) * scale);
			g_autoptr(GdkTexture) texture = gsk_renderer_render_texture(renderer, node, &viewport);
			gsk_render_node_unref(node);
			ASSERT_NE(texture, nullptr);
			// Compare with the same image drawn as one texture, including panning.
			snapshot = gtk_snapshot_new();
			gtk_snapshot_scale(snapshot, scale, scale);
			gtk_snapshot_translate(snapshot, &position);
			graphene_rect_t image_bounds;
			graphene_rect_init(&image_bounds, pr->x_offset - pr->x_scroll, pr->y_offset - pr->y_scroll, 800, 600);
			gtk_snapshot_append_texture(snapshot, reference, &image_bounds);
			node = gtk_snapshot_free_to_node(snapshot);
			g_autoptr(GdkTexture) expected = gsk_renderer_render_texture(renderer, node, &viewport);
			gsk_render_node_unref(node);
			ASSERT_NE(expected, nullptr);
			const int width = gdk_texture_get_width(texture);
			const int height = gdk_texture_get_height(texture);
			std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
			std::vector<unsigned char> expected_pixels(pixels.size());
			gdk_texture_download(texture, pixels.data(), width * 4);
			gdk_texture_download(expected, expected_pixels.data(), width * 4);
			int mismatches = 0;
			for (int y = 10; y < height - 10; ++y)
				{
				for (int x = 10; x < width - 10; ++x)
					{
					const size_t offset = (y * width + x) * 4;
					for (int channel = 0; channel < 4; ++channel)
						{
						if (std::abs(pixels[offset + channel] - expected_pixels[offset + channel]) > 1) ++mismatches;
						}
					}
				}
			EXPECT_EQ(mismatches, 0);
			}
		}
	gtk_window_destroy(window);
}

TEST(RendererTilesTexture, PreservesOpaqueColorsAndOwnsItsPixels)
{
	std::array<unsigned char, 32> pixels = {};
	const uint32_t color = 0x00123456;
	for (int row = 0; row < 2; ++row)
		{
		for (int col = 0; col < 3; ++col)
			{
			std::memcpy(pixels.data() + (row * 16) + (col * 4), &color, sizeof(color));
			}
		}
	auto *surface = cairo_image_surface_create_for_data(pixels.data(), CAIRO_FORMAT_RGB24, 3, 2, 16);
	auto *texture = renderer_tiles_surface_to_texture(surface);
	ASSERT_NE(texture, nullptr);
	EXPECT_EQ(gdk_texture_get_width(texture), 3);
	EXPECT_EQ(gdk_texture_get_height(texture), 2);
	pixels.fill(0);
	cairo_surface_mark_dirty(surface);
	cairo_surface_destroy(surface);
	std::array<unsigned char, 24> downloaded = {};
	gdk_texture_download(texture, downloaded.data(), 12);
	for (int i = 0; i < 6; ++i)
		{
		uint32_t pixel;
		std::memcpy(&pixel, downloaded.data() + (i * 4), sizeof(pixel));
		EXPECT_EQ(pixel, 0xff123456U);
		}
	g_object_unref(texture);
}


} // namespace
