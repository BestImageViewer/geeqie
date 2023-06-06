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

#include "main.h"
#include "fullscreen.h"

#include "image.h"
#include "misc.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "window.h"
#include "image-load.h"

enum {
	FULLSCREEN_CURSOR_HIDDEN = 1 << 0,
	FULLSCREEN_CURSOR_NORMAL = 1 << 1,
	FULLSCREEN_CURSOR_BUSY   = 1 << 2
};


/*
 *----------------------------------------------------------------------------
 * full screen functions
 *----------------------------------------------------------------------------
 */

static void clear_mouse_cursor(GtkWidget *widget, gint state)
{
	GdkWindow *window = gtk_widget_get_window(widget);
	if (!window) return;

	if (state & FULLSCREEN_CURSOR_BUSY)
		{
		GdkCursor *cursor;

		cursor = gdk_cursor_new(GDK_WATCH);
		gdk_window_set_cursor(window, cursor);
		g_object_unref(G_OBJECT(cursor));
		}
	else if (state & FULLSCREEN_CURSOR_NORMAL)
		{
		gdk_window_set_cursor(window, nullptr);
		}
	else
		{
		GdkCursor *cursor;

		cursor = gdk_cursor_new(GDK_BLANK_CURSOR);
		gdk_window_set_cursor(window, cursor);
		g_object_unref(G_OBJECT(cursor));
		}
}

static gboolean fullscreen_hide_mouse_cb(gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (!fs->hide_mouse_id) return FALSE;

	fs->cursor_state &= ~FULLSCREEN_CURSOR_NORMAL;
	if (!(fs->cursor_state & FULLSCREEN_CURSOR_BUSY)) clear_mouse_cursor(fs->window, fs->cursor_state);

	g_source_remove(fs->hide_mouse_id);
	fs->hide_mouse_id = 0;
	return FALSE;
}

static void fullscreen_hide_mouse_disable(FullScreenData *fs)
{
	if (fs->hide_mouse_id)
		{
		g_source_remove(fs->hide_mouse_id);
		fs->hide_mouse_id = 0;
		}
}

static void fullscreen_hide_mouse_reset(FullScreenData *fs)
{
	fullscreen_hide_mouse_disable(fs);
	fs->hide_mouse_id = g_timeout_add(FULL_SCREEN_HIDE_MOUSE_DELAY, fullscreen_hide_mouse_cb, fs);
}

static gboolean fullscreen_mouse_moved(GtkWidget *UNUSED(widget), GdkEventMotion *UNUSED(event), gpointer data)
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

static void fullscreen_busy_mouse_disable(FullScreenData *fs)
{
	if (fs->busy_mouse_id)
		{
		g_source_remove(fs->busy_mouse_id);
		fs->busy_mouse_id = 0;
		}
}

