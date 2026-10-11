/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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
 */

#include "similar.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

#include "options.h"

/**
 * @file
 *
 * These functions are intended to find images with similar color content. For
 * example when an image was saved at different compression levels or dimensions
 * (scaled down/up) the contents are similar, but these files do not match by file
 * size, dimensions, or checksum.
 *
 * These functions create a 32 x 32 array for each color channel (red, green, blue).
 * The array represents the average color of each corresponding part of the
 * image. (imagine the image cut into 1024 rectangles, or a 32 x 32 grid.
 * Each grid is then processed for the average color value, this is what
 * is stored in the array)
 *
 * To compare two images, generate a ImageSimilarityData for each image, then
 * pass them to the compare function. The return value is the percent match
 * of the two images. (for this, simple comparisons are used, basically the return
 * is an average of the corresponding array differences)
 *
 * for image_sim_compare(), the return is 0.0 to 1.0: \n
 *  1.0 for exact matches (an image is compared to itself) \n
 *  0.0 for exact opposite images (compare an all black to an all white image) \n
 * generally only a match of > 0.85 are significant at all, and >.95 is useful to
 * find images that have been re-saved to other formats, dimensions, or compression.
 *
 * The alternate algorithm uses grayscale DCT perceptual hashes. Eight hashes
 * represent all right-angle rotations and reflections of the same RGB grid.
 * The DC bit is unused; similarity is 1 minus the Hamming distance divided by 63.
 * Hashes must be prepared before comparisons, which may run in worker threads.
 */

namespace
{

using ImageSimilarityCheckAbort = std::function<bool(gdouble)>;

/*
 * 4 rotations (0, 90, 180, 270) combined with two mirrors (0, H)
 * generate all possible isometric transformations
 * = 8 tests
 * = change dir of x, change dir of y, exchange x and y = 2^3 = 8
 */
gdouble image_sim_data_compare_transfo(const ImageSimilarityData *a, const ImageSimilarityData *b, gchar transfo, const ImageSimilarityCheckAbort &check_abort)
{
	if (!image_sim_filled(a) || !image_sim_filled(b)) return 0.0;

	gint sim = 0.0;
	gint i2;
	gint *i;
	gint j2;
	gint *j;

	if (transfo & 1) { i = &j2; j = &i2; } else { i = &i2; j = &j2; }
	for (gint j1 = 0; j1 < 32; j1++)
		{
		if (transfo & 2) *j = 31-j1; else *j = j1;
		for (gint i1 = 0; i1 < 32; i1++)
			{
			if (transfo & 4) *i = 31-i1; else *i = i1;
			sim += abs(a->avg_r[(i1*32)+j1] - b->avg_r[(i2*32)+j2]);
			sim += abs(a->avg_g[(i1*32)+j1] - b->avg_g[(i2*32)+j2]);
			sim += abs(a->avg_b[(i1*32)+j1] - b->avg_b[(i2*32)+j2]);
			/* check for abort, if so return 0.0 */
			if (check_abort(sim)) return 0.0;
			}
		}

	return 1.0 - (static_cast<gdouble>(sim) / (255.0 * 1024.0 * 3.0));
}

gdouble image_sim_data_compare(const ImageSimilarityData *a, const ImageSimilarityData *b, const ImageSimilarityCheckAbort &check_abort)
{
	gchar max_t = (options->rot_invariant_sim ? 8 : 1);
	gdouble max_score = 0;

	for (gchar t = 0; t < max_t; t++)
	{
		max_score = std::max(image_sim_data_compare_transfo(a, b, t, check_abort), max_score);
	}

	return max_score;
}

} // namespace

ImageSimilarityData::ImageSimilarityData(const GdkPixbuf *pixbuf)
	: ImageSimilarityData()
{
	fill_data(pixbuf);
}

