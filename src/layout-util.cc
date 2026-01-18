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

#include "layout-util.h"

#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <gio/gio.h>
#include <glib-object.h>

#include <config.h>

#include "advanced-exif.h"
#include "archives.h"
#include "bar-keywords.h"
#include "bar-sort.h"
#include "bar.h"
#include "cache-maint.h"
#include "collect-io.h"
#include "collect.h"
#include "color-man.h"
#include "compat-deprecated.h"
#include "compat.h"
#include "desktop-file.h"
#include "dupe.h"
#include "editors.h"
#include "filedata.h"
#include "filefilter.h"
#include "fullscreen.h"
#include "histogram.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "layout-image.h"
#include "layout.h"
#include "logwindow.h"
#include "main-defines.h"
#include "main.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pan-view.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "preferences.h"
#include "print.h"
#include "rcfile.h"
#include "search-and-run.h"
#include "search.h"
#include "slideshow.h"
#include "toolbar.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "view-dir.h"
#include "view-file.h"
#include "window.h"

namespace
{

struct WindowNames
{
	gchar *name;
	gchar *path;
};

void window_names_free(WindowNames *wn)
{
	if (!wn) return;

	g_free(wn->name);
	g_free(wn->path);
	g_free(wn);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(WindowNames, window_names_free)

struct LayoutEditors
{
	gint reload_idle_id = -1;
	GList *desktop_files = nullptr;
} layout_editors;

/**
 * @brief Checks if event key is mapped to Help
 * @param event
 * @returns
 *
 * Used to check if the user has re-mapped the Help key
 * in Preferences/Keyboard
 *
 * Note: help_key.accel_mods and state
 * differ in the higher bits
 */
gboolean is_help_key_impl(guint keyval, GdkModifierType state)
{
	GtkAccelKey help_key;
	guint mask = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK;

	if (gtk_accel_map_lookup_entry("<Actions>/MenuActions/HelpContents", &help_key))
		{
		if (help_key.accel_key == keyval && (help_key.accel_mods & mask) == (state & mask))
			{
			return TRUE;
			}
		}

	return FALSE;
}

} // namespace

static gboolean layout_bar_enabled(LayoutWindow *lw);
static gboolean layout_bar_sort_enabled(LayoutWindow *lw);
static void layout_bars_hide_toggle(LayoutWindow *lw);
static void layout_util_sync_views(LayoutWindow *lw);
static void layout_search_and_run_window_new(LayoutWindow *lw);

/*
 *-----------------------------------------------------------------------------
 * keyboard handler
 *-----------------------------------------------------------------------------
 */

static guint tree_key_overrides[] = {
	GDK_KEY_Page_Up,	GDK_KEY_KP_Page_Up,
	GDK_KEY_Page_Down,	GDK_KEY_KP_Page_Down,
	GDK_KEY_Home,	GDK_KEY_KP_Home,
	GDK_KEY_End,	GDK_KEY_KP_End
};

static gboolean layout_key_match(guint keyval)
{
	const auto it = std::find(std::cbegin(tree_key_overrides), std::cend(tree_key_overrides), keyval);

	return it != std::cend(tree_key_overrides);
}

void keyboard_scroll_calc(gint &x, gint &y, const GdkEventKey *event)
{
	static gint delta = 0;
	static guint32 time_old = 0;
	static guint keyval_old = 0;

	if (event->state & GDK_SHIFT_MASK)
		{
		x *= 3;
		y *= 3;
		}

	if (event->state & GDK_CONTROL_MASK)
		{
		if (x < 0) x = G_MININT / 2;
		if (x > 0) x = G_MAXINT / 2;
		if (y < 0) y = G_MININT / 2;
		if (y > 0) y = G_MAXINT / 2;

		return;
		}

	if (options->progressive_key_scrolling)
		{
		guint32 time_diff;

		time_diff = event->time - time_old;

		/* key pressed within 125ms ? (1/8 second) */
		if (time_diff > 125 || event->keyval != keyval_old) delta = 0;

		time_old = event->time;
		keyval_old = event->keyval;

		delta += 2;
		}
	else
		{
		delta = 8;
		}

	x *= delta * options->keyboard_scroll_step;
	y *= delta * options->keyboard_scroll_step;
}

static gboolean layout_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->path_entry && gtk_widget_has_focus(lw->path_entry))
		{
		if (event->keyval == GDK_KEY_Escape && lw->dir_fd)
			{
			gq_gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);
			}

		/* the gtkaccelgroup of the window is stealing presses before they get to the entry (and more),
		 * so when the some widgets have focus, give them priority (HACK)
		 */
		if (gtk_widget_event(lw->path_entry, reinterpret_cast<GdkEvent *>(event)))
			{
			return TRUE;
			}
		}

	if (lw->vf->file_filter.combo)
		{
		GtkWidget *combo_entry = gtk_bin_get_child(GTK_BIN(lw->vf->file_filter.combo));
		if (gtk_widget_has_focus(combo_entry) && gtk_widget_event(combo_entry, reinterpret_cast<GdkEvent *>(event)))
			{
			return TRUE;
			}
		}

	if (lw->vd && lw->options.dir_view_type == DIRVIEW_TREE && gtk_widget_has_focus(lw->vd->view) &&
	    !layout_key_match(event->keyval) &&
	    gtk_widget_event(lw->vd->view, reinterpret_cast<GdkEvent *>(event)))
		{
		return TRUE;
		}

	if (lw->bar &&
	    bar_event(lw->bar, reinterpret_cast<GdkEvent *>(event)))
		{
		return TRUE;
		}

	GtkWidget *focused = gtk_container_get_focus_child(GTK_CONTAINER(lw->image->widget));
	gboolean stop_signal = FALSE;
	gint x = 0;
	gint y = 0;
	if (lw->image &&
	    ((focused && gtk_widget_has_focus(focused)) || (lw->tools && widget == lw->window) || lw->full_screen) )
		{
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

		if (!stop_signal &&
		    !(event->state & GDK_CONTROL_MASK))
			{
			stop_signal = TRUE;
			switch (event->keyval)
				{
				case GDK_KEY_Menu:
					layout_image_menu_popup(lw);
					break;
				default:
					stop_signal = FALSE;
					break;
				}
			}
		}

	if (x != 0 || y!= 0)
		{
		keyboard_scroll_calc(x, y, event);
		layout_image_scroll(lw, x, y, (event->state & GDK_SHIFT_MASK));
		}

	return stop_signal;
}

void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window)
{
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(layout_key_press_cb), lw);
}

bool layout_handle_user_defined_mouse_buttons(LayoutWindow *lw, GdkEventButton *event)
{
	enum MouseButton {
		MOUSE_BUTTON_8 = 8,
		MOUSE_BUTTON_9 = 9
	};

	const auto handle_button = [lw](const gchar *action_name)
	{
		if (!action_name) return false;

		if (g_strstr_len(action_name, -1, ".desktop") != nullptr)
			{
			file_util_start_editor_from_filelist(action_name, layout_selection_list(lw), layout_get_path(lw), lw->window);
			}
		else
			{
			GtkAction *action = gq_gtk_action_group_get_action(lw->action_group, action_name);
			if (action)
				{
				gq_gtk_action_activate(action);
				}
			}

		return true;
	};

	switch (event->button)
		{
		case MOUSE_BUTTON_8:
			return handle_button(options->mouse_button_8);
		case MOUSE_BUTTON_9:
			return handle_button(options->mouse_button_9);
		default:
			return false;
		}
}

/*
 *-----------------------------------------------------------------------------
 * menu callbacks
 *-----------------------------------------------------------------------------
 */


static GtkWidget *layout_window(LayoutWindow *lw)
{
	return lw->full_screen ? lw->full_screen->window : lw->window;
}

static void layout_exit_fullscreen(LayoutWindow *lw)
{
	if (!lw->full_screen) return;
	layout_image_full_screen_stop(lw);
}

static void clear_marks_cancel_cb(GenericDialog *gd, gpointer)
{
	generic_dialog_close(gd);
}

static void clear_marks_help_cb(GenericDialog *, gpointer)
{
	help_window_show("GuideMainWindowMenus.html");
}

static void layout_menu_clear_marks_ok_cb(GenericDialog *gd, gpointer)
{
	marks_clear_all();
	generic_dialog_close(gd);
}

static void layout_menu_clear_marks_cb(GtkAction *, gpointer)
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Clear Marks"),
				"marks_clear", nullptr, FALSE, clear_marks_cancel_cb, nullptr);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Clear all marks?"), _("This will clear all marks for all images,\nincluding those linked to keywords"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", layout_menu_clear_marks_ok_cb, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_HELP, _("Help"),
				clear_marks_help_cb, FALSE);

	gtk_widget_show(gd->dialog);
}

static void layout_menu_new_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	collection_window_new(nullptr);
}

static void layout_menu_search_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	search_new(lw->dir_fd, layout_image_get_fd(lw));
}

static void layout_menu_dupes_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	dupe_window_new();
}

static void layout_menu_pan_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	pan_window_new(lw->dir_fd);
}

static void layout_menu_print_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	print_window_new(layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_dir_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->vd) vd_new_folder(lw->vd, lw->dir_fd);
}

static void layout_menu_copy_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

template<gboolean quoted>
static void layout_menu_copy_path_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), quoted, ClipboardAction::COPY);
}

static void layout_menu_copy_image_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ImageWindow *imd = lw->image;

	GdkPixbuf *pixbuf;
	pixbuf = image_get_pixbuf(imd);
	if (!pixbuf) return;

#if HAVE_GTK4
	GdkDisplay *display = gdk_display_get_default();
	if (!display)
		{
		return;
		}

	GdkClipboard *clipboard = gdk_display_get_clipboard(display);
	if (!clipboard)
		{
		return;
		}

	GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
	gdk_clipboard_set_texture(clipboard, texture);
	g_object_unref(texture);
#else
	gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pixbuf);
#endif
}

static void layout_menu_cut_path_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), FALSE, ClipboardAction::CUT);
}

static void layout_menu_move_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_move(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

static void layout_menu_rename_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_rename(nullptr, layout_selection_list(lw), layout_window(lw));
}

template<gboolean safe_delete>
static void layout_menu_delete_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw), safe_delete);
}

static void layout_menu_move_to_trash_key_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (options->file_ops.enable_delete_key)
		{
		file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw), TRUE);
		}
}

template<gboolean disable>
static void layout_menu_disable_grouping_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_data_disable_grouping_list(layout_selection_list(lw), disable);
}

void layout_menu_close_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_close(lw);
}

static void layout_menu_exit_cb(GtkAction *, gpointer)
{
	exit_program();
}

template<AlterType type>
static void layout_menu_alter_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_alter_orientation(lw, type);
}

template<gint rating>
static void layout_menu_rating_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, std::to_string(rating).c_str());
}

static void layout_menu_alter_desaturate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_set_desaturate(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_alter_ignore_alpha_cb(GtkToggleAction *action, gpointer data)
{
   auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.ignore_alpha == gq_gtk_toggle_action_get_active(action)) return;

   layout_image_set_ignore_alpha(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_exif_rotate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->image.exif_rotate_enable = gq_gtk_toggle_action_get_active(action);
	layout_image_reset_orientation(lw);
}

static void layout_menu_select_rectangle_cb(GtkToggleAction *action, gpointer)
{
	options->draw_rectangle = gq_gtk_toggle_action_get_active(action);
}

static void layout_menu_split_pane_sync_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	lw->options.split_pane_sync = gq_gtk_toggle_action_get_active(action);
}

static void layout_menu_select_overunderexposed_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_set_overunderexposed(lw, gq_gtk_toggle_action_get_active(action));
}

template<gboolean keep_date>
static void layout_menu_write_rotate_cb(GtkToggleAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (!layout_valid(&lw)) return;
	if (!lw || !lw->vf) return;

	const gchar *keep_date_arg = keep_date ? "-t" : "";

	vf_selection_foreach(lw->vf, [keep_date_arg](FileData *fd_n)
	{
		g_autofree gchar *command = g_strdup_printf("%s/geeqie-rotate -r %d %s \"%s\"",
		                                            gq_bindir, fd_n->user_orientation, keep_date_arg, fd_n->path);
		int cmdstatus = runcmd(command);
		gint run_result = WEXITSTATUS(cmdstatus);
		if (!run_result)
			{
			fd_n->user_orientation = 0;
			}
		else
			{
			g_autoptr(GString) message = g_string_new(_("Operation failed:\n"));

			if (run_result == 1)
				message = g_string_append(message, _("No file extension\n"));
			else if (run_result == 3)
				message = g_string_append(message, _("Cannot create tmp file\n"));
			else if (run_result == 4)
				message = g_string_append(message, _("Operation not supported for filetype\n"));
			else if (run_result == 5)
				message = g_string_append(message, _("File is not writable\n"));
			else if (run_result == 6)
				message = g_string_append(message, _("Exiftran error\n"));
			else if (run_result == 7)
				message = g_string_append(message, _("Mogrify error\n"));

			message = g_string_append(message, fd_n->name);

			GenericDialog *gd = generic_dialog_new(_("Image orientation"), "image_orientation", nullptr, TRUE, nullptr, nullptr);
			generic_dialog_add_message(gd, GQ_ICON_DIALOG_ERROR, _("Image orientation"), message->str, TRUE);
			generic_dialog_add_button(gd, GQ_ICON_OK, "OK", nullptr, TRUE);

			gtk_widget_show(gd->dialog);
			}
	});
}

static void layout_menu_config_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_config_window(lw);
}

static void layout_menu_editors_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_editor_list_window();
}

static void layout_menu_layout_config_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_show_config_window(lw);
}

static void layout_menu_remove_thumb_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	cache_manager_show();
}

static void layout_menu_wallpaper_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_to_root(lw);
}

template<gboolean connect_zoom>
static void layout_menu_zoom_in_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, get_zoom_increment(), connect_zoom);
}

template<gboolean connect_zoom>
static void layout_menu_zoom_out_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, -get_zoom_increment(), connect_zoom);
}

template<int value, gboolean connect_zoom>
static void layout_menu_zoom_set_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, value, connect_zoom);
}

template<gboolean vertical, gboolean connect_zoom>
static void layout_menu_zoom_fit_hv_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set_fill_geometry(lw, vertical, connect_zoom);
}

static void layout_menu_zoom_to_rectangle_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	const auto [x1, y1, x2, y2] = image_get_rectangle();

	auto *pr = reinterpret_cast<PixbufRenderer *>(lw->image->pr);

	gint image_width = x2 - x1;
	gint image_height = y2 - y1;

	gdouble zoom_width = static_cast<gdouble>(pr->vis_width) / image_width;
	gdouble zoom_height = static_cast<gdouble>(pr->vis_height) / image_height;

	const GdkRectangle rect = pr_coords_map_orientation_reverse(pr->orientation,
	                                                            {x1, y1, image_width, image_height},
	                                                            pr->image_width, pr->image_height);

	gint center_x = (rect.width / 2) + rect.x;
	gint center_y = (rect.height / 2) + rect.y;

	layout_image_zoom_set(lw, std::min(zoom_width, zoom_height), FALSE);
	image_scroll_to_point(lw->image, center_x, center_y, 0.5, 0.5);
}

static void layout_menu_split_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ImageSplitMode mode;

	layout_exit_fullscreen(lw);
	mode = static_cast<ImageSplitMode>(gq_gtk_radio_action_get_current_value(action));
	layout_split_change(lw, mode);
}


static void layout_menu_thumb_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_thumb_set(lw, gq_gtk_toggle_action_get_active(action));
}


static void layout_menu_list_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_views_set(lw, lw->options.dir_view_type, static_cast<FileViewType>(gq_gtk_radio_action_get_current_value(action)));
}

static void layout_menu_view_dir_as_cb(GtkToggleAction *action,  gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);

	if (gq_gtk_toggle_action_get_active(action))
		{
		layout_views_set(lw, DIRVIEW_TREE, lw->options.file_view_type);
		}
	else
		{
		layout_views_set(lw, DIRVIEW_LIST, lw->options.file_view_type);
		}
}

static void layout_menu_view_in_new_window_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	view_window_new(layout_image_get_fd(lw));
}

struct OpenWithData
{
	GAppInfo *application;
	GList *g_file_list;
	GtkWidget *app_chooser_dialog;
};

static void open_with_data_free(OpenWithData *open_with_data)
{
	if (!open_with_data) return;

	g_object_unref(open_with_data->application);
	g_object_unref(g_list_first(open_with_data->g_file_list)->data);
	g_list_free(open_with_data->g_file_list);
	gq_gtk_widget_destroy(open_with_data->app_chooser_dialog);
	g_free(open_with_data);
}

static void open_with_response_cb(GtkDialog *, gint response_id, gpointer data)
{
	auto open_with_data = static_cast<OpenWithData *>(data);

	if (response_id == GTK_RESPONSE_OK)
		{
		g_autoptr(GError) error = nullptr;
		g_app_info_launch(open_with_data->application, open_with_data->g_file_list, nullptr, &error);

		if (error)
			{
			log_printf("Error launching app: %s\n", error->message);
			}
		}

	open_with_data_free(open_with_data);
}

