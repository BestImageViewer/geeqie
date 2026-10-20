/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <config.h>

#if HAVE_HEIF

#include <vector>

#include <libheif/heif_cxx.h>

#include "image-load.h"
#include "image-load-heif.h"

namespace
{

class HeifBuffer : public heif::Context::Writer
{
public:
	heif_error write(const void *data, size_t size) override
	{
		const auto *bytes = static_cast<const guchar *>(data);
		buffer.insert(buffer.end(), bytes, bytes + size);
		return {heif_error_Ok, heif_suberror_Unspecified, ""};
	}

	std::vector<guchar> buffer;
};

TEST(HeifPixels, AvifChannelsMatchAlphaFlag)
{
	if (!heif_have_encoder_for_format(heif_compression_AV1)) GTEST_SKIP() << "Requires an AV1 encoder";
	for (const bool alpha : {false, true})
		{
		SCOPED_TRACE(alpha);
		heif::Context context;
		heif::Image image;
		image.create(16, 16, heif_colorspace_RGB, alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB);
		image.add_plane(heif_channel_interleaved, 16, 16, 8);
		int stride;
		auto *pixels = image.get_plane(heif_channel_interleaved, &stride);
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				{
				auto *pixel = pixels + (y * stride) + (x * (alpha ? 4 : 3));
				pixel[0] = 200;
				pixel[1] = 40;
				pixel[2] = 90;
				if (alpha) pixel[3] = (x % 3) * 127;
				}
		heif::Encoder encoder(heif_compression_AV1);
		encoder.set_lossless(true);
		context.encode_image(image, encoder);
		HeifBuffer encoded;
		context.write(encoded);
		auto loader = get_image_loader_backend_heif();
		loader->init([](gpointer, gint, gint, gint, gint, gpointer) {}, nullptr, nullptr);
		gsize chunk_size = encoded.buffer.size();
		ASSERT_TRUE(loader->write(encoded.buffer.data(), chunk_size, encoded.buffer.size(), nullptr));
		auto *decoded = loader->get_pixbuf();
		ASSERT_NE(decoded, nullptr);
		EXPECT_EQ(gdk_pixbuf_get_has_alpha(decoded), alpha);
		ASSERT_EQ(gdk_pixbuf_get_n_channels(decoded), alpha ? 4 : 3);
		ASSERT_EQ(gdk_pixbuf_get_width(decoded), 16);
		ASSERT_EQ(gdk_pixbuf_get_height(decoded), 16);
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				{
				const auto *pixel = gdk_pixbuf_get_pixels(decoded) + (y * gdk_pixbuf_get_rowstride(decoded)) + (x * gdk_pixbuf_get_n_channels(decoded));
				if (!alpha || x % 3 != 0)
					{
					EXPECT_NEAR(pixel[0], 200, 5);
					EXPECT_NEAR(pixel[1], 40, 5);
					EXPECT_NEAR(pixel[2], 90, 5);
					}
				if (alpha) EXPECT_EQ(pixel[3], (x % 3) * 127);
				}
		}
}

} // namespace

#endif
