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

#include "slideshow.h"

#include <algorithm>
#include <numeric>
#include <random>

#include "collect.h"
#include "filedata.h"
#include "image.h"
#include "layout-image.h"
#include "layout.h"
#include "options.h"

namespace
{

void move_first_list_item(std::deque<gint> &src, std::deque<gint> &dst)
{
	dst.push_front(src.front());
	src.pop_front();
}

inline FileData *slideshow_get_fd(SlideShow *ss)
{
	return ss->lw ? layout_image_get_fd(ss->lw) : image_get_fd(ss->imd);
}

} // namespace

void slideshow_free(SlideShow *ss)
{
	if (!ss) return;

	g_clear_handle_id(&ss->timeout_id, g_source_remove);

	if (ss->stop_func) ss->stop_func(ss);

	if (ss->filelist) file_data_list_free(ss->filelist);
	if (ss->cd) collection_unref(ss->cd);
	file_data_unref(ss->dir_fd);

	file_data_unref(ss->slide_fd);

	delete ss;
}

static void slideshow_list_init(SlideShow *ss, gint start_index)
{
	ss->list_done.clear();
	ss->list.clear();

	if (ss->from_selection)
		{
		g_autoptr(GList) list = layout_selection_list_by_index(ss->lw);

		for (GList *work = list; work; work = work->next)
			{
			ss->list.push_back(GPOINTER_TO_INT(work->data));
			}
		}
	else
		{
		ss->list.resize(ss->slide_count);
		std::iota(ss->list.begin(), ss->list.end(), 0);
		}

	if (options->slideshow.random)
		{
		std::shuffle(ss->list.begin(), ss->list.end(), std::mt19937{std::random_device{}()});
		}
	else if (start_index > 0)
		{
		/* start with specified image by skipping to it */
		const auto n = std::min<gint>(ss->list.size(), start_index);
		for (gint i = 0; i < n; i++)
			{
			move_first_list_item(ss->list, ss->list_done);
			}
		}
}

gboolean slideshow_should_continue(SlideShow *ss)
{
	FileData *dir_fd;

	if (!ss) return FALSE;

	FileData *imd_fd = slideshow_get_fd(ss);

	if ( ((imd_fd == nullptr) != (ss->slide_fd == nullptr)) ||
	    (imd_fd && ss->slide_fd && imd_fd != ss->slide_fd) ) return FALSE;

	if (ss->filelist) return TRUE;

	if (ss->cd)
		{
		if (g_list_length(ss->cd->list) == ss->slide_count)
			return TRUE;

		return FALSE;
		}

	dir_fd = ss->lw->dir_fd;

	if (dir_fd && ss->dir_fd && dir_fd == ss->dir_fd)
		{
		if (ss->from_selection && ss->slide_count == layout_selection_count(ss->lw, nullptr)) return TRUE;
		if (!ss->from_selection && ss->slide_count == layout_list_count(ss->lw, nullptr)) return TRUE;
		}

	return FALSE;
}

static gboolean slideshow_step(SlideShow *ss, gboolean forward)
{
	if (!slideshow_should_continue(ss)) return FALSE;

	if (forward)
		{
		if (ss->list.empty()) return TRUE;

		move_first_list_item(ss->list, ss->list_done);
		}
	else
		{
		if (ss->list_done.size() <= 1) return TRUE;

		move_first_list_item(ss->list_done, ss->list);
		}

	gint row = ss->list_done.front();

	file_data_unref(ss->slide_fd);
	ss->slide_fd = nullptr;

	if (ss->filelist)
		{
		ss->slide_fd = file_data_ref((FileData *)g_list_nth_data(ss->filelist, row));
		if (ss->lw)
			layout_set_fd(ss->lw, ss->slide_fd);
		else
			image_change_fd(ss->imd, ss->slide_fd, image_zoom_get_default(ss->imd));
		}
	else if (ss->cd)
		{
		CollectInfo *info;

		info = static_cast<CollectInfo *>(g_list_nth_data(ss->cd->list, row));
		ss->slide_fd = file_data_ref(info->fd);

		ImageWindow *imd = ss->lw ? ss->lw->image : ss->imd;
		image_change_from_collection(imd, ss->cd, info, image_zoom_get_default(imd));
		}
	else
		{
		ss->slide_fd = file_data_ref(layout_list_get_fd(ss->lw, row));

		if (ss->from_selection)
			{
			layout_set_fd(ss->lw, ss->slide_fd);
			layout_status_update_info(ss->lw, nullptr);
			}
		else
			{
			layout_image_set_index(ss->lw, row);
			}
		}

	if (ss->list.empty() && options->slideshow.repeat)
		{
		slideshow_list_init(ss, -1);
		}

	if (ss->list.empty())
		{
		return FALSE;
		}

	/* read ahead */
	if (options->image.enable_read_ahead && (!ss->lw || ss->from_selection))
		{
		gint r;
		if (forward)
			{
			r = ss->list.front();
			}
		else
			{
			if (ss->list_done.size() <= 1) return TRUE;
			r = ss->list_done[1];
			}

		if (ss->filelist)
			{
			image_prebuffer_set(ss->imd, static_cast<FileData *>(g_list_nth_data(ss->filelist, r)));
			}
		else if (ss->cd)
			{
			CollectInfo *info;
			info = static_cast<CollectInfo *>(g_list_nth_data(ss->cd->list, r));
			if (info) image_prebuffer_set(ss->imd, info->fd);
			}
		else if (ss->from_selection)
			{
			image_prebuffer_set(ss->lw->image, layout_list_get_fd(ss->lw, r));
			}
		}

	return TRUE;
}