static void open_with_close(GtkDialog *, gpointer data)
{
	auto open_with_data = static_cast<OpenWithData *>(data);

	open_with_data_free(open_with_data);
}

static void open_with_application_selected_cb(GtkAppChooserWidget *, GAppInfo *application, gpointer data)
{
	auto open_with_data = static_cast<OpenWithData *>(data);

	g_object_unref(open_with_data->application);

	open_with_data->application = g_app_info_dup(application);
}

static void open_with_application_activated_cb(GtkAppChooserWidget *, GAppInfo *application, gpointer data)
{
	auto open_with_data = static_cast<OpenWithData *>(data);

	g_autoptr(GError) error = nullptr;
	g_app_info_launch(application, open_with_data->g_file_list, nullptr, &error);

	if (error)
		{
		log_printf("Error launching app.: %s\n", error->message);
		}

	open_with_data_free(open_with_data);
}

static void layout_menu_open_with_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd;
	GtkWidget *widget;
	OpenWithData *open_with_data;

	if (layout_selection_list(lw))
		{
		if (g_getenv("SNAP"))
			{
			/** FIXME The app chooser cannot be used within a sandbox as it does
			 * not see system .desktop files. The correct way is to use XDG DESKTOP PORTAL,
			 * but this method is far simpler.
			 * The video player .desktop is used, rather than creating a new one with
			 * the same function.
			 * The .desktop calls xdg-open which works as required when called externally.
			 */
			file_util_start_editor_from_filelist("org.geeqie.video-player.desktop", layout_selection_list(lw), nullptr, nullptr);
			}
		else
			{
			open_with_data = g_new(OpenWithData, 1);

			fd = static_cast<FileData *>(g_list_first(layout_selection_list(lw))->data);

			open_with_data->g_file_list = g_list_append(nullptr, g_file_new_for_path(fd->path));

			open_with_data->app_chooser_dialog = gtk_app_chooser_dialog_new(nullptr, GTK_DIALOG_MODAL, G_FILE(g_list_first(open_with_data->g_file_list)->data));

			widget = gtk_app_chooser_dialog_get_widget(GTK_APP_CHOOSER_DIALOG(open_with_data->app_chooser_dialog));

			open_with_data->application = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(open_with_data->app_chooser_dialog));

			g_signal_connect(G_OBJECT(widget), "application-selected", G_CALLBACK(open_with_application_selected_cb), open_with_data);
			g_signal_connect(G_OBJECT(widget), "application-activated", G_CALLBACK(open_with_application_activated_cb), open_with_data);
			g_signal_connect(G_OBJECT(open_with_data->app_chooser_dialog), "response", G_CALLBACK(open_with_response_cb), open_with_data);
			g_signal_connect(G_OBJECT(open_with_data->app_chooser_dialog), "close", G_CALLBACK(open_with_close), open_with_data);

			gtk_widget_show(open_with_data->app_chooser_dialog);
			}
		}
}

static void layout_menu_open_archive_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd;

	layout_exit_fullscreen(lw);
	fd = layout_image_get_fd(lw);

	if (fd->format_class != FORMAT_CLASS_ARCHIVE) return;

	g_autofree gchar *dest_dir = open_archive(layout_image_get_fd(lw));
	if (!dest_dir)
		{
		warning_dialog(_("Cannot open archive file"), _("See the Log Window"), GQ_ICON_DIALOG_WARNING, nullptr);
		return;
		}

	LayoutWindow *lw_new = layout_new_from_default();
	layout_set_path(lw_new, dest_dir);
}

static void open_file_cb(GtkFileChooser *chooser, gint response_id, gpointer)
{
	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
		g_autoptr(GFile) parent = g_file_get_parent(file);
		g_autofree gchar *dirname = g_file_get_path(parent);
		g_autofree gchar *filename = g_file_get_path(file);

		history_list_add_to_key("open_file", dirname, 0);

		if (g_str_has_suffix(filename, GQ_COLLECTION_EXT))
			{
			collection_window_new(filename);
			}
		else
			{
			layout_set_path(get_current_layout(), filename);
			}
		}

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void open_recent_file_cb(GtkFileChooser *chooser, gint response_id, gpointer)
{
	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		g_autofree gchar *uri_name = gtk_recent_chooser_get_current_uri(GTK_RECENT_CHOOSER(chooser));
		g_autofree gchar *file_name = g_filename_from_uri(uri_name, nullptr, nullptr);

		if (g_str_has_suffix(file_name, GQ_COLLECTION_EXT))
			{
			collection_window_new(file_name);
			}
		else
			{
			layout_set_path(get_current_layout(), file_name);
			}
		}

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void layout_menu_open_file_cb(GtkAction *, gpointer)
{
	GList *work = filter_get_list();
	g_autoptr(GString) extlist = g_string_new(nullptr);

	while (work)
		{
		auto fe = static_cast<FilterEntry *>(work->data);

		if (extlist->len > 0)
			{
			extlist = g_string_append(extlist, ";");
			}

		extlist = g_string_append(extlist, fe->extensions);

		work = work->next;
		}

	FileChooserDialogData fcdd{};

	fcdd.action = GTK_FILE_CHOOSER_ACTION_OPEN;
	fcdd.accept_text = _("Open");
	fcdd.filter = extlist->str;
	fcdd.filter_description = _("Image files");
	fcdd.history_key = "open_file";
	fcdd.response_callback = G_CALLBACK(open_file_cb);
	fcdd.suggested_name = _("Untitled.gqv");
	fcdd.title = _("Geeqie - Open File");

	GtkFileChooserDialog *dialog = file_chooser_dialog_new(fcdd);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

static void layout_menu_open_recent_file_cb(GtkAction *, gpointer)
{
	GtkWidget *dialog;

	dialog = gtk_recent_chooser_dialog_new(_("Open Recent File - Geeqie"), nullptr, _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Open"), GTK_RESPONSE_ACCEPT, nullptr);

	gtk_recent_chooser_set_show_tips(GTK_RECENT_CHOOSER(dialog), TRUE);
	gtk_recent_chooser_set_show_icons(GTK_RECENT_CHOOSER(dialog), TRUE);

	GtkRecentFilter *recent_filter = gtk_recent_filter_new();
	gtk_recent_filter_set_name(recent_filter, _("Geeqie image files"));

	GList *work = filter_get_list();

	while (work)
		{
		FilterEntry *fe;

		fe = static_cast<FilterEntry *>(work->data);

		g_auto(GStrv) extension_list = g_strsplit(fe->extensions, ";", -1);

		for (gint i = 0; extension_list[i] != nullptr; i++)
			{
			gchar ext[64];
			g_snprintf(ext, sizeof(ext), "*%s", extension_list[i]);
			gtk_recent_filter_add_pattern(recent_filter, ext);
			}

		work = work->next;
		}

	gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(dialog), recent_filter);

	GtkRecentFilter *all_filter = gtk_recent_filter_new();
	gtk_recent_filter_set_name(all_filter, _("All files"));
	gtk_recent_filter_add_pattern(all_filter, "*");
	gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(dialog), all_filter);

	gtk_recent_chooser_set_filter(GTK_RECENT_CHOOSER(dialog), recent_filter);

	g_signal_connect(dialog, "response", G_CALLBACK(open_recent_file_cb), dialog);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

static void open_collection_cb(GtkFileChooser *chooser, gint response_id, gpointer)
{
	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);

		if (file != nullptr)
			{
			g_autoptr(GFile) parent = g_file_get_parent(file);

			if (parent != nullptr)
				{
				g_autofree gchar *dirname = g_file_get_path(parent);
				history_list_add_to_key("open_collection", dirname, -1);
				}

			g_autofree gchar *filename = g_file_get_path(file);

			if (file_extension_match(filename, GQ_COLLECTION_EXT))
				{
				collection_window_new(filename);
				}
			}
		}
	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void layout_menu_open_collection_cb(GtkWidget *, gpointer)
{
	FileChooserDialogData fcdd{};

	fcdd.action = GTK_FILE_CHOOSER_ACTION_OPEN;
	fcdd.accept_text = _("Open");
	fcdd.filter = GQ_COLLECTION_EXT;
	fcdd.filter_description = _("Collection files");
	fcdd.history_key = "open_collection";
	fcdd.response_callback = G_CALLBACK(open_collection_cb);
	fcdd.shortcuts = get_collections_dir();
	fcdd.title = _("Geeqie - Open Collection");

	GtkFileChooserDialog *dialog = file_chooser_dialog_new(fcdd);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
}

static void layout_menu_fullscreen_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_full_screen_toggle(lw);
}

static void layout_menu_escape_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
}

static void layout_menu_overlay_toggle_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	image_osd_toggle(lw->image);
	layout_util_sync_views(lw);
}


static void layout_menu_overlay_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (gq_gtk_toggle_action_get_active(action))
		{
		OsdShowFlags flags = image_osd_get(lw->image);

		if ((flags | OSD_SHOW_INFO | OSD_SHOW_STATUS) != flags)
			image_osd_set(lw->image, static_cast<OsdShowFlags>(flags | OSD_SHOW_INFO | OSD_SHOW_STATUS));
		}
	else
		{
		GtkToggleAction *histogram_action = GQ_GTK_TOGGLE_ACTION(gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram"));

		image_osd_set(lw->image, OSD_SHOW_NOTHING);
		gq_gtk_toggle_action_set_active(histogram_action, FALSE); /* this calls layout_menu_histogram_cb */
		}
}

static void layout_menu_histogram_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (gq_gtk_toggle_action_get_active(action))
		{
		image_osd_set(lw->image, static_cast<OsdShowFlags>(OSD_SHOW_INFO | OSD_SHOW_STATUS | OSD_SHOW_HISTOGRAM));
		layout_util_sync_views(lw); /* show the overlay state, default channel and mode in the menu */
		}
	else
		{
		OsdShowFlags flags = image_osd_get(lw->image);
		if (flags & OSD_SHOW_HISTOGRAM)
			image_osd_set(lw->image, static_cast<OsdShowFlags>(flags & ~OSD_SHOW_HISTOGRAM));
		}
}

static void layout_menu_animate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.animate == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_animate_toggle(lw);
}

static void layout_menu_rectangular_selection_cb(GtkToggleAction *action, gpointer)
{
	options->collections.rectangular_selection = gq_gtk_toggle_action_get_active(action);
}

static void layout_menu_histogram_toggle_channel_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	image_osd_histogram_toggle_channel(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_histogram_toggle_mode_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	image_osd_histogram_toggle_mode(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_histogram_channel_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	gint channel = gq_gtk_radio_action_get_current_value(action);
	if (channel < 0 || channel >= HCHAN_COUNT) return;

	auto *lw = static_cast<LayoutWindow *>(data);
	GtkToggleAction *histogram_action = GQ_GTK_TOGGLE_ACTION(gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram"));
	gq_gtk_toggle_action_set_active(histogram_action, TRUE); /* this calls layout_menu_histogram_cb */

	image_osd_histogram_set_channel(lw->image, channel);
}

static void layout_menu_histogram_mode_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	gint mode = gq_gtk_radio_action_get_current_value(action);
	if (mode < 0 || mode >= HMODE_COUNT) return;

	auto *lw = static_cast<LayoutWindow *>(data);
	GtkToggleAction *histogram_action = GQ_GTK_TOGGLE_ACTION(gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram"));
	gq_gtk_toggle_action_set_active(histogram_action, TRUE); /* this calls layout_menu_histogram_cb */

	image_osd_histogram_set_mode(lw->image, mode);
}

static void layout_menu_refresh_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_refresh(lw);
}

static void layout_menu_bar_exif_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_exif_window_new(lw);
}

static void layout_menu_search_and_run_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_search_and_run_window_new(lw);
}


static void layout_menu_float_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.tools_float == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_tools_float_toggle(lw);
}

static void layout_menu_hide_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_tools_hide_toggle(lw);
}

static void layout_menu_selectable_toolbars_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.selectable_toolbars_hidden == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	current_layout_selectable_toolbars_toggle();
}

static void layout_menu_info_pixel_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.show_info_pixel == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_info_pixel_set(lw, !lw->options.show_info_pixel);
}

/* NOTE: these callbacks are called also from layout_util_sync_views */
static void layout_menu_bar_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_bar_enabled(lw) == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_toggle(lw);
}

static void layout_menu_bar_sort_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_bar_sort_enabled(lw) == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_sort_toggle(lw);
}

static void layout_menu_hide_bars_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.bars_state.hidden == gq_gtk_toggle_action_get_active(action))
		{
		return;
		}
	layout_bars_hide_toggle(lw);
}

static void layout_menu_slideshow_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_image_slideshow_active(lw) == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_slideshow_toggle(lw);
}

static void layout_menu_slideshow_pause_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_slideshow_pause_toggle(lw);
}

static void layout_menu_slideshow_slower_cb(GtkAction *, gpointer)
{
	options->slideshow.delay = std::min<gdouble>(options->slideshow.delay + 5, SLIDESHOW_MAX_SECONDS);
}

static void layout_menu_slideshow_faster_cb(GtkAction *, gpointer)
{
	options->slideshow.delay = std::max<gdouble>(options->slideshow.delay - 5, SLIDESHOW_MIN_SECONDS * 10);
}


static void layout_menu_stereo_mode_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* 0->1, 1->2, 2->3, 3->1 - disable auto, then cycle */
	const gint mode = (layout_image_stereo_pixbuf_get(lw) % 3) + 1;

	GtkAction *radio = gq_gtk_action_group_get_action(lw->action_group, "StereoAuto");
	gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(radio), mode);

	/*
	this is called via fallback in layout_menu_stereo_mode_cb
	layout_image_stereo_pixbuf_set(lw, mode);
	*/
}

static void layout_menu_stereo_mode_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_stereo_pixbuf_set(lw, static_cast<StereoPixbufData>(gq_gtk_radio_action_get_current_value(action)));
}

static void layout_menu_draw_rectangle_aspect_ratio_cb(GtkRadioAction *action, GtkRadioAction *, gpointer)
{
	options->rectangle_draw_aspect_ratio = static_cast<RectangleDrawAspectRatio>(gq_gtk_radio_action_get_current_value(action));
}

static void overlay_screen_display_profile_set(gint i)
{
	g_free(options->image_overlay.template_string);
	options->image_overlay.template_string = g_strdup(options->image_overlay_n.template_string[i]);
	options->image_overlay.x = options->image_overlay_n.x[i];
	options->image_overlay.y = options->image_overlay_n.y[i];
	options->image_overlay.text_red = options->image_overlay_n.text_red[i];
	options->image_overlay.text_green = options->image_overlay_n.text_green[i];
	options->image_overlay.text_blue = options->image_overlay_n.text_blue[i];
	options->image_overlay.background_red = options->image_overlay_n.background_red[i];
	options->image_overlay.background_green = options->image_overlay_n.background_green[i];
	options->image_overlay.background_blue = options->image_overlay_n.background_blue[i];
	g_free(options->image_overlay.font);
	options->image_overlay.font = g_strdup(options->image_overlay_n.font[i]);
}

static void layout_menu_osd_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->overlay_screen_display_selected_profile = static_cast<OverlayScreenDisplaySelectedTab>(gq_gtk_radio_action_get_current_value(action));
	overlay_screen_display_profile_set(options->overlay_screen_display_selected_profile);

	layout_image_refresh(lw);
}

static void layout_menu_help_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("index.html");
}

static void layout_menu_help_search_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_search_window_show();
}

static void layout_menu_help_pdf_cb(GtkAction *, gpointer)
{
	help_pdf();
}

static void layout_menu_help_keys_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("GuideReferenceKeyboardShortcuts.html");
}

static void layout_menu_notes_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("release_notes");
}

static void layout_menu_changelog_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("changelog");
}

static constexpr struct
{
	const gchar *menu_name;
	const gchar *key_name;
} keyboard_map_hardcoded[] = {
	{"Scroll","Left"},
	{"FastScroll", "&lt;Shift&gt;Left"},
	{"Left Border", "&lt;Primary&gt;Left"},
	{"Left Border", "&lt;Primary&gt;&lt;Shift&gt;Left"},
	{"Scroll", "Right"},
	{"FastScroll", "&lt;Shift&gt;Right"},
	{"Right Border", "&lt;Primary&gt;Right"},
	{"Right Border", "&lt;Primary&gt;&lt;Shift&gt;Right"},
	{"Scroll", "Up"},
	{"FastScroll", "&lt;Shift&gt;Up"},
	{"Upper Border", "&lt;Primary&gt;Up"},
	{"Upper Border", "&lt;Primary&gt;&lt;Shift&gt;Up"},
	{"Scroll", "Down"},
	{"FastScroll", "&lt;Shift&gt;Down"},
	{"Lower Border", "&lt;Primary&gt;Down"},
	{"Lower Border", "&lt;Primary&gt;&lt;Shift&gt;Down"},
	{"Next/Drag", "M1"},
	{"FastDrag", "&lt;Shift&gt;M1"},
	{"DnD Start", "M2"},
	{"Menu", "M3"},
	{"PrevImage", "MW4"},
	{"NextImage", "MW5"},
	{"ScrollUp", "&lt;Shift&gt;MW4"},
	{"ScrollDown", "&lt;Shift&gt;MW5"},
	{"ZoomIn", "&lt;Primary&gt;MW4"},
	{"ZoomOut", "&lt;Primary&gt;MW5"},
};

