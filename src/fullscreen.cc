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

#include "fullscreen.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "compat.h"
#include "image-load.h"
#include "image.h"
#include "intl.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "window.h"

namespace {

GdkMonitor *get_first_monitor(GdkDisplay *display)
{
	GListModel *monitors = gdk_display_get_monitors(display);
	if (!monitors || g_list_model_get_n_items(monitors) == 0)
		{
		return nullptr;
		}

	return GDK_MONITOR(g_list_model_get_item(monitors, 0));
}

/*
 *----------------------------------------------------------------------------
 * full screen functions
 *----------------------------------------------------------------------------
 */

#define FULL_SCREEN_HIDE_MOUSE_DELAY 3000
#define FULL_SCREEN_BUSY_MOUSE_DELAY 200

enum {
	FULLSCREEN_CURSOR_HIDDEN = 1 << 0,
	FULLSCREEN_CURSOR_NORMAL = 1 << 1,
	FULLSCREEN_CURSOR_BUSY   = 1 << 2
};

void clear_mouse_cursor(GtkWidget *widget, gint state)
{
	if (state & FULLSCREEN_CURSOR_BUSY)
		{
		gtk_widget_set_cursor_from_name(widget, "wait");
		}
	else if (state & FULLSCREEN_CURSOR_NORMAL)
		{
		gtk_widget_set_cursor_from_name(widget, nullptr);
		}
	else
		{
		gtk_widget_set_cursor_from_name(widget, nullptr);
		}
}

gboolean fullscreen_hide_mouse_cb(gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (fs->hide_mouse_id)
		{
		fs->cursor_state &= ~FULLSCREEN_CURSOR_NORMAL;
		if (!(fs->cursor_state & FULLSCREEN_CURSOR_BUSY)) clear_mouse_cursor(fs->window, fs->cursor_state);

		g_clear_handle_id(&fs->hide_mouse_id, g_source_remove);
		}

	return G_SOURCE_REMOVE;
}

void fullscreen_hide_mouse_reset(FullScreenData *fs)
{
	g_clear_handle_id(&fs->hide_mouse_id, g_source_remove);
	fs->hide_mouse_id = g_timeout_add(FULL_SCREEN_HIDE_MOUSE_DELAY, fullscreen_hide_mouse_cb, fs);
}

void fullscreen_mouse_moved(GtkEventControllerMotion *, double, double, gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (!(fs->cursor_state & FULLSCREEN_CURSOR_NORMAL))
		{
		fs->cursor_state |= FULLSCREEN_CURSOR_NORMAL;
		if (!(fs->cursor_state & FULLSCREEN_CURSOR_BUSY)) clear_mouse_cursor(fs->window, fs->cursor_state);
		}
	fullscreen_hide_mouse_reset(fs);
}

void fullscreen_mouse_set_busy(FullScreenData *fs, gboolean busy)
{
	g_clear_handle_id(&fs->busy_mouse_id, g_source_remove);

	if (!!(fs->cursor_state & FULLSCREEN_CURSOR_BUSY) == (busy)) return;

	if (busy)
		{
		fs->cursor_state |= FULLSCREEN_CURSOR_BUSY;
		}
	else
		{
		fs->cursor_state &= ~FULLSCREEN_CURSOR_BUSY;
		}

	clear_mouse_cursor(fs->window, fs->cursor_state);
}

gboolean fullscreen_mouse_set_busy_cb(gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	fs->busy_mouse_id = 0;
	fullscreen_mouse_set_busy(fs, TRUE);
	return G_SOURCE_REMOVE;
}

void fullscreen_mouse_set_busy_idle(FullScreenData *fs)
{
	if (!fs->busy_mouse_id)
		{
		fs->busy_mouse_id = g_timeout_add(FULL_SCREEN_BUSY_MOUSE_DELAY,
						  fullscreen_mouse_set_busy_cb, fs);
		}
}

void fullscreen_image_update_cb(ImageWindow *, gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (fs->imd->il &&
	    image_loader_get_pixbuf(fs->imd->il) != image_get_pixbuf(fs->imd))
		{
		fullscreen_mouse_set_busy_idle(fs);
		}
}

void fullscreen_image_complete_cb(ImageWindow *, gboolean preload, gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (!preload) fullscreen_mouse_set_busy(fs, FALSE);
}

#define XSCREENSAVER_BINARY	"xscreensaver-command"
#define XSCREENSAVER_COMMAND	"xscreensaver-command -deactivate >&- 2>&- &"

void fullscreen_saver_deactivate()
{
	static gboolean found = file_in_path(XSCREENSAVER_BINARY);

	if (found)
		{
		runcmd(XSCREENSAVER_COMMAND);
		}
}

gboolean fullscreen_saver_block_cb(gpointer)
{
	if (options->fullscreen.disable_saver)
		{
		fullscreen_saver_deactivate();
		}

	return G_SOURCE_CONTINUE;
}

gboolean fullscreen_close_request_cb(GtkWidget *, gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	fullscreen_stop(fs);
	return TRUE;
}


/*
 *----------------------------------------------------------------------------
 * full screen preferences and utils
 *----------------------------------------------------------------------------
 */

/**
 * @struct ScreenData
 * screen numbers for fullscreen_prefs are as follows: \n
 *   0  use default display size \n
 * 101  screen 0, monitor 0 \n
 * 102  screen 0, monitor 1 \n
 * 201  screen 1, monitor 0 \n
 */
struct ScreenData {
	gint number;
	std::string description;
	GdkRectangle geometry;
};

struct FullscreenTarget
{
	GdkRectangle geometry;
	GdkMonitor *monitor;
	gboolean same_region;
};

std::vector<ScreenData> fullscreen_prefs_list()
{
	std::vector<ScreenData> list;
	GdkDisplay *display;
	const gchar *name;

	display = gdk_display_get_default();
	GListModel *monitors = gdk_display_get_monitors(display);
	guint n_monitors = g_list_model_get_n_items(monitors);
	name = gdk_display_get_name(display);

	for (gint j = -1; j < static_cast<gint>(n_monitors); j++)
		{
		GdkRectangle rect;
		g_autofree gchar *subname = nullptr;

		if (j < 0)
			{
			/* GTK4: compute full size by union of all monitors */
			gboolean first = TRUE;

			for (guint i = 0; i < n_monitors; i++)
				{
				g_autoptr(GdkMonitor) monitor = GDK_MONITOR(g_list_model_get_item(monitors, i));

				GdkRectangle mrect;
				gdk_monitor_get_geometry(monitor, &mrect);

				if (first)
					{
					rect = mrect;
					first = FALSE;
					}
				else
					{
					gint x1 = MIN(rect.x, mrect.x);
					gint y1 = MIN(rect.y, mrect.y);
					gint x2 = MAX(rect.x + rect.width,  mrect.x + mrect.width);
					gint y2 = MAX(rect.y + rect.height, mrect.y + mrect.height);

					rect.x = x1;
					rect.y = y1;
					rect.width  = x2 - x1;
					rect.height = y2 - y1;
					}
				}

			subname = g_strdup(_("Full size"));
			}
		else
			{
			g_autoptr(GdkMonitor) monitor = GDK_MONITOR(g_list_model_get_item(monitors, j));
			gdk_monitor_get_geometry(monitor, &rect);
			subname = g_strdup_printf(_("Monitor %d"), j + 1);
			}

		ScreenData sd;
		sd.number = 100 + j + 1;
		sd.description = std::string(_("Screen")) + " " + name + ", " + subname;
		sd.geometry = rect;

		DEBUG_1("Screen %d %30s %4d,%4d (%4dx%4d)",
		        sd.number, sd.description.c_str(), sd.geometry.x, sd.geometry.y, sd.geometry.width, sd.geometry.height);

		list.push_back(sd);
		}

	return list;
}

/* screen_num is interpreted as such:
 *  -1  window manager determines size and position, fallback is (1) active monitor
 *   0  full size of screen containing widget
 *   1  size of monitor containing widget
 * 100  full size of screen 1 (screen, monitor counts start at 1)
 * 101  size of monitor 1 on screen 1
 * 203  size of monitor 3 on screen 2
 * returns:
 * In GTK4 this returns the target geometry and, for monitor-sized choices,
 * the monitor to request fullscreen on.
 */

GdkMonitor *fullscreen_current_monitor(GdkDisplay *display, GtkWidget *widget)
{
	GdkSurface *surface = nullptr;

	if (widget && gtk_widget_get_native(widget))
		{
		surface = gtk_native_get_surface(gtk_widget_get_native(widget));
		}

	if (surface)
		{
		GdkMonitor *monitor = gdk_display_get_monitor_at_surface(display, surface);
		if (monitor)
			{
			return static_cast<GdkMonitor *>(g_object_ref(monitor));
			}
		}

	return get_first_monitor(display);
}

FullscreenTarget fullscreen_target_new()
{
	FullscreenTarget target{};
	target.same_region = TRUE;

	return target;
}

FullscreenTarget fullscreen_prefs_get_target(gint screen_num, GtkWidget *widget)
{
	FullscreenTarget target = fullscreen_target_new();
	GdkDisplay *display = widget ? gtk_widget_get_display(widget) : gdk_display_get_default();
	GListModel *monitors = gdk_display_get_monitors(display);
	g_autoptr(GdkMonitor) current_monitor = fullscreen_current_monitor(display, widget);

	if (!monitors)
		{
		return target;
		}

	if (screen_num >= 100)
		{
		std::vector<ScreenData> list = fullscreen_prefs_list();
		auto it = std::find_if(list.cbegin(), list.cend(), [screen_num](const ScreenData &sd)
			{
			return sd.number == screen_num;
			});

		if (it == list.cend())
			{
			return target;
			}

		target.geometry = it->geometry;

		if (screen_num > 100)
			{
			const guint monitor_index = screen_num - 101;
			if (monitor_index < g_list_model_get_n_items(monitors))
				{
				target.monitor = GDK_MONITOR(g_list_model_get_item(monitors, monitor_index));
				target.same_region = (target.monitor == current_monitor);
				}
			}

		return target;
		}

	if (screen_num < 0)
		{
		if (current_monitor)
			{
			gdk_monitor_get_geometry(current_monitor, &target.geometry);
			}

		return target;
		}

	if (screen_num == 1)
		{
		if (current_monitor)
			{
			target.monitor = static_cast<GdkMonitor *>(g_object_ref(current_monitor));
			gdk_monitor_get_geometry(target.monitor, &target.geometry);
			}

		return target;
		}

	gboolean first = TRUE;
	for (guint i = 0; i < g_list_model_get_n_items(monitors); i++)
		{
		g_autoptr(GdkMonitor) monitor = GDK_MONITOR(g_list_model_get_item(monitors, i));
		GdkRectangle mrect;

		gdk_monitor_get_geometry(monitor, &mrect);

		if (first)
			{
			target.geometry = mrect;
			first = FALSE;
			}
		else
			{
			gint x1 = MIN(target.geometry.x, mrect.x);
			gint y1 = MIN(target.geometry.y, mrect.y);
			gint x2 = MAX(target.geometry.x + target.geometry.width,  mrect.x + mrect.width);
			gint y2 = MAX(target.geometry.y + target.geometry.height, mrect.y + mrect.height);

			target.geometry.x = x1;
			target.geometry.y = y1;
			target.geometry.width  = x2 - x1;
			target.geometry.height = y2 - y1;
			}
		}

	return target;
}

void fullscreen_prefs_selection_cb(GObject *object, GParamSpec *, gpointer value)
{
	if (!value) return;

	GtkDropDown *drop_down = GTK_DROP_DOWN(object);

	guint selected_index = gtk_drop_down_get_selected(drop_down);
	if (selected_index == GTK_INVALID_LIST_POSITION) return;

	GListModel *model = gtk_drop_down_get_model(drop_down);

	g_autoptr(GObject) item = G_OBJECT(g_list_model_get_item(model, selected_index));
	if (!item) return;

	auto *screen_value = static_cast<gint *>(value);
	*screen_value = GPOINTER_TO_INT(g_object_get_data(item, "number"));
}

} // namespace

