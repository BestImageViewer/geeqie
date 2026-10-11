/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "options.h"
#include "similar.h"

namespace
{

ImageSimilarityData make_pattern(guint32 seed = 12345)
{
	ImageSimilarityData image;
	for (guint i = 0; i < image.avg_r.size(); i++)
		{
		seed = seed * 1664525 + 1013904223;
		image.avg_r[i] = 32 + ((seed >> 24) & 63);
		seed = seed * 1664525 + 1013904223;
		image.avg_g[i] = 32 + ((seed >> 24) & 63);
		seed = seed * 1664525 + 1013904223;
		image.avg_b[i] = 32 + ((seed >> 24) & 63);
		}
	image.filled = true;
	return image;
}

ImageSimilarityData transform_grid(const ImageSimilarityData &image, guint orientation)
{
	ImageSimilarityData transformed;
	for (guint y = 0; y < 32; y++)
		{
		for (guint x = 0; x < 32; x++)
			{
			guint source_x = (orientation & 4) ? 31 - x : x;
			guint source_y = (orientation & 2) ? 31 - y : y;
			if (orientation & 1) std::swap(source_x, source_y);
			const guint source = source_y * 32 + source_x;
			const guint target = y * 32 + x;
			transformed.avg_r[target] = image.avg_r[source];
			transformed.avg_g[target] = image.avg_g[source];
			transformed.avg_b[target] = image.avg_b[source];
			}
		}
	transformed.filled = true;
	return transformed;
}

// Independent, direct 2D DCT to check the separable production implementation.
guint64 reference_hash(const ImageSimilarityData &image)
{
	std::array<gdouble, 64> coefficients{};
	for (guint v = 0; v < 8; v++)
		{
		for (guint u = 0; u < 8; u++)
			{
			if (u == 0 && v == 0) continue;
			gdouble value = 0;
			for (guint y = 0; y < 32; y++)
				{
				for (guint x = 0; x < 32; x++)
					{
					const guint i = y * 32 + x;
					const gdouble gray = (299.0 * image.avg_r[i] + 587.0 * image.avg_g[i] + 114.0 * image.avg_b[i]) / 1000.0;
					value += gray * std::cos(G_PI * (2 * x + 1) * u / 64.0) * std::cos(G_PI * (2 * y + 1) * v / 64.0);
					}
				}
			value *= std::sqrt((u == 0 ? 1.0 : 2.0) / 32.0) * std::sqrt((v == 0 ? 1.0 : 2.0) / 32.0);
			coefficients[v * 8 + u] = std::abs(value) < 1e-7 ? 0.0 : value;
			}
		}
	std::array<gdouble, 63> ranked{};
	std::copy(coefficients.begin() + 1, coefficients.end(), ranked.begin());
	std::sort(ranked.begin(), ranked.end());
	guint64 hash = 0;
	for (guint bit = 1; bit < coefficients.size(); bit++)
		{
		if (coefficients[bit] - ranked[31] > 1e-7) hash |= guint64{1} << bit;
		}
	return hash;
}

class PerceptualHash : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
		saved_enabled = options->alternate_similarity_algorithm.enabled;
		saved_orientation = options->rot_invariant_sim;
		options->alternate_similarity_algorithm.enabled = TRUE;
		options->rot_invariant_sim = FALSE;
	}

	void TearDown() override
	{
		options->alternate_similarity_algorithm.enabled = saved_enabled;
		options->rot_invariant_sim = saved_orientation;
	}

	gboolean saved_enabled;
	gboolean saved_orientation;
};

TEST_F(PerceptualHash, MatchesDirectDctForAllOrientations)
{
	auto image = make_pattern();
	ASSERT_TRUE(image.ensure_perceptual_hashes());
	ASSERT_TRUE(image.perceptual_hashes.has_value());
	for (guint orientation = 0; orientation < 8; orientation++)
		{
		SCOPED_TRACE(orientation);
		auto transformed = transform_grid(image, orientation);
		EXPECT_EQ((*image.perceptual_hashes)[orientation], reference_hash(transformed));
		ASSERT_TRUE(transformed.ensure_perceptual_hashes());
		EXPECT_EQ((*image.perceptual_hashes)[orientation], (*transformed.perceptual_hashes)[0]);
		if (orientation != 0)
			{
			EXPECT_LT(image_sim_compare(&image, &transformed), 1.0);
			}
		options->rot_invariant_sim = TRUE;
		EXPECT_DOUBLE_EQ(image_sim_compare(&image, &transformed), 1.0);
		EXPECT_DOUBLE_EQ(image_sim_compare_fast(&image, &transformed, 1.0), 1.0);
		EXPECT_DOUBLE_EQ(image_sim_compare(&transformed, &image), 1.0);
		options->rot_invariant_sim = FALSE;
		}
}

TEST_F(PerceptualHash, SymmetricGridsRemainStableAcrossOrientations)
{
	ImageSimilarityData image;
	image.filled = true;
	for (guint y = 0; y < 32; y++)
		{
		for (guint x = 0; x < 32; x++)
			{
			const guint i = y * 32 + x;
			image.avg_r[i] = image.avg_g[i] = image.avg_b[i] = (x * y + x + y) % 256;
			}
		}
	ASSERT_TRUE(image.ensure_perceptual_hashes());
	for (guint orientation = 0; orientation < 8; orientation++)
		{
		SCOPED_TRACE(orientation);
		auto transformed = transform_grid(image, orientation);
		ASSERT_TRUE(transformed.ensure_perceptual_hashes());
		EXPECT_EQ((*image.perceptual_hashes)[orientation], (*transformed.perceptual_hashes)[0]);
		EXPECT_EQ((*image.perceptual_hashes)[orientation], reference_hash(transformed));
		}
}