static void layout_menu_foreach_func(
					gpointer data,
					const gchar *accel_path,
					guint accel_key,
					GdkModifierType accel_mods,
					gboolean)
{
	gchar *key_name;
	gchar *menu_name;
	auto array = static_cast<GPtrArray *>(data);

	g_autofree gchar *path = g_strescape(accel_path, nullptr);
	g_autofree gchar *name = gtk_accelerator_name(accel_key, accel_mods);

	menu_name = g_strdup(strrchr(path, '/') + 1);

	if (strrchr(name, '>'))
		{
		g_auto(GStrv) subset_lt_arr = g_strsplit_set(name, "<", 4);
		g_autofree gchar *subset_lt = g_strjoinv("&lt;", subset_lt_arr);
		g_auto(GStrv) subset_gt_arr = g_strsplit_set(subset_lt, ">", 4);

		key_name = g_strjoinv("&gt;", subset_gt_arr);
		}
	else
		key_name = g_steal_pointer(&name);

	g_ptr_array_add(array, menu_name);
	g_ptr_array_add(array, key_name);
}

static gchar *convert_template_line(const gchar *template_line, const GPtrArray *keyboard_map_array)
{
	if (!g_strrstr(template_line, ">key:"))
		{
		return g_strdup_printf("%s\n", template_line);
		}

	g_auto(GStrv) pre_key = g_strsplit(template_line, ">key:", 2);
	g_auto(GStrv) post_key = g_strsplit(pre_key[1], "<", 2);

	const gchar *key_name = post_key[0];
	const gchar *menu_name = " ";
	for (guint index = 0; index < keyboard_map_array->len-1; index += 2)
		{
		if (!g_ascii_strcasecmp(static_cast<const gchar *>(g_ptr_array_index(keyboard_map_array, index+1)), key_name))
			{
			menu_name = static_cast<const gchar *>(g_ptr_array_index(keyboard_map_array, index+0));
			break;
			}
		}

	for (const auto &m : keyboard_map_hardcoded)
		{
		if (!g_strcmp0(m.key_name, key_name))
			{
			menu_name = m.menu_name;
			break;
			}
		}

	return g_strconcat(pre_key[0], ">", menu_name, "<", post_key[1], "\n", NULL);
}

static void convert_keymap_template_to_file(const gint fd, const GPtrArray *keyboard_map_array)
{
	g_autoptr(GIOChannel) channel = g_io_channel_unix_new(fd);

	g_autoptr(GInputStream) in_stream = g_resources_open_stream(GQ_RESOURCE_PATH_IMAGES "/keymap-template.svg", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
	g_autoptr(GDataInputStream) data_stream = g_data_input_stream_new(in_stream);

	gchar *template_line;
	while ((template_line = g_data_input_stream_read_line(G_DATA_INPUT_STREAM(data_stream), nullptr, nullptr, nullptr)))
		{
		g_autofree gchar *converted_line = convert_template_line(template_line, keyboard_map_array);

		g_autoptr(GError) error = nullptr;
		g_io_channel_write_chars(channel, converted_line, -1, nullptr, &error);
		if (error) log_printf("Warning: Keyboard Map:%s\n", error->message);
		}

	g_autoptr(GError) error = nullptr;
	g_io_channel_flush(channel, &error);
	if (error) log_printf("Warning: Keyboard Map:%s\n", error->message);
}

static void layout_menu_kbd_map_cb(GtkAction *, gpointer)
{
	g_autofree gchar *tmp_file = nullptr;
	g_autoptr(GError) error = nullptr;

	const gint fd = g_file_open_tmp("geeqie_keymap_XXXXXX.svg", &tmp_file, &error);
	if (error)
		{
		log_printf("Error: Keyboard Map - cannot create file:%s\n", error->message);
		return;
		}

	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	gtk_accel_map_foreach(array, layout_menu_foreach_func);

	convert_keymap_template_to_file(fd, array);

	view_window_new(file_data_new_simple(tmp_file));
}

static void layout_menu_about_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_about_window(lw);
}

static void layout_menu_crop_selection_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	start_editor_from_file("org.geeqie.image-crop.desktop", lw->image->image_fd);
}

static void layout_menu_log_window_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	log_window_new(lw);
}


/*
 *-----------------------------------------------------------------------------
 * select menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_select_all_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_all(lw);
}

static void layout_menu_unselect_all_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_none(lw);
}

static void layout_menu_invert_selection_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_invert(lw);
}

static void layout_menu_file_filter_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_file_filter_set(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_marks_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_marks_set(lw, gq_gtk_toggle_action_get_active(action));
}

template<SelectionToMarkMode mode>
static void layout_menu_selection_to_mark_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, mode);
}

template<MarkToSelectionMode mode>
static void layout_menu_mark_to_selection_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, mode);
}

static void layout_menu_mark_filter_toggle_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_marks_set(lw, TRUE);
	layout_mark_filter_toggle(lw, mark);
}


/*
 *-----------------------------------------------------------------------------
 * go menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_image_first_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_first(lw);
}

static void layout_menu_image_prev_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.split_pane_sync)
		{
		for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
			{
			if (lw->split_images[i])
				{
				DEBUG_1("image activate scroll %d", i);
				layout_image_activate(lw, i, FALSE);
				layout_image_prev(lw);
				}
			}
		}
	else
		{
		layout_image_prev(lw);
		}
}

static void layout_menu_image_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.split_pane_sync)
		{
		for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
			{
			if (lw->split_images[i])
				{
				DEBUG_1("image activate scroll %d", i);
				layout_image_activate(lw, i, FALSE);
				layout_image_next(lw);
				}
			}
		}
	else
		{
		layout_image_next(lw);
		}
}

static void layout_menu_page_first_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_set_page_num(fd, 1);
		}
}

static void layout_menu_page_last_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_set_page_num(fd, -1);
		}
}

static void layout_menu_page_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_inc_page_num(fd);
		}
}

static void layout_menu_page_previous_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_dec_page_num(fd);
		}
}

static void layout_menu_image_forward_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* Obtain next image */
	layout_set_path(lw, image_chain_forward());
}

static void layout_menu_image_back_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* Obtain previous image */
	layout_set_path(lw, image_chain_back());
}

static void layout_menu_split_pane_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint active_frame;

	active_frame = lw->active_split_image;

	if (active_frame < MAX_SPLIT_IMAGES-1 && lw->split_images[active_frame+1] )
		{
		active_frame++;
		}
	else
		{
		active_frame = 0;
		}
	layout_image_activate(lw, active_frame, FALSE);
}

static void layout_menu_split_pane_prev_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint active_frame;

	active_frame = lw->active_split_image;

	if (active_frame >=1 && lw->split_images[active_frame-1] )
		{
		active_frame--;
		}
	else
		{
		active_frame = MAX_SPLIT_IMAGES-1;
		while (!lw->split_images[active_frame])
			{
			active_frame--;
			}
		}
	layout_image_activate(lw, active_frame, FALSE);
}

static void layout_menu_split_pane_updown_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint active_frame;

	active_frame = lw->active_split_image;

	if (lw->split_images[MAX_SPLIT_IMAGES-1] )
		{
		active_frame = active_frame ^ 2;
		}
	else
		{
		active_frame = active_frame ^ 1;
		}
	layout_image_activate(lw, active_frame, FALSE);
}

static void layout_menu_image_last_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_last(lw);
}

static void layout_menu_back_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *dir_fd;

	/* Obtain previous path */
	dir_fd = file_data_new_dir(history_chain_back());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_forward_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *dir_fd;

	/* Obtain next path */
	dir_fd = file_data_new_dir(history_chain_forward());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_home_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const gchar *path;

	if (lw->options.home_path && *lw->options.home_path)
		path = lw->options.home_path;
	else
		path = homedir();

	if (path)
		{
		FileData *dir_fd = file_data_new_dir(path);
		layout_set_fd(lw, dir_fd);
		file_data_unref(dir_fd);
		}
}

static void layout_menu_up_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ViewDir *vd = lw->vd;

	if (!vd->dir_fd || strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) == 0) return;

	if (!vd->select_func) return;

	g_autofree gchar *path = remove_level_from_path(vd->dir_fd->path);
	FileData *fd = file_data_new_dir(path);
	vd->select_func(vd, fd, vd->select_data);
	file_data_unref(fd);
}


/*
 *-----------------------------------------------------------------------------
 * edit menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_edit_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const gchar *key = gq_gtk_action_get_name(action);

	if (!editor_window_flag_set(key))
		layout_exit_fullscreen(lw);

	file_util_start_editor_from_filelist(key, layout_selection_list(lw), layout_get_path(lw), lw->window);
}


static void layout_menu_metadata_write_cb(GtkAction *, gpointer)
{
	metadata_write_queue_confirm(TRUE, nullptr);
}

static GtkWidget *last_focussed = nullptr;
static void layout_menu_keyword_autocomplete_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *tmp;
	gboolean auto_has_focus;

	tmp = gtk_window_get_focus(GTK_WINDOW(lw->window));
	auto_has_focus = bar_keywords_autocomplete_focus(lw);

	if (auto_has_focus)
		{
		gtk_widget_grab_focus(last_focussed);
		}
	else
		{
		last_focussed = tmp;
		}
}

/*
 *-----------------------------------------------------------------------------
 * color profile button (and menu)
 *-----------------------------------------------------------------------------
 */
#if HAVE_LCMS
static void layout_color_menu_enable_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_image_color_profile_get_use(lw) == gq_gtk_toggle_action_get_active(action)) return;

	layout_image_color_profile_set_use(lw, gq_gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}

static void layout_color_menu_use_image_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint input;
	gboolean use_image;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;
	if (use_image == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_color_profile_set(lw, input, gq_gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}

static void layout_color_menu_input_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint type;
	gint input;
	gboolean use_image;

	type = gq_gtk_radio_action_get_current_value(action);
	if (type < 0 || type >= COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS) return;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;
	if (type == input) return;

	layout_image_color_profile_set(lw, type, use_image);
	layout_image_refresh(lw);
}
#else
static void layout_color_menu_enable_cb()
{
}

static void layout_color_menu_use_image_cb()
{
}

static void layout_color_menu_input_cb()
{
}
#endif

void layout_recent_add_path(const gchar *path)
{
	if (!path) return;

	history_list_add_to_key("recent", path, options->open_recent_list_maxsize);
}

/*
 *-----------------------------------------------------------------------------
 * window layout menu
 *-----------------------------------------------------------------------------
 */
struct RenameWindow
{
	GenericDialog *gd;
	LayoutWindow *lw;

	GtkWidget *window_name_entry;
};

struct DeleteWindow
{
	GenericDialog *gd;
	LayoutWindow *lw;
};

static gint layout_window_menu_list_sort_cb(gconstpointer a, gconstpointer b)
{
	auto wna = static_cast<const WindowNames *>(a);
	auto wnb = static_cast<const WindowNames *>(b);

	return g_strcmp0(wna->name, wnb->name);
}

static GList *layout_window_menu_list()
{
	DIR *dp;
	struct dirent *dir;

	g_autofree gchar *pathl = path_from_utf8(get_window_layouts_dir());
	dp = opendir(pathl);
	if (!dp)
		{
		/* dir not found */
		return nullptr;
		}

	GList *list = nullptr;

	while ((dir = readdir(dp)) != nullptr)
		{
		gchar *name_file = dir->d_name;

		if (g_str_has_suffix(name_file, ".xml"))
			{
			g_autofree gchar *name_utf8 = path_to_utf8(name_file);

			auto *wn  = g_new0(WindowNames, 1);
			wn->name = g_strndup(name_utf8, strlen(name_utf8) - 4);
			wn->path = g_build_filename(pathl, name_utf8, NULL);

			list = g_list_prepend(list, wn);
			}
		}
	closedir(dp);

	return g_list_sort(list, layout_window_menu_list_sort_cb);
}

static void layout_menu_new_window_cb(GtkWidget *, gpointer data)
{
	gint n;

	n = GPOINTER_TO_INT(data);

	g_autolist(WindowNames) menulist = layout_window_menu_list();
	auto wn = static_cast<WindowNames *>(g_list_nth(menulist, n )->data);

	if (wn->path) // @FIXME path can not be nullptr. Check for path existence?
		{
		load_config_from_file(wn->path, FALSE);
		}
	else
		{
		log_printf(_("Error: window layout name: %s does not exist\n"), wn->path);
		}
}

static void layout_menu_new_window_update(LayoutWindow *lw)
{
	if (!lw->ui_manager) return;

	g_autolist(WindowNames) list = layout_window_menu_list();

	GtkWidget *menu = gq_gtk_ui_manager_get_widget(lw->ui_manager,
	                                               options->hamburger_menu ? "/MainMenu/OpenMenu/WindowsMenu/NewWindow" : "/MainMenu/WindowsMenu/NewWindow");
	GtkWidget *sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	g_autoptr(GList) children = gtk_container_get_children(GTK_CONTAINER(sub_menu));
	for (GList *iter = g_list_nth(children, 4); // separator, default, from current, separator
	     iter != nullptr; iter = g_list_next(iter))
		{
		gq_gtk_widget_destroy(static_cast<GtkWidget *>(iter->data));
		}

	menu_item_add_divider(sub_menu);

	gint n = 0;
	for (GList *work = list; work; work = work->next, n++)
		{
		auto *wn = static_cast<WindowNames *>(work->data);
		GtkWidget *item = menu_item_add_simple(sub_menu, wn->name,
		                                       G_CALLBACK(layout_menu_new_window_cb), GINT_TO_POINTER(n));
		gtk_widget_set_sensitive(item, !layout_window_is_displayed(wn->name));
		}
}

static void window_rename_cancel_cb(GenericDialog *, gpointer data)
{
	auto rw = static_cast<RenameWindow *>(data);

	generic_dialog_close(rw->gd);
	g_free(rw);
}

static void window_rename_ok(RenameWindow *rw)
{
	g_autolist(WindowNames) list = layout_window_menu_list();
	const gchar *new_id = gq_gtk_entry_get_text(GTK_ENTRY(rw->window_name_entry));

	const auto window_names_compare_name = [](gconstpointer data, gconstpointer user_data)
	{
		return g_strcmp0(static_cast<const WindowNames *>(data)->name, static_cast<const gchar *>(user_data));
	};

	if (g_list_find_custom(list, new_id, window_names_compare_name))
		{
		g_autofree gchar *buf = g_strdup_printf(_("Window layout name \"%s\" already exists."), new_id);
		warning_dialog(_("Rename window"), buf, GQ_ICON_DIALOG_WARNING, rw->gd->dialog);
		}
	else
		{
		g_autofree gchar *xml_name = g_strdup_printf("%s.xml", rw->lw->options.id);
		g_autofree gchar *path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);

		if (isfile(path))
			{
			unlink_file(path);
			}

		g_free(rw->lw->options.id);
		rw->lw->options.id = g_strdup(new_id);
		layout_menu_new_window_update(rw->lw);
		layout_refresh(rw->lw);
		image_update_title(rw->lw->image);
		}

	save_layout(rw->lw);

	generic_dialog_close(rw->gd);
	g_free(rw);
}

static void window_rename_ok_cb(GenericDialog *, gpointer data)
{
	auto rw = static_cast<RenameWindow *>(data);

	window_rename_ok(rw);
}

static void window_delete_cancel_cb(GenericDialog *, gpointer data)
{
	auto dw = static_cast<DeleteWindow *>(data);

	g_free(dw);
}

static void window_delete_ok_cb(GenericDialog *, gpointer data)
{
	auto dw = static_cast<DeleteWindow *>(data);

	g_autofree gchar *xml_name = g_strdup_printf("%s.xml", dw->lw->options.id);
	g_autofree gchar *path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);

	layout_close(dw->lw);
	g_free(dw);

	if (isfile(path))
		{
		unlink_file(path);
		}
}

static void layout_menu_window_default_cb(GtkWidget *, gpointer)
{
	layout_new_from_default();
}

static void layout_menu_windows_menu_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;
	GtkWidget *sub_menu;

	menu = gq_gtk_ui_manager_get_widget(lw->ui_manager, options->hamburger_menu ? "/MainMenu/OpenMenu/WindowsMenu/" : "/MainMenu/WindowsMenu/");

	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	/* disable Delete for temporary windows */
	if (!g_str_has_prefix(lw->options.id, "lw")) return;

	static const auto set_sensitive = [](GtkWidget *widget, gpointer)
	{
		const gchar *menu_label = gtk_menu_item_get_label(GTK_MENU_ITEM(widget));
		if (g_strcmp0(menu_label, _("Delete window")) == 0)
			{
			gtk_widget_set_sensitive(widget, FALSE);
			}
	};
	gtk_container_foreach(GTK_CONTAINER(sub_menu), set_sensitive, nullptr);
}

