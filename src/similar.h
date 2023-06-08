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

#ifndef SIMILAR_H
#define SIMILAR_H


struct ImageSimilarityData
{
	guint8 avg_r[1024];
	guint8 avg_g[1024];
	guint8 avg_b[1024];

	gboolean filled;
};


ImageSimilarityData *image_sim_new(void);
void image_sim_free(ImageSimilarityData *sd);

void image_sim_fill_data(ImageSimilarityData *sd, GdkPixbuf *pixbuf);
ImageSimilarityData *image_sim_new_from_pixbuf(GdkPixbuf *pixbuf);

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b);
gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min);


void image_sim_alternate_set(gboolean enable);
gboolean image_sim_alternate_enabled(void);
void image_sim_alternate_processing(ImageSimilarityData *sd);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
