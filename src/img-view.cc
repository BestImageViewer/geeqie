/*
 * Copyright (C) 2006 John Ellis
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

#include "img-view.h"

#include <algorithm>
#include <array>
#include <vector>

#include <gdk/gdk.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "actions.h"
#include "archives.h"
#include "collect-io.h"
#include "collect.h"
#include "compat-deprecated.h"
#include "compat.h"
#include "dnd.h"
#include "editors.h"
#include "filedata.h"
#include "fullscreen.h"
#include "geometry.h"
#include "image-load.h"
#include "image-overlay.h"
#include "image.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "print.h"
#include "slideshow.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-utildlg.h"
#include "uri-utils.h"
#include "utilops.h"
#include "window.h"

namespace
{

struct ViewWindow
{
	GtkWidget *window;
	ImageWindow *imd;
	FullScreenData *fs;
	SlideShow *ss;

	GList *list;
	GList *list_pointer;
};


std::vector<ViewWindow *> view_window_list;

} // namespace

static void image_pop_menu_collections_cb(GSimpleAction *, GVariant *parameter, gpointer data);
static void view_popup_menu(ViewWindow *vw);
static void view_fullscreen_toggle(ViewWindow *vw, gboolean force_off);
static void view_overlay_toggle(GSimpleAction *, GVariant *, gpointer data);

static void view_slideshow_next(ViewWindow *vw);
static void view_slideshow_prev(ViewWindow *vw);
static void view_slideshow_start(ViewWindow *vw);
static void view_slideshow_stop(ViewWindow *vw);

static void view_window_close(ViewWindow *vw);

static void view_window_dnd_init(ViewWindow *vw);

static void view_window_notify_cb(FileData *fd, NotifyType type, gpointer data);

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

static ImageWindow *view_window_active_image(ViewWindow *vw)
{
	if (vw->fs) return vw->fs->imd;

	return vw->imd;
}

static void view_window_set_list(ViewWindow *vw, GList *list)
{

	file_data_list_free(vw->list);
	vw->list = nullptr;
	vw->list_pointer = nullptr;

	vw->list = filelist_copy(list);
}

static gboolean view_window_contains_collection(ViewWindow *vw)
{
	CollectionData *cd;
	CollectInfo *info;

	cd = image_get_collection(view_window_active_image(vw), &info);

	return (cd && info);
}

static void view_collection_step(ViewWindow *vw, gboolean next)
{
	ImageWindow *imd = view_window_active_image(vw);
	CollectionData *cd;
	CollectInfo *info;
	CollectInfo *read_ahead_info = nullptr;

	cd = image_get_collection(imd, &info);

	if (!cd || !info) return;

	if (next)
		{
		info = collection_next_by_info(cd, info);
		if (options->image.enable_read_ahead)
			{
			read_ahead_info = collection_next_by_info(cd, info);
			if (!read_ahead_info) read_ahead_info = collection_prev_by_info(cd, info);
			}
		}
	else
		{
		info = collection_prev_by_info(cd, info);
		if (options->image.enable_read_ahead)
			{
			read_ahead_info = collection_prev_by_info(cd, info);
			if (!read_ahead_info) read_ahead_info = collection_next_by_info(cd, info);
			}
		}

	if (info)
		{
		image_change_from_collection(imd, cd, info, image_zoom_get_default(imd));

		if (read_ahead_info) image_prebuffer_set(imd, read_ahead_info->fd);
		}

}

static void view_collection_step_to_end(ViewWindow *vw, gboolean last)
{
	ImageWindow *imd = view_window_active_image(vw);
	CollectionData *cd;
	CollectInfo *info;
	CollectInfo *read_ahead_info = nullptr;

	cd = image_get_collection(imd, &info);

	if (!cd || !info) return;

	if (last)
		{
		info = collection_get_last(cd);
		if (options->image.enable_read_ahead) read_ahead_info = collection_prev_by_info(cd, info);
		}
	else
		{
		info = collection_get_first(cd);
		if (options->image.enable_read_ahead) read_ahead_info = collection_next_by_info(cd, info);
		}

	if (info)
		{
		image_change_from_collection(imd, cd, info, image_zoom_get_default(imd));
		if (read_ahead_info) image_prebuffer_set(imd, read_ahead_info->fd);
		}
}

static void view_list_step(ViewWindow *vw, gboolean next)
{
	ImageWindow *imd = view_window_active_image(vw);
	FileData *fd;
	GList *work;
	GList *work_ahead;

	if (!vw->list) return;

	fd = image_get_fd(imd);
	if (!fd) return;

	if (g_list_position(vw->list, vw->list_pointer) >= 0)
		{
		work = vw->list_pointer;
		}
	else
		{
		work = g_list_find(vw->list, fd);
		}
	if (!work) return;

	work_ahead = nullptr;
	if (next)
		{
		work = work->next;
		if (work) work_ahead = work->next;
		}
	else
		{
		work = work->prev;
		if (work) work_ahead = work->prev;
		}

	if (!work) return;

	vw->list_pointer = work;
	fd = static_cast<FileData *>(work->data);
	image_change_fd(imd, fd, image_zoom_get_default(imd));

	if (options->image.enable_read_ahead && work_ahead)
		{
		auto next_fd = static_cast<FileData *>(work_ahead->data);
		image_prebuffer_set(imd, next_fd);
		}
}

static void view_list_step_to_end(ViewWindow *vw, gboolean last)
{
	ImageWindow *imd = view_window_active_image(vw);
	FileData *fd;
	GList *work;
	GList *work_ahead;

	if (!vw->list) return;

	if (last)
		{
		work = g_list_last(vw->list);
		work_ahead = work->prev;
		}
	else
		{
		work = vw->list;
		work_ahead = work->next;
		}

	vw->list_pointer = work;
	fd = static_cast<FileData *>(work->data);
	image_change_fd(imd, fd, image_zoom_get_default(imd));

	if (options->image.enable_read_ahead && work_ahead)
		{
		auto next_fd = static_cast<FileData *>(work_ahead->data);
		image_prebuffer_set(imd, next_fd);
		}
}

static void view_step_next(ViewWindow *vw)
{
	if (vw->ss)
		{
		view_slideshow_next(vw);
		}
	else if (vw->list)
		{
		view_list_step(vw, TRUE);
		}
	else
		{
		view_collection_step(vw, TRUE);
		}
}

static void view_step_next_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	view_step_next(vw);
}

static void view_step_prev(ViewWindow *vw)
{
	if (vw->ss)
		{
		view_slideshow_prev(vw);
		}
	else if (vw->list)
		{
		view_list_step(vw, FALSE);
		}
	else
		{
		view_collection_step(vw, FALSE);
		}
}

static void view_step_prev_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	view_step_prev(vw);
}

template<gboolean last>
static void view_step_to_end(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	if (vw->list)
		{
		view_list_step_to_end(vw, last);
		}
	else
		{
		view_collection_step_to_end(vw, last);
		}
}

template<gboolean in>
static void view_zoom_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	ImageWindow *imd = view_window_active_image(vw);

	if (in)
		{
		image_zoom_adjust(imd, get_zoom_increment());
		}
	else
		{
		image_zoom_adjust(imd, -get_zoom_increment());
		}
}

template<int value>
static void view_zoom_set_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	ImageWindow *imd = view_window_active_image(vw);

	image_zoom_set(imd, value);
}

template<gboolean vertical>
static void view_zoom_set_fill_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	ImageWindow *imd = view_window_active_image(vw);

	image_zoom_set_fill_geometry(imd, vertical);
}

static void view_reload_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	ImageWindow *imd = view_window_active_image(vw);

	image_reload(imd);
}

static void view_slideshow_start_stop_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	if (vw->ss)
		{
		vw->ss->pause_toggle();
		}

}

static void view_slideshow_pause_toggle_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	if (vw->ss)
		{
		view_slideshow_stop(vw);
		}
	else
		{
		view_slideshow_start(vw);
		}
}

static void view_escape_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	if (vw->fs)
		{
		view_fullscreen_toggle(vw, TRUE);
		}
	else
		{
		view_window_close(vw);
		}
}

static void view_help_cb(GSimpleAction *, GVariant *, gpointer)
{
	help_window_show("GuideOtherWindowsImageWindow.html");
}

/*
 *-----------------------------------------------------------------------------
 * view window keyboard
 *-----------------------------------------------------------------------------
 */

