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

#include <deque>
#include <functional>

#include <glib.h>

struct CollectInfo;
struct CollectionData;
class FileData;
struct ImageWindow;
struct LayoutWindow;

#define SLIDESHOW_SUBSECOND_PRECISION 10
#define SLIDESHOW_MIN_SECONDS    0.1
#define SLIDESHOW_MAX_SECONDS 86399.0 /* 24 hours - 1 sec */

/*
 * It works like this, it uses path_list, if that does not exist, it uses
 * CollectionData, then finally falls back to the layout listing.
 */

struct SlideShowData
{
	LayoutWindow *lw;        /**< use this window to display the slideshow */
	ImageWindow *imd;        /**< use this window only if lw is not available,
	                            @FIXME it is probably required only by img-view.cc and should be dropped with it */

	GList *filelist;
	CollectionData *cd;
	FileData *dir_fd;

	std::deque<gint> list;
	std::deque<gint> list_done;

	FileData *slide_fd;

	guint slide_count;
	guint timeout_id; /**< event source id */

	gboolean from_selection;

	using StopFunc = std::function<void(SlideShowData *)>;
	StopFunc stop_func;

	gboolean paused;
};

void slideshow_free(SlideShowData *ss);

gboolean slideshow_should_continue(SlideShowData *ss);

void slideshow_next(SlideShowData *ss);
void slideshow_prev(SlideShowData *ss);

SlideShowData *slideshow_start_from_filelist(LayoutWindow *target_lw, ImageWindow *imd, GList *list,
                                             const SlideShowData::StopFunc &stop_func);
SlideShowData *slideshow_start_from_collection(LayoutWindow *target_lw, ImageWindow *imd,
                                               CollectionData *cd, CollectInfo *start_info,
                                               const SlideShowData::StopFunc &stop_func);
SlideShowData *slideshow_start(LayoutWindow *lw, const SlideShowData::StopFunc &stop_func);

void slideshow_get_index_and_total(SlideShowData *ss, gint &index, gint &total);

gboolean slideshow_paused(SlideShowData *ss);
void slideshow_pause_toggle(SlideShowData *ss);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
