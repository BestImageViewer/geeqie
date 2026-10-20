/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <config.h>

#if HAVE_WEBP

#include <cstdint>
#include <memory>
#include <vector>

#include <webp/encode.h>

#include "image-load.h"
#include "image-load-webp.h"

namespace
{

std::vector<uint8_t> encode_webp(int width, int height, uint32_t color)
{
	std::vector<uint8_t> pixels(width * height * 4);
	for (size_t i = 0; i < pixels.size(); i += 4)
		for (int channel = 0; channel < 4; ++channel)
			pixels[i + channel] = color >> ((3 - channel) * 8);
	uint8_t *encoded = nullptr;
	const size_t size = WebPEncodeLosslessRGBA(pixels.data(), width, height, width * 4, &encoded);
	std::unique_ptr<uint8_t, decltype(&WebPFree)> owner(encoded, WebPFree);
	if (!size) return {};
	return {encoded, encoded + size};
}

void append_size(std::vector<uint8_t> &bytes, size_t size)
{
	for (int shift = 0; shift < 32; shift += 8) bytes.push_back(size >> shift);
}

void append_chunk(std::vector<uint8_t> &bytes, const char *name, const std::vector<uint8_t> &data)
{
	bytes.insert(bytes.end(), name, name + 4);
	append_size(bytes, data.size());
	bytes.insert(bytes.end(), data.begin(), data.end());
	if (data.size() % 2) bytes.push_back(0);
}

TEST(WebPPixels, AnimatedThumbnailUsesComposedFirstFrameAndOwnsPixels)
{
	// First frame: a 2x2 green square at (2,2) on an 8x6 transparent canvas.
	// Second frame: a blue full-canvas image, which must not replace the thumbnail.
	const auto green = encode_webp(2, 2, 0x00ff00ff);
	const auto blue = encode_webp(8, 6, 0x0000ffff);
	ASSERT_GT(green.size(), 12U);
	ASSERT_GT(blue.size(), 12U);
	std::vector<uint8_t> chunks;
	append_chunk(chunks, "VP8X", {0x12, 0, 0, 0, 7, 0, 0, 5, 0, 0});
	append_chunk(chunks, "ANIM", {0, 0, 0, 0, 0, 0});
	std::vector<uint8_t> first = {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 100, 0, 0, 2};
	first.insert(first.end(), green.begin() + 12, green.end());
	append_chunk(chunks, "ANMF", first);
	std::vector<uint8_t> second = {0, 0, 0, 0, 0, 0, 7, 0, 0, 5, 0, 0, 100, 0, 0, 2};
	second.insert(second.end(), blue.begin() + 12, blue.end());
	append_chunk(chunks, "ANMF", second);
	std::vector<uint8_t> animation = {'R', 'I', 'F', 'F'};
	append_size(animation, chunks.size() + 4);
	animation.insert(animation.end(), {'W', 'E', 'B', 'P'});
	animation.insert(animation.end(), chunks.begin(), chunks.end());
	auto loader = get_image_loader_backend_webp();
	int updates = 0;
	loader->init([](gpointer, gint, gint, gint width, gint height, gpointer data)
		{
		EXPECT_EQ(width, 8);
		EXPECT_EQ(height, 6);
		++*static_cast<int *>(data);
		}, nullptr, &updates);
	gsize chunk_size = 0;
	ASSERT_TRUE(loader->write(animation.data(), chunk_size, animation.size(), nullptr));
	EXPECT_EQ(chunk_size, animation.size());
	EXPECT_EQ(updates, 1);
	g_autoptr(GdkPixbuf) decoded = GDK_PIXBUF(g_object_ref(loader->get_pixbuf()));
	loader.reset();
	EXPECT_EQ(gdk_pixbuf_get_width(decoded), 8);
	EXPECT_EQ(gdk_pixbuf_get_height(decoded), 6);
	ASSERT_TRUE(gdk_pixbuf_get_has_alpha(decoded));
	for (int y = 0; y < 6; ++y)
		for (int x = 0; x < 8; ++x)
			{
			const auto *pixel = gdk_pixbuf_get_pixels(decoded) + (y * gdk_pixbuf_get_rowstride(decoded)) + (x * 4);
			const bool inside = x >= 2 && x < 4 && y >= 2 && y < 4;
			EXPECT_EQ(pixel[3], inside ? 255 : 0);
			if (inside)
				{
				EXPECT_EQ(pixel[0], 0);
				EXPECT_EQ(pixel[1], 255);
				EXPECT_EQ(pixel[2], 0);
				}
			}
}

TEST(WebPPixels, StillImagesKeepRgbAndRgbaLayouts)
{
	for (const int alpha : {128, 255})
		{
		const auto encoded = encode_webp(8, 6, 0xff000000U | alpha);
		ASSERT_FALSE(encoded.empty());
		auto loader = get_image_loader_backend_webp();
		loader->init([](gpointer, gint, gint, gint, gint, gpointer) {}, nullptr, nullptr);
		gsize chunk_size = 0;
		ASSERT_TRUE(loader->write(encoded.data(), chunk_size, encoded.size(), nullptr));
		auto *pixbuf = loader->get_pixbuf();
		ASSERT_NE(pixbuf, nullptr);
		EXPECT_EQ(gdk_pixbuf_get_n_channels(pixbuf), alpha == 255 ? 3 : 4);
		EXPECT_EQ(gdk_pixbuf_get_pixels(pixbuf)[0], 255);
		if (alpha != 255) EXPECT_EQ(gdk_pixbuf_get_pixels(pixbuf)[3], alpha);
		}
}

TEST(WebPPixels, InvalidInputDoesNotProducePixels)
{
	auto loader = get_image_loader_backend_webp();
	loader->init([](gpointer, gint, gint, gint, gint, gpointer) {}, nullptr, nullptr);
	const uint8_t invalid[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
	gsize chunk_size = 0;
	EXPECT_FALSE(loader->write(invalid, chunk_size, sizeof(invalid), nullptr));
	EXPECT_EQ(loader->get_pixbuf(), nullptr);
}

} // namespace

#endif
