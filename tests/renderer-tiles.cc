/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include <sys/mman.h>

#include <cairo.h>

#include "renderer-tiles.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"

namespace
{

TEST(RendererTiles, TransferPreservesViewedCenterAcrossViewportAllocation)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2000, 1600);
	gdk_pixbuf_fill(image, 0x123456ff);
	for (const bool move : {false, true})
		{
		SCOPED_TRACE(move);
		for (const double center : {0.2, 0.5, 0.8})
			{
			SCOPED_TRACE(center);
			auto *source = pixbuf_renderer_new();
			auto *destination = pixbuf_renderer_new();
			g_object_ref_sink(source);
			g_object_ref_sink(destination);
			g_signal_emit_by_name(source, "resize", 400, 300);
			pixbuf_renderer_set_pixbuf(source, image, 2.0);
			pixbuf_renderer_set_scroll_center(source, center, center);
			if (move) pixbuf_renderer_move(destination, source);
			else pixbuf_renderer_copy(destination, source);
			g_signal_emit_by_name(destination, "resize", 1000, 700);
			EXPECT_NEAR(destination->norm_center_x, center, 0.001);
			EXPECT_NEAR(destination->norm_center_y, center, 0.001);
			EXPECT_NEAR(destination->x_scroll, center * 4000 - 500, 1);
			EXPECT_NEAR(destination->y_scroll, center * 3200 - 350, 1);
			// Leaving fullscreen transfers back to an already allocated renderer.
			if (move) pixbuf_renderer_move(source, destination);
			else pixbuf_renderer_copy(source, destination);
			EXPECT_NEAR(source->norm_center_x, center, 0.001);
			EXPECT_NEAR(source->norm_center_y, center, 0.001);
			g_object_unref(destination);
			g_object_unref(source);
			}
		}
}

TEST(RendererTiles, SourceOffsetBeyondTwoGiB)
{
	if (sizeof(gsize) < 8) GTEST_SKIP() << "Requires a 64-bit address space";
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();

	constexpr int width = 39137;
	constexpr int height = 22279;
	constexpr int rowstride = (width * 3 + 3) & ~3;
	constexpr gsize bytes = static_cast<gsize>(rowstride) * height;
	// Anonymous pages reserve address space without allocating the whole image in RAM.
	auto *pixels = static_cast<guchar *>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
	if (pixels == MAP_FAILED) GTEST_SKIP() << "Cannot reserve image address space";
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, FALSE, 8,
	                                                   width, height, rowstride,
	                                                   [](guchar *data, gpointer) { munmap(data, bytes); }, nullptr);
	ASSERT_NE(image, nullptr);

	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	gtk_window_set_default_size(window, 400, 300);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	gtk_window_present(window);
	const auto finish_rendering = [pr]()
		{
		for (int i = 0; i < 1000; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (pr->complete && gtk_widget_get_mapped(GTK_WIDGET(pr))) return true;
			g_usleep(1000);
			}
		return false;
		};
	EXPECT_TRUE(finish_rendering());
	pixbuf_renderer_scroll(pr, width, height);
	EXPECT_GT(static_cast<gsize>(pr->y_scroll) * rowstride, static_cast<gsize>(G_MAXINT));
	EXPECT_TRUE(finish_rendering());
	gtk_window_destroy(window);
}