static gboolean view_window_press_cb(GtkWidget *, GdkEventButton *bevent, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	switch (bevent->button)
		{
		case GDK_BUTTON_PRIMARY:
			if (bevent->type == GDK_2BUTTON_PRESS)
				{
				view_fullscreen_toggle(vw, TRUE);
				}
			break;
		default:
			break;
		}

	return FALSE;
}

static gboolean view_window_key_press_cb(GtkWidget *, GdkEventKey *event, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	ImageWindow *imd;
	gint stop_signal;
	gint x = 0;
	gint y = 0;

	imd = view_window_active_image(vw);

	stop_signal = TRUE;
	switch (event->keyval)
		{
		case GDK_KEY_Left: case GDK_KEY_KP_Left:
			x -= 1;
			break;
		case GDK_KEY_Right: case GDK_KEY_KP_Right:
			x += 1;
			break;
		case GDK_KEY_Up: case GDK_KEY_KP_Up:
			y -= 1;
			break;
		case GDK_KEY_Down: case GDK_KEY_KP_Down:
			y += 1;
			break;
		default:
			stop_signal = FALSE;
			break;
		}

	if (x != 0 || y!= 0)
		{
		keyboard_scroll_calc(x, y,static_cast<GdkModifierType>(event->state), event->keyval, event->time);

		keyboard_scroll_calc(x, y,static_cast<GdkModifierType>(event->state), event->keyval, event->time);
		image_scroll(imd, x, y);

		image_scroll(imd, x, y);
		}

	if (stop_signal) return stop_signal;

/** @FIXME GTK4
	stop_signal = TRUE;
	if (event->state & GDK_CONTROL_MASK)
		{
		}
	else if (event->state & GDK_SHIFT_MASK)
		{
		}
	else
		{
		switch (event->keyval)
			{
				break;
			case GDK_KEY_Menu:
			case GDK_KEY_F10:
				menu = view_popup_menu(vw);
				gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER, nullptr);
				break;
			default:
				stop_signal = FALSE;
				break;
			}
		}

	if (!stop_signal && is_help_key(event))
		{
		help_window_show("GuideOtherWindowsImageWindow.html");
		stop_signal = TRUE;
		}
*/

	return stop_signal;
}