/*
 *----------------------------------------------------------------------------
 * full screen functions
 *----------------------------------------------------------------------------
 */

FullScreenData *fullscreen_start(GtkWidget *window, ImageWindow *imd,
                                 const FullScreenData::StopFunc &stop_func)
{
	FullScreenData *fs;
	GdkDisplay *display = nullptr;

	if (!window || !imd) return nullptr;

	fs = g_new0(FullScreenData, 1);

	fs->cursor_state = FULLSCREEN_CURSOR_HIDDEN;

	fs->osd_flags = image_osd_get(imd);
	if (options->hide_osd_in_fullscreen)
		{
		image_osd_set(imd, OSD_SHOW_NOTHING);
		}

	fs->normal_window = window;
	fs->normal_imd = imd;

	fs->stop_func = stop_func;

	DEBUG_1("full screen requests screen %d", options->fullscreen.screen);

	FullscreenTarget target = fullscreen_prefs_get_target(options->fullscreen.screen, window);

	fs->window = window_new("fullscreen", nullptr, _("Full screen"));
	DEBUG_NAME(fs->window);

	gtk_window_set_decorated(GTK_WINDOW(fs->window), FALSE);
	gtk_window_set_default_size(GTK_WINDOW(fs->window), target.geometry.width, target.geometry.height);
	g_signal_connect(G_OBJECT(fs->window), "close-request",
			 G_CALLBACK(fullscreen_close_request_cb), fs);
	if (target.monitor)
		{
		gtk_window_fullscreen_on_monitor(GTK_WINDOW(fs->window), target.monitor);
		}
	else
		{
		gtk_window_fullscreen(GTK_WINDOW(fs->window));
		}

	fs->same_region = target.same_region;
	g_clear_object(&target.monitor);

	fs->imd = image_new(FALSE);

	gq_gtk_container_add(fs->window, fs->imd->widget);

	image_background_set_color_from_options(fs->imd, TRUE);
	image_set_delay_flip(fs->imd, options->fullscreen.clean_flip);
	image_auto_refresh_enable(fs->imd, fs->normal_imd->auto_refresh);

	if (options->fullscreen.clean_flip)
		{
		image_set_update_func(fs->imd, fullscreen_image_update_cb, fs);
		image_set_complete_func(fs->imd, fullscreen_image_complete_cb, fs);
		}

	gtk_widget_show(fs->imd->widget);

	if (fs->same_region)
		{
		DEBUG_2("Original window is not visible, enabling std. fullscreen mode");
		image_move_from_image(fs->imd, fs->normal_imd);
		}
	else
		{
		DEBUG_2("Original window is still visible, enabling presentation fullscreen mode");
		image_copy_from_image(fs->imd, fs->normal_imd);
		}

	if (options->stereo.enable_fsmode)
		{
		image_stereo_set(fs->imd, options->stereo.fsmode);
		}

	/* Use the XDG Activation protocol to get the newly created fullscreen
	 * window automatically activated also on Wayland with focus stealing
	 * prevention in strict mode.
	 *
	 * https://blogs.gnome.org/shell-dev/2024/09/20/understanding-gnome-shells-focus-stealing-prevention/
	 */
	if (g_getenv("WAYLAND_DISPLAY"))
		{
		if (display == nullptr)
			{
			display = gtk_widget_get_display(window);
			}

		GdkAppLaunchContext *context = gdk_display_get_app_launch_context(display);
		g_autofree char *id = g_app_launch_context_get_startup_notify_id(G_APP_LAUNCH_CONTEXT(context), nullptr, nullptr);
		DEBUG_1("full screen setting startup id %s", id);
		gtk_window_set_startup_id(GTK_WINDOW(fs->window), id);
		}

	gtk_widget_show(fs->window);

	/* for hiding the mouse */
	GtkEventController *controller = gtk_event_controller_motion_new();
	g_signal_connect(controller, "motion", G_CALLBACK(fullscreen_mouse_moved), fs);
	gtk_widget_add_controller(fs->imd->pr, controller);

	clear_mouse_cursor(fs->window, fs->cursor_state);

	/* set timer to block screen saver */
	fs->saver_block_id = g_timeout_add(60 * 1000, fullscreen_saver_block_cb, nullptr);

	/* hide normal window */
	 /** @FIXME properly restore this window on show
	 */
	if (fs->same_region)
		{
		if (options->hide_window_in_fullscreen)
			{
			/** @FIXME Wayland corrupts the size and position of the window when restoring it */
			if (!g_getenv("WAYLAND_DISPLAY"))
				{
				gtk_widget_hide(fs->normal_window);
				}
			}
		image_change_fd(fs->normal_imd, nullptr, image_zoom_get(fs->normal_imd));
		}

	return fs;
}

