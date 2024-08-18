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
#include <vector>

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
 */

namespace
{

using ImageSimilarityCheckAbort = std::function<bool(gdouble)>;

void image_sim_channel_equal(guint8 *pix, gsize len)
{
	struct IndexedPix
	{
		gsize index;
		guint8 pix;
	};

	std::vector<IndexedPix> buf;
	buf.reserve(len);

	for (gsize i = 0; i < len; i++)
		{
		buf.push_back({i, pix[i]});
		}

	std::sort(buf.begin(), buf.end(), [](const IndexedPix &a, const IndexedPix &b){ return a.pix < b.pix; });

	for (gsize i = 0; i < len; i++)
		{
		gint n = buf[i].index;

		pix[n] = static_cast<guint8>(255 * i / len);
		}
}

/*
 * 4 rotations (0, 90, 180, 270) combined with two mirrors (0, H)
 * generate all possible isometric transformations
 * = 8 tests
 * = change dir of x, change dir of y, exchange x and y = 2^3 = 8
 */
gdouble image_sim_data_compare_transfo(const ImageSimilarityData *a, const ImageSimilarityData *b, gchar transfo, const ImageSimilarityCheckAbort &check_abort)
{
	if (!a || !b || !a->filled || !b->filled) return 0.0;

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
			sim += abs(a->avg_r[i1*32+j1] - b->avg_r[i2*32+j2]);
			sim += abs(a->avg_g[i1*32+j1] - b->avg_g[i2*32+j2]);
			sim += abs(a->avg_b[i1*32+j1] - b->avg_b[i2*32+j2]);
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

ImageSimilarityData *image_sim_new()
{
	auto sd = g_new0(ImageSimilarityData, 1);

	return sd;
}

void image_sim_free(ImageSimilarityData *sd)
{
	g_free(sd);
}

static void image_sim_channel_norm(guint8 *pix, gint len)
{
	guint8 l;
	guint8 h;
	guint8 delta;
	gint i;
	gdouble scale;

	l = h = pix[0];

	for (i = 0; i < len; i++)
		{
		if (pix[i] < l) l = pix[i];
		if (pix[i] > h) h = pix[i];
		}

	delta = h - l;
	scale = (delta != 0) ? 255.0 / static_cast<gdouble>(delta) : 1.0;

	for (i = 0; i < len; i++)
		{
		pix[i] = static_cast<guint8>(static_cast<gdouble>(pix[i] - l) * scale);
		}
}

/*
 * The Alternate algorithm is only for testing of new techniques to
 * improve the result, and hopes to reduce false positives.
 */
void image_sim_alternate_processing(ImageSimilarityData *sd)
{
	gint i;

	if (!options->alternate_similarity_algorithm.enabled)
		{
		return;
		}

	image_sim_channel_norm(sd->avg_r, sizeof(sd->avg_r));
	image_sim_channel_norm(sd->avg_g, sizeof(sd->avg_g));
	image_sim_channel_norm(sd->avg_b, sizeof(sd->avg_b));

	image_sim_channel_equal(sd->avg_r, sizeof(sd->avg_r));
	image_sim_channel_equal(sd->avg_g, sizeof(sd->avg_g));
	image_sim_channel_equal(sd->avg_b, sizeof(sd->avg_b));

	if (options->alternate_similarity_algorithm.grayscale)
		{
		for (i = 0; i < (gint)sizeof(sd->avg_r); i++)
			{
			guint8 n;

			n = (guint8)((gint)(sd->avg_r[i] + sd->avg_g[i] + sd->avg_b[i]) / 3);
			sd->avg_r[i] = sd->avg_g[i] = sd->avg_b[i] = n;
			}
		}
}

void image_sim_fill_data(ImageSimilarityData *sd, GdkPixbuf *pixbuf)
{
	gint w;
	gint h;
	gint rs;
	guchar *pix;
	gboolean has_alpha;
	gint p_step;

	guchar *p;
	gint i;
	gint j;
	gint x_inc;
	gint y_inc;
	gint xy_inc;
	gint xs;
	gint ys;
	gint w_left;
	gint h_left;

	gboolean x_small = FALSE;	/* if less than 32 w or h, set TRUE */
	gboolean y_small = FALSE;
	if (!sd || !pixbuf) return;

	w = gdk_pixbuf_get_width(pixbuf);
	h = gdk_pixbuf_get_height(pixbuf);
	rs = gdk_pixbuf_get_rowstride(pixbuf);
	pix = gdk_pixbuf_get_pixels(pixbuf);
	has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

	p_step = has_alpha ? 4 : 3;
	x_inc = w / 32;
	y_inc = h / 32;
	w_left = w;
	h_left = h;

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

	j = 0;

	for (ys = 0; ys < 32; ys++)
		{
		if (y_small) j = static_cast<gdouble>(h) / 32 * ys;
		else y_inc = std::lround(static_cast<gdouble>(h_left)/(32-ys));
		i = 0;

		w_left = w;
		for (xs = 0; xs < 32; xs++)
			{
			gint x;
			gint y;
			gint r;
			gint g;
			gint b;
			gint t;
			guchar *xpos;

			if (x_small) i = static_cast<gdouble>(w) / 32 * xs;
			else x_inc = std::lround(static_cast<gdouble>(w_left)/(32-xs));
			xy_inc = x_inc * y_inc;
			r = g = b = 0;
			xpos = pix + (i * p_step);

			for (y = j; y < j + y_inc; y++)
				{
				p = xpos + (y * rs);
				for (x = i; x < i + x_inc; x++)
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

			t = ys * 32 + xs;
			sd->avg_r[t] = r;
			sd->avg_g[t] = g;
			sd->avg_b[t] = b;

			i += x_inc;
			w_left -= x_inc;
			}

		j += y_inc;
		h_left -= y_inc;
		}

	sd->filled = TRUE;
}

ImageSimilarityData *image_sim_new_from_pixbuf(GdkPixbuf *pixbuf)
{
	ImageSimilarityData *sd;

	sd = image_sim_new();
	image_sim_fill_data(sd, pixbuf);

	return sd;
}

static gdouble alternate_image_sim_compare_fast(const ImageSimilarityData *a, const ImageSimilarityData *b, gdouble min)
{
	gint sim;
	gint i;
	gint j;
	gint ld;

	if (!a || !b || !a->filled || !b->filled) return 0.0;

	sim = 0.0;
	ld = 0;

	for (j = 0; j < 1024; j += 32)
		{
		for (i = j; i < j + 32; i++)
			{
			gint cr;
			gint cg;
			gint cb;
			gint cd;

			cr = abs(a->avg_r[i] - b->avg_r[i]);
			cg = abs(a->avg_g[i] - b->avg_g[i]);
			cb = abs(a->avg_b[i] - b->avg_b[i]);

			cd = cr + cg + cb;
			sim += cd + abs(cd - ld);
			ld = cd / 3;
			}
		/* check for abort, if so return 0.0 */
		if ((gdouble)sim / (255.0 * 1024.0 * 4.0) > min) return 0.0;
		}

	return (1.0 - ((gdouble)sim / (255.0 * 1024.0 * 4.0)) );
}

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b)
{
	return image_sim_data_compare(a, b, [](gdouble){ return false; });
}

/* this uses a cutoff point so that it can abort early when it gets to
 * a point that can simply no longer make the cut-off point.
 */
gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min)
{
	min = 1.0 - min;

	if (options->alternate_similarity_algorithm.enabled)
		{
		return alternate_image_sim_compare_fast(a, b, min);
		}

	return image_sim_data_compare(a, b, [min](gdouble sim){ return (sim / (255.0 * 1024.0 * 3.0)) > min; });
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