TEST(RendererTiles, OverscrollLimitsAndBackground)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	gtk_window_set_default_size(window, 400, 300);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	GdkRGBA background = {0, 0, 1, 1};
	pixbuf_renderer_set_color(pr, background);
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 800, 600);
	gdk_pixbuf_fill(image, 0xff0000ff);
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	gtk_window_present(window);
	const auto settle = [pr]()
		{
		for (int i = 0; i < 100; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
		return gtk_widget_get_mapped(GTK_WIDGET(pr));
		};
	ASSERT_TRUE(settle());
	for (const double zoom : {1.0, -4.0, 0.0})
		{
		SCOPED_TRACE(zoom);
		pixbuf_renderer_set_allow_overscroll(pr, TRUE);
		pixbuf_renderer_zoom_set(pr, zoom);
		for (const int direction : {-1, 1})
			{
			SCOPED_TRACE(direction);
			pixbuf_renderer_scroll(pr, direction * 10000, direction * 10000);
			ASSERT_TRUE(settle());
			EXPECT_EQ(pr->x_scroll, (direction < 0 ? 0 : pr->width) - (pr->viewport_width / 2));
			EXPECT_EQ(pr->y_scroll, (direction < 0 ? 0 : pr->height) - (pr->viewport_height / 2));
			for (const double display_scale : {1.0, 1.25})
				{
				auto *snapshot = gtk_snapshot_new();
				renderer_tiles_snapshot(pr->renderer, snapshot, display_scale);
				auto *node = gtk_snapshot_free_to_node(snapshot);
				ASSERT_NE(node, nullptr);
				auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pr->viewport_width, pr->viewport_height);
				auto *cr = cairo_create(surface);
				gsk_render_node_draw(node, cr);
				cairo_destroy(cr);
				cairo_surface_flush(surface);
				const int center_x = pr->viewport_width / 2;
				const int center_y = pr->viewport_height / 2;
				const int inside = direction < 0 ? 5 : -5;
				const auto pixel = [surface](int x, int y)
					{
					return *reinterpret_cast<guint32 *>(cairo_image_surface_get_data(surface) + (y * cairo_image_surface_get_stride(surface)) + (x * 4));
					};
				EXPECT_EQ(pixel(center_x + inside, center_y + inside), 0xffff0000U);
				EXPECT_EQ(pixel(center_x - inside, center_y - inside), 0xff0000ffU);
				cairo_surface_destroy(surface);
				gsk_render_node_unref(node);
				}
			}
		pixbuf_renderer_set_allow_overscroll(pr, FALSE);
		EXPECT_GE(pr->x_scroll, 0);
		EXPECT_GE(pr->y_scroll, 0);
		EXPECT_LE(pr->x_scroll, pr->width - pr->vis_width);
		EXPECT_LE(pr->y_scroll, pr->height - pr->vis_height);
		}
	gtk_window_destroy(window);
}

TEST(RendererTiles, HighZoomNearestMatchesExplicitQualityAcrossThreshold)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	gtk_window_set_default_size(window, 240, 180);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	gtk_window_present(window);
	const auto capture = [pr]()
		{
		for (int i = 0; i < 100; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
		auto *snapshot = gtk_snapshot_new();
		renderer_tiles_snapshot(pr->renderer, snapshot, 1.0);
		auto *node = gtk_snapshot_free_to_node(snapshot);
		auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pr->viewport_width, pr->viewport_height);
		auto *cr = cairo_create(surface);
		if (node) gsk_render_node_draw(node, cr);
		cairo_destroy(cr);
		cairo_surface_flush(surface);
		auto *bytes = cairo_image_surface_get_data(surface);
		std::vector<guchar> pixels(bytes, bytes + (cairo_image_surface_get_stride(surface) * pr->viewport_height));
		cairo_surface_destroy(surface);
		if (node) gsk_render_node_unref(node);
		return pixels;
		};
	EXPECT_FALSE(pr->nearest_above_200);
	for (const bool alpha : {false, true})
		{
		g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alpha, 8, 32, 24);
		for (int y = 0; y < 24; ++y)
			for (int x = 0; x < 32; ++x)
				{
				auto *pixel = gdk_pixbuf_get_pixels(image) + (y * gdk_pixbuf_get_rowstride(image)) + (x * gdk_pixbuf_get_n_channels(image));
				pixel[0] = (x % 2) ? 255 : 0;
				pixel[1] = (y % 2) ? 255 : 0;
				pixel[2] = 128;
				if (alpha) pixel[3] = 128;
				}
		for (const int orientation : {1, 6})
			{
			pixbuf_renderer_set_pixbuf(pr, image, 1.0);
			pixbuf_renderer_set_orientation(pr, orientation);
			for (const double zoom : {3.0, 2.0, 1.5, 2.5, 1.0, -2.0})
				{
				SCOPED_TRACE(zoom);
				SCOPED_TRACE(orientation);
				SCOPED_TRACE(alpha);
				pr->zoom_quality = GDK_INTERP_BILINEAR;
				pixbuf_renderer_set_nearest_above_200(pr, TRUE);
				pixbuf_renderer_zoom_set(pr, zoom);
				const auto automatic = capture();
				EXPECT_EQ(pr->zoom_quality, GDK_INTERP_BILINEAR);
				pr->zoom_quality = zoom > 2.0 ? GDK_INTERP_NEAREST : GDK_INTERP_BILINEAR;
				pixbuf_renderer_set_nearest_above_200(pr, FALSE);
				EXPECT_EQ(automatic, capture());
				}
			}
		}
	gtk_window_destroy(window);
}

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

