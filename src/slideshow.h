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

struct SlideShow
{
	using StopFunc = std::function<void(SlideShow *)>;

	~SlideShow();

	bool should_continue() const;

	void next();
	void prev();

	static SlideShow *start_from_filelist(LayoutWindow *target_lw, ImageWindow *imd,
	                                      GList *list, const StopFunc &stop_func);
	static SlideShow *start_from_collection(LayoutWindow *target_lw, ImageWindow *imd,
	                                        CollectionData *cd, CollectInfo *start_info,
	                                        const StopFunc &stop_func);
	static SlideShow *start(LayoutWindow *lw, const StopFunc &stop_func);

	void get_index_and_total(gint &index, gint &total) const;

	gboolean is_paused() const;
	void pause_toggle();

	LayoutWindow *lw = nullptr;        /**< use this window to display the slideshow */
	ImageWindow *imd = nullptr;        /**< use this window only if lw is not available,
	                                      @FIXME it is probably required only by img-view.cc and should be dropped with it */

	GList *filelist = nullptr;
	CollectionData *cd = nullptr;
	FileData *dir_fd = nullptr;

	std::deque<gint> list{};
	std::deque<gint> list_done{};

	FileData *slide_fd = nullptr;

	guint slide_count = 0;
	guint timeout_id = 0; /**< event source id */

	bool from_selection = false;

	StopFunc stop_func{};

	gboolean paused = FALSE;

private:
	SlideShow(LayoutWindow *target_lw, ImageWindow *imd)
	    : lw(target_lw)
	    , imd(imd)
	{}
};

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
