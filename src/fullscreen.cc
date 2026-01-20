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

#include "compat-deprecated.h"
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
		widget_set_cursor(widget, GDK_WATCH);
		}
	else if (state & FULLSCREEN_CURSOR_NORMAL)
		{
		widget_set_cursor(widget, -1);
		}
	else
		{
		widget_set_cursor(widget, GDK_BLANK_CURSOR);
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

gboolean fullscreen_mouse_moved(GtkWidget *, GdkEventMotion *, gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (!(fs->cursor_state & FULLSCREEN_CURSOR_NORMAL))
		{
		fs->cursor_state |= FULLSCREEN_CURSOR_NORMAL;
		if (!(fs->cursor_state & FULLSCREEN_CURSOR_BUSY)) clear_mouse_cursor(fs->window, fs->cursor_state);
		}
	fullscreen_hide_mouse_reset(fs);

	return FALSE;
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

gboolean fullscreen_delete_cb(GtkWidget *, GdkEventAny *, gpointer data)
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

#if !HAVE_GTK4
GdkRectangle get_screen_default_geometry(GdkScreen *screen)
{
	GdkRectangle geometry;

	geometry.x = 0;
	geometry.y = 0;
	geometry.width = deprecated_gdk_screen_get_width(screen);
	geometry.height = deprecated_gdk_screen_get_height(screen);

	return geometry;
}
#endif

std::vector<ScreenData> fullscreen_prefs_list()
{
	std::vector<ScreenData> list;
	GdkDisplay *display;
	gint monitors;
	const gchar *name;

	display = gdk_display_get_default();
	monitors = gdk_display_get_n_monitors(display);
	name = gdk_display_get_name(display);

	for (gint j = -1; j < monitors; j++)
		{
		GdkRectangle rect;
		g_autofree gchar *subname = nullptr;

#if HAVE_GTK4
		if (j < 0)
			{
			/* GTK4: compute full size by union of all monitors */
			gboolean first = TRUE;

			for (gint i = 0; i < monitors; i++)
				{
				GdkMonitor *monitor = gdk_display_get_monitor(display, i);
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
#else
		if (j < 0)
			{
			GdkScreen *screen = gdk_display_get_default_screen(display);
			rect = get_screen_default_geometry(screen);
			subname = g_strdup(_("Full size"));
			}
		else
			{
			auto monitor = gdk_display_get_monitor(display, j);
			gdk_monitor_get_geometry(monitor, &rect);
			subname = g_strdup(gdk_monitor_get_model(monitor));
			if (subname == nullptr)
				{
				subname = g_strdup_printf("%s %d", _("Monitor"), j + 1);
				}
			}
#endif
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
 * dest_screen: screen to place widget [use gtk_window_set_screen()]
 * same_region: the returned region will overlap the current location of widget.
 */

#if HAVE_GTK4
GdkRectangle fullscreen_prefs_get_geometry( int screen_num, GtkWidget *widget, gboolean &same_region)
{
	GdkDisplay *display = widget ? gtk_widget_get_display(widget)  : gdk_display_get_default();

	if (screen_num >= 100)
		{
		std::vector<ScreenData> list = fullscreen_prefs_list();
		auto it = std::find_if( list.cbegin(), list.cend(), [screen_num](const ScreenData &sd)
			{
			return sd.number == screen_num;
			});

		if (it != list.cend())
			{
			dest_monitor = gdk_display_get_primary_monitor(display);

			GdkSurface *surface = nullptr;
			if (widget && gtk_widget_get_native(widget))
				{
				surface = gtk_native_get_surface( gtk_widget_get_native(widget));
				}

			same_region = (!surface || (dest_monitor == gdk_display_get_monitor_at_surface(display, surface)));

			return it->geometry;
			}
		}
	else if (screen_num < 0)
		{
		screen_num = 1;
		}

	GdkRectangle geometry{};

	GdkSurface *surface = nullptr;
	if (widget && gtk_widget_get_native(widget))
		{
		surface = gtk_native_get_surface( gtk_widget_get_native(widget));
		}

	if (screen_num != 1 || !surface)
		{
		dest_monitor = gdk_display_get_primary_monitor(display);
		gdk_monitor_get_geometry(dest_monitor, &geometry);
		}
	else
		{
		dest_monitor = gdk_display_get_monitor_at_surface(display, surface);

		gdk_monitor_get_geometry(dest_monitor, &geometry);
		}

	same_region = TRUE;
	return geometry;
}
#else
GdkRectangle fullscreen_prefs_get_geometry(gint screen_num, GtkWidget *widget, GdkScreen *&dest_screen, gboolean &same_region)
{
	if (screen_num >= 100)
		{
		std::vector<ScreenData> list = fullscreen_prefs_list();
		auto it = std::find_if(list.cbegin(), list.cend(), [screen_num](const ScreenData &sd){ return sd.number == screen_num; });
		if (it != list.cend())
			{
			GdkDisplay *display = gdk_display_get_default();

			dest_screen = gdk_display_get_default_screen(display);
			same_region = (!widget || !gtk_widget_get_window(widget) ||
			               (dest_screen == gtk_widget_get_screen(widget) &&
			                (it->number%100 == 0 ||
			                 it->number%100 == deprecated_gdk_screen_get_monitor_at_window(dest_screen, gtk_widget_get_window(widget)) + 1)));
			return it->geometry;
			}
		}
	else if (screen_num < 0) screen_num = 1;

	GdkRectangle geometry;

	if (screen_num != 1 || !widget || !gtk_widget_get_window(widget))
		{
		if (widget)
			{
			dest_screen = gtk_widget_get_screen(widget);
			}
		else
			{
			dest_screen = gdk_screen_get_default();
			}

		geometry = get_screen_default_geometry(dest_screen);
		}
	else
		{
		GdkDisplay *display;
		GdkMonitor *monitor;

		display = gtk_widget_get_display(widget);
		monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(widget));

		gdk_monitor_get_geometry(monitor, &geometry);

		dest_screen = gtk_widget_get_screen(widget);
		}

	same_region = TRUE;
	return geometry;
}
#endif

enum {
	FS_MENU_COLUMN_NAME = 0,
	FS_MENU_COLUMN_VALUE
};

void fullscreen_prefs_selection_cb(GtkWidget *combo, gpointer value)
{
	if (!value) return;

	GtkTreeModel *store = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
	GtkTreeIter iter;
	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) return;

	gtk_tree_model_get(store, &iter, FS_MENU_COLUMN_VALUE, value, -1);
}

gint get_monitor_index(GdkDisplay *display, GdkMonitor *target_monitor)
{
	gint num_monitors = gdk_display_get_n_monitors(display);

	for (gint i = 0; i < num_monitors; i++)
		{
		GdkMonitor *monitor = gdk_display_get_monitor(display, i);

		if (monitor == target_monitor)
			{
			return i;
			}
		}

	return -1; // Monitor not found
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
	GdkGeometry geometry;
	GdkMonitor *monitor;
	GdkScreen *screen;

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

	GdkRectangle rect{};
	gboolean same_region = FALSE;

#if HAVE_GTK4
	rect = fullscreen_prefs_get_geometry(options->fullscreen.screen, window, same_region);
#else
	screen = gtk_widget_get_screen(window);
	rect = fullscreen_prefs_get_geometry(options->fullscreen.screen, window, screen, same_region);
#endif

	fs->same_region = same_region;

	fs->window = window_new("fullscreen", nullptr, _("Full screen"));
	DEBUG_NAME(fs->window);

#if !HAVE_GTK4
	g_signal_connect(fs->window, "delete-event",
	                 G_CALLBACK(fullscreen_delete_cb), fs);
#endif

	gtk_window_set_decorated(GTK_WINDOW(fs->window), FALSE);

#if !HAVE_GTK4
	gtk_container_set_border_width(GTK_CONTAINER(fs->window), 0);
#endif

#if !HAVE_GTK4
	/* set default size and position, so the window appears where it was before */
	gtk_window_set_default_size(GTK_WINDOW(fs->window), rect.width, rect.height);
	gq_gtk_window_move(GTK_WINDOW(fs->window), rect.x, rect.y);

	/* By setting USER_POS and USER_SIZE, most window managers will
	 * not request positioning of the full screen window (for example twm).
	 *
	 * In addition, setting gravity to STATIC will result in the
	 * decorations of twm to not effect the requested window position,
	 * the decorations will simply be off screen, except in multi monitor setups :-/
	 */
	geometry.min_width = 1;
	geometry.min_height = 1;
	geometry.base_width = rect.width;
	geometry.base_height = rect.height;
	geometry.win_gravity = GDK_GRAVITY_STATIC;
	gtk_window_set_geometry_hints(GTK_WINDOW(fs->window), fs->window, &geometry,
			static_cast<GdkWindowHints>(GDK_HINT_WIN_GRAVITY | GDK_HINT_USER_POS | GDK_HINT_USER_SIZE));

	gtk_widget_realize(fs->window);

	if ((options->fullscreen.screen % 100) == 0)
		{
		GdkWindow *gdkwin;
		gdkwin = gtk_widget_get_window(fs->window);
		if (gdkwin != nullptr)
			gdk_window_set_fullscreen_mode(gdkwin, GDK_FULLSCREEN_ON_ALL_MONITORS);
		}
#endif

	/* Monitor selection */
	auto monitor_number = options->fullscreen.screen;

	if (monitor_number < 0)
		{
		monitor_number = 0;
		}
	else if (monitor_number == 1)
		{
#if !HAVE_GTK4
		display = gtk_widget_get_display(window);
		monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(window));

		monitor_number = get_monitor_index(display, monitor);
#endif
		}
	else if (monitor_number >= 100)
		{
		monitor_number = (monitor_number % 100) - 1;
		}

#if HAVE_GTK4
	if (monitor_number >= 0)
		{
		gq_gtk_window_fullscreen_on_monitor(GTK_WINDOW(fs->window), monitor_number);
		}
	else
		{
		gtk_window_fullscreen(GTK_WINDOW(fs->window));
		}
#else
	gq_gtk_window_fullscreen_on_monitor(GTK_WINDOW(fs->window), screen, monitor_number);
#endif

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
	g_signal_connect(G_OBJECT(fs->imd->pr), "motion_notify_event",
			   G_CALLBACK(fullscreen_mouse_moved), fs);
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
	widget_input_ungrab(fs->window);

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

	g_autoptr(GtkListStore) store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	GtkWidget *combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));

	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer,
				       "text", FS_MENU_COLUMN_NAME, NULL);

	const std::array<ScreenData, 3> default_list = {{
	    {-1, _("Determined by Window Manager"), {}},
	    { 0, _("Active screen"), {},},
	    { 1, _("Active monitor"), {} },
	}};

	std::vector<ScreenData> list = fullscreen_prefs_list();
	list.insert(list.begin(), default_list.cbegin(), default_list.cend());
	for (const ScreenData &sd : list)
		{
		GtkTreeIter iter;

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
		                   FS_MENU_COLUMN_NAME, sd.description.c_str(),
		                   FS_MENU_COLUMN_VALUE, sd.number,
		                   -1);
		}

	const auto it = std::find_if(list.cbegin(), list.cend(),
	                             [screen_num = *screen_value](const ScreenData &sd){ return sd.number == screen_num; });
	const gint current = (it != list.cend()) ? std::distance(list.cbegin(), it) : 0;
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	gq_gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);
	gtk_widget_show(combo);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(fullscreen_prefs_selection_cb), screen_value);

	return hbox;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