GdkTexture *find_scaled_texture(GskRenderNode *node, GskScalingFilter *filter)
{
	switch (gsk_render_node_get_node_type(node))
		{
		case GSK_TEXTURE_SCALE_NODE:
			if (filter) *filter = gsk_texture_scale_node_get_filter(node);
			return gsk_texture_scale_node_get_texture(node);
		case GSK_CLIP_NODE:
			return find_scaled_texture(gsk_clip_node_get_child(node), filter);
		case GSK_CONTAINER_NODE:
			for (guint i = 0; i < gsk_container_node_get_n_children(node); ++i)
				{
				if (auto *texture = find_scaled_texture(gsk_container_node_get_child(node, i), filter)) return texture;
				}
			break;
		default:
			break;
		}
	return nullptr;
}

TEST(RendererTilesTexture, ActualSizeUsesPhysicalPixels)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	pr->zoom_quality = GDK_INTERP_NEAREST;
	gtk_window_set_default_size(window, 600, 400);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 320, 240);
	const int rowstride = gdk_pixbuf_get_rowstride(image);
	auto *pixels = gdk_pixbuf_get_pixels(image);
	for (int y = 0; y < 240; ++y)
		{
		for (int x = 0; x < 320; ++x)
			{
			const guchar color = (x % 2) ? 255 : 0;
			std::fill_n(pixels + y * rowstride + x * 3, 3, color);
			}
		}
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	gtk_window_present(window);
	for (int orientation = 1; orientation <= 8; ++orientation)
		{
		SCOPED_TRACE(orientation);
		g_autoptr(GdkPixbuf) alpha = gdk_pixbuf_add_alpha(image, FALSE, 0, 0, 0);
		pixbuf_renderer_set_pixbuf(pr, orientation % 2 ? image : alpha, 1.0);
		pixbuf_renderer_set_orientation(pr, orientation);
		for (int i = 0; i < 1000; ++i)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (pr->complete && gtk_widget_get_mapped(GTK_WIDGET(pr))) break;
			g_usleep(1000);
			}
		const double scale = gdk_surface_get_scale(gtk_native_get_surface(GTK_NATIVE(window)));
		EXPECT_NEAR(pr->width * scale, orientation < 5 ? 320 : 240, scale);
		EXPECT_NEAR(pr->height * scale, orientation < 5 ? 240 : 320, scale);
		EXPECT_DOUBLE_EQ(pixbuf_renderer_zoom_get_scale(pr), 1.0);
		auto *snapshot = gtk_snapshot_new();
		gtk_snapshot_scale(snapshot, scale, scale);
		renderer_tiles_snapshot(pr->renderer, snapshot, scale);
		g_autoptr(GskRenderNode) node = gtk_snapshot_free_to_node(snapshot);
		graphene_rect_t viewport;
		graphene_rect_init(&viewport, 0, 0, pr->viewport_width * scale, pr->viewport_height * scale);
		g_autoptr(GdkTexture) rendered = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(window)), node, &viewport);
		ASSERT_NE(rendered, nullptr);
		const int stride = gdk_texture_get_width(rendered);
		std::vector<uint32_t> output(stride * gdk_texture_get_height(rendered));
		gdk_texture_download(rendered, reinterpret_cast<guchar *>(output.data()), stride * 4);
		if (scale == std::floor(scale))
			{
			const int left = pr->x_offset * scale;
			const int top = pr->y_offset * scale;
			g_autoptr(GdkPixbuf) reference = pixbuf_apply_orientation(image, orientation);
			const auto *row = gdk_pixbuf_get_pixels(reference) + 50 * gdk_pixbuf_get_rowstride(reference);
			for (int x = 8; x < gdk_pixbuf_get_width(reference) - 8; ++x)
				{
				EXPECT_EQ(output[(top + 50) * stride + left + x], row[x * 3] ? 0xffffffffU : 0xff000000U) << x;
				}
			}
		}
	for (double zoom : {2.0, -2.0, 0.0})
		{
		pixbuf_renderer_zoom_set(pr, zoom);
		const double expected = zoom == 2.0 ? 2.0 : zoom == -2.0 ? 0.5 : 1.0;
		EXPECT_DOUBLE_EQ(pixbuf_renderer_zoom_get_scale(pr), expected);
		}
	gtk_window_destroy(window);
}