TEST_F(PerceptualHash, OrientationIndependentScoresAreSymmetricForDifferentImages)
{
	auto a = make_pattern();
	auto b = make_pattern(54321);
	ASSERT_TRUE(a.ensure_perceptual_hashes());
	ASSERT_TRUE(b.ensure_perceptual_hashes());
	options->rot_invariant_sim = TRUE;
	const gdouble expected = image_sim_compare(&a, &b);
	EXPECT_DOUBLE_EQ(image_sim_compare(&b, &a), expected);
	for (guint orientation = 0; orientation < 8; orientation++)
		{
		SCOPED_TRACE(orientation);
		auto transformed_a = transform_grid(a, orientation);
		auto transformed_b = transform_grid(b, orientation);
		ASSERT_TRUE(transformed_a.ensure_perceptual_hashes());
		ASSERT_TRUE(transformed_b.ensure_perceptual_hashes());
		EXPECT_DOUBLE_EQ(image_sim_compare(&transformed_a, &b), expected);
		EXPECT_DOUBLE_EQ(image_sim_compare(&a, &transformed_b), expected);
		}
}

TEST_F(PerceptualHash, BrightnessContrastAndResizePreserveHash)
{
	auto image = make_pattern();
	auto adjusted = image;
	for (guint i = 0; i < adjusted.avg_r.size(); i++)
		{
		adjusted.avg_r[i] = image.avg_r[i] * 2 + 10;
		adjusted.avg_g[i] = image.avg_g[i] * 2 + 10;
		adjusted.avg_b[i] = image.avg_b[i] * 2 + 10;
		}
	ASSERT_TRUE(image.ensure_perceptual_hashes());
	ASSERT_TRUE(adjusted.ensure_perceptual_hashes());
	EXPECT_EQ(image.perceptual_hashes, adjusted.perceptual_hashes);

	g_autoptr(GdkPixbuf) pixbuf = image.to_pixbuf();
	g_autoptr(GdkPixbuf) scaled = gdk_pixbuf_scale_simple(pixbuf, 64, 64, GDK_INTERP_NEAREST);
	ImageSimilarityData resized{scaled};
	EXPECT_EQ(image.perceptual_hashes, resized.perceptual_hashes);
}

TEST_F(PerceptualHash, ScoreUsesHammingDistanceAndInclusiveThreshold)
{
	auto a = make_pattern();
	auto b = a;
	a.perceptual_hashes = ImageSimilarityData::PerceptualHashes{};
	b.perceptual_hashes = ImageSimilarityData::PerceptualHashes{};
	(*b.perceptual_hashes)[0] = 0x0e; // Three differing AC bits.
	const gdouble expected = 1.0 - 3.0 / 63.0;
	EXPECT_DOUBLE_EQ(image_sim_compare(&a, &b), expected);
	EXPECT_DOUBLE_EQ(image_sim_compare_fast(&a, &b, expected), expected);
	EXPECT_DOUBLE_EQ(image_sim_compare_fast(&a, &b, 0.95), expected);
	EXPECT_DOUBLE_EQ(image_sim_compare_fast(&a, &b, 0.96), 0.0);
	(*b.perceptual_hashes)[0] = 0x1e;
	EXPECT_DOUBLE_EQ(image_sim_compare_fast(&a, &b, 0.95), 0.0);
}

TEST_F(PerceptualHash, StandardGridRemainsUnmodifiedAndHashesAreOptional)
{
	auto image = make_pattern();
	const auto original = image;
	ASSERT_TRUE(image.ensure_perceptual_hashes());
	EXPECT_FALSE(image.ensure_perceptual_hashes());
	EXPECT_EQ(image.avg_r, original.avg_r);
	EXPECT_EQ(image.avg_g, original.avg_g);
	EXPECT_EQ(image.avg_b, original.avg_b);

	g_autoptr(GdkPixbuf) pixbuf = image.to_pixbuf();
	options->alternate_similarity_algorithm.enabled = FALSE;
	ImageSimilarityData standard{pixbuf};
	EXPECT_FALSE(standard.perceptual_hashes.has_value());
	EXPECT_DOUBLE_EQ(image_sim_compare(&image, &standard), 1.0);
	image.fill_data(pixbuf);
	EXPECT_FALSE(image.perceptual_hashes.has_value());
	image.fill_data(static_cast<GdkPixbuf *>(nullptr));
	EXPECT_FALSE(image.filled);
}

TEST_F(PerceptualHash, UniformImagesHaveStableHashesAndUnfilledImagesCannotMatch)
{
	ImageSimilarityData black;
	black.filled = true;
	ImageSimilarityData white = black;
	white.avg_r.fill(255);
	white.avg_g.fill(255);
	white.avg_b.fill(255);
	ASSERT_TRUE(black.ensure_perceptual_hashes());
	ASSERT_TRUE(white.ensure_perceptual_hashes());
	EXPECT_EQ(black.perceptual_hashes, white.perceptual_hashes);
	EXPECT_EQ(black.perceptual_hashes, ImageSimilarityData::PerceptualHashes{});
	ImageSimilarityData empty;
	EXPECT_FALSE(empty.ensure_perceptual_hashes());
	EXPECT_DOUBLE_EQ(image_sim_compare(&black, &empty), 0.0);
	EXPECT_DOUBLE_EQ(image_sim_compare(&empty, &empty), 0.0);
	EXPECT_DOUBLE_EQ(image_sim_compare(nullptr, &black), 0.0);
}

} // namespace