void fullscreen_stop(FullScreenData *fs)
{
	if (!fs) return;

	if (options->hide_osd_in_fullscreen)
		{
		image_osd_set(fs->normal_imd, fs->osd_flags);
		}

	if (fs->saver_block_id) g_source_remove(fs->saver_block_id);

	g_clear_handle_id(&fs->hide_mouse_id, g_source_remove);
	g_clear_handle_id(&fs->busy_mouse_id, g_source_remove);

	if (fs->same_region)
		{
		image_move_from_image(fs->normal_imd, fs->imd);
		if (options->hide_window_in_fullscreen)
			{
			/** @FIXME Wayland corrupts the size and position of the window when restoring it */
			if (!g_getenv("WAYLAND_DISPLAY"))
				{
				gtk_widget_show(fs->normal_window);
				}
			}
		if (options->stereo.enable_fsmode)
			{
			image_stereo_set(fs->normal_imd, options->stereo.mode);
			}
		}

	gtk_window_unfullscreen(GTK_WINDOW(fs->window));

	if (fs->stop_func) fs->stop_func(fs);

	gq_gtk_widget_destroy(fs->window);

	gtk_window_present(GTK_WINDOW(fs->normal_window));

	g_free(fs);
}


/*
 *----------------------------------------------------------------------------
 * full screen preferences and utils
 *----------------------------------------------------------------------------
 */