static void layout_menu_view_menu_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;
	GtkWidget *sub_menu;
	FileData *fd;

	menu = gq_gtk_ui_manager_get_widget(lw->ui_manager, options->hamburger_menu ? "/MainMenu/OpenMenu/ViewMenu/" : "/MainMenu/ViewMenu/");
	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	fd = layout_image_get_fd(lw);
	const gboolean sensitive = (fd && fd->format_class == FORMAT_CLASS_ARCHIVE);

	static const auto set_sensitive = [](GtkWidget *widget, gpointer data)
	{
		const gchar *menu_label = gtk_menu_item_get_label(GTK_MENU_ITEM(widget));
		if (g_strcmp0(menu_label, _("Open archive")) == 0)
			{
			gtk_widget_set_sensitive(widget, GPOINTER_TO_INT(data));
			}
	};
	gtk_container_foreach(GTK_CONTAINER(sub_menu), set_sensitive, GINT_TO_POINTER(sensitive));
}

static gchar *create_tmp_config_file()
{
	gchar *tmp_file;
	g_autoptr(GError) error = nullptr;

	gint fd = g_file_open_tmp("geeqie_layout_name_XXXXXX.xml", &tmp_file, &error);
	if (error)
		{
		log_printf("Error: Window layout - cannot create file: %s\n", error->message);
		return nullptr;
		}

	close(fd);

	return tmp_file;
}

static void change_window_id(const gchar *infile, const gchar *outfile)
{
	g_autoptr(GFile) in_file = g_file_new_for_path(infile);
	g_autoptr(GFileInputStream) in_file_stream = g_file_read(in_file, nullptr, nullptr);
	g_autoptr(GDataInputStream) in_data_stream = g_data_input_stream_new(G_INPUT_STREAM(in_file_stream));

	g_autoptr(GFile) out_file = g_file_new_for_path(outfile);
	g_autoptr(GFileOutputStream) out_file_stream = g_file_append_to(out_file, G_FILE_CREATE_PRIVATE, nullptr, nullptr);
	g_autoptr(GDataOutputStream) out_data_stream = g_data_output_stream_new(G_OUTPUT_STREAM(out_file_stream));

	g_autofree gchar *id_name = layout_get_unique_id();

	gchar *line;
	while ((line = g_data_input_stream_read_line(in_data_stream, nullptr, nullptr, nullptr)))
		{
		g_data_output_stream_put_string(out_data_stream, line, nullptr, nullptr);
		g_data_output_stream_put_string(out_data_stream, "\n", nullptr, nullptr);

		if (g_str_has_suffix(line, "<layout"))
			{
			g_free(line);
			line = g_data_input_stream_read_line(in_data_stream, nullptr, nullptr, nullptr);

			g_data_output_stream_put_string(out_data_stream, "id = \"", nullptr, nullptr);
			g_data_output_stream_put_string(out_data_stream, id_name, nullptr, nullptr);
			g_data_output_stream_put_string(out_data_stream, "\"\n", nullptr, nullptr);
			}

		g_free(line);
		}
}

static void layout_menu_window_from_current_cb(GtkWidget *, gpointer data)
{
	g_autofree gchar *tmp_file_in = create_tmp_config_file();
	if (!tmp_file_in)
		{
		return;
		}

	g_autofree gchar *tmp_file_out = create_tmp_config_file();
	if (!tmp_file_out)
		{
		unlink_file(tmp_file_in);
		return;
		}

	auto *lw = static_cast<LayoutWindow *>(data);
	save_config_to_file(tmp_file_in, options, lw);
	change_window_id(tmp_file_in, tmp_file_out);
	load_config_from_file(tmp_file_out, FALSE);

	unlink_file(tmp_file_in);
	unlink_file(tmp_file_out);
}

static void layout_menu_window_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_menu_new_window_update(lw);
}

static void layout_menu_window_rename_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	RenameWindow *rw;
	GtkWidget *hbox;

	rw = g_new0(RenameWindow, 1);
	rw->lw = lw;

	rw->gd = generic_dialog_new(_("Rename window"), "rename_window", nullptr, FALSE, window_rename_cancel_cb, rw);

	generic_dialog_add_button(rw->gd, GQ_ICON_OK, _("OK"), window_rename_ok_cb, TRUE);
	generic_dialog_add_message(rw->gd, nullptr, _("rename window"), nullptr, FALSE);

	hbox = pref_box_new(rw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);

	hbox = pref_box_new(rw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	rw->window_name_entry = gtk_entry_new();
	gtk_widget_set_can_focus(rw->window_name_entry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(rw->window_name_entry), TRUE);
	gq_gtk_entry_set_text(GTK_ENTRY(rw->window_name_entry), lw->options.id);
	gq_gtk_box_pack_start(GTK_BOX(hbox), rw->window_name_entry, TRUE, TRUE, 0);
	gtk_widget_grab_focus(rw->window_name_entry);
	gtk_widget_show(rw->window_name_entry);
	g_signal_connect_swapped(rw->window_name_entry, "activate", G_CALLBACK(window_rename_ok), rw);

	gtk_widget_show(rw->gd->dialog);
}

static void layout_menu_window_delete_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	DeleteWindow *dw;
	GtkWidget *hbox;

	dw = g_new0(DeleteWindow, 1);
	dw->lw = lw;

	dw->gd = generic_dialog_new(_("Delete window"), "delete_window", nullptr, TRUE, window_delete_cancel_cb, dw);

	generic_dialog_add_button(dw->gd, GQ_ICON_OK, _("OK"), window_delete_ok_cb, TRUE);
	generic_dialog_add_message(dw->gd, nullptr, _("Delete window layout"), nullptr, FALSE);

	hbox = pref_box_new(dw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);

	GtkWidget *group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, lw->options.id);

	gtk_widget_show(dw->gd->dialog);
}

/*
 *-----------------------------------------------------------------------------
 * menu
 *-----------------------------------------------------------------------------
 */

#define CB G_CALLBACK
/**
 * tooltip is used as the description field in the Help manual shortcuts documentation
 *
 * struct GtkActionEntry:
 *  name, stock_id, label, accelerator, tooltip, callback
 */
