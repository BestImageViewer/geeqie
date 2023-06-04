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


#include "main.h"
#include "similar.h"

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

/*
 * The experimental (alternate) algorithm is only for testing of new techniques to
 * improve the result, and hopes to reduce false positives.
 */

static gboolean alternate_enabled = FALSE;

void image_sim_alternate_set(gboolean enable)
{
	alternate_enabled = enable;
}

//gboolean image_sim_alternate_enabled(void)
//{
	//return alternate_enabled;
//}

ImageSimilarityData *image_sim_new(void)
{
	auto sd = g_new0(ImageSimilarityData, 1);

	return sd;
}

void image_sim_free(ImageSimilarityData *sd)
{
	g_free(sd);
}

static gint image_sim_channel_eq_sort_cb(gconstpointer a, gconstpointer b)
{
	auto pa = static_cast<const gint *>(a);
	auto pb = static_cast<const gint *>(b);
	if (pa[1] < pb[1]) return -1;
	if (pa[1] > pb[1]) return 1;
	return 0;
}

static void image_sim_channel_equal(guint8 *pix, gint len)
{
	gint *buf;
	gint i;
	gint p;

	buf = g_new0(gint, len * 2);

	p = 0;
	for (i = 0; i < len; i++)
		{
		buf[p] = i;
		p++;
		buf[p] = static_cast<gint>(pix[i]);
		p++;
		}

	qsort(buf, len, sizeof(gint) * 2, image_sim_channel_eq_sort_cb);

	p = 0;
	for (i = 0; i < len; i++)
		{
		gint n;

		n = buf[p];
		p += 2;
		pix[n] = static_cast<guint8>(255 * i / len);
		}

	g_free(buf);
}

