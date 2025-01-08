/*
 * Copyright (C) 2024 The Geeqie Team
 *
 * Author: Marcin Owsiany <marcin@owsiany.pl>
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
 *
 *
 * Helper class for computing equivalence sets of pictures.
 *
 */

#include "pic-equiv.h"

/**
 * @param cname path to picture file to represent
 */
pic_equiv::pic_equiv(char const *cname) : name(cname), equivalent{name}, sim(load_image_sim(cname)) {}

ImageSimilarityData *pic_equiv::load_image_sim(const char *cname)
{
	g_autoptr(GError) error = nullptr;
	g_autoptr(GdkPixbuf) buf = gdk_pixbuf_new_from_file(cname, &error);
	if (error)
		{
		fprintf(stderr, "Unable to read file %s: %s\n", cname, error->message);
		return nullptr;
		}
	return image_sim_new_from_pixbuf(buf);
}

pic_equiv::~pic_equiv()
{
	if (sim != nullptr)
		image_sim_free(sim);
}

/**
 * @brief orders two pic_equiv objects, according to the paths they represent
 */
int operator<(const pic_equiv &a, const pic_equiv &b)
{
	return a.name < b.name;
}

/**
 * @brief compares this pic_equiv object to another
 * @param other object to compare to
 * @returns a number between 0 and 100 which denotes visual similarity
 */
gdouble pic_equiv::compare(const pic_equiv &other)
{
	if (sim == nullptr || other.sim == nullptr)
		return 0.0;
	return 100.0 * image_sim_compare(sim, other.sim);
}