/*
 *-----------------------------------------------------------------------------
 * view window main routines
 *-----------------------------------------------------------------------------
 */
#if HAVE_GTK4
static void button_cb(ImageWindow *imd, GqMouseButtonEvent *event, gpointer data)
#else
static void button_cb(ImageWindow *imd, GdkEventButton *event, gpointer data)
#endif
{
	auto vw = static_cast<ViewWindow *>(data);
	LayoutWindow *lw_new;

	switch (event->button)
		{
		case GDK_BUTTON_PRIMARY:
			if (options->image_l_click_archive && imd->image_fd->format_class == FORMAT_CLASS_ARCHIVE)
				{
				g_autofree gchar *dest_dir = open_archive(imd->image_fd);
				if (dest_dir)
					{
					lw_new = layout_new_from_default();
					layout_set_path(lw_new, dest_dir);
					}
				else
					{
					warning_dialog(_("Cannot open archive file"), _("See the Log Window"), GQ_ICON_DIALOG_WARNING, nullptr);
					}
				}
			else if (options->image_l_click_video && options->image_l_click_video_editor && imd->image_fd->format_class == FORMAT_CLASS_VIDEO)
				{
				start_editor_from_file(options->image_l_click_video_editor, imd->image_fd);
				}
			else if (options->image_lm_click_nav)
				view_step_next(vw);
			break;
		case GDK_BUTTON_MIDDLE:
			if (options->image_lm_click_nav)
				view_step_prev(vw);
			break;
		case GDK_BUTTON_SECONDARY:
			view_popup_menu(vw);
			break;
		default:
			break;
		}
}

static void scroll_cb(ImageWindow *imd, GdkEventScroll *event, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	if ((event->state & GDK_CONTROL_MASK) ||
				(imd->mouse_wheel_mode && !options->image_lm_click_nav))
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				image_zoom_adjust_at_point(imd, get_zoom_increment(), event->x, event->y);
				break;
			case GDK_SCROLL_DOWN:
				image_zoom_adjust_at_point(imd, -get_zoom_increment(), event->x, event->y);
				break;
			default:
				break;
			}
		}
	else if ( (event->state & GDK_SHIFT_MASK) != static_cast<guint>(options->mousewheel_scrolls))
		{
		image_mousewheel_scroll(imd, event->direction);
		}
	else
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				view_step_prev(vw);
				break;
			case GDK_SCROLL_DOWN:
				view_step_next(vw);
				break;
			default:
				break;
			}
		}
}

static void view_image_set_buttons(ViewWindow *vw, ImageWindow *imd)
{
	image_set_button_func(imd, button_cb, vw);
	image_set_scroll_func(imd, scroll_cb, vw);
}

static void view_fullscreen_toggle(ViewWindow *vw, gboolean force_off)
{
	if (force_off && !vw->fs) return;

	if (vw->fs)
		{
		if (image_osd_get(vw->imd) & OSD_SHOW_INFO)
			image_osd_set(vw->imd, image_osd_get(vw->fs->imd));

		fullscreen_stop(vw->fs);
		}
	else
		{
		const auto view_fullscreen_stop_func = [vw](FullScreenData *)
		{
			vw->fs = nullptr;

			if (vw->ss) vw->ss->imd = vw->imd;
		};
		vw->fs = fullscreen_start(vw->window, vw->imd, view_fullscreen_stop_func);

		view_image_set_buttons(vw, vw->fs->imd);
		g_signal_connect(G_OBJECT(vw->fs->window), "key_press_event",
				 G_CALLBACK(view_window_key_press_cb), vw);

		if (vw->ss) vw->ss->imd = vw->fs->imd;

		if (image_osd_get(vw->imd) & OSD_SHOW_INFO)
			{
			image_osd_set(vw->fs->imd, image_osd_get(vw->imd));
			image_osd_set(vw->imd, OSD_SHOW_NOTHING);
			}
		}
}