static void image_sim_channel_norm(guint8 *pix, gint len)
{
	guint8 l, h, delta;
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
 * define these to enable various components of the experimental compare functions
 *
 * Convert the thumbprint to greyscale (ignore all color information when comparing)
 *  #define ALTERNATE_USES_GREYSCALE 1
 *
 * Take into account the difference in change from one pixel to the next
 *  #define ALTERNATE_INCLUDE_COMPARE_CHANGE 1
 */

void image_sim_alternate_processing(ImageSimilarityData *sd)
{
#ifdef ALTERNATE_USES_GREYSCALE
	gint i;
#endif

	if (!alternate_enabled) return;

	image_sim_channel_norm(sd->avg_r, sizeof(sd->avg_r));
	image_sim_channel_norm(sd->avg_g, sizeof(sd->avg_g));
	image_sim_channel_norm(sd->avg_b, sizeof(sd->avg_b));

	image_sim_channel_equal(sd->avg_r, sizeof(sd->avg_r));
	image_sim_channel_equal(sd->avg_g, sizeof(sd->avg_g));
	image_sim_channel_equal(sd->avg_b, sizeof(sd->avg_b));

#ifdef ALTERNATE_USES_GREYSCALE
	for (i = 0; i < sizeof(sd->avg_r); i++)
		{
		guint8 n;

		n = (guint8)((gint)(sd->avg_r[i] + sd->avg_g[i] + sd->avg_b[i]) / 3);
		sd->avg_r[i] = sd->avg_g[i] = sd->avg_b[i] = n;
		}
#endif
}

gint mround(gdouble x)
{
	gint ipart = x;
	gdouble fpart = x-ipart;
	return (fpart < 0.5 ? ipart : ipart+1);
}

void image_sim_fill_data(ImageSimilarityData *sd, GdkPixbuf *pixbuf)
{
	gint w, h;
	gint rs;
	guchar *pix;
	gboolean has_alpha;
	gint p_step;

	guchar *p;
	gint i;
	gint j;
	gint x_inc, y_inc, xy_inc;
	gint xs, ys;
	gint w_left, h_left;

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

	h_left = h;
	for (ys = 0; ys < 32; ys++)
		{
		if (y_small) j = static_cast<gdouble>(h) / 32 * ys;
		        else y_inc = mround(static_cast<gdouble>(h_left)/(32-ys));
		i = 0;

		w_left = w;
		for (xs = 0; xs < 32; xs++)
			{
			gint x, y;
			gint r, g, b;
			gint t;
			guchar *xpos;

			if (x_small) i = static_cast<gdouble>(w) / 32 * xs;
			        else x_inc = mround(static_cast<gdouble>(w_left)/(32-xs));
			xy_inc = x_inc * y_inc;
			r = g = b = 0;
			xpos = pix + (i * p_step);

			for (y = j; y < j + y_inc; y++)
				{
				p = xpos + (y * rs);
				for (x = i; x < i + x_inc; x++)
					{
					r += *p; p++;
					g += *p; p++;
					b += *p; p++;
					if (has_alpha) p++;
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

#ifdef ALTERNATE_INCLUDE_COMPARE_CHANGE
static gdouble alternate_image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min)
{
	gint sim;
	gint i;
	gint j;
	gint ld;

	if (!a || !b || !a->filled || !b->filled) return 0.0;

	min = 1.0 - min;
	sim = 0.0;
	ld = 0;

	for (j = 0; j < 1024; j += 32)
		{
		for (i = j; i < j + 32; i++)
			{
			gint cr, cg, cb;
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
#endif

gdouble image_sim_compare_transfo(ImageSimilarityData *a, ImageSimilarityData *b, gchar transfo)
{
	gint sim;
	gint i1, i2, *i;
	gint j1, j2, *j;

	if (!a || !b || !a->filled || !b->filled) return 0.0;

	sim = 0.0;

	if (transfo & 1) { i = &j2; j = &i2; } else { i = &i2; j = &j2; }
	for (j1 = 0; j1 < 32; j1++)
		{
		if (transfo & 2) *j = 31-j1; else *j = j1;
		for (i1 = 0; i1 < 32; i1++)
			{
			if (transfo & 4) *i = 31-i1; else *i = i1;
			sim += abs(a->avg_r[i1*32+j1] - b->avg_r[i2*32+j2]);
			sim += abs(a->avg_g[i1*32+j1] - b->avg_g[i2*32+j2]);
			sim += abs(a->avg_b[i1*32+j1] - b->avg_b[i2*32+j2]);
			}
		}

	return 1.0 - (static_cast<gdouble>(sim) / (255.0 * 1024.0 * 3.0));
}

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b)
{
	gint max_t = (options->rot_invariant_sim ? 8 : 1);

	gint t;
	gdouble score, max_score = 0;

	for(t = 0; t < max_t; t++)
	{
		score = image_sim_compare_transfo(a, b, t);
		if (score > max_score) max_score = score;
	}
	return max_score;
}


/*
4 rotations (0, 90, 180, 270) combined with two mirrors (0, H)
generate all possible isometric transformations
= 8 tests
= change dir of x, change dir of y, exchange x and y = 2^3 = 8
*/
gdouble image_sim_compare_fast_transfo(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min, gchar transfo)
{
	gint sim;
	gint i1, i2, *i;
	gint j1, j2, *j;

#ifdef ALTERNATE_INCLUDE_COMPARE_CHANGE
	if (alternate_enabled) return alternate_image_sim_compare_fast(a, b, min);
#endif

	if (!a || !b || !a->filled || !b->filled) return 0.0;

	min = 1.0 - min;
	sim = 0.0;

	if (transfo & 1) { i = &j2; j = &i2; } else { i = &i2; j = &j2; }
	for (j1 = 0; j1 < 32; j1++)
		{
		if (transfo & 2) *j = 31-j1; else *j = j1;
		for (i1 = 0; i1 < 32; i1++)
			{
			if (transfo & 4) *i = 31-i1; else *i = i1;
			sim += abs(a->avg_r[i1*32+j1] - b->avg_r[i2*32+j2]);
			sim += abs(a->avg_g[i1*32+j1] - b->avg_g[i2*32+j2]);
			sim += abs(a->avg_b[i1*32+j1] - b->avg_b[i2*32+j2]);
			}
		/* check for abort, if so return 0.0 */
		if (static_cast<gdouble>(sim) / (255.0 * 1024.0 * 3.0) > min) return 0.0;
		}

	return (1.0 - (static_cast<gdouble>(sim) / (255.0 * 1024.0 * 3.0)) );
}

/* this uses a cutoff point so that it can abort early when it gets to
 * a point that can simply no longer make the cut-off point.
 */
gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min)
{
	gint max_t = (options->rot_invariant_sim ? 8 : 1);

	gint t;
	gdouble score, max_score = 0;

	for(t = 0; t < max_t; t++)
	{
		score = image_sim_compare_fast_transfo(a, b, min, t);
		if (score > max_score) max_score = score;
	}
	return max_score;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
