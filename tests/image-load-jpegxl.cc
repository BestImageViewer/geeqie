/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <config.h>

#if HAVE_JPEGXL

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>
#include <jxl/encode_cxx.h>
#include <jxl/version.h>
#if HAVE_LCMS
#include <lcms2.h>
#endif
#include <unistd.h>

#include "color-man.h"
#include "exif.h"
#include "filedata.h"
#include "image-load.h"
#include "image-load-jpegxl.h"
#include "options.h"
#include "thumb-standard.h"

#if JPEGXL_MAJOR_VERSION > 0 || JPEGXL_MINOR_VERSION >= 7
namespace
{

class JpegXLPixels : public testing::TestWithParam<int>
{
};

TEST_P(JpegXLPixels, PreservesAlphaAndOpaqueFilteredColors)
{
	const int channels = GetParam();
	const bool has_alpha = channels == 2 || channels == 4;
	constexpr int width = 17;
	constexpr int height = 15;
	std::vector<uint8_t> pixels(width * height * channels, 128);
	if (has_alpha)
		{
		for (int i = 0; i < width * height; ++i)
			pixels[i * channels + channels - 1] = (i % 4) * 85;
		}

	auto encoder = JxlEncoderMake(nullptr);
	ASSERT_NE(encoder, nullptr);
	JxlBasicInfo info;
	JxlEncoderInitBasicInfo(&info);
	info.xsize = width;
	info.ysize = height;
	info.bits_per_sample = 8;
	info.num_color_channels = channels <= 2 ? 1 : 3;
	info.num_extra_channels = has_alpha ? 1 : 0;
	info.alpha_bits = has_alpha ? 8 : 0;
	info.uses_original_profile = JXL_TRUE;
	ASSERT_EQ(JxlEncoderSetBasicInfo(encoder.get(), &info), JXL_ENC_SUCCESS);
	JxlColorEncoding color;
	JxlColorEncodingSetToSRGB(&color, channels <= 2);
	ASSERT_EQ(JxlEncoderSetColorEncoding(encoder.get(), &color), JXL_ENC_SUCCESS);
	auto *settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
	ASSERT_EQ(JxlEncoderSetFrameLossless(settings, JXL_TRUE), JXL_ENC_SUCCESS);
	const JxlPixelFormat format = {static_cast<uint32_t>(channels), JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
	ASSERT_EQ(JxlEncoderAddImageFrame(settings, &format, pixels.data(), pixels.size()), JXL_ENC_SUCCESS);
	JxlEncoderCloseInput(encoder.get());
	std::vector<uint8_t> encoded(65536);
	auto *next = encoded.data();
	size_t available = encoded.size();
	ASSERT_EQ(JxlEncoderProcessOutput(encoder.get(), &next, &available), JXL_ENC_SUCCESS);
	encoded.resize(next - encoded.data());

	auto loader = get_image_loader_backend_jpegxl();
	loader->init([](gpointer, gint, gint, gint, gint, gpointer) {}, [](gpointer, gint, gint, gpointer) {}, nullptr);
	gsize chunk_size = encoded.size();
	ASSERT_TRUE(loader->write(encoded.data(), chunk_size, encoded.size(), nullptr));
	auto *decoded = loader->get_pixbuf();
	ASSERT_NE(decoded, nullptr);
	EXPECT_EQ(gdk_pixbuf_get_has_alpha(decoded), has_alpha);
	EXPECT_EQ(gdk_pixbuf_get_n_channels(decoded), has_alpha ? 4 : 3);
	ASSERT_EQ(gdk_pixbuf_get_width(decoded), width);
	ASSERT_EQ(gdk_pixbuf_get_height(decoded), height);
	for (int y = 0; y < height; ++y)
		{
		for (int x = 0; x < width; ++x)
			{
			const auto *pixel = gdk_pixbuf_get_pixels(decoded) + y * gdk_pixbuf_get_rowstride(decoded) + x * gdk_pixbuf_get_n_channels(decoded);
			for (int channel = 0; channel < 3; ++channel) EXPECT_EQ(pixel[channel], 128);
			if (has_alpha)
				{
				EXPECT_EQ(pixel[3], pixels[(y * width + x) * channels + channels - 1]);
				}
			}
		}

	if (has_alpha) return;
	// This compositing API also scales RGB sources, while opaque RGBA sources
	// take the blending path that previously darkened filtered JXL images.
	for (const auto quality : {GDK_INTERP_NEAREST, GDK_INTERP_TILES, GDK_INTERP_BILINEAR, GDK_INTERP_HYPER})
		{
		SCOPED_TRACE(quality);
		g_autoptr(GdkPixbuf) rendered = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 7, 6);
		gdk_pixbuf_composite_color(decoded, rendered, 0, 0, 7, 6, 0, 0, 7.0 / width, 6.0 / height,
		                           quality, 255, 0, 0, 16, 0xffffff, 0);
		for (int y = 0; y < 6; ++y)
			for (int x = 0; x < 7 * 3; ++x)
				ASSERT_EQ(gdk_pixbuf_get_pixels(rendered)[y * gdk_pixbuf_get_rowstride(rendered) + x], 128);
		}
}

INSTANTIATE_TEST_SUITE_P(SourceChannels, JpegXLPixels, testing::Values(1, 2, 3, 4));

#if HAVE_LCMS
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(cmsHPROFILE, cmsCloseProfile, nullptr)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(cmsHTRANSFORM, cmsDeleteTransform, nullptr)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(cmsToneCurve, cmsFreeToneCurve)

struct TemporaryJxl
{
	gchar *path = nullptr;
	~TemporaryJxl()
	{
		if (path) g_unlink(path);
		g_free(path);
	}
};

void check_jpegxl_profile(bool original_profile, bool embedded_profile)
{
	if (!options) options = conf_options_new();
	const cmsCIExyY white = {0.3127, 0.3290, 1.0};
	const cmsCIExyYTRIPLE primaries = {{0.68, 0.32, 1.0}, {0.265, 0.69, 1.0}, {0.15, 0.06, 1.0}};
	g_autoptr(cmsToneCurve) curve = cmsBuildGamma(nullptr, 1.0);
	cmsToneCurve *curves[] = {curve, curve, curve};
	g_auto(cmsHPROFILE) input_profile = cmsCreateRGBProfile(&white, &primaries, curves);
	ASSERT_NE(input_profile, nullptr);
	cmsUInt32Number profile_size = 0;
	ASSERT_TRUE(cmsSaveProfileToMem(input_profile, nullptr, &profile_size));
	std::vector<uint8_t> icc(profile_size);
	ASSERT_TRUE(cmsSaveProfileToMem(input_profile, icc.data(), &profile_size));

	auto encoder = JxlEncoderMake(nullptr);
	ASSERT_NE(encoder, nullptr);
	JxlBasicInfo info;
	JxlEncoderInitBasicInfo(&info);
	info.xsize = 1;
	info.ysize = 1;
	info.bits_per_sample = 8;
	info.num_color_channels = 3;
	info.num_extra_channels = 1;
	info.alpha_bits = 8;
	info.uses_original_profile = original_profile;
	ASSERT_EQ(JxlEncoderSetBasicInfo(encoder.get(), &info), JXL_ENC_SUCCESS);
	if (embedded_profile)
		{
		ASSERT_EQ(JxlEncoderSetICCProfile(encoder.get(), icc.data(), icc.size()), JXL_ENC_SUCCESS);
		}
	else
		{
		JxlColorEncoding color;
		JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
		ASSERT_EQ(JxlEncoderSetColorEncoding(encoder.get(), &color), JXL_ENC_SUCCESS);
		}
	auto *settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
	ASSERT_EQ(JxlEncoderSetFrameLossless(settings, original_profile), JXL_ENC_SUCCESS);
	const JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
	const std::array<uint8_t, 4> pixel = {64, 128, 192, 128};
	ASSERT_EQ(JxlEncoderAddImageFrame(settings, &format, pixel.data(), pixel.size()), JXL_ENC_SUCCESS);
	JxlEncoderCloseInput(encoder.get());
	std::vector<uint8_t> encoded(65536);
	auto *next = encoded.data();
	size_t available = encoded.size();
	ASSERT_EQ(JxlEncoderProcessOutput(encoder.get(), &next, &available), JXL_ENC_SUCCESS);
	encoded.resize(next - encoded.data());

	TemporaryJxl file;
	const int descriptor = g_file_open_tmp("geeqie-jxl-profile-XXXXXX", &file.path, nullptr);
	ASSERT_GE(descriptor, 0);
	close(descriptor);
	ASSERT_TRUE(g_file_set_contents(file.path, reinterpret_cast<const gchar *>(encoded.data()), encoded.size(), nullptr));
	std::unique_ptr<FileData, void (*)(FileData *)> fd(file_data_new_simple(file.path), [](FileData *value) { file_data_unref(value); });
	fd->format_name = g_strdup("jxl");
	ColorManProfileType type = COLOR_PROFILE_NONE;
	auto profile = exif_get_color_profile(fd.get(), type);
	ASSERT_EQ(type, COLOR_PROFILE_MEM);
	ASSERT_NE(profile.ptr, nullptr);
	EXPECT_GT(profile.len, 0U);

	auto loader = get_image_loader_backend_jpegxl();
	loader->init([](gpointer, gint, gint, gint, gint, gpointer) {}, [](gpointer, gint, gint, gpointer) {}, nullptr);
	gsize chunk_size = encoded.size();
	ASSERT_TRUE(loader->write(encoded.data(), chunk_size, encoded.size(), nullptr));
	auto *decoded = loader->get_pixbuf();
	ASSERT_NE(decoded, nullptr);
	if (original_profile)
		{
		EXPECT_EQ(memcmp(gdk_pixbuf_get_pixels(decoded), pixel.data(), pixel.size()), 0);
		}

	g_auto(cmsHPROFILE) srgb = cmsCreate_sRGBProfile();
	g_auto(cmsHTRANSFORM) transform = cmsCreateTransform(embedded_profile ? input_profile : srgb, TYPE_RGBA_8, srgb, TYPE_RGBA_8, INTENT_PERCEPTUAL, cmsFLAGS_COPY_ALPHA);
	ASSERT_NE(transform, nullptr);
	std::array<uint8_t, 4> expected{};
	cmsDoTransform(transform, pixel.data(), expected.data(), 1);
	g_autoptr(GdkPixbuf) display = gdk_pixbuf_copy(decoded);
	std::unique_ptr<ColorMan> color_man(color_man_new_embedded(display, profile, COLOR_PROFILE_SRGB, nullptr, {}));
	ASSERT_NE(color_man, nullptr);
	color_man->correct_region(display, {0, 0, 1, 1});
	const gboolean previous = options->thumbnails.use_color_management;
	options->thumbnails.use_color_management = TRUE;
	thumb_loader_std_calibrate_pixbuf(fd.get(), decoded);
	options->thumbnails.use_color_management = previous;
	for (size_t channel = 0; channel < 3; ++channel)
		{
		EXPECT_NEAR(gdk_pixbuf_get_pixels(decoded)[channel], expected[channel], original_profile ? 2 : 5);
		EXPECT_NEAR(gdk_pixbuf_get_pixels(display)[channel], expected[channel], original_profile ? 2 : 5);
		}
	EXPECT_EQ(gdk_pixbuf_get_pixels(decoded)[3], pixel[3]);
}

TEST(JpegXLColorProfile, EmbeddedProfileReachesDisplayAndThumbnails)
{
	check_jpegxl_profile(true, true);
}

TEST(JpegXLColorProfile, ProfileDescribesDecodedPixels)
{
	check_jpegxl_profile(false, true);
}

TEST(JpegXLColorProfile, StructuredSrgbWithoutEmbeddedIcc)
{
	check_jpegxl_profile(true, false);
}

TEST(JpegXLColorProfile, MissingFileClearsLength)
{
	guint length = 123;
	g_autofree guchar *profile = jpegxl_color_profile("/nonexistent/geeqie-profile.jxl", length);
	EXPECT_EQ(profile, nullptr);
	EXPECT_EQ(length, 0U);
}

TEST(JpegXLColorProfile, NoCurrentImage)
{
	ColorManProfileType type = COLOR_PROFILE_MEM;
	auto profile = exif_get_color_profile(nullptr, type);
	EXPECT_EQ(type, COLOR_PROFILE_NONE);
	EXPECT_EQ(profile.ptr, nullptr);
	EXPECT_EQ(profile.len, 0U);
}

#endif // HAVE_LCMS

} // namespace
#endif
#endif
