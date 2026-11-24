/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "exif.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>

#include "gtest/gtest.h"

namespace
{

struct DigitalZoomCase
{
	uint32_t numerator;
	uint32_t denominator;
	const char *expected;
};

class DigitalZoom : public testing::TestWithParam<DigitalZoomCase>
{
protected:
	void SetUp() override
	{
		directory = g_dir_make_tmp("geeqie-digital-zoom-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image.jpg", nullptr);
	}

	void TearDown() override
	{
		if (path) g_unlink(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
	}

	void save(uint32_t numerator, uint32_t denominator, bool present = true)
	{
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
		gdk_pixbuf_fill(pixels, 0x123456ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "jpeg", nullptr, nullptr));
		g_autofree gchar *contents = nullptr;
		gsize length;
		ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
		ASSERT_GT(length, 2U);

		// Construct a little-endian EXIF rational directly so these tests also
		// work with the internal parser and can represent a zero denominator.
		std::array<guchar, 52> tiff{};
		const auto put16 = [&tiff](size_t offset, uint16_t value)
			{
			tiff[offset] = value & 0xff;
			tiff[offset + 1] = value >> 8;
			};
		const auto put32 = [&tiff](size_t offset, uint32_t value)
			{
			for (unsigned i = 0; i < 4; ++i) tiff[offset + i] = (value >> (i * 8)) & 0xff;
			};
		tiff[0] = 'I';
		tiff[1] = 'I';
		put16(2, 42);
		put32(4, 8);
		put16(8, 1);
		put16(10, 0x8769); // Exif IFD pointer.
		put16(12, 4);
		put32(14, 1);
		put32(18, 26);
		put16(26, 1);
		put16(28, present ? 0xa404 : 0x920a); // DigitalZoomRatio or FocalLength.
		put16(30, 5);
		put32(32, 1);
		put32(36, 44);
		put32(44, numerator);
		put32(48, denominator);

		std::string jpeg(contents, 2);
		jpeg.append("\xff\xe1\x00\x3c", 4); // APP1 marker and EXIF segment length.
		jpeg.append("Exif\0\0", 6);
		jpeg.append(reinterpret_cast<const char *>(tiff.data()), tiff.size());
		jpeg.append(contents + 2, length - 2);
		ASSERT_TRUE(g_file_set_contents(path, jpeg.data(), jpeg.size(), nullptr));
	}

	gchar *directory = nullptr;
	gchar *path = nullptr;
};

TEST_P(DigitalZoom, FormatsRatioWithoutChangingRawMetadata)
{
	const auto &sample = GetParam();
	save(sample.numerator, sample.denominator);
	std::unique_ptr<ExifData, decltype(&exif_free)> data(exif_read(path, nullptr, nullptr), exif_free);
	ASSERT_NE(data, nullptr);
	g_autofree gchar *text = exif_get_data_as_text(data.get(), "formatted.DigitalZoomRatio");
	EXPECT_STREQ(text, sample.expected);
	auto *item = exif_get_item(data.get(), "Exif.Photo.DigitalZoomRatio");
	ASSERT_NE(item, nullptr);
	auto *rational = exif_item_get_rational(item, 0);
	ASSERT_NE(rational, nullptr);
	EXPECT_EQ(rational->num, sample.numerator);
	EXPECT_EQ(rational->den, sample.denominator);
}

TEST_F(DigitalZoom, MissingTagHasNoFormattedValue)
{
	save(35, 1, false);
	std::unique_ptr<ExifData, decltype(&exif_free)> data(exif_read(path, nullptr, nullptr), exif_free);
	ASSERT_NE(data, nullptr);
	g_autofree gchar *text = exif_get_data_as_text(data.get(), "formatted.DigitalZoomRatio");
	EXPECT_EQ(text, nullptr);
}

INSTANTIATE_TEST_SUITE_P(ExifValues, DigitalZoom, testing::Values(
	DigitalZoomCase{0, 1, nullptr},
	DigitalZoomCase{0, 0, nullptr},
	DigitalZoomCase{1, 0, nullptr},
	DigitalZoomCase{1, 1, "1.0×"},
	DigitalZoomCase{2, 1, "2.0×"},
	DigitalZoomCase{3, 2, "1.5×"},
	DigitalZoomCase{5, 4, "1.25×"},
	DigitalZoomCase{1, 1000, "0.001×"}));

} // namespace