static void slideshow_timer_reset(SlideShow *ss);

static gboolean slideshow_loop_cb(gpointer data)
{
	auto *ss = static_cast<SlideShow *>(data);

	if (ss->paused) return G_SOURCE_CONTINUE;

	if (slideshow_step(ss, TRUE))
		{
		/* Check if the user has changed the timer interval */
		slideshow_timer_reset(ss);

		return G_SOURCE_CONTINUE;
		}

	ss->timeout_id = 0;
	slideshow_free(ss);
	return G_SOURCE_REMOVE;
}

static void slideshow_timer_reset(SlideShow *ss)
{
	options->slideshow.delay = std::max(options->slideshow.delay, 1);

	if (ss->timeout_id) g_source_remove(ss->timeout_id);
	ss->timeout_id = g_timeout_add(options->slideshow.delay * 1000 / SLIDESHOW_SUBSECOND_PRECISION,
				       slideshow_loop_cb, ss);
}

static void slideshow_move(SlideShow *ss, gboolean forward)
{
	if (!ss) return;

	if (!slideshow_step(ss, forward))
		{
		slideshow_free(ss);
		return;
		}

	slideshow_timer_reset(ss);
}

void slideshow_next(SlideShow *ss)
{
	slideshow_move(ss, TRUE);
}

void slideshow_prev(SlideShow *ss)
{
	slideshow_move(ss, FALSE);
}

static SlideShow *slideshow_new(LayoutWindow *target_lw, ImageWindow *imd)
{
	auto *ss = new SlideShow();
	ss->lw = target_lw;
	ss->imd = imd; /** @FIXME ss->imd is used only for img-view.cc and can be dropped with it */

	return ss;
}

static SlideShow *slideshow_start_real(SlideShow *ss, gint start_index,
                                       const SlideShow::StopFunc &stop_func)
{
	slideshow_list_init(ss, start_index);

	ss->slide_fd = file_data_ref(slideshow_get_fd(ss));

	if (!slideshow_step(ss, TRUE))
		{
		slideshow_free(ss);
		return nullptr;
		}

	slideshow_timer_reset(ss);

	ss->stop_func = stop_func;

	return ss;
}

SlideShow *SlideShow::start_from_filelist(LayoutWindow *target_lw, ImageWindow *imd,
                                          GList *list, const StopFunc &stop_func)
{
	if (!list) return nullptr;

	SlideShow *ss = slideshow_new(target_lw, imd);
	ss->filelist = list;
	ss->slide_count = g_list_length(ss->filelist);

	return slideshow_start_real(ss, -1, stop_func);
}

SlideShow *SlideShow::start_from_collection(LayoutWindow *target_lw, ImageWindow *imd,
                                            CollectionData *cd, CollectInfo *start_info,
                                            const StopFunc &stop_func)
{
	if (!cd) return nullptr;

	SlideShow *ss = slideshow_new(target_lw, imd);
	ss->cd = cd;
	ss->slide_count = g_list_length(ss->cd->list);

	collection_ref(ss->cd);

	return slideshow_start_real(ss, (!options->slideshow.random && start_info) ?
	                                g_list_index(ss->cd->list, start_info) : -1,
	                            stop_func);
}

SlideShow *SlideShow::start(LayoutWindow *lw, const StopFunc &stop_func)
{
	if (layout_list_count(lw, nullptr) < 1) return nullptr;

	SlideShow *ss = slideshow_new(lw, nullptr);
	ss->dir_fd = file_data_ref(ss->lw->dir_fd);
	ss->slide_count = layout_selection_count(ss->lw, nullptr);

	gint start_index = -1;
	if (ss->slide_count < 2)
		{
		ss->slide_count = layout_list_count(ss->lw, nullptr);
		if (!options->slideshow.random)
			{
			const gint start_point = layout_list_get_index(lw, layout_image_get_fd(lw));
			if (start_point >= 0 && static_cast<guint>(start_point) < ss->slide_count)
				{
				start_index = start_point;
				}
			}
		}
	else
		{
		ss->from_selection = TRUE;
		}

	return slideshow_start_real(ss, start_index, stop_func);
}

void slideshow_get_index_and_total(SlideShow *ss, gint &index, gint &total)
{
	index = ss->list_done.empty() ? ss->list.size() : ss->list_done.size();
	total = ss->list_done.size() + ss->list.size();
}

gboolean slideshow_paused(SlideShow *ss)
{
	if (!ss) return FALSE;

	return ss->paused;
}

void slideshow_pause_toggle(SlideShow *ss)
{
	if (!ss) return;

	ss->paused = !ss->paused;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