static GtkActionEntry menu_entries[] = {
  { "About",                 GQ_ICON_ABOUT,                     N_("_About"),                                           nullptr,               N_("About"),                                           CB(layout_menu_about_cb) },
  { "AlterNone",             PIXBUF_INLINE_ICON_ORIGINAL,       N_("_Original state"),                                  "<shift>O",            N_("Image rotate Original state"),                     CB(layout_menu_alter_cb<ALTER_NONE>) },
  { "AspectRatioMenu",       PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Aspect Ratio"),                                     nullptr,               N_("Aspect Ratio"),                                    nullptr },
  { "Back",                  GQ_ICON_GO_PREV,                   N_("_Back"),                                            nullptr,               N_("Back in folder history"),                          CB(layout_menu_back_cb) },
  { "ClearMarks",            PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Clear Marks..."),                                   nullptr,               N_("Clear Marks"),                                     CB(layout_menu_clear_marks_cb) },
  { "CloseWindow",           GQ_ICON_CLOSE,                     N_("C_lose window"),                                    "<control>W",          N_("Close window"),                                    CB(layout_menu_close_cb) },
  { "ColorMenu",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Color Management"),                                nullptr,               nullptr,                                               nullptr },
  { "ConnectZoom100Alt1",    GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "<shift>KP_Divide",    N_("Connected Zoom 1:1"),                              (GCallback)layout_menu_zoom_set_cb<1, TRUE> },
  { "ConnectZoom100",        GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "<shift>Z",            N_("Connected Zoom 1:1"),                              (GCallback)layout_menu_zoom_set_cb<1, TRUE> },
  { "ConnectZoom200",        PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Zoom _2:1"),                                        nullptr,               N_("Connected Zoom 2:1"),                              (GCallback)layout_menu_zoom_set_cb<2, TRUE> },
  { "ConnectZoom25",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Zoom 1:4"),                                         nullptr,               N_("Connected Zoom 1:4"),                              (GCallback)layout_menu_zoom_set_cb<-4, TRUE> },
  { "ConnectZoom300",        PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Zoom _3:1"),                                        nullptr,               N_("Connected Zoom 3:1"),                              (GCallback)layout_menu_zoom_set_cb<3, TRUE> },
  { "ConnectZoom33",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Zoom 1:3"),                                         nullptr,               N_("Connected Zoom 1:3"),                              (GCallback)layout_menu_zoom_set_cb<-3, TRUE> },
  { "ConnectZoom400",        PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Zoom _4:1"),                                        nullptr,               N_("Connected Zoom 4:1"),                              (GCallback)layout_menu_zoom_set_cb<4, TRUE> },
  { "ConnectZoom50",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Zoom 1:2"),                                         nullptr,               N_("Connected Zoom 1:2"),                              (GCallback)layout_menu_zoom_set_cb<-2, TRUE> },
  { "ConnectZoomFillHor",    PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Fit _Horizontally"),                                "<shift>H",            N_("Connected Fit Horizontally"),                      (GCallback)layout_menu_zoom_fit_hv_cb<FALSE, TRUE> },
  { "ConnectZoomFillVert",   PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Fit _Vertically"),                                  "<shift>W",            N_("Connected Fit Vertically"),                        (GCallback)layout_menu_zoom_fit_hv_cb<TRUE, TRUE> },
  { "ConnectZoomFitAlt1",    GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "<shift>KP_Multiply",  N_("Connected Zoom to fit"),                           (GCallback)layout_menu_zoom_set_cb<0, TRUE> },
  { "ConnectZoomFit",        GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "<shift>X",            N_("Connected Zoom to fit"),                           (GCallback)layout_menu_zoom_set_cb<0, TRUE> },
  { "ConnectZoomInAlt1",     GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "<shift>KP_Add",       N_("Connected Zoom in"),                               CB(layout_menu_zoom_in_cb<TRUE>) },
  { "ConnectZoomIn",         GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "plus",                N_("Connected Zoom in"),                               CB(layout_menu_zoom_in_cb<TRUE>) },
  { "ConnectZoomMenu",       PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Connected Zoom"),                                  nullptr,               nullptr,                                               nullptr },
  { "ConnectZoomOutAlt1",    GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "<shift>KP_Subtract",  N_("Connected Zoom out"),                              CB(layout_menu_zoom_out_cb<TRUE>) },
  { "ConnectZoomOut",        GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "underscore",          N_("Connected Zoom out"),                              CB(layout_menu_zoom_out_cb<TRUE>) },
  { "Copy",                  GQ_ICON_COPY,                      N_("_Copy..."),                                         "<control>C",          N_("Copy..."),                                         CB(layout_menu_copy_cb) },
  { "CopyImage",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Copy image to clipboard"),                         nullptr,               N_("Copy image to clipboard"),                         CB(layout_menu_copy_image_cb) },
  { "CopyPath",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Copy to clipboard"),                               nullptr,               N_("Copy to clipboard"),                               CB(layout_menu_copy_path_cb<TRUE>) },
  { "CopyPathUnquoted",      PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Copy to clipboard (unquoted)"),                    nullptr,               N_("Copy to clipboard (unquoted)"),                    CB(layout_menu_copy_path_cb<FALSE>) },
  { "CropRectangle",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Crop Rectangle"),                                   nullptr,               N_("Crop Rectangle"),                                  CB(layout_menu_crop_selection_cb) },
  { "CutPath",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Cut to clipboard"),                                "<control>X",          N_("Cut to clipboard"),                                CB(layout_menu_cut_path_cb) },
  { "DeleteAlt1",            GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "Delete",              N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_key_cb) },
  { "DeleteAlt2",            GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "KP_Delete",           N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_key_cb) },
  { "Delete",                GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "<control>D",          N_("Move selection to Trash..."),                      CB(layout_menu_delete_cb<TRUE>) },
  { "DeleteWindow",          GQ_ICON_DELETE,                    N_("Delete window"),                                    nullptr,               N_("Delete window"),                                   CB(layout_menu_window_delete_cb) },
  { "DisableGrouping",       PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Disable file groupi_ng"),                           nullptr,               N_("Disable file grouping"),                           CB(layout_menu_disable_grouping_cb<TRUE>) },
  { "EditMenu",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Edit"),                                            nullptr,               nullptr,                                               nullptr },
  { "EnableGrouping",        PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Enable file _grouping"),                            nullptr,               N_("Enable file grouping"),                            CB(layout_menu_disable_grouping_cb<FALSE>) },
  { "EscapeAlt1",            GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                               "Q",                   N_("Leave full screen"),                               CB(layout_menu_escape_cb) },
  { "Escape",                GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                              "Escape",               N_("Leave full screen"),                               CB(layout_menu_escape_cb) },
  { "ExifWin",               PIXBUF_INLINE_ICON_EXIF,           N_("_Exif window"),                                     "<control>E",          N_("Exif window"),                                     CB(layout_menu_bar_exif_cb) },
  { "FileDirMenu",           PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Files and Folders"),                               nullptr,               nullptr,                                               nullptr },
  { "FileMenu",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_File"),                                            nullptr,               nullptr,                                               nullptr },
  { "FindDupes",             GQ_ICON_FIND,                      N_("_Find duplicates..."),                              "D",                   N_("Find duplicates..."),                              CB(layout_menu_dupes_cb) },
  { "FirstImage",            GQ_ICON_GO_TOP,                    N_("_First Image"),                                     "Home",                N_("First Image"),                                     CB(layout_menu_image_first_cb) },
  { "FirstPage",             GQ_ICON_PREV_PAGE,                 N_("_First Page"),                                      "<control>Home",       N_( "First Page of multi-page image"),                 CB(layout_menu_page_first_cb) },
  { "Flip",                  GQ_ICON_FLIP_VERTICAL,             N_("_Flip"),                                            "<shift>F",            N_("Image Flip"),                                      CB(layout_menu_alter_cb<ALTER_FLIP>) },
  { "Forward",               GQ_ICON_GO_NEXT,                   N_("_Forward"),                                         nullptr,               N_("Forward in folder history"),                       CB(layout_menu_forward_cb) },
  { "FullScreenAlt1",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "V",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt2",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F11",                 N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreen",            GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "GoMenu",                PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Go"),                                              nullptr,               nullptr,                                               nullptr },
  { "HelpChangeLog",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_ChangeLog"),                                       nullptr,               N_("ChangeLog notes"),                                 CB(layout_menu_changelog_cb) },
  { "HelpContents",          GQ_ICON_HELP,                      N_("_Help manual"),                                     "F1",                  N_("Help manual"),                                     CB(layout_menu_help_cb) },
  { "HelpKbd",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Keyboard map"),                                    nullptr,               N_("Keyboard map"),                                    CB(layout_menu_kbd_map_cb) },
  { "HelpMenu",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Help"),                                            nullptr,               nullptr,                                               nullptr },
  { "HelpNotes",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Readme"),                                          nullptr,               N_("Readme"),                                          CB(layout_menu_notes_cb) },
  { "HelpPdf",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Help in pdf format"),                               nullptr,               N_("Help in pdf format"),                              CB(layout_menu_help_pdf_cb) },
  { "HelpSearch",            PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("On-line help search"),                              nullptr,               N_("On-line help search"),                             CB(layout_menu_help_search_cb) },
  { "HelpShortcuts",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Keyboard shortcuts"),                              nullptr,               N_("Keyboard shortcuts"),                              CB(layout_menu_help_keys_cb) },
  { "HideTools",             PIXBUF_INLINE_ICON_HIDETOOLS,      N_("_Hide file list"),                                  "<control>H",          N_("Hide file list"),                                  CB(layout_menu_hide_cb) },
  { "HistogramChanCycle",    PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Cycle through histogram ch_annels"),                "K",                   N_("Cycle through histogram channels"),                CB(layout_menu_histogram_toggle_channel_cb) },
  { "HistogramModeCycle",    PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Cycle through histogram mo_des"),                   "J",                   N_("Cycle through histogram modes"),                   CB(layout_menu_histogram_toggle_mode_cb) },
  { "Home",                  GQ_ICON_HOME,                      N_("_Home"),                                            nullptr,               N_("Home"),                                            CB(layout_menu_home_cb) },
  { "ImageBack",             GQ_ICON_GO_FIRST,                  N_("Image Back"),                                       nullptr,               N_("Back in image history"),                           CB(layout_menu_image_back_cb) },
  { "ImageForward",          GQ_ICON_GO_LAST,                   N_("Image Forward"),                                    nullptr,               N_("Forward in image history"),                        CB(layout_menu_image_forward_cb) },
  { "ImageOverlayCycle",     PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Cycle through overlay modes"),                     "I",                   N_("Cycle through Overlay modes"),                     CB(layout_menu_overlay_toggle_cb) },
  { "KeywordAutocomplete",   PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Keyword autocomplete"),                             "<alt>K",              N_("Keyword Autocomplete"),                            CB(layout_menu_keyword_autocomplete_cb) },
  { "LastImage",             GQ_ICON_GO_BOTTOM,                 N_("_Last Image"),                                      "End",                 N_("Last Image"),                                      CB(layout_menu_image_last_cb) },
  { "LastPage",              GQ_ICON_NEXT_PAGE,                 N_("_Last Page"),                                       "<control>End",        N_("Last Page of multi-page image"),                   CB(layout_menu_page_last_cb) },
  { "LayoutConfig",          GQ_ICON_PREFERENCES,               N_("_Configure this window..."),                        nullptr,               N_("Configure this window..."),                        CB(layout_menu_layout_config_cb) },
  { "LogWindow",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Log Window"),                                      nullptr,               N_("Log Window"),                                      CB(layout_menu_log_window_cb) },
  { "Maintenance",           PIXBUF_INLINE_ICON_MAINTENANCE,    N_("_Cache maintenance..."),                            nullptr,               N_("Cache maintenance..."),                            CB(layout_menu_remove_thumb_cb) },
  { "Mirror",                GQ_ICON_FLIP_HORIZONTAL,           N_("_Mirror"),                                          "<shift>M",            N_("Image Mirror"),                                    CB(layout_menu_alter_cb<ALTER_MIRROR>) },
  { "Move",                  PIXBUF_INLINE_ICON_MOVE,           N_("_Move..."),                                         "<control>M",          N_("Move..."),                                         CB(layout_menu_move_cb) },
  { "NewCollection",         PIXBUF_INLINE_COLLECTION,          N_("_New collection"),                                  "C",                   N_("New collection"),                                  CB(layout_menu_new_cb) },
  { "NewFolder",             GQ_ICON_DIRECTORY,                 N_("N_ew folder..."),                                   "<control>F",          N_("New folder..."),                                   CB(layout_menu_dir_cb) },
  { "NewWindowDefault",      PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("default"),                                          "<control>N",          N_("New window (default)"),                            CB(layout_menu_window_default_cb)  },
  { "NewWindowFromCurrent",  PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("from current"),                                     nullptr,               N_("from current"),                                    CB(layout_menu_window_from_current_cb)  },
  { "NewWindow",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("New window"),                                       nullptr,               N_("New window"),                                      CB(layout_menu_window_cb) },
  { "NextImageAlt1",         GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "Page_Down",           N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextImageAlt2",         GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "KP_Page_Down",        N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextImage",             GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "space",               N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextPage",              GQ_ICON_FORWARD_PAGE,              N_("_Next Page"),                                       "<control>Page_Down",  N_("Next Page of multi-page image"),                   CB(layout_menu_page_next_cb) },
  { "OpenArchive",           GQ_ICON_OPEN,                      N_("Open archive"),                                     nullptr,               N_("Open archive"),                                    CB(layout_menu_open_archive_cb) },
  { "OpenCollection",        GQ_ICON_OPEN,                      N_("_Open collection..."),                              "O",                   N_("Open collection..."),                              CB(layout_menu_open_collection_cb) },
  { "OpenFile",              GQ_ICON_OPEN,                      N_("Open file..."),                                     nullptr,               N_("Open file..."),                                    CB(layout_menu_open_file_cb) },
  { "OpenMenu",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("☰"),                                                nullptr,               nullptr,                                               nullptr },
  { "OpenRecent",            PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Open recen_t"),                                     nullptr,               N_("Open recent collection"),                          nullptr },
  { "OpenRecentFile",        PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Open recent file..."),                              nullptr,               N_("Open recent file..."),                                CB(layout_menu_open_recent_file_cb) },
  { "OpenWith",              GQ_ICON_OPEN_WITH,                 N_("Open With..."),                                     nullptr,               N_("Open With..."),                                    CB(layout_menu_open_with_cb) },
  { "OrientationMenu",       PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Orientation"),                                     nullptr,               nullptr,                                               nullptr },
  { "OverlayMenu",           PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Image _Overlay"),                                   nullptr,               nullptr,                                               nullptr },
  { "PanView",               PIXBUF_INLINE_ICON_PANORAMA,       N_("Pa_n view"),                                        "<control>J",          N_("Pan view"),                                        CB(layout_menu_pan_cb) },
  { "PermanentDelete",       GQ_ICON_DELETE,                    N_("Delete selection..."),                              "<shift>Delete",       N_("Delete selection..."),                             CB(layout_menu_delete_cb<FALSE>) },
  { "Plugins",               GQ_ICON_PREFERENCES,               N_("Configure _Plugins..."),                            nullptr,               N_("Configure Plugins..."),                            CB(layout_menu_editors_cb) },
  { "PluginsMenu",           PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Plugins"),                                         nullptr,               nullptr,                                               nullptr },
  { "Preferences",           GQ_ICON_PREFERENCES,               N_("P_references..."),                                  "<control>O",          N_("Preferences..."),                                  CB(layout_menu_config_cb) },
  { "PreferencesMenu",       PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("P_references"),                                     nullptr,               nullptr,                                               nullptr },
  { "PrevImageAlt1",         GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "Page_Up",             N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevImageAlt2",         GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "KP_Page_Up",          N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevImage",             GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "BackSpace",           N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevPage",              GQ_ICON_BACK_PAGE,                 N_("_Previous Page"),                                   "<control>Page_Up",    N_("Previous Page of multi-page image"),               CB(layout_menu_page_previous_cb) },
  { "Print",                 GQ_ICON_PRINT,                     N_("_Print..."),                                        "<shift>P",            N_("Print..."),                                        CB(layout_menu_print_cb) },
  { "Quit",                  GQ_ICON_QUIT,                      N_("_Quit"),                                            "<control>Q",          N_("Quit"),                                            CB(layout_menu_exit_cb) },
  { "Rating0",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating 0"),                                        "<alt>KP_0",           N_("Rating 0"),                                        CB(layout_menu_rating_cb<0>) },
  { "Rating1",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating 1"),                                        "<alt>KP_1",           N_("Rating 1"),                                        CB(layout_menu_rating_cb<1>) },
  { "Rating2",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating 2"),                                        "<alt>KP_2",           N_("Rating 2"),                                        CB(layout_menu_rating_cb<2>) },
  { "Rating3",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating 3"),                                        "<alt>KP_3",           N_("Rating 3"),                                        CB(layout_menu_rating_cb<3>) },
  { "Rating4",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating 4"),                                        "<alt>KP_4",           N_("Rating 4"),                                        CB(layout_menu_rating_cb<4>) },
  { "Rating5",               PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating 5"),                                        "<alt>KP_5",           N_("Rating 5"),                                        CB(layout_menu_rating_cb<5>) },
  { "RatingM1",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating -1"),                                       "<alt>KP_Subtract",    N_("Rating -1"),                                       CB(layout_menu_rating_cb<-1>) },
  { "RatingMenu",            PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Rating"),                                          nullptr,               nullptr,                                               nullptr },
  { "Refresh",               GQ_ICON_REFRESH,                   N_("_Refresh"),                                         "R",                   N_("Refresh"),                                         CB(layout_menu_refresh_cb) },
  { "Rename",                PIXBUF_INLINE_ICON_RENAME,         N_("_Rename..."),                                       "<control>R",          N_("Rename..."),                                       CB(layout_menu_rename_cb) },
  { "RenameWindow",          GQ_ICON_EDIT,                      N_("Rename window"),                                    nullptr,               N_("Rename window"),                                   CB(layout_menu_window_rename_cb) },
  { "Rotate180",             PIXBUF_INLINE_ICON_180,            N_("Rotate 1_80°"),                                     "<shift>R",            N_("Image Rotate 180°"),                               CB(layout_menu_alter_cb<ALTER_ROTATE_180>) },
  { "RotateCCW",             GQ_ICON_ROTATE_LEFT,               N_("Rotate _counterclockwise 90°"),                     "bracketleft",         N_("Rotate counterclockwise 90°"),                     CB(layout_menu_alter_cb<ALTER_ROTATE_90_CC>) },
  { "RotateCW",              GQ_ICON_ROTATE_RIGHT,              N_("_Rotate clockwise 90°"),                            "bracketright",        N_("Image Rotate clockwise 90°"),                      CB(layout_menu_alter_cb<ALTER_ROTATE_90>) },
  { "SaveMetadata",          GQ_ICON_SAVE,                      N_("_Save metadata"),                                   "<control>S",          N_("Save metadata"),                                   CB(layout_menu_metadata_write_cb) },
  { "SearchAndRunCommand",   GQ_ICON_FIND,                      N_("Search and Run command"),                           "slash",               N_("Search commands by keyword and run them"),         CB(layout_menu_search_and_run_cb) },
  { "Search",                GQ_ICON_FIND,                      N_("_Search..."),                                       "F3",                  N_("Search..."),                                       CB(layout_menu_search_cb) },
  { "SelectAll",             PIXBUF_INLINE_ICON_SELECT_ALL,     N_("Select _all"),                                      "<control>A",          N_("Select all"),                                      CB(layout_menu_select_all_cb) },
  { "SelectInvert",          PIXBUF_INLINE_ICON_SELECT_INVERT,  N_("_Invert Selection"),                                "<control><shift>I",   N_("Invert Selection"),                                CB(layout_menu_invert_selection_cb) },
  { "SelectMenu",            PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Select"),                                          nullptr,               nullptr,                                               nullptr },
  { "SelectNone",            PIXBUF_INLINE_ICON_SELECT_NONE,    N_("Select _none"),                                     "<control><shift>A",   N_("Select none"),                                     CB(layout_menu_unselect_all_cb) },
  { "SelectOSD",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Select Overlay Screen Display"),                    nullptr,               N_("Select Overlay Screen Display"),                   nullptr },
  { "SlideShowFaster",       GQ_ICON_GENERIC,                   N_("Faster"),                                           "<control>equal",      N_("Slideshow Faster"),                                CB(layout_menu_slideshow_faster_cb) },
  { "SlideShowPause",        GQ_ICON_PAUSE,                     N_("_Pause slideshow"),                                 "P",                   N_("Pause slideshow"),                                 CB(layout_menu_slideshow_pause_cb) },
  { "SlideShowSlower",       GQ_ICON_GENERIC,                   N_("Slower"),                                           "<control>minus",      N_("Slideshow Slower"),                                CB(layout_menu_slideshow_slower_cb) },
  { "SplitDownPane",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Down Pane"),                                       "<alt>Down",           N_("Down Split Pane"),                                 CB(layout_menu_split_pane_updown_cb) },
  { "SplitMenu",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Spli_t"),                                           nullptr,               nullptr,                                               nullptr },
  { "SplitNextPane",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Next Pane"),                                       "<alt>Right",          N_("Next Split Pane"),                                 CB(layout_menu_split_pane_next_cb) },
  { "SplitPreviousPane",     PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Previous Pane"),                                   "<alt>Left",           N_("Previous Split Pane"),                             CB(layout_menu_split_pane_prev_cb) },
  { "SplitUpPane",           PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Up Pane"),                                         "<alt>Up",             N_("Up Split Pane"),                                   CB(layout_menu_split_pane_updown_cb) },
  { "StereoCycle",           PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Cycle through stereo modes"),                      nullptr,               N_("Cycle through stereo modes"),                      CB(layout_menu_stereo_mode_next_cb) },
  { "StereoMenu",            PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Stere_o"),                                          nullptr,               nullptr,                                               nullptr },
  { "Up",                    GQ_ICON_GO_UP,                     N_("_Up"),                                              nullptr,               N_("Up one folder"),                                   CB(layout_menu_up_cb) },
  { "ViewInNewWindow",       PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_View in new window"),                              "<control>V",          N_("View in new window"),                              CB(layout_menu_view_in_new_window_cb) },
  { "ViewMenu",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_View"),                                            nullptr,               nullptr,                                               CB(layout_menu_view_menu_cb)  },
  { "Wallpaper",             PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Set as _wallpaper"),                                nullptr,               N_("Set as wallpaper"),                                CB(layout_menu_wallpaper_cb) },
  { "WindowsMenu",           PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Windows"),                                         nullptr,               nullptr,                                               CB(layout_menu_windows_menu_cb)  },
  { "WriteRotationKeepDate", PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Write orientation to file (preserve timestamp)"),  nullptr,               N_("Write orientation to file (preserve timestamp)"),  CB(layout_menu_write_rotate_cb<TRUE>) },
  { "WriteRotation",         PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Write orientation to file"),                       nullptr,               N_("Write orientation to file"),                       CB(layout_menu_write_rotate_cb<FALSE>) },
  { "Zoom100Alt1",           GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "KP_Divide",           N_("Zoom 1:1"),                                        (GCallback)layout_menu_zoom_set_cb<1, FALSE> },
  { "Zoom100",               GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "Z",                   N_("Zoom 1:1"),                                        (GCallback)layout_menu_zoom_set_cb<1, FALSE> },
  { "Zoom200",               GQ_ICON_GENERIC,                   N_("Zoom _2:1"),                                        nullptr,               N_("Zoom 2:1"),                                        (GCallback)layout_menu_zoom_set_cb<2, FALSE> },
  { "Zoom25",                GQ_ICON_GENERIC,                   N_("Zoom 1:4"),                                         nullptr,               N_("Zoom 1:4"),                                        (GCallback)layout_menu_zoom_set_cb<-4, FALSE> },
  { "Zoom300",               GQ_ICON_GENERIC,                   N_("Zoom _3:1"),                                        nullptr,               N_("Zoom 3:1"),                                        (GCallback)layout_menu_zoom_set_cb<3, FALSE> },
  { "Zoom33",                GQ_ICON_GENERIC,                   N_("Zoom 1:3"),                                         nullptr,               N_("Zoom 1:3"),                                        (GCallback)layout_menu_zoom_set_cb<-3, FALSE> },
  { "Zoom400",               GQ_ICON_GENERIC,                   N_("Zoom _4:1"),                                        nullptr,               N_("Zoom 4:1"),                                        (GCallback)layout_menu_zoom_set_cb<4, FALSE> },
  { "Zoom50",                GQ_ICON_GENERIC,                   N_("Zoom 1:2"),                                         nullptr,               N_("Zoom 1:2"),                                        (GCallback)layout_menu_zoom_set_cb<-2, FALSE> },
  { "ZoomFillHor",           PIXBUF_INLINE_ICON_ZOOMFILLHOR,    N_("Fit _Horizontally"),                                "H",                   N_("Fit Horizontally"),                                (GCallback)layout_menu_zoom_fit_hv_cb<FALSE, FALSE> },
  { "ZoomFillVert",          PIXBUF_INLINE_ICON_ZOOMFILLVERT,   N_("Fit _Vertically"),                                  "W",                   N_("Fit Vertically"),                                  (GCallback)layout_menu_zoom_fit_hv_cb<TRUE, FALSE> },
  { "ZoomFitAlt1",           GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "KP_Multiply",         N_("Zoom to fit"),                                     (GCallback)layout_menu_zoom_set_cb<0, FALSE> },
  { "ZoomFit",               GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "X",                   N_("Zoom to fit"),                                     (GCallback)layout_menu_zoom_set_cb<0, FALSE> },
  { "ZoomInAlt1",            GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "KP_Add",              N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb<FALSE>) },
  { "ZoomIn",                GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "equal",               N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb<FALSE>) },
  { "ZoomMenu",              PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("_Zoom"),                                            nullptr,               nullptr,                                               nullptr },
  { "ZoomOutAlt1",           GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "KP_Subtract",         N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb<FALSE>) },
  { "ZoomToRectangle",       PIXBUF_INLINE_ICON_PLACEHOLDER,    N_("Zoom to rectangle"),                                nullptr,               N_("Zoom to rectangle"),                               CB(layout_menu_zoom_to_rectangle_cb) },
  { "ZoomOut",               GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "minus",               N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb<FALSE>) }
};

static GtkToggleActionEntry menu_toggle_entries[] = {
  { "Animate",                 PIXBUF_INLINE_ICON_PLACEHOLDER,       N_("_Animation"),               "A",               N_("Toggle animation"),              CB(layout_menu_animate_cb),                  FALSE  },
  { "DrawRectangle",           PIXBUF_INLINE_ICON_DRAW_RECTANGLE,    N_("Draw Rectangle"),           nullptr,           N_("Draw Rectangle"),                CB(layout_menu_select_rectangle_cb),         FALSE  },
  { "ExifRotate",              GQ_ICON_ROTATE_LEFT,                  N_("_Exif rotate"),             "<alt>X",          N_("Toggle Exif rotate"),            CB(layout_menu_exif_rotate_cb),              FALSE  },
  { "FloatTools",              PIXBUF_INLINE_ICON_FLOAT,             N_("_Float file list"),         "L",               N_("Float file list"),               CB(layout_menu_float_cb),                    FALSE  },
  { "Grayscale",               PIXBUF_INLINE_ICON_GRAYSCALE,         N_("Toggle _grayscale"),        "<shift>G",        N_("Toggle grayscale"),              CB(layout_menu_alter_desaturate_cb),         FALSE  },
  { "HideBars",                PIXBUF_INLINE_ICON_PLACEHOLDER,       N_("Hide Bars and Files"),      "grave",           N_("Hide Bars and Files"),           CB(layout_menu_hide_bars_cb),                FALSE  },
  { "HideSelectableToolbars",  PIXBUF_INLINE_ICON_PLACEHOLDER,       N_("Hide Selectable Bars"),     "<control>grave",  N_("Hide Selectable Bars"),          CB(layout_menu_selectable_toolbars_cb),      FALSE  },
  { "IgnoreAlpha",             GQ_ICON_STRIKETHROUGH,                N_("Hide _alpha"),              "<shift>A",        N_("Hide alpha channel"),            CB(layout_menu_alter_ignore_alpha_cb),       FALSE  },
  { "ImageHistogram",          PIXBUF_INLINE_ICON_PLACEHOLDER,       N_("_Show Histogram"),          nullptr,           N_("Show Histogram"),                CB(layout_menu_histogram_cb),                FALSE  },
  { "ImageOverlay",            PIXBUF_INLINE_ICON_PLACEHOLDER,       N_("Image _Overlay"),           nullptr,           N_("Image Overlay"),                 CB(layout_menu_overlay_cb),                  FALSE  },
  { "OverUnderExposed",        PIXBUF_INLINE_ICON_EXPOSURE,          N_("Over/Under Exposed"),       "<shift>E",        N_("Highlight over/under exposed"),  CB(layout_menu_select_overunderexposed_cb),  FALSE  },
  { "RectangularSelection",    PIXBUF_INLINE_ICON_SELECT_RECTANGLE,  N_("Rectangular Selection"),    "<alt>R",          N_("Rectangular Selection"),         CB(layout_menu_rectangular_selection_cb),    FALSE  },
  { "SBar",                    PIXBUF_INLINE_ICON_PROPERTIES,        N_("_Info sidebar"),            "<control>K",      N_("Info sidebar"),                  CB(layout_menu_bar_cb),                      FALSE  },
  { "SBarSort",                PIXBUF_INLINE_ICON_SORT,              N_("Sort _manager"),            "<shift>S",        N_("Sort manager"),                  CB(layout_menu_bar_sort_cb),                 FALSE  },
  { "ShowFileFilter",          GQ_ICON_FILE_FILTER,                  N_("Show File Filter"),         nullptr,           N_("Show File Filter"),              CB(layout_menu_file_filter_cb),              FALSE  },
  { "ShowInfoPixel",           GQ_ICON_SELECT_COLOR,                 N_("Pi_xel Info"),              nullptr,           N_("Show Pixel Info"),               CB(layout_menu_info_pixel_cb),               FALSE  },
  { "ShowMarks",               PIXBUF_INLINE_ICON_MARKS,             N_("Show _Marks"),              "M",               N_("Show Marks"),                    CB(layout_menu_marks_cb),                    FALSE  },
  { "SlideShow",               GQ_ICON_PLAY,                         N_("Toggle _slideshow"),        "S",               N_("Toggle slideshow"),              CB(layout_menu_slideshow_cb),                FALSE  },
  { "SplitPaneSync",           PIXBUF_INLINE_SPLIT_PANE_SYNC,        N_("Split Pane Sync"),          nullptr,           N_("Split Pane Sync"),               CB(layout_menu_split_pane_sync_cb),          FALSE  },
  { "Thumbnails",              PIXBUF_INLINE_ICON_THUMB,             N_("Show _Thumbnails"),         "T",               N_("Show Thumbnails"),               CB(layout_menu_thumb_cb),                    FALSE  },
  { "UseColorProfiles",        GQ_ICON_COLOR_MANAGEMENT,             N_("Use _color profiles"),      nullptr,           N_("Use color profiles"),            CB(layout_color_menu_enable_cb),             FALSE  },
  { "UseImageProfile",         PIXBUF_INLINE_ICON_PLACEHOLDER,       N_("Use profile from _image"),  nullptr,           N_("Use profile from image"),        CB(layout_color_menu_use_image_cb),          FALSE  }
};

static GtkRadioActionEntry menu_radio_entries[] = {
  { "ViewIcons",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Images as I_cons"),  "<control>I",  N_("View Images as Icons"),  FILEVIEW_ICON },
  { "ViewList",   PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Images as _List"),   "<control>L",  N_("View Images as List"),   FILEVIEW_LIST }
};

static GtkToggleActionEntry menu_view_dir_toggle_entries[] = {
  { "FolderTree",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("T_oggle Folder View"),  "<control>T",  N_("Toggle Folders View"),  CB(layout_menu_view_dir_as_cb),FALSE },
};

static GtkRadioActionEntry menu_split_radio_entries[] = {
  { "SplitHorizontal",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Horizontal"),  "E",      N_("Split panes horizontal."),  SPLIT_HOR },
  { "SplitQuad",        PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Quad"),        nullptr,  N_("Split panes quad"),         SPLIT_QUAD },
  { "SplitSingle",      PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Single"),      "Y",      N_("Single pane"),              SPLIT_NONE },
  { "SplitTriple",      PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Triple"),      nullptr,  N_("Split panes triple"),       SPLIT_TRIPLE },
  { "SplitVertical",    PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Vertical"),    "U",      N_("Split panes vertical"),     SPLIT_VERT }
};

static GtkRadioActionEntry menu_color_radio_entries[] = {
  { "ColorProfile0",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Input _0: sRGB"),                 nullptr,  N_("Input 0: sRGB"),                 COLOR_PROFILE_SRGB },
  { "ColorProfile1",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Input _1: AdobeRGB compatible"),  nullptr,  N_("Input 1: AdobeRGB compatible"),  COLOR_PROFILE_ADOBERGB },
  { "ColorProfile2",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Input _2"),                       nullptr,  N_("Input 2"),                       COLOR_PROFILE_FILE },
  { "ColorProfile3",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Input _3"),                       nullptr,  N_("Input 3"),                       COLOR_PROFILE_FILE + 1 },
  { "ColorProfile4",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Input _4"),                       nullptr,  N_("Input 4"),                       COLOR_PROFILE_FILE + 2 },
  { "ColorProfile5",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Input _5"),                       nullptr,  N_("Input 5"),                       COLOR_PROFILE_FILE + 3 }
};

static GtkRadioActionEntry menu_histogram_channel[] = {
  { "HistogramChanB",    PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Histogram on _Blue"),   nullptr,  N_("Histogram on Blue"),   HCHAN_B },
  { "HistogramChanG",    PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Histogram on _Green"),  nullptr,  N_("Histogram on Green"),  HCHAN_G },
  { "HistogramChanRGB",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Histogram on RGB"),    nullptr,  N_("Histogram on RGB"),    HCHAN_RGB },
  { "HistogramChanR",    PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Histogram on _Red"),    nullptr,  N_("Histogram on Red"),    HCHAN_R },
  { "HistogramChanV",    PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Histogram on _Value"),  nullptr,  N_("Histogram on Value"),  HCHAN_MAX }
};

static GtkRadioActionEntry menu_histogram_mode[] = {
  { "HistogramModeLin",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("Li_near Histogram"),  nullptr,  N_("Linear Histogram"),  0 },
  { "HistogramModeLog",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Log Histogram"),     nullptr,  N_("Log Histogram"),     1 },
};

static GtkRadioActionEntry menu_stereo_mode_entries[] = {
  { "StereoAuto",   PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Auto"),          nullptr,  N_("Stereo Auto"),          STEREO_PIXBUF_DEFAULT },
  { "StereoCross",  PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Cross"),         nullptr,  N_("Stereo Cross"),         STEREO_PIXBUF_CROSS },
  { "StereoOff",    PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Off"),           nullptr,  N_("Stereo Off"),           STEREO_PIXBUF_NONE },
  { "StereoSBS",    PIXBUF_INLINE_ICON_PLACEHOLDER,  N_("_Side by Side"),  nullptr,  N_("Stereo Side by Side"),  STEREO_PIXBUF_SBS }
};

static GtkRadioActionEntry menu_draw_rectangle_aspect_ratios[] = {
  { "CropNone",        PIXBUF_INLINE_ICON_PLACEHOLDER, N_("Crop None"), nullptr, N_("Crop rectangle None"), RECTANGLE_DRAW_ASPECT_RATIO_NONE },
  { "CropOneOne",      PIXBUF_INLINE_ICON_PLACEHOLDER, N_("Crop 1:1"),  nullptr, N_("Crop rectangle 1:1"),  RECTANGLE_DRAW_ASPECT_RATIO_ONE_ONE },
  { "CropFourThree",   PIXBUF_INLINE_ICON_PLACEHOLDER, N_("Crop 4:3"),  nullptr, N_("Crop rectangle 4:3"),  RECTANGLE_DRAW_ASPECT_RATIO_FOUR_THREE },
  { "CropThreeTwo",    PIXBUF_INLINE_ICON_PLACEHOLDER, N_("Crop 3:2"),  nullptr, N_("Crop rectangle 3:2"),  RECTANGLE_DRAW_ASPECT_RATIO_THREE_TWO },
  { "CropSixteenNine", PIXBUF_INLINE_ICON_PLACEHOLDER, N_("Crop 16:9"), nullptr, N_("Crop rectangle 16:9"), RECTANGLE_DRAW_ASPECT_RATIO_SIXTEEN_NINE }
};

static GtkRadioActionEntry menu_osd[] = {
  { "OSD1", PIXBUF_INLINE_ICON_PLACEHOLDER, N_("OSD 1"), nullptr, N_("Overlay Screen Display 1"), OVERLAY_SCREEN_DISPLAY_1 },
  { "OSD2", PIXBUF_INLINE_ICON_PLACEHOLDER, N_("OSD 2"), nullptr, N_("Overlay Screen Display 2"), OVERLAY_SCREEN_DISPLAY_2 },
  { "OSD3", PIXBUF_INLINE_ICON_PLACEHOLDER, N_("OSD 3"), nullptr, N_("Overlay Screen Display 3"), OVERLAY_SCREEN_DISPLAY_3 },
  { "OSD4", PIXBUF_INLINE_ICON_PLACEHOLDER, N_("OSD 4"), nullptr, N_("Overlay Screen Display 4"), OVERLAY_SCREEN_DISPLAY_4 }
};
#undef CB

static gchar *menu_translate(const gchar *path, gpointer)
{
	return const_cast<gchar *>(_(path));
}

static void layout_actions_setup_mark(LayoutWindow *lw, gint mark, const gchar *name_tmpl,
				      const gchar *label_tmpl, const gchar *accel_tmpl, const gchar *tooltip_tmpl, GCallback cb)
{
	gchar name[50];
	gchar label[100];
	gchar accel[50];
	gchar tooltip[100];
	GtkActionEntry entry = { name, nullptr, label, accel, tooltip, cb };
	GtkAction *action;

	g_snprintf(name, sizeof(name), name_tmpl, mark);
	g_snprintf(label, sizeof(label), label_tmpl, mark);

	if (accel_tmpl)
		g_snprintf(accel, sizeof(accel), accel_tmpl, mark % 10);
	else
		entry.accelerator = nullptr;

	if (tooltip_tmpl)
		g_snprintf(tooltip, sizeof(tooltip), tooltip_tmpl, mark);
	else
		entry.tooltip = nullptr;

	gq_gtk_action_group_add_actions(lw->action_group, &entry, 1, lw);
	action = gq_gtk_action_group_get_action(lw->action_group, name);
	g_object_set_data(G_OBJECT(action), "mark_num", GINT_TO_POINTER(mark > 0 ? mark : 10));
}

static void layout_actions_setup_marks(LayoutWindow *lw)
{
	gint mark;
	g_autoptr(GString) desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>");

	if (options->hamburger_menu)
		{
		g_string_append(desc, "    <menu action='OpenMenu'>");
		}
	g_string_append(desc, "      <menu action='SelectMenu'>");

	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		gint i = (mark < 10 ? mark : 0);

		layout_actions_setup_mark(lw, i, "Mark%d",		_("Mark _%d"), nullptr, nullptr, nullptr);
		layout_actions_setup_mark(lw, i, "SetMark%d",	_("_Set mark %d"),			nullptr,		_("Set mark %d"), G_CALLBACK(layout_menu_selection_to_mark_cb<STM_MODE_SET>));
		layout_actions_setup_mark(lw, i, "ResetMark%d",	_("_Reset mark %d"),			nullptr,		_("Reset mark %d"), G_CALLBACK(layout_menu_selection_to_mark_cb<STM_MODE_RESET>));
		layout_actions_setup_mark(lw, i, "ToggleMark%d",	_("_Toggle mark %d"),			"%d",		_("Toggle mark %d"), G_CALLBACK(layout_menu_selection_to_mark_cb<STM_MODE_TOGGLE>));
		layout_actions_setup_mark(lw, i, "ToggleMark%dAlt1",	_("_Toggle mark %d"),			"KP_%d",	_("Toggle mark %d"), G_CALLBACK(layout_menu_selection_to_mark_cb<STM_MODE_TOGGLE>));
		layout_actions_setup_mark(lw, i, "SelectMark%d",	_("Se_lect mark %d"),			"<control>%d",	_("Select mark %d"), G_CALLBACK(layout_menu_mark_to_selection_cb<MTS_MODE_SET>));
		layout_actions_setup_mark(lw, i, "SelectMark%dAlt1",	_("_Select mark %d"),			"<control>KP_%d", _("Select mark %d"), G_CALLBACK(layout_menu_mark_to_selection_cb<MTS_MODE_SET>));
		layout_actions_setup_mark(lw, i, "AddMark%d",	_("_Add mark %d"),			nullptr,		_("Add mark %d"), G_CALLBACK(layout_menu_mark_to_selection_cb<MTS_MODE_OR>));
		layout_actions_setup_mark(lw, i, "IntMark%d",	_("_Intersection with mark %d"),	nullptr,		_("Intersection with mark %d"), G_CALLBACK(layout_menu_mark_to_selection_cb<MTS_MODE_AND>));
		layout_actions_setup_mark(lw, i, "UnselMark%d",	_("_Unselect mark %d"),			nullptr,		_("Unselect mark %d"), G_CALLBACK(layout_menu_mark_to_selection_cb<MTS_MODE_MINUS>));
		layout_actions_setup_mark(lw, i, "FilterMark%d",	_("_Filter mark %d"),			nullptr,		_("Filter mark %d"), G_CALLBACK(layout_menu_mark_filter_toggle_cb));

		g_string_append_printf(desc,
				"      <menu action='Mark%d'>"
				"        <menuitem action='ToggleMark%d'/>"
				"        <menuitem action='SetMark%d'/>"
				"        <menuitem action='ResetMark%d'/>"
				"        <separator/>"
				"        <menuitem action='SelectMark%d'/>"
				"        <menuitem action='AddMark%d'/>"
				"        <menuitem action='IntMark%d'/>"
				"        <menuitem action='UnselMark%d'/>"
				"        <separator/>"
				"        <menuitem action='FilterMark%d'/>"
				"      </menu>",
				i, i, i, i, i, i, i, i, i);
		}

	g_string_append(desc,
				"      </menu>");
	if (options->hamburger_menu)
		{
		g_string_append(desc, "    </menu>");
		}
	g_string_append(desc, "  </menubar>");

	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		gint i = (mark < 10 ? mark : 0);

		g_string_append_printf(desc,
				"<accelerator action='ToggleMark%dAlt1'/>"
				"<accelerator action='SelectMark%dAlt1'/>",
				i, i);
		}
	g_string_append(desc,   "</ui>" );

	g_autoptr(GError) error = nullptr;
	if (!gq_gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error))
		{
		g_message("building menus failed: %s", error->message);
		exit(EXIT_FAILURE);
		}
}

static GList *layout_actions_editor_menu_path(const EditorDescription *editor)
{
	g_auto(GStrv) split = g_strsplit(editor->menu_path, "/", 0);

	const guint split_count = g_strv_length(split);
	if (split_count == 0) return nullptr;

	GList *ret = nullptr;
	for (guint i = 0; i < split_count; i++)
		{
		ret = g_list_prepend(ret, g_strdup(split[i]));
		}

	ret = g_list_prepend(ret, g_strdup(editor->key));

	return g_list_reverse(ret);
}

static void layout_actions_editor_add(GString *desc, GList *path, GList *old_path)
{
	gint to_open;
	gint to_close;
	gint i;
	while (path && old_path && strcmp(static_cast<gchar *>(path->data), static_cast<gchar *>(old_path->data)) == 0)
		{
		path = path->next;
		old_path = old_path->next;
		}
	to_open = g_list_length(path) - 1;
	to_close = g_list_length(old_path) - 1;

	if (to_close > 0)
		{
		old_path = g_list_last(old_path);
		old_path = old_path->prev;
		}

	for (i =  0; i < to_close; i++)
		{
		auto name = static_cast<gchar *>(old_path->data);
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append(desc,	"      </placeholder>");
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append(desc,	"    </menu>");
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		old_path = old_path->prev;
		}

	for (i =  0; i < to_open; i++)
		{
		auto name = static_cast<gchar *>(path->data);
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append_printf(desc,	"      <placeholder name='%s'>", name);
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append_printf(desc,	"    <menu action='%s'>", name);
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		path = path->next;
		}

	if (path)
		g_string_append_printf(desc, "      <menuitem action='%s'/>", static_cast<gchar *>(path->data));
}

static void layout_actions_setup_editors(LayoutWindow *lw)
{
	if (lw->ui_editors_id)
		{
		gq_gtk_ui_manager_remove_ui(lw->ui_manager, lw->ui_editors_id);
		}

	if (lw->action_group_editors)
		{
		gq_gtk_ui_manager_remove_action_group(lw->ui_manager, lw->action_group_editors);
		g_object_unref(lw->action_group_editors);
		}
	lw->action_group_editors = gq_gtk_action_group_new("MenuActionsExternal");
	gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);

	/* lw->action_group_editors contains translated entries, no translate func is required */
	g_autoptr(GString) desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>");

	if (options->hamburger_menu)
		{
		g_string_append(desc, "    <menu action='OpenMenu'>");
		}

	GList *old_path = nullptr;

	GtkWidget *main_toolbar = lw->toolbar[TOOLBAR_MAIN];
	if (GTK_IS_CONTAINER(main_toolbar))
		{
		static const auto set_image_and_tooltip_from_editor = [](GtkWidget *widget, gpointer data)
		{
#if HAVE_GTK4
			const gchar *tooltip = gtk_widget_get_tooltip_text(widget);
#else
			g_autofree gchar *tooltip = gtk_widget_get_tooltip_text(widget);
#endif
			auto *editor = static_cast<EditorDescription *>(data);
			if (g_strcmp0(tooltip, editor->key) != 0) return; // @todo Use g_list_find_custom() if tooltip is unique

			GtkWidget *image = nullptr;
			if (editor->icon)
				{
				image = gq_gtk_image_new_from_stock(editor->key, GTK_ICON_SIZE_BUTTON);
				}
			else
				{
				image = gtk_image_new_from_icon_name(GQ_ICON_MISSING_IMAGE, GTK_ICON_SIZE_BUTTON);
				}
			gtk_button_set_image(GTK_BUTTON(widget), image);
			gtk_widget_set_tooltip_text(widget, editor->name);
		};

		EditorsList editors_list = editor_list_get();
		for (EditorDescription *editor : editors_list)
			{
			GtkActionEntry entry = { editor->key,
			                         editor->icon ? editor->key : nullptr,
			                         editor->name,
			                         editor->hotkey,
			                         editor->comment ? editor->comment : editor->name,
			                         G_CALLBACK(layout_menu_edit_cb) };

			gq_gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);

			// @todo Use g_list_find_custom() if tooltip is unique
			gtk_container_foreach(GTK_CONTAINER(main_toolbar), set_image_and_tooltip_from_editor, editor);

			GList *path = layout_actions_editor_menu_path(editor);
			layout_actions_editor_add(desc, path, old_path);

			g_list_free_full(old_path, g_free);
			old_path = path;
			}
		}

	layout_actions_editor_add(desc, nullptr, old_path);
	g_list_free_full(old_path, g_free);

	if (options->hamburger_menu)
		{
		g_string_append(desc, "</menu>");
		}

	g_string_append(desc,
	                "  </menubar>"
	                "</ui>" );

	g_autoptr(GError) error = nullptr;

	lw->ui_editors_id = gq_gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error);
	if (!lw->ui_editors_id)
		{
		g_message("building menus failed: %s", error->message);
		exit(EXIT_FAILURE);
		}
}

void create_toolbars(LayoutWindow *lw)
{
	gint i;

	for (i = 0; i < TOOLBAR_COUNT; i++)
		{
		layout_actions_toolbar(lw, static_cast<ToolbarType>(i));
		layout_toolbar_add_default(lw, static_cast<ToolbarType>(i));
		}
}

void layout_actions_setup(LayoutWindow *lw)
{
	DEBUG_1("%s layout_actions_setup: start", get_exec_time());
	if (lw->ui_manager) return;

	lw->action_group = gq_gtk_action_group_new("MenuActions");
	gq_gtk_action_group_set_translate_func(lw->action_group, menu_translate, nullptr, nullptr);

	gq_gtk_action_group_add_actions(lw->action_group,
	                                menu_entries, std::size(menu_entries), lw);
	gq_gtk_action_group_add_toggle_actions(lw->action_group,
	                                       menu_toggle_entries, std::size(menu_toggle_entries),
	                                       lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_radio_entries, std::size(menu_radio_entries),
	                                      0, G_CALLBACK(layout_menu_list_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_split_radio_entries, std::size(menu_split_radio_entries),
	                                      0, G_CALLBACK(layout_menu_split_cb), lw);
	gq_gtk_action_group_add_toggle_actions(lw->action_group,
	                                       menu_view_dir_toggle_entries, std::size(menu_view_dir_toggle_entries),
	                                       lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_color_radio_entries, std::size(menu_color_radio_entries),
	                                      0, G_CALLBACK(layout_color_menu_input_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_histogram_channel, std::size(menu_histogram_channel),
	                                      0, G_CALLBACK(layout_menu_histogram_channel_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_histogram_mode, std::size(menu_histogram_mode),
	                                      0, G_CALLBACK(layout_menu_histogram_mode_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_stereo_mode_entries, std::size(menu_stereo_mode_entries),
	                                      0, G_CALLBACK(layout_menu_stereo_mode_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_draw_rectangle_aspect_ratios, std::size(menu_draw_rectangle_aspect_ratios),
	                                      0, G_CALLBACK(layout_menu_draw_rectangle_aspect_ratio_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
	                                      menu_osd, std::size(menu_osd),
	                                      0, G_CALLBACK(layout_menu_osd_cb), lw);


	lw->ui_manager = gq_gtk_ui_manager_new();
	gq_gtk_ui_manager_set_add_tearoffs(lw->ui_manager, TRUE);
	gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group, 0);

	DEBUG_1("%s layout_actions_setup: add menu", get_exec_time());
	g_autoptr(GError) error = nullptr;

	if (!gq_gtk_ui_manager_add_ui_from_resource(lw->ui_manager, options->hamburger_menu ? GQ_RESOURCE_PATH_UI "/menu-hamburger.ui" : GQ_RESOURCE_PATH_UI "/menu-classic.ui" , &error))
		{
		g_message("building menus failed: %s", error->message);
		exit(EXIT_FAILURE);
		}

	DEBUG_1("%s layout_actions_setup: marks", get_exec_time());
	layout_actions_setup_marks(lw);

	DEBUG_1("%s layout_actions_setup: editors", get_exec_time());
	layout_actions_setup_editors(lw);

	DEBUG_1("%s layout_actions_setup: status_update_write", get_exec_time());
	layout_util_status_update_write(lw);

	DEBUG_1("%s layout_actions_setup: actions_add_window", get_exec_time());
	layout_actions_add_window(lw, lw->window);
	DEBUG_1("%s layout_actions_setup: end", get_exec_time());
}

static gboolean layout_editors_reload_idle_cb(gpointer user_data)
{
	auto *layout_editors = static_cast<LayoutEditors *>(user_data);

	if (!layout_editors->desktop_files)
		{
		DEBUG_1("%s layout_editors_reload_idle_cb: get_desktop_files", get_exec_time());
		layout_editors->desktop_files = editor_get_desktop_files();
		return G_SOURCE_CONTINUE;
		}

	editor_read_desktop_file(static_cast<const gchar *>(layout_editors->desktop_files->data));
	g_free(layout_editors->desktop_files->data);
	layout_editors->desktop_files = g_list_delete_link(layout_editors->desktop_files, layout_editors->desktop_files);

	if (layout_editors->desktop_files) return G_SOURCE_CONTINUE;

	DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors", get_exec_time());
	editor_table_finish();

	layout_window_foreach([](LayoutWindow *lw)
	{
		layout_actions_setup_editors(lw);
		if (lw->bar_sort_enabled)
			{
			layout_bar_sort_toggle(lw);
			}
	});

	DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors done", get_exec_time());

	/* The toolbars need to be regenerated in case they contain a plugin */
	LayoutWindow *lw = get_current_layout();

	for (gint i = 0; i < TOOLBAR_COUNT; i++)
		{
		toolbar_select_new(lw, static_cast<ToolbarType>(i));
		toolbar_apply(static_cast<ToolbarType>(i));
		}

	layout_editors->reload_idle_id = -1;
	return G_SOURCE_REMOVE;
}

void layout_editors_reload_start()
{
	DEBUG_1("%s layout_editors_reload_start", get_exec_time());

	if (layout_editors.reload_idle_id != -1)
		{
		g_source_remove(layout_editors.reload_idle_id);
		g_list_free_full(layout_editors.desktop_files, g_free);
		}

	editor_table_clear();
	layout_editors.reload_idle_id = g_idle_add(layout_editors_reload_idle_cb, &layout_editors);
}

void layout_editors_reload_finish()
{
	if (layout_editors.reload_idle_id == -1) return;

	DEBUG_1("%s layout_editors_reload_finish", get_exec_time());

	g_source_remove(layout_editors.reload_idle_id);
	while (layout_editors.reload_idle_id != -1)
		{
		layout_editors_reload_idle_cb(&layout_editors);
		}
}

void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window)
{
	GtkAccelGroup *group;

	if (!lw->ui_manager) return;

	group = gq_gtk_ui_manager_get_accel_group(lw->ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), group);
}

GtkWidget *layout_actions_menu_bar(LayoutWindow *lw)
{
	if (lw->menu_bar) return lw->menu_bar;
	lw->menu_bar = gq_gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu");
	return g_object_ref(lw->menu_bar);
}

GtkWidget *layout_actions_toolbar(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar[type]) return lw->toolbar[type];

	lw->toolbar[type] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	gtk_widget_show(lw->toolbar[type]);
	return g_object_ref(lw->toolbar[type]);
}

GtkWidget *layout_actions_menu_tool_bar(LayoutWindow *lw)
{
	GtkWidget *menu_bar;
	GtkWidget *toolbar;

	if (lw->menu_tool_bar) return lw->menu_tool_bar;

	toolbar = layout_actions_toolbar(lw, TOOLBAR_MAIN);
	DEBUG_NAME(toolbar);
	lw->menu_tool_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	if (!options->hamburger_menu)
		{
		menu_bar = layout_actions_menu_bar(lw);
		DEBUG_NAME(menu_bar);
		gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), menu_bar, FALSE, FALSE, 0);
		}

	gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), toolbar, FALSE, FALSE, 0);

	return g_object_ref(lw->menu_tool_bar);
}

static void toolbar_clear_cb(GtkWidget *widget, gpointer)
{
	GtkAction *action;

	if (GTK_IS_BUTTON(widget))
		{
		action = static_cast<GtkAction *>(g_object_get_data(G_OBJECT(widget), "action"));
		if (g_object_get_data(G_OBJECT(widget), "id") )
			{
			g_signal_handler_disconnect(action, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "id")));
			}
		}
	gq_gtk_widget_destroy(widget);
}

void layout_toolbar_clear(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar_merge_id[type])
		{
		gq_gtk_ui_manager_remove_ui(lw->ui_manager, lw->toolbar_merge_id[type]);
		gq_gtk_ui_manager_ensure_update(lw->ui_manager);
		}
	g_list_free_full(lw->toolbar_actions[type], g_free);
	lw->toolbar_actions[type] = nullptr;

	lw->toolbar_merge_id[type] = gq_gtk_ui_manager_new_merge_id(lw->ui_manager);

	if (lw->toolbar[type])
		{
		gtk_container_foreach(GTK_CONTAINER(lw->toolbar[type]), toolbar_clear_cb, nullptr);
		}
}

static void action_radio_changed_cb(GtkAction *action, GtkAction *current, gpointer data)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data), action == current);
}

static void action_toggle_activate_cb(GtkAction* self, gpointer data)
{
	auto button = static_cast<GtkToggleButton *>(data);
	const gboolean action_active = gq_gtk_toggle_action_get_active(GQ_GTK_TOGGLE_ACTION(self));

	if (gtk_toggle_button_get_active(button) != action_active)
		{
		gtk_toggle_button_set_active(button, action_active);
		}
}

static gboolean toolbar_button_press_event_cb(GtkWidget *, GdkEvent *, gpointer data)
{
	gq_gtk_action_activate(GQ_GTK_ACTION(data));

	return TRUE;
}

void layout_toolbar_add(LayoutWindow *lw, ToolbarType type, const gchar *action_name)
{
	const gchar *path = nullptr;
	const gchar *tooltip_text = nullptr;
	GtkAction *action;
	GtkWidget *action_icon = nullptr;
	GtkWidget *button;
	gulong id;

	if (!action_name || !lw->ui_manager) return;

	if (!lw->toolbar[type])
		{
		return;
		}

	switch (type)
		{
		case TOOLBAR_MAIN:
			path = "/ToolBar";
			break;
		case TOOLBAR_STATUS:
			path = "/StatusBar";
			break;
		default:
			break;
		}

	if (g_str_has_suffix(action_name, ".desktop"))
		{
		/* this may be called before the external editors are read
		   create a dummy action for now */
		if (!lw->action_group_editors)
			{
			lw->action_group_editors = gq_gtk_action_group_new("MenuActionsExternal");
			gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);
			}
		if (!gq_gtk_action_group_get_action(lw->action_group_editors, action_name))
			{
			GtkActionEntry entry = { action_name,
			                         GQ_ICON_MISSING_IMAGE,
			                         action_name,
			                         nullptr,
			                         nullptr,
			                         nullptr
			                       };
			DEBUG_1("Creating temporary action %s", action_name);
			gq_gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);
			}
		}

	if (g_strcmp0(action_name, "Separator") == 0)
		{
		button = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		}
	else
		{
		if (g_str_has_suffix(action_name, ".desktop"))
			{
			action = gq_gtk_action_group_get_action(lw->action_group_editors, action_name);

			/** @FIXME Using tootip as a flag to layout_actions_setup_editors()
			 * is not a good way.
			 */
			tooltip_text = gq_gtk_action_get_label(action);
			}
		else
			{
			action = gq_gtk_action_group_get_action(lw->action_group, action_name);

			tooltip_text = gq_gtk_action_get_tooltip(action);
			}

		action_icon = gq_gtk_action_create_icon(action, GTK_ICON_SIZE_SMALL_TOOLBAR);

		/** @FIXME This is a hack to remove run-time errors */
		if (lw->toolbar_merge_id[type] > 0)
			{
			gq_gtk_ui_manager_add_ui(lw->ui_manager, lw->toolbar_merge_id[type], path, action_name, action_name, GTK_UI_MANAGER_TOOLITEM, FALSE);
			}

		if (GQ_GTK_IS_RADIO_ACTION(action) || GQ_GTK_IS_TOGGLE_ACTION(action))
			{
			button = gtk_toggle_button_new();
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), gq_gtk_toggle_action_get_active(GQ_GTK_TOGGLE_ACTION(action)));
			}
		else
			{
			button = gtk_button_new();
			}

		if (action_icon)
			{
			gtk_button_set_image(GTK_BUTTON(button), action_icon);
			}
		else
			{
			gtk_button_set_label(GTK_BUTTON(button), action_name);
			}

		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(button, tooltip_text);

		if (GQ_GTK_IS_RADIO_ACTION(action))
			{
			id = g_signal_connect(G_OBJECT(action), "changed", G_CALLBACK(action_radio_changed_cb), button);
			g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(id));
			}
		else if (GQ_GTK_IS_TOGGLE_ACTION(action))
			{
			id = g_signal_connect(G_OBJECT(action), "activate", G_CALLBACK(action_toggle_activate_cb), button);
			g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(id));
			}

		g_signal_connect(G_OBJECT(button), "button_press_event", G_CALLBACK(toolbar_button_press_event_cb), action);
		g_object_set_data(G_OBJECT(button), "action", action);
		}

	gq_gtk_container_add(lw->toolbar[type], button);
	gtk_widget_show(button);

	lw->toolbar_actions[type] = g_list_append(lw->toolbar_actions[type], g_strdup(action_name));
}

void layout_toolbar_add_default(LayoutWindow *lw, ToolbarType type)
{
	if (type >= TOOLBAR_COUNT) return;

	if (layout_window_count() > 0)
		{
		return;
		}

	switch (type)
		{
		case TOOLBAR_MAIN:
			layout_toolbar_add(lw, type, "Thumbnails");
			layout_toolbar_add(lw, type, "Back");
			layout_toolbar_add(lw, type, "Forward");
			layout_toolbar_add(lw, type, "Up");
			layout_toolbar_add(lw, type, "Home");
			layout_toolbar_add(lw, type, "Refresh");
			layout_toolbar_add(lw, type, "ZoomIn");
			layout_toolbar_add(lw, type, "ZoomOut");
			layout_toolbar_add(lw, type, "ZoomFit");
			layout_toolbar_add(lw, type, "Zoom100");
			layout_toolbar_add(lw, type, "Preferences");
			layout_toolbar_add(lw, type, "FloatTools");
			break;
		case TOOLBAR_STATUS:
			layout_toolbar_add(lw, type, "ExifRotate");
			layout_toolbar_add(lw, type, "ShowInfoPixel");
			layout_toolbar_add(lw, type, "UseColorProfiles");
			layout_toolbar_add(lw, type, "SaveMetadata");
			break;
		default:
			break;
		}
}


void layout_toolbar_write_config(LayoutWindow *lw, ToolbarType type, GString *outstr, gint indent)
{
	const gchar *name = toolbar_type_config_name(type);

	WRITE_NL(); WRITE_FORMAT_STRING("<%s>", name);
	indent++;
	WRITE_NL(); WRITE_STRING("<clear/>");
	for (GList *work = lw->toolbar_actions[type]; work; work = work->next)
		{
		auto action = static_cast<gchar *>(work->data);
		WRITE_NL(); WRITE_STRING("<toolitem ");
		write_char_option(outstr, "action", action);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_FORMAT_STRING("</%s>", name);
}

void layout_toolbar_add_from_config(LayoutWindow *lw, ToolbarType type, const char **attribute_names, const gchar **attribute_values)
{
	g_autofree gchar *action = nullptr;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("action", action)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	layout_toolbar_add(lw, type, action);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

void layout_util_status_update_write(LayoutWindow *lw)
{
	GtkAction *action;
	gint n = metadata_queue_length();
	action = gq_gtk_action_group_get_action(lw->action_group, "SaveMetadata");
	gq_gtk_action_set_sensitive(action, n > 0);

	const gchar *icon_name = n > 0 ? GQ_ICON_SAVE_AS : GQ_ICON_SAVE;
	g_autofree const gchar *icon_tooltip= n > 0 ? g_strdup_printf("Number of files with unsaved metadata: %d",n) : g_strdup("No unsaved metadata");

	struct UpdateIconData
		{
		GtkAction *act;
		const gchar *name;
		const gchar *tooltip;
		};

	static const auto update_icon_cb = [](GtkWidget *widget, gpointer data)
		{
		auto *uid = static_cast<UpdateIconData *>(data);
		if (!GTK_IS_BUTTON(widget)) return;
		auto *waction = static_cast<GtkAction *>(g_object_get_data(G_OBJECT(widget), "action"));
		if (waction != uid->act) return;
#if HAVE_GTK4
		GtkWidget *image = gtk_button_get_child(GTK_BUTTON(widget));
		if (GTK_IS_IMAGE(image))
			{
			gtk_image_set_from_icon_name(GTK_IMAGE(image), uid->name);
			}
#else
		GtkWidget *image = gtk_button_get_image(GTK_BUTTON(widget));
		if (GTK_IS_IMAGE(image))
			{
			gtk_image_set_from_icon_name(GTK_IMAGE(image), uid->name, GTK_ICON_SIZE_SMALL_TOOLBAR);
			}
#endif
		gtk_widget_set_tooltip_text(widget, uid->tooltip);
		};

	UpdateIconData uid = {action, icon_name, icon_tooltip};
	for (int i = 0; i < TOOLBAR_COUNT; i++) // NOLINT(modernize-loop-convert)
		{
		if (lw->toolbar[i])
			{
			gtk_container_foreach(GTK_CONTAINER(lw->toolbar[i]), update_icon_cb, &uid);
			}
		}
}

void layout_util_status_update_write_all()
{
	layout_window_foreach(layout_util_status_update_write);
}

static gchar *layout_color_name_parse(const gchar *name)
{
	if (!name || !*name) return g_strdup(_("Empty"));
	return g_strdelimit(g_strdup(name), "_", '-');
}

void layout_util_sync_color(LayoutWindow *lw)
{
	GtkAction *action;
	gint input = 0;
	gboolean use_color;
	gboolean use_image = FALSE;
	gint i;
	gchar action_name[15];

	if (!lw->action_group) return;
	if (!layout_image_color_profile_get(lw, input, use_image)) return;

	use_color = layout_image_color_profile_get_use(lw);

	action = gq_gtk_action_group_get_action(lw->action_group, "UseColorProfiles");
#if HAVE_LCMS
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), use_color);

	if (const auto status = layout_image_color_profile_get_status(lw); status.has_value())
		{
		g_autofree gchar *buf = g_strdup_printf(_("Image profile: %s\nScreen profile: %s"),
		                                        status->image_profile.c_str(), status->screen_profile.c_str());
		gq_gtk_action_set_tooltip(action, buf);
		}
	else
		{
		gq_gtk_action_set_tooltip(action, _("Click to enable color management"));
		}
#else
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), FALSE);
	gq_gtk_action_set_sensitive(action, FALSE);
	gq_gtk_action_set_tooltip(action, _("Color profiles not supported"));
#endif

	action = gq_gtk_action_group_get_action(lw->action_group, "UseImageProfile");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), use_image);
	gq_gtk_action_set_sensitive(action, use_color);

	for (i = 0; i < COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS; i++)
		{
		sprintf(action_name, "ColorProfile%d", i);
		action = gq_gtk_action_group_get_action(lw->action_group, action_name);

		if (i >= COLOR_PROFILE_FILE)
			{
			const gchar *name = options->color_profile.input_name[i - COLOR_PROFILE_FILE];
			const gchar *file = options->color_profile.input_file[i - COLOR_PROFILE_FILE];

			if (!name || !name[0]) name = filename_from_path(file);

			g_autofree gchar *end = layout_color_name_parse(name);
			g_autofree gchar *buf = g_strdup_printf(_("Input _%d: %s"), i, end);

			gq_gtk_action_set_label(action, buf);
			gq_gtk_action_set_visible(action, file && file[0]);
			}

		gq_gtk_action_set_sensitive(action, !use_image);
		gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), (i == input));
		}

	action = gq_gtk_action_group_get_action(lw->action_group, "Grayscale");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), layout_image_get_desaturate(lw));
}

void layout_util_sync_file_filter(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);
}

void layout_util_sync_marks(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowMarks");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.show_marks);
}

static void layout_util_sync_views(LayoutWindow *lw)
{
	GtkAction *action;
	OsdShowFlags osd_flags = image_osd_get(lw->image);

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "FolderTree");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.dir_view_type);

	action = gq_gtk_action_group_get_action(lw->action_group, "SplitSingle");
	gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(action), lw->split_mode);

	action = gq_gtk_action_group_get_action(lw->action_group, "SplitNextPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gq_gtk_action_group_get_action(lw->action_group, "SplitPreviousPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gq_gtk_action_group_get_action(lw->action_group, "SplitUpPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gq_gtk_action_group_get_action(lw->action_group, "SplitDownPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));

	action = gq_gtk_action_group_get_action(lw->action_group, "SplitPaneSync");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.split_pane_sync);

	action = gq_gtk_action_group_get_action(lw->action_group, "ViewIcons");
	gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(action), lw->options.file_view_type);

	action = gq_gtk_action_group_get_action(lw->action_group, "CropNone");
	gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(action), options->rectangle_draw_aspect_ratio);

	action = gq_gtk_action_group_get_action(lw->action_group, "OSD1");
	gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(action), options->overlay_screen_display_selected_profile);

	action = gq_gtk_action_group_get_action(lw->action_group, "FloatTools");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.tools_float);

	action = gq_gtk_action_group_get_action(lw->action_group, "SBar");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), layout_bar_enabled(lw));

	action = gq_gtk_action_group_get_action(lw->action_group, "SBarSort");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), layout_bar_sort_enabled(lw));

	action = gq_gtk_action_group_get_action(lw->action_group, "HideSelectableToolbars");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.selectable_toolbars_hidden);

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowInfoPixel");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.show_info_pixel);

	action = gq_gtk_action_group_get_action(lw->action_group, "SlideShow");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), layout_image_slideshow_active(lw));

	action = gq_gtk_action_group_get_action(lw->action_group, "IgnoreAlpha");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.ignore_alpha);

	action = gq_gtk_action_group_get_action(lw->action_group, "Animate");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.animate);

	action = gq_gtk_action_group_get_action(lw->action_group, "ImageOverlay");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), osd_flags != OSD_SHOW_NOTHING);

	action = gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), osd_flags & OSD_SHOW_HISTOGRAM);

	action = gq_gtk_action_group_get_action(lw->action_group, "ExifRotate");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), options->image.exif_rotate_enable);

	action = gq_gtk_action_group_get_action(lw->action_group, "OverUnderExposed");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), options->overunderexposed);

	action = gq_gtk_action_group_get_action(lw->action_group, "DrawRectangle");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), options->draw_rectangle);

	action = gq_gtk_action_group_get_action(lw->action_group, "RectangularSelection");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), options->collections.rectangular_selection);

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);

	action = gq_gtk_action_group_get_action(lw->action_group, "HideBars");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), (lw->options.bars_state.hidden));

	if (osd_flags & OSD_SHOW_HISTOGRAM)
		{
		action = gq_gtk_action_group_get_action(lw->action_group, "HistogramChanR");
		gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(action), image_osd_histogram_get_channel(lw->image));

		action = gq_gtk_action_group_get_action(lw->action_group, "HistogramModeLin");
		gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(action), image_osd_histogram_get_mode(lw->image));
		}

	action = gq_gtk_action_group_get_action(lw->action_group, "ConnectZoomMenu");
	gq_gtk_action_set_sensitive(action, lw->split_mode != SPLIT_NONE);

	// @todo `which` is deprecated, use command -v
	gboolean is_write_rotation = !runcmd("which exiftran >/dev/null 2>&1")
	                          && !runcmd("which mogrify >/dev/null 2>&1")
	                          && !options->metadata.write_orientation;
	action = gq_gtk_action_group_get_action(lw->action_group, "WriteRotation");
	gq_gtk_action_set_sensitive(action, is_write_rotation);
	action = gq_gtk_action_group_get_action(lw->action_group, "WriteRotationKeepDate");
	gq_gtk_action_set_sensitive(action, is_write_rotation);

	action = gq_gtk_action_group_get_action(lw->action_group, "StereoAuto");
	gq_gtk_radio_action_set_current_value(GQ_GTK_RADIO_ACTION(action), layout_image_stereo_pixbuf_get(lw));

	layout_util_sync_marks(lw);
	layout_util_sync_color(lw);
	layout_image_set_ignore_alpha(lw, lw->options.ignore_alpha);

	overlay_screen_display_profile_set(options->overlay_screen_display_selected_profile);
}

void layout_util_sync_thumb(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "Thumbnails");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.show_thumbnails);
	gq_gtk_action_set_sensitive(action, lw->options.file_view_type == FILEVIEW_LIST);
}

void layout_util_sync(LayoutWindow *lw)
{
	layout_util_sync_views(lw);
	layout_util_sync_thumb(lw);
}

gboolean is_help_key(guint keyval, GdkModifierType state)
{
	return is_help_key_impl(keyval, state);
}
#if !HAVE_GTK4
gboolean is_help_key(GdkEventKey *event)
{
	return is_help_key_impl(event->keyval, (GdkModifierType)event->state);
}
#endif

/*
 *-----------------------------------------------------------------------------
 * sidebars
 *-----------------------------------------------------------------------------
 */

static gboolean layout_bar_enabled(LayoutWindow *lw)
{
	return lw->bar && gtk_widget_get_visible(lw->bar);
}

static void layout_bar_destroyed(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	lw->bar = nullptr;
/*
    do not call layout_util_sync_views(lw) here
    this is called either when whole layout is destroyed - no need for update
    or when the bar is replaced - sync is called by upper function at the end of whole operation

*/
}

static void layout_bar_set_default(LayoutWindow *lw)
{
	GtkWidget *bar;

	if (!lw->utility_box) return;

	bar = bar_new(lw);
	DEBUG_NAME(bar);

	layout_bar_set(lw, bar);

	bar_populate_default(bar);
}

static void layout_bar_close(LayoutWindow *lw)
{
	if (lw->bar)
		{
		bar_close(lw->bar);
		lw->bar = nullptr;
		}
}


void layout_bar_set(LayoutWindow *lw, GtkWidget *bar)
{
	if (!lw->utility_box) return;

	layout_bar_close(lw); /* if any */

	if (!bar) return;
	lw->bar = bar;

	g_signal_connect(G_OBJECT(lw->bar), "destroy",
			 G_CALLBACK(layout_bar_destroyed), lw);

	gtk_paned_pack2(GTK_PANED(lw->utility_paned), lw->bar, FALSE, TRUE);

	bar_set_fd(lw->bar, layout_image_get_fd(lw));
}


void layout_bar_toggle(LayoutWindow *lw)
{
	if (layout_bar_enabled(lw))
		{
		gtk_widget_hide(lw->bar);
		}
	else
		{
		if (!lw->bar)
			{
			layout_bar_set_default(lw);
			}
		gtk_widget_show(lw->bar);
		bar_set_fd(lw->bar, layout_image_get_fd(lw));
		}
	layout_util_sync_views(lw);
}

static void layout_bar_new_image(LayoutWindow *lw)
{
	if (!layout_bar_enabled(lw)) return;

	bar_set_fd(lw->bar, layout_image_get_fd(lw));
}

static void layout_bar_new_selection(LayoutWindow *lw, gint count)
{
	if (!layout_bar_enabled(lw)) return;

	bar_notify_selection(lw->bar, count);
}

static gboolean layout_bar_sort_enabled(LayoutWindow *lw)
{
	return lw->bar_sort && gtk_widget_get_visible(lw->bar_sort);
}


static void layout_bar_sort_destroyed(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	lw->bar_sort = nullptr;

/*
    do not call layout_util_sync_views(lw) here
    this is called either when whole layout is destroyed - no need for update
    or when the bar is replaced - sync is called by upper function at the end of whole operation

*/
}

static void layout_bar_sort_set_default(LayoutWindow *lw)
{
	GtkWidget *bar;

	if (!lw->utility_box) return;

	bar = bar_sort_new_default(lw);

	layout_bar_sort_set(lw, bar);
}

static void layout_bar_sort_close(LayoutWindow *lw)
{
	if (lw->bar_sort)
		{
		bar_sort_close(lw->bar_sort);
		lw->bar_sort = nullptr;
		}
}

void layout_bar_sort_set(LayoutWindow *lw, GtkWidget *bar)
{
	if (!lw->utility_box) return;

	layout_bar_sort_close(lw); /* if any */

	if (!bar) return;
	lw->bar_sort = bar;

	g_signal_connect(G_OBJECT(lw->bar_sort), "destroy",
			 G_CALLBACK(layout_bar_sort_destroyed), lw);

	gq_gtk_box_pack_end(GTK_BOX(lw->utility_box), lw->bar_sort, FALSE, FALSE, 0);
}

void layout_bar_sort_toggle(LayoutWindow *lw)
{
	if (layout_bar_sort_enabled(lw))
		{
		gtk_widget_hide(lw->bar_sort);
		}
	else
		{
		if (!lw->bar_sort)
			{
			layout_bar_sort_set_default(lw);
			}
		gtk_widget_show(lw->bar_sort);
		}
	layout_util_sync_views(lw);
}

static void layout_bars_hide_toggle(LayoutWindow *lw)
{
	if (lw->options.bars_state.hidden)
		{
		lw->options.bars_state.hidden = FALSE;
		if (lw->options.bars_state.sort)
			{
			if (lw->bar_sort)
				{
				gtk_widget_show(lw->bar_sort);
				}
			else
				{
				layout_bar_sort_set_default(lw);
				}
			}
		if (lw->options.bars_state.info)
			{
			gtk_widget_show(lw->bar);
			}
		layout_tools_float_set(lw, lw->options.tools_float,
									lw->options.bars_state.tools_hidden);
		}
	else
		{
		lw->options.bars_state.hidden = TRUE;
		lw->options.bars_state.sort = layout_bar_sort_enabled(lw);
		lw->options.bars_state.info = layout_bar_enabled(lw);
		lw->options.bars_state.tools_float = lw->options.tools_float;
		lw->options.bars_state.tools_hidden = lw->options.tools_hidden;

		if (lw->bar)
			{
			gtk_widget_hide(lw->bar);
			}

		if (lw->bar_sort)
			gtk_widget_hide(lw->bar_sort);
		layout_tools_float_set(lw, lw->options.tools_float, TRUE);
		}

	layout_util_sync_views(lw);
}

void layout_bars_new_image(LayoutWindow *lw)
{
	layout_bar_new_image(lw);

	if (lw->exif_window) advanced_exif_set_fd(lw->exif_window, layout_image_get_fd(lw));

	/* this should be called here to handle the metadata edited in bars */
	if (options->metadata.confirm_on_image_change)
		metadata_write_queue_confirm(FALSE, nullptr);
}

void layout_bars_new_selection(LayoutWindow *lw, gint count)
{
	layout_bar_new_selection(lw, count);
}

GtkWidget *layout_bars_prepare(LayoutWindow *lw, GtkWidget *image)
{
	if (lw->utility_box) return lw->utility_box;
	lw->utility_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	lw->utility_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(lw->utility_paned);
	gq_gtk_box_pack_start(GTK_BOX(lw->utility_box), lw->utility_paned, TRUE, TRUE, 0);

	/* Prevent the info sidebar being minimized to invisible
	 */
	gtk_paned_set_wide_handle(GTK_PANED(lw->utility_paned), TRUE);

	gtk_paned_pack1(GTK_PANED(lw->utility_paned), image, TRUE, FALSE);
	gtk_widget_show(lw->utility_paned);

	gtk_widget_show(image);

	return g_object_ref(lw->utility_box);
}

void layout_bars_close(LayoutWindow *lw)
{
	layout_bar_sort_close(lw);
	layout_bar_close(lw);
}

static gboolean layout_exif_window_destroy(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	lw->exif_window = nullptr;

	return TRUE;
}

void layout_exif_window_new(LayoutWindow *lw)
{
	if (lw->exif_window) return;

	lw->exif_window = advanced_exif_new(lw);
	if (!lw->exif_window) return;
	g_signal_connect(G_OBJECT(lw->exif_window), "destroy",
			 G_CALLBACK(layout_exif_window_destroy), lw);
	advanced_exif_set_fd(lw->exif_window, layout_image_get_fd(lw));
}

static void layout_search_and_run_window_new(LayoutWindow *lw)
{
	if (lw->sar_window)
		{
		gtk_window_present(GTK_WINDOW(lw->sar_window));
		return;
		}

	lw->sar_window = search_and_run_new(lw);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