GtkWidget *fullscreen_prefs_selection_new(const gchar *text, gint *screen_value)
{
	if (!screen_value) return nullptr;

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	DEBUG_NAME(hbox);

	if (text) pref_label_new(hbox, text);

	const std::array<ScreenData, 3> default_list = {{
	    {-1, _("Determined by Window Manager"), {}},
	    { 0, _("Active screen"), {},},
	    { 1, _("Active monitor"), {} },
	}};

	std::vector<ScreenData> list = fullscreen_prefs_list();
	list.insert(list.begin(), default_list.cbegin(), default_list.cend());

	GtkStringList *string_list = gtk_string_list_new(nullptr);
	guint index = 0;
	for (const ScreenData &sd : list)
		{
		gtk_string_list_append(string_list, sd.description.c_str());

		g_autoptr(GObject) item = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(string_list), index++));
		g_object_set_data(item, "number", GINT_TO_POINTER(sd.number));
		}

	GtkWidget *drop_down = gtk_drop_down_new(G_LIST_MODEL(string_list), nullptr);

	const auto it = std::find_if(list.cbegin(), list.cend(),
	                             [screen_num = *screen_value](const ScreenData &sd){ return sd.number == screen_num; });
	const gint current = (it != list.cend()) ? std::distance(list.cbegin(), it) : 0;
	gtk_drop_down_set_selected(GTK_DROP_DOWN(drop_down), current);

	gq_gtk_box_pack_start(GTK_BOX(hbox), drop_down, FALSE, FALSE, 0);

	g_signal_connect(G_OBJECT(drop_down), "notify::selected",
	                 G_CALLBACK(fullscreen_prefs_selection_cb), screen_value);

	return hbox;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
