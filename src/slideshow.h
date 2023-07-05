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

#ifndef SLIDESHOW_H
#define SLIDESHOW_H


#include <glib.h>

struct CollectInfo;
struct CollectionData;
struct ImageWindow;
struct LayoutWindow;
struct SlideShowData;

#define SLIDESHOW_SUBSECOND_PRECISION 10
#define SLIDESHOW_MIN_SECONDS    0.1
#define SLIDESHOW_MAX_SECONDS 86399.0 /* 24 hours - 1 sec */

/*
 * It works like this, it uses path_list, if that does not exist, it uses
 * CollectionData, then finally falls back to the layout listing.
 */

void slideshow_free(SlideShowData *ss);

gboolean slideshow_should_continue(SlideShowData *ss);

void slideshow_next(SlideShowData *ss);
void slideshow_prev(SlideShowData *ss);

SlideShowData *slideshow_start_from_filelist(LayoutWindow *target_lw, ImageWindow *imd, GList *list,
					      void (*stop_func)(SlideShowData *, gpointer), gpointer stop_data);
SlideShowData *slideshow_start_from_collection(LayoutWindow *target_lw, ImageWindow *imd, CollectionData *cd,
					       void (*stop_func)(SlideShowData *, gpointer), gpointer stop_data,
					       CollectInfo *start_info);
SlideShowData *slideshow_start(LayoutWindow *lw, gint start_point,
			       void (*stop_func)(SlideShowData *, gpointer), gpointer stop_data);

gboolean slideshow_paused(SlideShowData *ss);
void slideshow_pause_set(SlideShowData *ss, gboolean paused);
gboolean slideshow_pause_toggle(SlideShowData *ss);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
