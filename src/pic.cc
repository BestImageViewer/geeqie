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

#include "pic.h"

pic::pic(char const *cname): name(cname), equivalent{name}, sim(NULL) {
    GError *err = NULL;
    GdkPixbuf *buf = gdk_pixbuf_new_from_file(cname, &err);
    if (buf == NULL) {
        fprintf(stderr, "Unable to read file %s: %s\n", cname, err->message);
        g_error_free(err);
        return;
    }
    sim = image_sim_new_from_pixbuf(buf);
    g_object_unref(buf);
}

pic::~pic() {
    if (sim != NULL) image_sim_free(sim);
}

int operator<(const pic &a, const pic &b)
{
    return a.name < b.name;
}

gdouble pic::compare(const pic & other)
{
    if (sim == NULL || other.sim == NULL)
        return 0.0;
    return 100.0 * image_sim_compare(sim, other.sim);
}