template<gboolean force_off>
static void view_fullscreen_toggle_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	view_fullscreen_toggle(vw, force_off);

}

static void view_overlay_toggle(GSimpleAction *, GVariant *, gpointer data)
{
	ImageWindow *imd;

	auto vw = static_cast<ViewWindow *>(data);

	imd = view_window_active_image(vw);

	image_osd_toggle(imd);
}

static void view_slideshow_next(ViewWindow *vw)
{
	if (vw->ss) vw->ss->next();
}

static void view_slideshow_prev(ViewWindow *vw)
{
	if (vw->ss) vw->ss->prev();
}

static void view_slideshow_stop_func(ViewWindow *vw)
{
	vw->ss = nullptr;

	FileData *fd = image_get_fd(view_window_active_image(vw));

	GList *work = g_list_find(vw->list, fd);
	if (work)
		{
		vw->list_pointer = work;
		}
}

static void view_slideshow_start(ViewWindow *vw)
{
	if (vw->ss) return;

	if (vw->list)
		{
		vw->ss = SlideShow::start_from_filelist(nullptr, view_window_active_image(vw), filelist_copy(vw->list),
		                                        [vw](SlideShow *){ view_slideshow_stop_func(vw); });
		vw->list_pointer = nullptr;
		return;
		}

	CollectInfo *info;
	CollectionData *cd = image_get_collection(view_window_active_image(vw), &info);

	if (cd && info)
		{
		vw->ss = SlideShow::start_from_collection(nullptr, view_window_active_image(vw), cd, info,
		                                          [vw](SlideShow *){ view_slideshow_stop_func(vw); });
		}
}

static void view_slideshow_stop(ViewWindow *vw)
{
	delete vw->ss; /* the stop_func sets vv->ss to nullptr for us */
}

static void view_window_destroy_cb(GtkWidget *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	view_window_list.erase(std::remove(view_window_list.begin(), view_window_list.end(), vw),
	                       view_window_list.end());

	view_slideshow_stop(vw);
	fullscreen_stop(vw->fs);

	file_data_list_free(vw->list);

	file_data_unregister_notify_func(view_window_notify_cb, vw);

	g_free(vw);
}

static void view_window_close(ViewWindow *vw)
{
	view_slideshow_stop(vw);
	view_fullscreen_toggle(vw, TRUE);
	gq_gtk_widget_destroy(vw->window);
}

static gboolean view_window_delete_cb(GtkWidget *, GdkEventAny *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	view_window_close(vw);
	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * view window menu routines and callbacks
 *-----------------------------------------------------------------------------
 */

static void view_new_window_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	CollectionData *cd;
	CollectInfo *info;

	cd = image_get_collection(vw->imd, &info);

	if (cd && info)
		{
		view_window_new_from_collection(cd, info);
		}
	else
		{
		view_window_new(image_get_fd(vw->imd));
		}
}

static void view_edit_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *vw = static_cast<ViewWindow *>(data);
	if (!vw) return;

	const char *key = g_variant_get_string(parameter, nullptr);

	if (!editor_window_flag_set(key))
		{
		view_fullscreen_toggle(vw, TRUE);
		}

	ImageWindow *imd = view_window_active_image(vw);
	file_util_start_editor_from_file(key, image_get_fd(imd), imd->widget);
}

template<AlterType type>
static void view_alter_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	if (!vw)
		{
		return;
		}

	image_alter_orientation(vw->imd, vw->imd->image_fd, type);
}

static void view_zoom_fit_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	image_zoom_set(view_window_active_image(vw), 0.0);
}

 void view_copy_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	ImageWindow *imd;

	imd = view_window_active_image(vw);
	file_util_copy(image_get_fd(imd), nullptr, nullptr, imd->widget);
}

static void view_move_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	ImageWindow *imd;

	imd = view_window_active_image(vw);
	file_util_move(image_get_fd(imd), nullptr, nullptr, imd->widget);
}

static void view_rename_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	ImageWindow *imd;

	imd = view_window_active_image(vw);
	file_util_rename(image_get_fd(imd), nullptr, imd->widget);
}

template<gboolean safe_delete>
static void view_delete_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	ImageWindow *imd = view_window_active_image(vw);

	if (options->file_ops.enable_delete_key)
		{
		file_util_delete(image_get_fd(imd), nullptr, imd->widget, safe_delete);
		}
}

template<gboolean quoted>
static void view_copy_path_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	ImageWindow *imd = view_window_active_image(vw);

	file_util_copy_path_to_clipboard(image_get_fd(imd), quoted, ClipboardAction::COPY);
}

static void view_fullscreen_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	view_fullscreen_toggle(vw, FALSE);
}

