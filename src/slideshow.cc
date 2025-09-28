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
#include <cstdlib>
#include <utility>

#include "collect.h"
#include "filedata.h"
#include "image.h"
#include "layout-image.h"
#include "layout.h"
#include "options.h"

namespace
{

void move_first_list_item(GList **src, GList **dst)
{
	gpointer data = (*src)->data;
	*dst = g_list_prepend(*dst, data);
	*src = g_list_remove(*src, data);
}

inline FileData *slideshow_get_fd(SlideShowData *ss)
{
	return ss->lw ? layout_image_get_fd(ss->lw) : image_get_fd(ss->imd);
}

} // namespace

void slideshow_free(SlideShowData *ss)
{
	if (!ss) return;

	g_clear_handle_id(&ss->timeout_id, g_source_remove);

	if (ss->stop_func) ss->stop_func(ss);

	if (ss->filelist) file_data_list_free(ss->filelist);
	if (ss->cd) collection_unref(ss->cd);
	file_data_unref(ss->dir_fd);

	g_list_free(ss->list);
	g_list_free(ss->list_done);

	file_data_unref(ss->slide_fd);

	g_free(ss);
}

static GList *generate_list(SlideShowData *ss)
{
	GList *list = nullptr;

	if (ss->from_selection)
		{
		list = layout_selection_list_by_index(ss->lw);
		}
	else
		{
		guint i;
		for (i = 0; i < ss->slide_count; i++)
			{
			list = g_list_prepend(list, GINT_TO_POINTER(i));
			}
		list = g_list_reverse(list);
		}

	return list;
}

static GPtrArray *generate_ptr_array_from_list(GList *src_list)
{
	GPtrArray *arr = g_ptr_array_sized_new(g_list_length(src_list));

	static const auto ptr_array_add = [](gpointer data, gpointer user_data)
	{
		auto *array = static_cast<GPtrArray *>(user_data);
		g_ptr_array_add(array, data);
	};

	g_list_foreach(src_list, ptr_array_add, arr);

	return arr;
}

static void slideshow_randomize_list(SlideShowData *ss)
{
	GPtrArray *array = generate_ptr_array_from_list(ss->list);

	for (guint i = 0; i < array->len; ++i)
		{
		guint p = g_random_int_range(0, array->len);
		std::swap(g_ptr_array_index(array, i), g_ptr_array_index(array, p));
		}

	static const auto list_prepend = [](gpointer data, gpointer user_data)
	{
		auto *ss = static_cast<SlideShowData *>(user_data);
		ss->list = g_list_prepend(ss->list, data);
	};

	g_clear_list(&ss->list, NULL);
	g_ptr_array_foreach(array, list_prepend, &ss);

	g_ptr_array_free(array, TRUE);
}

static void slideshow_list_init(SlideShowData *ss, gint start_index)
{
	g_list_free(ss->list_done);
	ss->list_done = nullptr;

	g_list_free(ss->list);
	ss->list = generate_list(ss);

	if (options->slideshow.random)
		{
		slideshow_randomize_list(ss);
		}
	else if (start_index > 0)
		{
		/* start with specified image by skipping to it */
		for (gint i = 0; ss->list && i < start_index; i++)
			{
			move_first_list_item(&ss->list, &ss->list_done);
			}
		}
}

gboolean slideshow_should_continue(SlideShowData *ss)
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