static void fullscreen_mouse_set_busy(FullScreenData *fs, gboolean busy)
{
	fullscreen_busy_mouse_disable(fs);

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

static gboolean fullscreen_mouse_set_busy_cb(gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	fs->busy_mouse_id = 0;
	fullscreen_mouse_set_busy(fs, TRUE);
	return FALSE;
}

static void fullscreen_mouse_set_busy_idle(FullScreenData *fs)
{
	if (!fs->busy_mouse_id)
		{
		fs->busy_mouse_id = g_timeout_add(FULL_SCREEN_BUSY_MOUSE_DELAY,
						  fullscreen_mouse_set_busy_cb, fs);
		}
}

static void fullscreen_image_update_cb(ImageWindow *UNUSED(imd), gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (fs->imd->il &&
	    image_loader_get_pixbuf(fs->imd->il) != image_get_pixbuf(fs->imd))
		{
		fullscreen_mouse_set_busy_idle(fs);
		}
}

static void fullscreen_image_complete_cb(ImageWindow *UNUSED(imd), gboolean preload, gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	if (!preload) fullscreen_mouse_set_busy(fs, FALSE);
}

#define XSCREENSAVER_BINARY	"xscreensaver-command"
#define XSCREENSAVER_COMMAND	"xscreensaver-command -deactivate >&- 2>&- &"

static void fullscreen_saver_deactivate(void)
{
	static gboolean checked = FALSE;
	static gboolean found = FALSE;

	if (!checked)
		{
		checked = TRUE;
		found = file_in_path(XSCREENSAVER_BINARY);
		}

	if (found)
		{
		runcmd(XSCREENSAVER_COMMAND);
		}
}

static gboolean fullscreen_saver_block_cb(gpointer UNUSED(data))
{
	if (options->fullscreen.disable_saver)
		{
		fullscreen_saver_deactivate();
		}

	return TRUE;
}

static gboolean fullscreen_delete_cb(GtkWidget *UNUSED(widget), GdkEventAny *UNUSED(event), gpointer data)
{
	auto fs = static_cast<FullScreenData *>(data);

	fullscreen_stop(fs);
	return TRUE;
}

FullScreenData *fullscreen_start(GtkWidget *window, ImageWindow *imd,
				 void (*stop_func)(FullScreenData *, gpointer), gpointer stop_data)
{
	FullScreenData *fs;
	GdkScreen *screen;
	gint x, y;
	gint w, h;
	GdkGeometry geometry;

	if (!window || !imd) return nullptr;

	fs = g_new0(FullScreenData, 1);

	fs->cursor_state = FULLSCREEN_CURSOR_HIDDEN;

	fs->normal_window = window;
	fs->normal_imd = imd;

	fs->stop_func = stop_func;
	fs->stop_data = stop_data;

	DEBUG_1("full screen requests screen %d", options->fullscreen.screen);
	fullscreen_prefs_get_geometry(options->fullscreen.screen, window, &x, &y, &w, &h,
				      &screen, &fs->same_region);

	fs->window = window_new(GTK_WINDOW_TOPLEVEL, "fullscreen", nullptr, nullptr, _("Full screen"));
	DEBUG_NAME(fs->window);

	g_signal_connect(G_OBJECT(fs->window), "delete_event",
			 G_CALLBACK(fullscreen_delete_cb), fs);

	/* few cosmetic details */
	gtk_window_set_decorated(GTK_WINDOW(fs->window), FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(fs->window), 0);

	/* keep window above others, if requested */
	if (options->fullscreen.above) {
		gtk_window_set_keep_above(GTK_WINDOW(fs->window), TRUE);
	}

	/* set default size and position, so the window appears where it was before */
	gtk_window_set_default_size(GTK_WINDOW(fs->window), w, h);
	gtk_window_move(GTK_WINDOW(fs->window), x, y);

	/* By setting USER_POS and USER_SIZE, most window managers will
	 * not request positioning of the full screen window (for example twm).
	 *
	 * In addition, setting gravity to STATIC will result in the
	 * decorations of twm to not effect the requested window position,
	 * the decorations will simply be off screen, except in multi monitor setups :-/
	 */
	geometry.min_width = 1;
	geometry.min_height = 1;
	geometry.base_width = w;
	geometry.base_height = h;
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

	/* make window fullscreen -- let Gtk do it's job, don't screw it in any way */
	gtk_window_fullscreen(GTK_WINDOW(fs->window));

	/* move it to requested screen */
	if (options->fullscreen.screen >= 0)
		{
		gtk_window_set_screen(GTK_WINDOW(fs->window), screen);
		}

	fs->imd = image_new(FALSE);

	gtk_container_add(GTK_CONTAINER(fs->window), fs->imd->widget);

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

	if (options->stereo.enable_fsmode) {
		image_stereo_set(fs->imd, options->stereo.fsmode);
	}

	gtk_widget_show(fs->window);

	/* for hiding the mouse */
	g_signal_connect(G_OBJECT(fs->imd->pr), "motion_notify_event",
			   G_CALLBACK(fullscreen_mouse_moved), fs);
	clear_mouse_cursor(fs->window, fs->cursor_state);

	/* set timer to block screen saver */
	fs->saver_block_id = g_timeout_add(60 * 1000, fullscreen_saver_block_cb, fs);

	/* hide normal window */
	 /** @FIXME properly restore this window on show
	 */
	if (fs->same_region)
		{
		if (options->hide_window_in_fullscreen)
			{
			gtk_widget_hide(fs->normal_window);
			}
		image_change_fd(fs->normal_imd, nullptr, image_zoom_get(fs->normal_imd));
		}

	return fs;
}

void fullscreen_stop(FullScreenData *fs)
{
	if (!fs) return;

	if (fs->saver_block_id) g_source_remove(fs->saver_block_id);

	fullscreen_hide_mouse_disable(fs);
	fullscreen_busy_mouse_disable(fs);
	gdk_keyboard_ungrab(GDK_CURRENT_TIME);

	if (fs->same_region)
		{
		image_move_from_image(fs->normal_imd, fs->imd);
		if (options->hide_window_in_fullscreen)
			{
			gtk_widget_show(fs->normal_window);
			}
		if (options->stereo.enable_fsmode)
			{
			image_stereo_set(fs->normal_imd, options->stereo.mode);
			}
		}


	if (fs->stop_func) fs->stop_func(fs, fs->stop_data);

	gtk_widget_destroy(fs->window);

	gtk_window_present(GTK_WINDOW(fs->normal_window));

	g_free(fs);
}


/*
 *----------------------------------------------------------------------------
 * full screen preferences and utils
 *----------------------------------------------------------------------------
 */

GList *fullscreen_prefs_list(void)
{
	GList *list = nullptr;
	GdkDisplay *display;
	gint number;
	gint i;

	display = gdk_display_get_default();
	number = gdk_display_get_n_screens(display);

	for (i = 0; i < number; i++)
		{
		GdkScreen *screen;
		gint monitors;
		gint j;

		screen = gdk_display_get_screen(display, i);
		monitors = gdk_screen_get_n_monitors(screen);

		for (j = -1; j < monitors; j++)
			{
			ScreenData *sd;
			GdkRectangle rect;
			gchar *name;
			gchar *subname;

			name = gdk_screen_make_display_name(screen);

			if (j < 0)
				{
				rect.x = 0;
				rect.y = 0;
				rect.width = gdk_screen_get_width(screen);
				rect.height = gdk_screen_get_height(screen);
				subname = g_strdup(_("Full size"));
				}
			else
				{
				gdk_screen_get_monitor_geometry(screen, j, &rect);
				subname = gdk_screen_get_monitor_plug_name(screen, j);
				if (subname == nullptr)
					{
					subname = g_strdup_printf("%s %d", _("Monitor"), j + 1);
					}
				}

			sd = g_new0(ScreenData, 1);
			sd->number = (i+1) * 100 + j + 1;
			sd->description = g_strdup_printf("%s %s, %s", _("Screen"), name, subname);
			sd->x = rect.x;
			sd->y = rect.y;
			sd->width = rect.width;
			sd->height = rect.height;

			DEBUG_1("Screen %d %30s %4d,%4d (%4dx%4d)",
					  sd->number, sd->description, sd->x, sd->y, sd->width, sd->height);

			list = g_list_append(list, sd);

			g_free(name);
			g_free(subname);
			}
		}

	return list;
}

void fullscreen_prefs_list_free(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		auto sd = static_cast<ScreenData *>(work->data);
		work = work->next;

		g_free(sd->description);
		g_free(sd);
		}

	g_list_free(list);
}