static void view_slideshow_pause_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	vw->ss->pause_toggle();
}

static void view_close_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	view_window_close(vw);
}

static LayoutWindow *view_new_layout_with_fd(FileData *fd)
{
	LayoutWindow *nw;

	nw = layout_new_from_default();
	layout_set_fd(nw, fd);
	return nw;
}

static void view_set_layout_path_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	LayoutWindow *lw;
	ImageWindow *imd;

	imd = view_window_active_image(vw);

	if (!imd || !imd->image_fd) return;

	lw = layout_find_by_image_fd(imd);
	if (lw)
		{
		layout_set_fd(lw, imd->image_fd);
		gtk_window_present(GTK_WINDOW(lw->window));
		}
	else
		{
		view_new_layout_with_fd(imd->image_fd);
		}

	view_window_close(vw);
}

static GList *view_window_get_fd_list(ViewWindow *vw)
{
	GList *list = nullptr;
	ImageWindow *imd = view_window_active_image(vw);

	if (imd)
		{
		FileData *fd = image_get_fd(imd);
		if (fd) list = g_list_append(nullptr, file_data_ref(fd));
		}

	return list;
}

static void view_print_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	view_fullscreen_toggle(vw, TRUE);
	ImageWindow *imd = view_window_active_image(vw);
	FileData *fd = image_get_fd(imd);
	print_window_new(fd ? g_list_append(nullptr, file_data_ref(fd)) : nullptr, vw->window);
}

static void image_set_desaturate_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	ImageWindow *imd = view_window_active_image(vw);

	image_set_desaturate(imd, !image_get_desaturate(imd));
}

/* const ActionDef image_actions[]
 */
#include "img-view-actions.inc"