TEST(RendererTilesTexture, HighDpiMagnificationDisplaysTheWholeImage)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	pr->zoom_quality = GDK_INTERP_BILINEAR;
	gtk_window_set_default_size(window, 600, 400);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 320, 240);
	gdk_pixbuf_fill(image, 0x123456ff);
	pixbuf_renderer_set_pixbuf(pr, image, 2.0);
	gtk_window_present(window);
	for (int i = 0; i < 1000; ++i)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		if (pr->complete && gtk_widget_get_mapped(GTK_WIDGET(pr))) break;
		g_usleep(1000);
		}
	EXPECT_TRUE(pr->complete);
	auto *surface = gtk_native_get_surface(GTK_NATIVE(window));
	if (gdk_surface_get_scale(surface) == 1.0)
		{
		gtk_window_destroy(window);
		GTEST_SKIP() << "Requires a high-DPI display (for example GDK_SCALE=2)";
		}
	auto *snapshot = gtk_snapshot_new();
	renderer_tiles_snapshot(pr->renderer, snapshot, gdk_surface_get_scale(surface));
	g_autoptr(GskRenderNode) node = gtk_snapshot_free_to_node(snapshot);
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(find_scaled_texture(node, nullptr), nullptr);
	graphene_rect_t viewport;
	graphene_rect_init(&viewport, 0, 0, pr->viewport_width, pr->viewport_height);
	g_autoptr(GdkTexture) rendered = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(window)), node, &viewport);
	ASSERT_NE(rendered, nullptr);
	const int stride = gdk_texture_get_width(rendered);
	std::vector<uint32_t> output(stride * gdk_texture_get_height(rendered));
	gdk_texture_download(rendered, reinterpret_cast<guchar *>(output.data()), stride * 4);
	for (int y = 1; y < pr->vis_height - 1; y += 29)
		{
		for (int x = 1; x < pr->vis_width - 1; x += 37)
			{
			EXPECT_EQ(output[((y + pr->y_offset) * stride) + x + pr->x_offset], 0xff123456U);
			}
		}
	gtk_window_destroy(window);
}