bool ImageSimilarityData::ensure_perceptual_hashes()
{
	if (!filled || perceptual_hashes) return false;

	// Orthonormal DCT-II basis for the low-frequency 8 x 8 block.
	static const auto basis = []()
		{
		std::array<std::array<gdouble, 32>, 8> result{};
		for (guint frequency = 0; frequency < 8; frequency++)
			{
			const gdouble scale = std::sqrt((frequency == 0 ? 1.0 : 2.0) / 32.0);
			for (guint position = 0; position < 32; position++)
				{
				result[frequency][position] = scale * std::cos(G_PI * (2 * position + 1) * frequency / 64.0);
				}
			}
		return result;
		}();

	std::array<gdouble, 1024> grayscale{};
	gdouble mean = 0;
	for (guint i = 0; i < grayscale.size(); i++)
		{
		grayscale[i] = (299.0 * avg_r[i] + 587.0 * avg_g[i] + 114.0 * avg_b[i]) / 1000.0;
		mean += grayscale[i];
		}
	mean /= grayscale.size();

	// Subtract the mean to make uniform images and brightness changes stable.
	std::array<std::array<gdouble, 8>, 32> rows{};
	for (guint y = 0; y < 32; y++)
		{
		for (guint u = 0; u < 8; u++)
			{
			for (guint x = 0; x < 32; x++)
				{
				rows[y][u] += (grayscale[(y * 32) + x] - mean) * basis[u][x];
				}
			}
		}

	std::array<gdouble, 64> coefficients{};
	for (guint v = 0; v < 8; v++)
		{
		for (guint u = 0; u < 8; u++)
			{
			gdouble value = 0;
			for (guint y = 0; y < 32; y++) value += rows[y][u] * basis[v][y];
			// Discard floating-point noise around mathematically zero coefficients.
			coefficients[(v * 8) + u] = std::abs(value) < 1e-7 ? 0.0 : value;
			}
		}

	PerceptualHashes hashes{};
	for (guint orientation = 0; orientation < hashes.size(); orientation++)
		{
		std::array<gdouble, 64> transformed{};
		for (guint v = 0; v < 8; v++)
			{
			for (guint u = 0; u < 8; u++)
				{
				// A transpose swaps frequencies; reflections negate odd frequencies.
				gdouble value = coefficients[(orientation & 1) ? (u * 8) + v : (v * 8) + u];
				if ((orientation & 2) && (v & 1)) value = -value;
				if ((orientation & 4) && (u & 1)) value = -value;
				transformed[(v * 8) + u] = value;
				}
			}

		// The DC coefficient is excluded. Bit zero stays clear, leaving 63 meaningful bits.
		std::array<gdouble, 63> ranked{};
		std::copy(transformed.begin() + 1, transformed.end(), ranked.begin());
		std::nth_element(ranked.begin(), ranked.begin() + 31, ranked.end());
		const gdouble median = ranked[31];
		for (guint bit = 1; bit < transformed.size(); bit++)
			{
			// Treat near-equal coefficients as ties, including after a transpose.
			if (transformed[bit] - median > 1e-7) hashes[orientation] |= guint64{1} << bit;
			}
		}

	perceptual_hashes = hashes;
	return true;
}

void ImageSimilarityData::fill_data(const GdkPixbuf *pixbuf)
{
	filled = false;
	perceptual_hashes.reset();
	if (!pixbuf) return;

	const gint w = gdk_pixbuf_get_width(pixbuf);
	const gint h = gdk_pixbuf_get_height(pixbuf);
	const gint rs = gdk_pixbuf_get_rowstride(pixbuf);
	const guchar *pix = gdk_pixbuf_read_pixels(pixbuf);
	const gint p_step = gdk_pixbuf_get_has_alpha(pixbuf) ? 4 : 3;

	gint x_inc = w / 32;
	gint y_inc = h / 32;
	gboolean x_small = FALSE;	/* if less than 32 w or h, set TRUE */
	gboolean y_small = FALSE;
	gint w_left = w;
	gint h_left = h;

	if (x_inc < 1)
		{
		x_inc = 1;
		x_small = TRUE;
		}
	if (y_inc < 1)
		{
		y_inc = 1;
		y_small = TRUE;
		}

	gint j = 0;

	for (gint ys = 0; ys < 32; ys++)
		{
		gint i = 0;

		if (y_small) j = static_cast<gdouble>(h) / 32 * ys;
		else y_inc = std::lround(static_cast<gdouble>(h_left)/(32-ys));

		w_left = w;
		for (gint xs = 0; xs < 32; xs++)
			{
			gint r = 0;
			gint g = 0;
			gint b = 0;

			if (x_small) i = static_cast<gdouble>(w) / 32 * xs;
			else x_inc = std::lround(static_cast<gdouble>(w_left)/(32-xs));

			const gint xy_inc = x_inc * y_inc;
			const guchar *xpos = pix + (i * p_step);

			for (gint y = j; y < j + y_inc; y++)
				{
				const guchar *p = xpos + (y * rs);
				for (gint x = i; x < i + x_inc; x++)
					{
					r += p[0];
					g += p[1];
					b += p[2];
					p += p_step;
					}
				}

			r /= xy_inc;
			g /= xy_inc;
			b /= xy_inc;

			gint t = (ys * 32) + xs;
			avg_r[t] = r;
			avg_g[t] = g;
			avg_b[t] = b;

			i += x_inc;
			w_left -= x_inc;
			}

		j += y_inc;
		h_left -= y_inc;
		}

	filled = true;
	if (options->alternate_similarity_algorithm.enabled) ensure_perceptual_hashes();
}