static ViewWindow *real_view_window_new(FileData *fd, GList *list, CollectionData *cd, CollectInfo *info)
{
	ViewWindow *vw;
	GdkGeometry geometry;

	if (!fd && !list && (!cd || !info)) return nullptr;

	vw = g_new0(ViewWindow, 1);

	vw->window = window_new("view", PIXBUF_INLINE_ICON_VIEW, nullptr);
	DEBUG_NAME(vw->window);

	geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
	gtk_window_set_geometry_hints(GTK_WINDOW(vw->window), nullptr, &geometry, GDK_HINT_MIN_SIZE);

	gtk_window_set_resizable(GTK_WINDOW(vw->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(vw->window), 0);

	vw->imd = image_new(FALSE);
	image_color_profile_set(vw->imd,
				options->color_profile.input_type,
				options->color_profile.use_image);
	image_color_profile_set_use(vw->imd, options->color_profile.enabled);

	image_background_set_color_from_options(vw->imd, FALSE);

	image_attach_window(vw->imd, vw->window, nullptr, GQ_APPNAME, TRUE);

	image_auto_refresh_enable(vw->imd, TRUE);
	image_top_window_set_sync(vw->imd, TRUE);

	gq_gtk_container_add(vw->window, vw->imd->widget);
	gtk_widget_show(vw->imd->widget);

	view_window_dnd_init(vw);

	view_image_set_buttons(vw, vw->imd);

	g_signal_connect(G_OBJECT(vw->window), "destroy",
			 G_CALLBACK(view_window_destroy_cb), vw);
	g_signal_connect(G_OBJECT(vw->window), "delete_event",
			 G_CALLBACK(view_window_delete_cb), vw);
	g_signal_connect(G_OBJECT(vw->window), "key_press_event",
			 G_CALLBACK(view_window_key_press_cb), vw);
	g_signal_connect(G_OBJECT(vw->window), "button_press_event",
			 G_CALLBACK(view_window_press_cb), vw);

	if (cd && info)
		{
		image_change_from_collection(vw->imd, cd, info, image_zoom_get_default(nullptr));
		/* Grab the fd so we can correctly size the window in
		   the call to image_load_dimensions() below. */
		fd = info->fd;
		if (options->image.enable_read_ahead)
			{
			CollectInfo * r_info = collection_next_by_info(cd, info);
			if (!r_info) r_info = collection_prev_by_info(cd, info);
			if (r_info) image_prebuffer_set(vw->imd, r_info->fd);
			}
		}
	else if (list)
		{
		view_window_set_list(vw, list);
		vw->list_pointer = vw->list;
		image_change_fd(vw->imd, static_cast<FileData *>(vw->list->data), image_zoom_get_default(nullptr));
		/* Set fd to first in list */
		fd = static_cast<FileData *>(vw->list->data);

		if (options->image.enable_read_ahead)
			{
			GList *work = vw->list->next;
			if (work) image_prebuffer_set(vw->imd, static_cast<FileData *>(work->data));
			}
		}
	else
		{
		image_change_fd(vw->imd, fd, image_zoom_get_default(nullptr));
		}

	/* Wait until image is loaded otherwise size is not defined */
	GqSize size;
	image_load_dimensions(fd, size);

	if (options->image.limit_window_size)
		{
		gint mw = deprecated_gdk_screen_width() * options->image.max_window_size / 100;
		gint mh = deprecated_gdk_screen_height() * options->image.max_window_size / 100;

		size.width = std::min(size.width, mw);
		size.height = std::min(size.height, mh);
		}

	gtk_window_set_default_size(GTK_WINDOW(vw->window), size.width, size.height);

	GtkAllocation req_size{ 0, 0, size.width, size.height };
	gtk_widget_size_allocate(vw->window, &req_size);

	gtk_window_set_focus_on_map(GTK_WINDOW(vw->window), FALSE);
	gtk_widget_show(vw->window);

	view_window_list.push_back(vw);

	file_data_register_notify_func(view_window_notify_cb, vw, NOTIFY_PRIORITY_LOW);

	/** @FIXME This is a hack to fix #965 View in new window - blank image
	 * The problem occurs when zoom is set to Original Size and Preload
	 * Next Image is set.
	 * An extra reload is required to force the image to be displayed.
	 * See also layout-image.cc layout_image_full_screen_start()
	 * This is probably not the correct solution.
	 **/
	image_reload(vw->imd);


	GApplication *app = g_application_get_default();
	register_actions_from_table(GTK_APPLICATION(app), vw->window, image_actions, get_keyfile_merged(), vw);

	return vw;
}

void view_window_new(FileData *fd)
{
	GList *list;

	if (fd)
		{
		if (file_extension_match(fd->path, GQ_COLLECTION_EXT))
			{
			ViewWindow *vw;
			CollectionData *cd;
			CollectInfo *info;

			cd = collection_new(fd->path);
			if (collection_load(cd, fd->path, COLLECTION_LOAD_NONE))
				{
				info = collection_get_first(cd);
				}
			else
				{
				g_clear_pointer(&cd, collection_unref);
				info = nullptr;
				}

			vw = real_view_window_new(nullptr, nullptr, cd, info);
			if (vw && cd)
				{
				g_signal_connect_swapped(G_OBJECT(vw->window), "destroy",
				                         G_CALLBACK(collection_unref), cd);
				}
			}
		else if (isdir(fd->path) && filelist_read(fd, &list, nullptr))
			{
			list = filelist_sort_path(list);
			list = filelist_filter(list, FALSE);
			real_view_window_new(nullptr, list, nullptr, nullptr);
			file_data_list_free(list);
			}
		else
			{
			real_view_window_new(fd, nullptr, nullptr, nullptr);
			}
		}
}

void view_window_new_from_list(GList *list)
{
	real_view_window_new(nullptr, list, nullptr, nullptr);
}

void view_window_new_from_collection(CollectionData *cd, CollectInfo *info)
{
	real_view_window_new(nullptr, nullptr, cd, info);
}

/*
 *-----------------------------------------------------------------------------
 * public
 *-----------------------------------------------------------------------------
 */

void view_window_colors_update()
{
	for (ViewWindow *vw : view_window_list)
		{
		image_background_set_color_from_options(vw->imd, !!vw->fs);
		}
}

gboolean view_window_find_image(const ImageWindow *imd, gint &index, gint &total)
{
	const auto it = std::find_if(view_window_list.cbegin(), view_window_list.cend(),
	                             [imd](const ViewWindow *vw) { return vw->imd == imd || (vw->fs && vw->fs->imd == imd); });
	if (it == view_window_list.cend()) return FALSE;

	const ViewWindow *vw = *it;

	if (vw->ss)
		{
		vw->ss->get_index_and_total(index, total);
		}
	else
		{
		index = g_list_position(vw->list, vw->list_pointer) + 1;
		total = g_list_length(vw->list);
		}

	return TRUE;
}

/**
 * @brief Add file selection list to a collection
 * @param[in] widget
 * @param[in] data Index to the collection list menu item selected, or -1 for new collection
 *
 *
 */
static void image_pop_menu_collections_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	ImageWindow *imd;
	FileData *fd;

 	auto *vw = static_cast<ViewWindow *>(data);
	int index = g_variant_get_int32(parameter);

	imd = view_window_active_image(vw);
	fd = image_get_fd(imd);

	g_autoptr(FileDataList) selection_list = g_list_append(nullptr, file_data_ref(fd));
	collection_by_index_add_filelist(index, selection_list);
}