TEST(RendererTilesTexture, MagnificationReusesSourceAndInvalidatesChangedPixels)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *pr = pixbuf_renderer_new();
	pr->tile_cache_max = 8;
	gtk_window_set_default_size(window, 300, 200);
	gtk_window_set_child(window, GTK_WIDGET(pr));
	g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 800, 600);
	for (int y = 0; y < 600; ++y)
		{
		for (int x = 0; x < 800; ++x)
			{
			auto *pixel = gdk_pixbuf_get_pixels(image) + y * gdk_pixbuf_get_rowstride(image) + x * 3;
			pixel[0] = 0x20 + x % 128;
			pixel[1] = 0x40 + y % 128;
			pixel[2] = 0x60;
			}
		}
	pixbuf_renderer_set_pixbuf(pr, image, 1.0);
	gtk_window_present(window);
	const auto finish_rendering = [pr]()
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
	if (GSK_IS_CAIRO_RENDERER(gtk_native_get_renderer(GTK_NATIVE(window))) ||
	    gdk_surface_get_scale(gtk_native_get_surface(GTK_NATIVE(window))) != 1.0)
		{
		gtk_window_destroy(window);
		GTEST_SKIP() << "Requires a GL or Vulkan renderer at 100% display scaling";
		}
	const auto source_texture = [pr](GskScalingFilter *filter = nullptr)
	{
		auto *snapshot = gtk_snapshot_new();
		renderer_tiles_snapshot(pr->renderer, snapshot, 1.0);
		auto *node = gtk_snapshot_free_to_node(snapshot);
		if (!node) return static_cast<GdkTexture *>(nullptr);
		auto *texture = find_scaled_texture(node, filter);
		if (texture) g_object_ref(texture);
		gsk_render_node_unref(node);
		return texture;
	};
	g_autoptr(GdkTexture) original = source_texture();
	ASSERT_NE(original, nullptr);
	EXPECT_EQ(gdk_texture_get_width(original), 800);
	EXPECT_EQ(gdk_texture_get_height(original), 600);

	for (const auto quality : {GDK_INTERP_NEAREST, GDK_INTERP_BILINEAR})
		{
		pr->zoom_quality = quality;
		for (const double zoom : {2.0, 3.0, 1.5})
			{
			if (quality == GDK_INTERP_NEAREST && zoom == 1.5) continue;
			pixbuf_renderer_zoom_set(pr, zoom);
			ASSERT_TRUE(finish_rendering());
			pixbuf_renderer_scroll(pr, 31, 17);
			ASSERT_TRUE(finish_rendering());
			GskScalingFilter filter = GSK_SCALING_FILTER_NEAREST;
			g_autoptr(GdkTexture) zoomed = source_texture(&filter);
			EXPECT_EQ(zoomed, original);
			EXPECT_EQ(filter, quality == GDK_INTERP_NEAREST ? GSK_SCALING_FILTER_NEAREST : GSK_SCALING_FILTER_LINEAR);
			auto *snapshot = gtk_snapshot_new();
			renderer_tiles_snapshot(pr->renderer, snapshot, 1.0);
			auto *node = gtk_snapshot_free_to_node(snapshot);
			graphene_rect_t viewport;
			graphene_rect_init(&viewport, 0, 0, pr->viewport_width, pr->viewport_height);
			g_autoptr(GdkTexture) rendered = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(window)), node, &viewport);
			gsk_render_node_unref(node);
			ASSERT_NE(rendered, nullptr);
			const int stride = gdk_texture_get_width(rendered);
			std::vector<uint32_t> output(stride * gdk_texture_get_height(rendered));
			gdk_texture_download(rendered, reinterpret_cast<guchar *>(output.data()), stride * 4);
			g_autoptr(GdkPixbuf) reference = gdk_pixbuf_scale_simple(image, pr->width, pr->height, quality);
			for (int y = 5; y < pr->vis_height - 5; y += 29)
				{
				for (int x = 5; x < pr->vis_width - 5; x += 37)
					{
					const uint32_t actual = output[(y + pr->y_offset) * stride + x + pr->x_offset];
					const auto *expected = gdk_pixbuf_get_pixels(reference) +
					                       (y + pr->y_scroll) * gdk_pixbuf_get_rowstride(reference) + (x + pr->x_scroll) * 3;
					const int tolerance = quality == GDK_INTERP_NEAREST ? 0 : 2;
					for (int channel = 0; channel < 3; ++channel)
						{
						EXPECT_LE(std::abs(static_cast<int>((actual >> (16 - channel * 8)) & 0xff) - expected[channel]), tolerance);
						}
					}
				}

			}
		}

	gdk_pixbuf_fill(image, 0xabcdefFF);
	pixbuf_renderer_area_changed(pr, {0, 0, 800, 600});
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) changed = source_texture();
	ASSERT_NE(changed, nullptr);
	EXPECT_NE(changed, original);
	std::vector<uint32_t> pixels(800 * 600);
	gdk_texture_download(original, reinterpret_cast<guchar *>(pixels.data()), 800 * 4);
	EXPECT_EQ(pixels[0], 0xff204060U);
	gdk_texture_download(changed, reinterpret_cast<guchar *>(pixels.data()), 800 * 4);
	EXPECT_EQ(pixels[0], 0xffabcdefU);

	pr->zoom_quality = GDK_INTERP_NEAREST;
	pixbuf_renderer_zoom_set(pr, 2.0);
	ASSERT_TRUE(finish_rendering());
	pixbuf_renderer_zoom_set(pr, 1.5);
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) fractional_nearest = source_texture();
	EXPECT_EQ(fractional_nearest, nullptr);
	pr->zoom_quality = GDK_INTERP_BILINEAR;

	// Downsampling and images larger than the cache budget keep the tile path.
	pixbuf_renderer_zoom_set(pr, -2.0);
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) reduced = source_texture();
	EXPECT_EQ(reduced, nullptr);
	pr->tile_cache_max = 1;
	pixbuf_renderer_zoom_set(pr, 2.0);
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) over_budget = source_texture();
	EXPECT_EQ(over_budget, nullptr);

	pr->tile_cache_max = 8;
	pixbuf_renderer_set_post_process_func(pr,
		[](PixbufRenderer *, GdkPixbuf **, gint, gint, gint, gint) {}, FALSE);
	pixbuf_renderer_zoom_set(pr, 3.0);
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) processed = source_texture();
	EXPECT_EQ(processed, nullptr);
	pixbuf_renderer_set_post_process_func(pr, nullptr, FALSE);
	pixbuf_renderer_set_orientation(pr, 6);
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) rotated = source_texture();
	EXPECT_EQ(rotated, nullptr);

	g_autoptr(GdkPixbuf) alpha = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 800, 600);
	gdk_pixbuf_fill(alpha, 0x20406080);
	pixbuf_renderer_set_orientation(pr, 1);
	pixbuf_renderer_set_pixbuf(pr, alpha, 2.0);
	ASSERT_TRUE(finish_rendering());
	g_autoptr(GdkTexture) transparent = source_texture();
	EXPECT_EQ(transparent, nullptr);
	gtk_window_destroy(window);
}