GdkPixbuf *ImageSimilarityData::to_pixbuf() const
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);

	const gint rs = gdk_pixbuf_get_rowstride(pixbuf);
	guchar *d_pix = gdk_pixbuf_get_pixels(pixbuf);

	for (gint y = 0; y < 32; y++)
		{
		guchar *dp = d_pix + (y * rs);
		const gint sp = y * 32;

		for (gint x = 0; x < 32; x++)
			{
			*(dp++) = avg_r[sp + x];
			*(dp++) = avg_g[sp + x];
			*(dp++) = avg_b[sp + x];
			}
		}

	return pixbuf;
}

bool ImageSimilarityData::fill_data(FILE *f)
{
	guint8 pixel_buf[3];

	for (gint y = 0; y < 32; y++)
		{
		gint s = y * 32;
		for (gint x = 0; x < 32; x++)
			{
			if (fread(&pixel_buf, sizeof(pixel_buf), 1, f) != 1) return false;

			avg_r[s + x] = pixel_buf[0];
			avg_g[s + x] = pixel_buf[1];
			avg_b[s + x] = pixel_buf[2];
			}
		}

	filled = true;
	return true;
}

void ImageSimilarityData::to_string(GString *str) const
{
	guint8 buf[3 * 32];

	for (guint y = 0; y < 32; y++)
		{
		guint s = y * 32;
		guint n = 0;

		for (guint x = 0; x < 32; x++)
			{
			buf[n++] = avg_r[s + x];
			buf[n++] = avg_g[s + x];
			buf[n++] = avg_b[s + x];
			}

		g_string_append_len(str, (const gchar *)buf, sizeof(buf));
		}
}

static gdouble image_sim_compare_perceptual_hash(const ImageSimilarityData *a, const ImageSimilarityData *b)
{
	if (!image_sim_filled(a) || !image_sim_filled(b) || !a->perceptual_hashes || !b->perceptual_hashes) return 0.0;

	guint distance = 63;
	const guint orientations = options->rot_invariant_sim ? 8 : 1;
	// Median thresholds change under reflection. Compare both sets so the score
	// is symmetric and does not depend on either image's initial orientation.
	for (guint orientation_a = 0; orientation_a < orientations; orientation_a++)
		{
		for (guint orientation_b = 0; orientation_b < orientations; orientation_b++)
			{
			const guint64 difference = (*a->perceptual_hashes)[orientation_a] ^ (*b->perceptual_hashes)[orientation_b];
			distance = std::min(distance, static_cast<guint>(__builtin_popcountll(difference)));
			if (distance == 0) return 1.0;
			}
		}

	return 1.0 - (distance / 63.0);
}

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b)
{
	if (options->alternate_similarity_algorithm.enabled) return image_sim_compare_perceptual_hash(a, b);

	return image_sim_data_compare(a, b, [](gdouble){ return false; });
}

/* this uses a cutoff point so that it can abort early when it gets to
 * a point that can simply no longer make the cut-off point.
 */
gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min)
{
	if (options->alternate_similarity_algorithm.enabled)
		{
		const gdouble score = image_sim_compare_perceptual_hash(a, b);
		return score >= min ? score : 0.0;
		}

	min = 1.0 - min;
	return image_sim_data_compare(a, b, [min](gdouble sim){ return (sim / (255.0 * 1024.0 * 3.0)) > min; });
}

bool image_sim_filled(const ImageSimilarityData *sd)
{
	return sd && sd->filled;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