static void view_popup_menu(ViewWindow *vw)
{
	GtkBuilder *builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-img-view.ui");
	GMenu *menu_model = G_MENU(gtk_builder_get_object(builder, "menu-image"));
	GList *editmenu_fd_list;

	editmenu_fd_list = view_window_get_fd_list(vw);
	GMenu *plugins_menu = G_MENU(gtk_builder_get_object(builder, "plugins-submenu"));
	plugins_menu_populate(plugins_menu, "win.image-win-plugin-run", editmenu_fd_list);

	GMenu *collections_menu = G_MENU(gtk_builder_get_object(builder, "collections-submenu"));
	submenu_add_collections_new(collections_menu, TRUE, "win.image-win-collections", vw);

	if (options->file_ops.confirm_move_to_trash)
		{
		menu_item_include_ellipsis(G_MENU_MODEL(menu_model), "win.image-win-delete");
		}
	if (options->file_ops.confirm_delete)
		{
		menu_item_include_ellipsis(G_MENU_MODEL(menu_model), "win.image-win-delete-permanent");
		}

	GtkWidget *menu = popup_menu(menu_model, vw->window);
 	g_signal_connect_swapped(G_OBJECT(menu), "destroy", G_CALLBACK(file_data_list_free), editmenu_fd_list);
}

/*
 *-------------------------------------------------------------------
 * dnd confirm dir
 *-------------------------------------------------------------------
 */

struct CViewConfirmD {
	ViewWindow *vw;
	GList *list;
};

static void view_dir_list_cancel(GtkWidget *, gpointer)
{
	/* do nothing */
}

static void view_dir_list_do(ViewWindow *vw, GList *list, gboolean skip, gboolean recurse)
{
	GList *work;

	view_window_set_list(vw, nullptr);

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (isdir(fd->path))
			{
			if (!skip)
				{
				GList *list = nullptr;

				if (recurse)
					{
					list = filelist_recursive(fd);
					}
				else
					{ /** @FIXME ?? */
					filelist_read(fd, &list, nullptr);
					list = filelist_sort_path(list);
					list = filelist_filter(list, FALSE);
					}
				if (list) vw->list = g_list_concat(vw->list, list);
				}
			}
		else
			{
			/** @FIXME no filtering here */
			vw->list = g_list_append(vw->list, file_data_ref(fd));
			}
		}

	if (vw->list)
		{
		FileData *fd;

		vw->list_pointer = vw->list;
		fd = static_cast<FileData *>(vw->list->data);
		image_change_fd(vw->imd, fd, image_zoom_get_default(vw->imd));

		work = vw->list->next;
		if (options->image.enable_read_ahead && work)
			{
			fd = static_cast<FileData *>(work->data);
			image_prebuffer_set(vw->imd, fd);
			}
		}
	else
		{
		image_change_fd(vw->imd, nullptr, image_zoom_get_default(vw->imd));
		}
}

template<gboolean recurse>
static void view_dir_list_add(GtkWidget *, gpointer data)
{
	auto d = static_cast<CViewConfirmD *>(data);
	view_dir_list_do(d->vw, d->list, FALSE, recurse);
}

static void view_dir_list_skip(GtkWidget *, gpointer data)
{
	auto d = static_cast<CViewConfirmD *>(data);
	view_dir_list_do(d->vw, d->list, TRUE, FALSE);
}

static void view_dir_list_destroy(GtkWidget *, gpointer data)
{
	auto d = static_cast<CViewConfirmD *>(data);
	file_data_list_free(d->list);
	g_free(d);
}

static GtkWidget *view_confirm_dir_list(ViewWindow *vw, GList *list)
{
	GtkWidget *menu;
	CViewConfirmD *d;

	d = g_new(CViewConfirmD, 1);
	d->vw = vw;
	d->list = list;

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(view_dir_list_destroy), d);

	menu_item_add_icon(menu, _("Dropped list includes folders."), GQ_ICON_DIRECTORY, nullptr, nullptr);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("_Add contents"), GQ_ICON_OK,
	                   G_CALLBACK(view_dir_list_add<FALSE>), d);
	menu_item_add_icon(menu, _("Add contents _recursive"), GQ_ICON_ADD,
	                   G_CALLBACK(view_dir_list_add<TRUE>), d);
	menu_item_add_icon(menu, _("_Skip folders"), GQ_ICON_REMOVE, G_CALLBACK(view_dir_list_skip), d);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("Cancel"), GQ_ICON_CANCEL, G_CALLBACK(view_dir_list_cancel), d);

	return menu;
}

/*
 *-----------------------------------------------------------------------------
 * image drag and drop routines
 *-----------------------------------------------------------------------------
 */