// Run explicitly with --gtest_also_run_disabled_tests and a hardware GSK renderer.
TEST(RendererTilesTexture, DISABLED_BenchmarkMagnification)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	for (const int image_width : {1600, 4000})
		{
		const int image_height = image_width * 3 / 4;
		g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, image_width, image_height);
		for (int y = 0; y < image_height; ++y)
			{
			for (int x = 0; x < image_width; ++x)
				{
				auto *pixel = gdk_pixbuf_get_pixels(image) + y * gdk_pixbuf_get_rowstride(image) + x * 3;
				pixel[0] = (x / 3 + y / 7) % 256;
				pixel[1] = (x / 11 + y / 5) % 256;
				pixel[2] = (x / 13 + y / 17) % 256;
				}
			}
		for (int repeat = 0; repeat < 5; ++repeat)
			{
			for (int order = 0; order < 2; ++order)
				{
				const bool gpu = (repeat + order) % 2 != 0;
				auto *window = GTK_WINDOW(gtk_window_new());
				auto *pr = pixbuf_renderer_new();
				pr->tile_cache_max = 64;
				pr->zoom_2pass = FALSE;
				pr->zoom_quality = GDK_INTERP_BILINEAR;
				if (!gpu)
					{
					// An identity callback forces the existing CPU tile path without
					// changing the pixels, cache budget, or interpolation settings.
					pixbuf_renderer_set_post_process_func(pr,
						[](PixbufRenderer *, GdkPixbuf **, gint, gint, gint, gint) {}, FALSE);
					}
				gtk_window_set_title(window, "Geeqie zoom benchmark");
				gtk_window_set_default_size(window, 1024, 768);
				gtk_window_set_child(window, GTK_WIDGET(pr));
				pr->complete = FALSE;
				pixbuf_renderer_set_pixbuf(pr, image, 1.0);
				gtk_window_present(window);
				for (int i = 0; i < 5000 && (!gtk_widget_get_mapped(GTK_WIDGET(pr)) || pr->viewport_width < 1 || pr->viewport_height < 1 || !pr->complete); ++i)
					{
					while (g_main_context_iteration(nullptr, FALSE)) {}
					g_usleep(1000);
					}
				ASSERT_TRUE(gtk_widget_get_mapped(GTK_WIDGET(pr)));
				ASSERT_GT(pr->viewport_width, 0);
				ASSERT_GT(pr->viewport_height, 0);
				ASSERT_TRUE(pr->complete);
				auto *renderer = gtk_native_get_renderer(GTK_NATIVE(window));
				if (GSK_IS_CAIRO_RENDERER(renderer))
					{
					gtk_window_destroy(window);
					GTEST_SKIP() << "Requires a GL or Vulkan renderer";
					}
				g_print("BENCH_RENDERER,%s,viewport=%dx%d\n", G_OBJECT_TYPE_NAME(renderer), pr->viewport_width, pr->viewport_height);
				std::vector<guchar> pixels(static_cast<size_t>(pr->viewport_width) * pr->viewport_height * 4);
				const auto frame = [&](double zoom, double &prepare_ms, double &total_ms)
				{
					const gint64 start = g_get_monotonic_time();
					pr->complete = FALSE;
					pixbuf_renderer_zoom_set(pr, zoom);
					const gint64 deadline = start + 5 * G_TIME_SPAN_SECOND;
					while (!pr->complete && g_get_monotonic_time() < deadline)
						{
						if (!g_main_context_iteration(nullptr, FALSE)) g_usleep(50);
						}
					ASSERT_TRUE(pr->complete);
					prepare_ms = (g_get_monotonic_time() - start) / 1000.0;
					auto *snapshot = gtk_snapshot_new();
					renderer_tiles_snapshot(pr->renderer, snapshot, 1.0);
					auto *node = gtk_snapshot_free_to_node(snapshot);
					ASSERT_NE(node, nullptr);
					EXPECT_EQ(find_scaled_texture(node, nullptr) != nullptr, gpu);
					graphene_rect_t viewport;
					graphene_rect_init(&viewport, 0, 0, pr->viewport_width, pr->viewport_height);
					g_autoptr(GdkTexture) output = gsk_renderer_render_texture(renderer, node, &viewport);
					gsk_render_node_unref(node);
					ASSERT_NE(output, nullptr);
					pixels.resize(static_cast<size_t>(gdk_texture_get_width(output)) * gdk_texture_get_height(output) * 4);
					// Download waits for the GPU result. Both paths pay the same readback cost.
					gdk_texture_download(output, pixels.data(), gdk_texture_get_width(output) * 4);
					total_ms = (g_get_monotonic_time() - start) / 1000.0;
				};
				double prepare_ms = 0;
				double total_ms = 0;
				frame(1.1, prepare_ms, total_ms);
				g_print("BENCH_FIRST_ZOOM,%d,%s,%d,%.3f,%.3f\n", image_width, gpu ? "texture" : "cpu", repeat, prepare_ms, total_ms);
				for (const double zoom : {1.25, 1.5, 1.75}) frame(zoom, prepare_ms, total_ms);
				std::vector<double> prepare;
				std::vector<double> completed;
				for (int i = 0; i < 24; ++i)
					{
					frame(1.1 + (i % 12) * 0.15, prepare_ms, total_ms);
					prepare.push_back(prepare_ms);
					completed.push_back(total_ms);
					}
				std::sort(prepare.begin(), prepare.end());
				std::sort(completed.begin(), completed.end());
				g_print("BENCH_WARM,%d,%s,%d,%.3f,%.3f,%.3f\n", image_width, gpu ? "texture" : "cpu", repeat,
				        (prepare[11] + prepare[12]) / 2, (completed[11] + completed[12]) / 2, completed[22]);
				gtk_window_destroy(window);
				while (g_main_context_iteration(nullptr, FALSE)) {}
				}
			}
		}
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