ScreenData *fullscreen_prefs_list_find(GList *list, gint screen)
{
	GList *work;

	work = list;
	while (work)
		{
		auto sd = static_cast<ScreenData *>(work->data);
		work = work->next;

		if (sd->number == screen) return sd;
		}

	return nullptr;
}

/* screen is interpreted as such:
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
void fullscreen_prefs_get_geometry(gint screen, GtkWidget *widget, gint *x, gint *y, gint *width, gint *height,
				   GdkScreen **dest_screen, gboolean *same_region)
{
	GList *list;
	ScreenData *sd;

	list = fullscreen_prefs_list();
	if (screen >= 100)
		{
		sd = fullscreen_prefs_list_find(list, screen);
		}
	else
		{
		sd = nullptr;
		if (screen < 0) screen = 1;
		}

	if (sd)
		{
		GdkDisplay *display;
		GdkScreen *screen;
		gint n;

		display = gdk_display_get_default();
		n = sd->number / 100 - 1;
		if (n >= 0 && n < gdk_display_get_n_screens(display))
			{
			screen = gdk_display_get_screen(display, n);
			}
		else
			{
			screen = gdk_display_get_default_screen(display);
			}

		if (x) *x = sd->x;
		if (y) *y = sd->y;
		if (width) *width = sd->width;
		if (height) *height = sd->height;

		if (dest_screen) *dest_screen = screen;
		if (same_region) *same_region = (!widget || !gtk_widget_get_window(widget) ||
					(screen == gtk_widget_get_screen(widget) &&
					(sd->number%100 == 0 ||
					 sd->number%100 == gdk_screen_get_monitor_at_window(screen, gtk_widget_get_window(widget))+1)));

		}
	else if (screen != 1 || !widget || !gtk_widget_get_window(widget))
		{
		GdkScreen *screen;

		if (widget)
			{
			screen = gtk_widget_get_screen(widget);
			}
		else
			{
			screen = gdk_screen_get_default();
			}

		if (x) *x = 0;
		if (y) *y = 0;
		if (width) *width = gdk_screen_get_width(screen);
		if (height) *height = gdk_screen_get_height(screen);

		if (dest_screen) *dest_screen = screen;
		if (same_region) *same_region = TRUE;
		}
	else
		{
		GdkScreen *screen;
		gint monitor;
		GdkRectangle rect;

		screen = gtk_widget_get_screen(widget);
		monitor = gdk_screen_get_monitor_at_window(screen, gtk_widget_get_window(widget));

		gdk_screen_get_monitor_geometry(screen, monitor, &rect);

		if (x) *x = rect.x;
		if (y) *y = rect.y;
		if (width) *width = rect.width;
		if (height) *height = rect.height;

		if (dest_screen) *dest_screen = screen;
		if (same_region) *same_region = TRUE;
		}

	fullscreen_prefs_list_free(list);
}

//gint fullscreen_prefs_find_screen_for_widget(GtkWidget *widget)
//{
	//GdkScreen *screen;
	//gint monitor;
	//gint n;

	//if (!widget || !gtk_widget_get_window(widget)) return 0;

	//screen = gtk_widget_get_screen(widget);
	//monitor = gdk_screen_get_monitor_at_window(screen, gtk_widget_get_window(widget));

	//n = (gdk_screen_get_number(screen)+1) * 100 + monitor + 1;

	//DEBUG_1("Screen appears to be %d", n);

	//return n;
//}

enum {
	FS_MENU_COLUMN_NAME = 0,
	FS_MENU_COLUMN_VALUE
};

#define BUTTON_ABOVE_KEY  "button_above"

static void fullscreen_prefs_selection_cb(GtkWidget *combo, gpointer data)
{
	auto value = static_cast<gint *>(data);
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkWidget *button;

	if (!value) return;

	store = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) return;
	gtk_tree_model_get(store, &iter, FS_MENU_COLUMN_VALUE, value, -1);

	button = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(combo), BUTTON_ABOVE_KEY));
	if (button)
		{
		gtk_widget_set_sensitive(button, *value != -1);
		}
}

static void fullscreen_prefs_selection_add(GtkListStore *store, const gchar *text, gint value)
{
	GtkTreeIter iter;

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, FS_MENU_COLUMN_NAME, text,
					 FS_MENU_COLUMN_VALUE, value, -1);
}

GtkWidget *fullscreen_prefs_selection_new(const gchar *text, gint *screen_value, gboolean *UNUSED(above_value))
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *combo;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GList *list;
	GList *work;
	gint current = 0;
	gint n;

	if (!screen_value) return nullptr;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	DEBUG_NAME(vbox);
	hbox = pref_box_new(vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	if (text) pref_label_new(hbox, text);

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer,
				       "text", FS_MENU_COLUMN_NAME, NULL);

	fullscreen_prefs_selection_add(store, _("Determined by Window Manager"), -1);
	fullscreen_prefs_selection_add(store, _("Active screen"), 0);
	if (*screen_value == 0) current = 1;
	fullscreen_prefs_selection_add(store, _("Active monitor"), 1);
	if (*screen_value == 1) current = 2;

	n = 3;
	list = fullscreen_prefs_list();
	work = list;
	while (work)
		{
		auto sd = static_cast<ScreenData *>(work->data);

		fullscreen_prefs_selection_add(store, sd->description, sd->number);
		if (*screen_value == sd->number) current = n;

		work = work->next;
		n++;
		}
	fullscreen_prefs_list_free(list);

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);
	gtk_widget_show(combo);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(fullscreen_prefs_selection_cb), screen_value);

	return vbox;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