#if !HAVE_GTK4
static void view_window_get_dnd_data(GtkWidget *, GdkDragContext *context,
				     gint, gint,
				     GtkSelectionData *selection_data, guint info,
				     guint, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	ImageWindow *imd;

	if (gtk_drag_get_source_widget(context) == vw->imd->pr) return;

	imd = vw->imd;

	if (info == TARGET_URI_LIST || info == TARGET_APP_COLLECTION_MEMBER)
		{
		CollectionData *source;
		g_autoptr(FileDataList) list = nullptr;
		GList *info_list;

		if (info == TARGET_URI_LIST)
			{
			list = uri_filelist_from_gtk_selection_data(selection_data);

			if (file_data_list_has_dir(list))
				{
				GtkWidget *menu = view_confirm_dir_list(vw, g_steal_pointer(&list));
				gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
				return;
				}

			list = filelist_filter(list, FALSE);

			source = nullptr;
			info_list = nullptr;
			}
		else
			{
			source = collection_from_dnd_data(reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data)), &list, &info_list);
			}

		if (list)
			{
			FileData *fd;

			fd = static_cast<FileData *>(list->data);
			if (isfile(fd->path))
				{
				view_slideshow_stop(vw);
				view_window_set_list(vw, nullptr);

				if (source && info_list)
					{
					image_change_from_collection(imd, source, static_cast<CollectInfo *>(info_list->data), image_zoom_get_default(imd));
					}
				else
					{
					if (list->next)
						{
						vw->list = list;
						list = nullptr;

						vw->list_pointer = vw->list;
						}
					image_change_fd(imd, fd, image_zoom_get_default(imd));
					}
				}
			}

		g_list_free(info_list);
		}
}

static void view_window_set_dnd_data(GtkWidget *, GdkDragContext *,
				     GtkSelectionData *selection_data, guint,
				     guint, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);
	FileData *fd;

	fd = image_get_fd(vw->imd);

	if (fd)
		{
		GList *list;

		list = g_list_append(nullptr, fd);
		uri_selection_data_set_uris_from_filelist(selection_data, list);
		g_list_free(list);
		}
	else
		{
		gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data),
				       8, nullptr, 0);
		}
}

static void view_window_dnd_init(ViewWindow *vw)
{
	ImageWindow *imd;

	imd = vw->imd;

	gq_gtk_drag_source_set(imd->pr, GDK_BUTTON2_MASK,
	                    dnd_file_drag_types.data(), dnd_file_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gq_drag_g_signal_connect(G_OBJECT(imd->pr), "drag_data_get",
			 G_CALLBACK(view_window_set_dnd_data), vw);

	gq_gtk_drag_dest_set(imd->pr,
	                  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP),
	                  dnd_file_drop_types.data(), dnd_file_drop_types.size(),
	                  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gq_drag_g_signal_connect(G_OBJECT(imd->pr), "drag_data_received",
			 G_CALLBACK(view_window_get_dnd_data), vw);
}
#endif

/*
 *-----------------------------------------------------------------------------
 * maintenance (for rename, move, remove)
 *-----------------------------------------------------------------------------
 */

static void view_real_removed(ViewWindow *vw, FileData *fd)
{
	ImageWindow *imd;
	FileData *image_fd;

	imd = view_window_active_image(vw);
	image_fd = image_get_fd(imd);

	if (image_fd && image_fd == fd)
		{
		if (vw->list)
			{
			view_list_step(vw, TRUE);
			if (image_get_fd(imd) == image_fd)
				{
				view_list_step(vw, FALSE);
				}
			}
		else if (view_window_contains_collection(vw))
			{
			view_collection_step(vw, TRUE);
			if (image_get_fd(imd) == image_fd)
				{
				view_collection_step(vw, FALSE);
				}
			}
		if (image_get_fd(imd) == image_fd)
			{
			image_change_fd(imd, nullptr, image_zoom_get_default(imd));
			}
		}

	if (vw->list)
		{
		GList *work;
		GList *old;

		old = vw->list_pointer;

		work = vw->list;
		while (work)
			{
			FileData *chk_fd;
			GList *chk_link;

			chk_fd = static_cast<FileData *>(work->data);
			chk_link = work;
			work = work->next;

			if (chk_fd == fd)
				{
				if (vw->list_pointer == chk_link)
					{
					vw->list_pointer = (chk_link->next) ? chk_link->next : chk_link->prev;
					}
				vw->list = g_list_remove(vw->list, chk_fd);
				file_data_unref(chk_fd);
				}
			}

		/* handles stepping correctly when same image is in the list more than once */
		if (old && old != vw->list_pointer)
			{
			FileData *fd;

			if (vw->list_pointer)
				{
				fd = static_cast<FileData *>(vw->list_pointer->data);
				}
			else
				{
				fd = nullptr;
				}

			image_change_fd(imd, fd, image_zoom_get_default(imd));
			}
		}

	image_osd_update(imd);
}

static void view_window_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto vw = static_cast<ViewWindow *>(data);

	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify view_window: %s %04x", fd->path, type);

	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
			break;
		case FILEDATA_CHANGE_COPY:
			break;
		case FILEDATA_CHANGE_DELETE:
			view_real_removed(vw, fd);
			break;
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}
}

const ActionDef *get_image_actions()
{
	return image_actions;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
