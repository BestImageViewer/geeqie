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

#include <array>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

struct ImageSimilarityData
{
	void fill_data(GdkPixbuf *pixbuf);

	using Avg = std::array<guint8, 1024>;
	Avg avg_r;
	Avg avg_g;
	Avg avg_b;

	gboolean filled;
};


ImageSimilarityData *image_sim_new();
void image_sim_free(ImageSimilarityData *sd);

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b);
gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min);

gboolean image_sim_filled(const ImageSimilarityData *sd);

void image_sim_alternate_set(gboolean enable);
void image_sim_alternate_processing(ImageSimilarityData *sd);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