static gboolean slideshow_step(SlideShowData *ss, gboolean forward)
{
	if (!slideshow_should_continue(ss)) return FALSE;

	if (forward)
		{
		if (!ss->list) return TRUE;

		move_first_list_item(&ss->list, &ss->list_done);
		}
	else
		{
		if (!ss->list_done || !ss->list_done->next) return TRUE;

		move_first_list_item(&ss->list_done, &ss->list);
		}

	auto row = GPOINTER_TO_INT(ss->list_done->data);

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

	if (!ss->list && options->slideshow.repeat)
		{
		slideshow_list_init(ss, -1);
		}

	if (!ss->list)
		{
		return FALSE;
		}

	/* read ahead */
	if (options->image.enable_read_ahead && (!ss->lw || ss->from_selection))
		{
		gint r;
		if (forward)
			{
			r = GPOINTER_TO_INT(ss->list->data);
			}
		else
			{
			if (!ss->list_done || !ss->list_done->next) return TRUE;
			r = GPOINTER_TO_INT(ss->list_done->next->data);
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

static void slideshow_timer_reset(SlideShowData *ss);

static gboolean slideshow_loop_cb(gpointer data)
{
	auto ss = static_cast<SlideShowData *>(data);

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

static void slideshow_timer_reset(SlideShowData *ss)
{
	options->slideshow.delay = std::max(options->slideshow.delay, 1);

	if (ss->timeout_id) g_source_remove(ss->timeout_id);
	ss->timeout_id = g_timeout_add(options->slideshow.delay * 1000 / SLIDESHOW_SUBSECOND_PRECISION,
				       slideshow_loop_cb, ss);
}

static void slideshow_move(SlideShowData *ss, gboolean forward)
{
	if (!ss) return;

	if (!slideshow_step(ss, forward))
		{
		slideshow_free(ss);
		return;
		}

	slideshow_timer_reset(ss);
}

void slideshow_next(SlideShowData *ss)
{
	slideshow_move(ss, TRUE);
}

void slideshow_prev(SlideShowData *ss)
{
	slideshow_move(ss, FALSE);
}

static SlideShowData *real_slideshow_start(LayoutWindow *target_lw, ImageWindow *imd,
                                           GList *filelist, gint start_point,
                                           CollectionData *cd, CollectInfo *start_info,
                                           const SlideShowData::StopFunc &stop_func)
{
	SlideShowData *ss;
	gint start_index = -1;

	if (!filelist && !cd && layout_list_count(target_lw, nullptr) < 1) return nullptr;

	ss = g_new0(SlideShowData, 1);

	ss->lw = target_lw;
	ss->imd = imd; /** @FIXME ss->imd is used only for img-view.cc and can be dropped with it */
	ss->filelist = filelist;
	ss->cd = cd;

	if (ss->filelist)
		{
		ss->slide_count = g_list_length(ss->filelist);
		}
	else if (ss->cd)
		{
		collection_ref(ss->cd);
		ss->slide_count = g_list_length(ss->cd->list);
		if (!options->slideshow.random && start_info)
			{
			start_index = g_list_index(ss->cd->list, start_info);
			}
		}
	else
		{
		/* layout method */

		ss->slide_count = layout_selection_count(ss->lw, nullptr);
		ss->dir_fd = file_data_ref(ss->lw->dir_fd);
		if (ss->slide_count < 2)
			{
			ss->slide_count = layout_list_count(ss->lw, nullptr);
			if (!options->slideshow.random && start_point >= 0 && static_cast<guint>(start_point) < ss->slide_count)
				{
				start_index = start_point;
				}
			}
		else
			{
			ss->from_selection = TRUE;
			}
		}

	slideshow_list_init(ss, start_index);

	ss->slide_fd = file_data_ref(slideshow_get_fd(ss));

	if (slideshow_step(ss, TRUE))
		{
		slideshow_timer_reset(ss);

		ss->stop_func = stop_func;
		}
	else
		{
		slideshow_free(ss);
		ss = nullptr;
		}

	return ss;
}

SlideShowData *slideshow_start_from_filelist(LayoutWindow *target_lw, ImageWindow *imd, GList *list,
                                             const SlideShowData::StopFunc &stop_func)
{
	return real_slideshow_start(target_lw, imd, list, -1, nullptr, nullptr, stop_func);
}

SlideShowData *slideshow_start_from_collection(LayoutWindow *target_lw, ImageWindow *imd,
                                               CollectionData *cd, CollectInfo *start_info,
                                               const SlideShowData::StopFunc &stop_func)
{
	return real_slideshow_start(target_lw, imd, nullptr, -1, cd, start_info, stop_func);
}

SlideShowData *slideshow_start(LayoutWindow *lw, gint start_point,
                               const SlideShowData::StopFunc &stop_func)
{
	return real_slideshow_start(lw, nullptr, nullptr, start_point, nullptr, nullptr, stop_func);
}

gboolean slideshow_paused(SlideShowData *ss)
{
	if (!ss) return FALSE;

	return ss->paused;
}

void slideshow_pause_toggle(SlideShowData *ss)
{
	if (!ss) return;

	ss->paused = !ss->paused;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
