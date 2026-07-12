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

#include "accelerators.h"
#include "actions.h"
#include "advanced-exif.h"
#include "archives.h"
#include "bar-keywords.h"
#include "bar-sort.h"
#include "bar.h"
#include "cache-maint.h"
#include "collect-io.h"
#include "collect.h"
#include "color-man.h"
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
#include "keyboard-shortcuts.h"
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

extern const ActionDef app_actions[];

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

GPtrArray *plugin_accel_actions = nullptr;

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

void keyboard_scroll_calc(gint &x, gint &y, GdkModifierType state, guint keyval, guint32 time)
{
	static gint delta = 0;
	static guint32 time_old = 0;
	static guint keyval_old = 0;

	if (state & GDK_SHIFT_MASK)
		{
		x *= 3;
		y *= 3;
		}

	if (state & GDK_CONTROL_MASK)
		{
		if (x < 0) x = G_MININT / 2;
		if (x > 0) x = G_MAXINT / 2;
		if (y < 0) y = G_MININT / 2;
		if (y > 0) y = G_MAXINT / 2;

		return;
		}

	if (options->progressive_key_scrolling)
		{
		guint32 time_diff = time - time_old;

		/* key pressed within 125 ms? */
		if (time_diff > 125 || keyval != keyval_old)
			{
			delta = 0;
			}

		time_old = time;
		keyval_old = keyval;

		delta += 2;
		}
	else
		{
		delta = 8;
		}

	x *= delta * options->keyboard_scroll_step;
	y *= delta * options->keyboard_scroll_step;
}

static gboolean layout_key_press_common(GtkWidget *widget, guint keyval, GdkModifierType state, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->path_entry && gtk_widget_has_focus(lw->path_entry))
		{
		if (keyval == GDK_KEY_Escape && lw->dir_fd)
			{
			gq_gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);
			return TRUE;
			}

		}

	if (lw->vf->file_filter.combo)
		{
		GtkWidget *combo_entry = gtk_combo_box_get_child(GTK_COMBO_BOX(lw->vf->file_filter.combo));

		if (combo_entry && gtk_widget_has_focus(combo_entry))
			{
			return FALSE;
			}
		}

	GtkWidget *focused = nullptr;

	gboolean stop_signal = FALSE;
	gint x = 0;
	gint y = 0;

	if (lw->image &&
	    ((focused && gtk_widget_has_focus(focused)) ||
	     (lw->tools && widget == lw->window) ||
	     lw->full_screen))
		{
		stop_signal = TRUE;

		switch (keyval)
			{
			case GDK_KEY_Left:
			case GDK_KEY_KP_Left:
				x -= 1;
				break;

			case GDK_KEY_Right:
			case GDK_KEY_KP_Right:
				x += 1;
				break;

			case GDK_KEY_Up:
			case GDK_KEY_KP_Up:
				y -= 1;
				break;

			case GDK_KEY_Down:
			case GDK_KEY_KP_Down:
				y += 1;
				break;

			default:
				stop_signal = FALSE;
				break;
			}

		if (!stop_signal && !(state & GDK_CONTROL_MASK))
			{
			stop_signal = TRUE;

			switch (keyval)
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

	if (x != 0 || y != 0)
		{
		keyboard_scroll_calc(x, y, state, keyval, static_cast<guint32>(g_get_monotonic_time() / 1000));
		layout_image_scroll(lw, x, y, (state & GDK_SHIFT_MASK));
		}

	return stop_signal;
}

static gboolean layout_key_press_cb(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	(void)keycode;
	return layout_key_press_common(widget, keyval, state, data);
}

void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window)
{
	GtkEventController *controller = gtk_event_controller_key_new();
	g_signal_connect(controller, "key-pressed", G_CALLBACK(layout_key_press_cb), lw);
	gtk_widget_add_controller(window, controller);
}

bool layout_handle_user_defined_mouse_buttons(LayoutWindow *lw, guint button)
{
	enum MouseButton
		{
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
/** @FIXME GTK4
			GtkAction *action = deprecated_gtk_action_group_get_action(lw->action_group, action_name);
			if (action)
				{
				g_action_activate(action, nullptr);
				}
*/
			}

		return true;
		};

	switch (button)
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
	if (!lw) return;
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

static void layout_menu_clear_marks_cb(GSimpleAction *, GVariant *, gpointer)
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

static void layout_menu_new_collection_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);

		collection_window_new(nullptr);
}

static void layout_menu_search_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	search_new(lw->dir_fd, layout_image_get_fd(lw));
}

static void layout_menu_dupes_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	dupe_window_new();
}

static void layout_menu_pan_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	pan_window_new(lw->dir_fd);
}

static void layout_menu_print_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	print_window_new(layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_dir_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	if (lw->vd) vd_new_folder(lw->vd, lw->dir_fd);
}

static void layout_menu_copy_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();


		file_util_copy(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

template<gboolean quoted>
static void layout_menu_copy_path_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	file_util_path_list_to_clipboard(layout_selection_list(lw), quoted, ClipboardAction::COPY);
}

static void layout_menu_copy_image_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	ImageWindow *imd = lw->image;

	GdkPixbuf *pixbuf;
	pixbuf = image_get_pixbuf(imd);
	if (!pixbuf) return;

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
}

static void layout_menu_cut_path_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	file_util_path_list_to_clipboard(layout_selection_list(lw), FALSE, ClipboardAction::CUT);
}

static void layout_menu_move_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

		file_util_move(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

static void layout_menu_rename_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

		file_util_rename(nullptr, layout_selection_list(lw), layout_window(lw));
}

template<gboolean safe_delete>
static void layout_menu_delete_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

		file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw), safe_delete);
}

static void layout_menu_return_cb(GSimpleAction *, GVariant *, gpointer)
{

}

static void layout_menu_return_primary_cb(GSimpleAction *, GVariant *, gpointer)
{

}

static void layout_menu_remove_cb(GSimpleAction *, GVariant *, gpointer)
{

}

template<gboolean disable>
static void layout_menu_disable_grouping_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	file_data_disable_grouping_list(layout_selection_list(lw), disable);
}

void layout_menu_close_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	layout_close(lw);
}

static void layout_menu_exit_cb(GSimpleAction *, GVariant *, gpointer)
{
	exit_program();
}

template<AlterType type>
static void layout_menu_alter_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_image_alter_orientation(lw, type);
}

template<int rating>
static void layout_menu_rating_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	layout_image_rating(lw, std::to_string(rating).c_str());
}

static void layout_menu_alter_desaturate_cb(GSimpleAction *action, GVariant  *, gpointer  )
{
	auto lw = get_current_layout();

	layout_image_set_desaturate(lw, g_variant_get_boolean(g_action_get_state(G_ACTION(action))));
}

static void layout_menu_alter_ignore_alpha_cb(GSimpleAction *action, GVariant   *, gpointer)
{
   auto lw = get_current_layout();

	if (lw->options.ignore_alpha == g_variant_get_boolean(g_action_get_state(G_ACTION(action)))) return;

   layout_image_set_ignore_alpha(lw, g_variant_get_boolean(g_action_get_state(G_ACTION(action))));
}

static void layout_menu_exif_rotate_cb(GSimpleAction *action, GVariant *value, gpointer)
{
	auto lw = get_current_layout();

	gboolean active = g_variant_get_boolean(value);
	g_simple_action_set_state(G_SIMPLE_ACTION(action), value);

	options->image.exif_rotate_enable = !active;
	layout_image_reset_orientation(lw);
}

static void layout_menu_select_rectangle_cb(GSimpleAction *action, GVariant *value, gpointer)
{
	gboolean active = g_variant_get_boolean(value);
	g_simple_action_set_state(G_SIMPLE_ACTION(action), value);

	options->draw_rectangle = !active;
}

static void layout_menu_split_pane_sync_cb(GSimpleAction *action, GVariant *, gpointer       data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	GVariant *state = g_action_get_state(G_ACTION(action));
	gboolean active = !g_variant_get_boolean(state);
	g_variant_unref(state);

	g_simple_action_set_state(action, g_variant_new_boolean(active));

	lw->options.split_pane_sync = active;
}

static void layout_menu_select_overunderexposed_cb(GSimpleAction *action, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_image_set_overunderexposed(lw, g_variant_get_boolean(g_action_get_state(G_ACTION(action))));
}

template<bool keep_date>
static void layout_menu_write_rotate_cb(GSimpleAction  *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	if (!layout_valid(&lw)) return;
	if (!lw || !lw->vf) return;

	const char *keep_date_arg = keep_date ? "-t" : "";

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

static void layout_menu_config_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();


	layout_exit_fullscreen(lw);
	show_config_window(lw);
}

static void layout_menu_editors_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	show_editor_list_window();
}

static void layout_menu_layout_config_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	layout_show_config_window(lw);
}

static void layout_menu_remove_thumb_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	cache_manager_show();
}

template<gboolean connect_zoom>
static void layout_menu_zoom_in_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	layout_image_zoom_adjust(lw, get_zoom_increment(), connect_zoom);
}

template<gboolean connect_zoom>
static void layout_menu_zoom_out_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	layout_image_zoom_adjust(lw, -get_zoom_increment(), connect_zoom);
}

template<int value, gboolean connect_zoom>
static void layout_menu_zoom_set_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	layout_image_zoom_set(lw, value, connect_zoom);
}

template<gboolean vertical, gboolean connect_zoom>
static void layout_menu_zoom_fit_hv_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	layout_image_zoom_set_fill_geometry(lw, vertical, connect_zoom);
}

static void layout_menu_zoom_to_rectangle_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

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

static void layout_menu_split_cb(GSimpleAction *, GVariant *state, gpointer)
{
	auto lw = get_current_layout();
	ImageSplitMode mode;

	layout_exit_fullscreen(lw);

	const char *value = g_variant_get_string(state, nullptr);

	if (g_str_equal(value, "none"))
		mode = SPLIT_NONE;
	else if (g_str_equal(value, "vertical"))
		mode = SPLIT_VERT;
	else if (g_str_equal(value, "horizontal"))
		mode = SPLIT_HOR;
	else if (g_str_equal(value, "triple"))
		mode = SPLIT_TRIPLE;
	else if (g_str_equal(value, "quad"))
		mode = SPLIT_QUAD;
	else
		mode = SPLIT_NONE;

	layout_split_change(lw, mode);
}

static void layout_menu_thumb_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();
	bool enabled = g_variant_get_boolean(state);

	layout_thumb_set(lw, enabled);

	g_simple_action_set_state(action, g_variant_new_boolean(!enabled));
}

static void layout_menu_list_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);

	const char *value = g_variant_get_string(state, nullptr);

	if (g_str_equal(value, "list"))
		{
		layout_views_set(lw, lw->options.dir_view_type, FILEVIEW_LIST);
		g_simple_action_set_state(G_SIMPLE_ACTION (action), g_variant_new_string("list"));

		}
	else
		{
		layout_views_set(lw, lw->options.dir_view_type, FILEVIEW_ICON);
		g_simple_action_set_state(G_SIMPLE_ACTION (action), g_variant_new_string("icons"));
		}
}

static void layout_menu_view_dir_as_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();
	if (!lw)
		{
		return;
		}

	layout_exit_fullscreen(lw);

	bool active = g_variant_get_boolean(state);

	if (active)
		{
		layout_views_set(lw, DIRVIEW_LIST, lw->options.file_view_type);
		}
	else
		{
		layout_views_set(lw, DIRVIEW_TREE, lw->options.file_view_type);
		}

	g_simple_action_set_state((action), state);
}

static void layout_menu_view_in_new_window_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

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

static void layout_menu_open_with_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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

static void layout_menu_open_archive_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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

static void open_file_cb(GFile *file, gpointer)
{
	if (file)
		{
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
}

static void open_recent_path(const gchar *file_name)
{
	if (!file_name)
		{
		return;
		}

	if (g_str_has_suffix(file_name, GQ_COLLECTION_EXT))
		{
		collection_window_new(file_name);
		}
	else
		{
		layout_set_path(get_current_layout(), file_name);
		}
}

struct OpenRecentDialogData
{
	GenericDialog *gd;
	GtkWidget *list;
	GtkWidget *open_button;
};

static void open_recent_dialog_data_free(OpenRecentDialogData *dialog_data)
{
	if (!dialog_data) return;

	generic_dialog_close(dialog_data->gd);
	g_free(dialog_data);
}

static void open_recent_dialog_update(OpenRecentDialogData *dialog_data)
{
	if (!dialog_data->open_button)
		{
		return;
		}

	GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(dialog_data->list));
	gtk_widget_set_sensitive(dialog_data->open_button, row != nullptr);
}

static void open_recent_dialog_open(OpenRecentDialogData *dialog_data)
{
	GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(dialog_data->list));
	if (!row)
		{
		return;
		}

	const auto *path = static_cast<const gchar *>(g_object_get_data(G_OBJECT(row), "recent-path"));
	open_recent_path(path);
	open_recent_dialog_data_free(dialog_data);
}

static void open_recent_dialog_cancel_cb(GenericDialog *, gpointer data)
{
	open_recent_dialog_data_free(static_cast<OpenRecentDialogData *>(data));
}

static void open_recent_dialog_open_cb(GenericDialog *, gpointer data)
{
	open_recent_dialog_open(static_cast<OpenRecentDialogData *>(data));
}

static void open_recent_dialog_row_selected_cb(GtkListBox *, GtkListBoxRow *, gpointer data)
{
	open_recent_dialog_update(static_cast<OpenRecentDialogData *>(data));
}

static void open_recent_dialog_row_activated_cb(GtkListBox *, GtkListBoxRow *, gpointer data)
{
	open_recent_dialog_open(static_cast<OpenRecentDialogData *>(data));
}

static void layout_menu_open_file_cb(GSimpleAction *, GVariant *, gpointer)
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

	FileDialogData fdd{};

	fdd.action = FileDialogAction::OPEN;
	fdd.accept_text = _("Open");
	fdd.callback = open_file_cb;
	fdd.filter = extlist->str;
	fdd.filter_description = _("Image files");
	fdd.history_key = "open_file";
	fdd.suggested_name = _("Untitled.gqv");
	fdd.title = _("Geeqie - Open File");

	file_dialog_show(fdd);
}

static void layout_menu_open_recent_file_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto *dialog_data = g_new0(OpenRecentDialogData, 1);
	dialog_data->gd = generic_dialog_new(_("Open Recent File"),
					     "open_recent_file",
					     get_current_layout()->window,
					     FALSE,
					     open_recent_dialog_cancel_cb,
					     dialog_data);

	gtk_window_set_default_size(GTK_WINDOW(dialog_data->gd->dialog), 700, 400);

	dialog_data->list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(dialog_data->list), GTK_SELECTION_SINGLE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(dialog_data->list), FALSE);
	g_signal_connect(dialog_data->list, "row-selected",
			 G_CALLBACK(open_recent_dialog_row_selected_cb), dialog_data);
	g_signal_connect(dialog_data->list, "row-activated",
			 G_CALLBACK(open_recent_dialog_row_activated_cb), dialog_data);
	gq_gtk_box_pack_start(GTK_BOX(dialog_data->gd->vbox), dialog_data->list, TRUE, TRUE, 0);

	HistoryList *recent_items = history_list_find_by_key("recent");

	if (recent_items)
		{
		for (const auto &path : *recent_items)
			{
			if (!isfile(path.c_str()))
				{
				continue;
				}

			GtkWidget *row = gtk_list_box_row_new();
			GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
			g_autofree gchar *basename = g_path_get_basename(path.c_str());

			GtkWidget *name_label = gtk_label_new(basename);
			gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
			gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
			gtk_widget_add_css_class(name_label, "heading");
			gtk_box_append(GTK_BOX(box), name_label);

			GtkWidget *path_label = gtk_label_new(path.c_str());
			gtk_label_set_xalign(GTK_LABEL(path_label), 0.0);
			gtk_label_set_wrap(GTK_LABEL(path_label), TRUE);
			gtk_label_set_wrap_mode(GTK_LABEL(path_label), PANGO_WRAP_WORD_CHAR);
			gtk_widget_add_css_class(path_label, "dim-label");
			gtk_box_append(GTK_BOX(box), path_label);

			gq_gtk_widget_set_border_width(box, 6);
			gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
			g_object_set_data_full(G_OBJECT(row), "recent-path", g_strdup(path.c_str()), g_free);
			gtk_list_box_append(GTK_LIST_BOX(dialog_data->list), row);
			}
		}

	if (gtk_widget_get_first_child(dialog_data->list) == nullptr)
		{
		GtkWidget *label = gtk_label_new(_("No recent files available."));
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gq_gtk_box_pack_start(GTK_BOX(dialog_data->gd->vbox), label, FALSE, FALSE, 0);
		}
	else
		{
		gtk_list_box_select_row(GTK_LIST_BOX(dialog_data->list),
					GTK_LIST_BOX_ROW(gtk_widget_get_first_child(dialog_data->list)));
		}

	dialog_data->open_button = generic_dialog_add_button(dialog_data->gd,
							     GQ_ICON_OPEN,
							     _("_Open"),
							     open_recent_dialog_open_cb,
							     TRUE);
	gtk_widget_set_sensitive(dialog_data->open_button, FALSE);
	open_recent_dialog_update(dialog_data);

	gtk_widget_show(dialog_data->list);
	gtk_widget_show(dialog_data->gd->dialog);
}

static void open_collection_cb(GFile *file, gpointer)
{
	if (file)
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

static void layout_menu_open_collection_cb(GSimpleAction *, GVariant *, gpointer)
{
	FileDialogData fdd{};

	fdd.action = FileDialogAction::OPEN;
	fdd.accept_text = _("Open");
	fdd.callback = open_collection_cb;
	fdd.filter = GQ_COLLECTION_EXT;
	fdd.filter_description = _("Collection files");
	fdd.history_key = "open_collection";
	fdd.filename = get_collections_dir();
	fdd.title = _("Geeqie - Open Collection");

	file_dialog_show(fdd);
}

static void layout_menu_fullscreen_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_image_full_screen_toggle(lw);
}

static void layout_menu_escape_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
}

static void layout_menu_overlay_toggle_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	image_osd_toggle(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_overlay_cb(GSimpleAction *action, GVariant *value, gpointer)
{
	auto lw = get_current_layout();

	gboolean active = g_variant_get_boolean(value);
	g_simple_action_set_state(G_SIMPLE_ACTION(action), value);

	if (active)
		{
		OsdShowFlags flags = image_osd_get(lw->image);

		if ((flags | OSD_SHOW_INFO | OSD_SHOW_STATUS) != flags)
			image_osd_set(lw->image, static_cast<OsdShowFlags>(flags | OSD_SHOW_INFO | OSD_SHOW_STATUS));
		}
	else
		{

		image_osd_set(lw->image, OSD_SHOW_NOTHING);
		}
}

static void layout_menu_histogram_cb(GSimpleAction *action, GVariant *value , gpointer)
{
	auto lw = get_current_layout();

	gboolean active = g_variant_get_boolean(value);
	g_simple_action_set_state(G_SIMPLE_ACTION(action), value);

	if (!active)
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

static void layout_menu_animate_cb(GSimpleAction *action, GVariant *value, gpointer)
{
	auto lw = get_current_layout();

	layout_image_animate_toggle(lw);

	g_simple_action_set_state(action,  value);
}

static void layout_menu_rectangular_selection_cb(GSimpleAction *action, GVariant *state, gpointer)
{

	bool enabled = g_variant_get_boolean(state);

	options->collections.rectangular_selection = enabled;

   g_simple_action_set_state(action, g_variant_new_boolean(enabled));
}

static void layout_menu_histogram_toggle_channel_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	image_osd_histogram_toggle_channel(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_histogram_toggle_mode_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	image_osd_histogram_toggle_mode(lw->image);
	layout_util_sync_views(lw);
}

static gint histogram_mode_from_string(const gchar *value)
{
	if (g_str_equal(value, "linear")) return HMODE_LINEAR;
	if (g_str_equal(value, "log")) return HMODE_LOG;

	return -1;
}

static gint histogram_channel_from_string(const gchar *value)
{
	if (g_str_equal(value, "red")) return HCHAN_R;
	if (g_str_equal(value, "green")) return HCHAN_G;
	if (g_str_equal(value, "blue")) return HCHAN_B;
	if (g_str_equal(value, "rgb")) return HCHAN_RGB;
	if (g_str_equal(value, "value")) return HCHAN_MAX;

	return -1;
}

static const gchar *histogram_channel_to_string(gint channel)
{
	switch (channel)
		{
		case HCHAN_R:
			return "red";
		case HCHAN_G:
			return "green";
		case HCHAN_B:
			return "blue";
		case HCHAN_RGB:
			return "rgb";
		case HCHAN_MAX:
			return "value";
		default:
			return "rgb";
		}
}

static const gchar *histogram_mode_to_string(gint mode)
{
	switch (mode)
		{
		case HMODE_LINEAR:
			return "linear";
		case HMODE_LOG:
			return "log";
		default:
			return "linear";
		}
}

static void layout_menu_histogram_channel_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	const gchar *value = g_variant_get_string(state, nullptr);
	gint channel = histogram_channel_from_string(value);
	if (channel < 0 || channel >= HCHAN_COUNT) return;

	auto lw = get_current_layout();

	g_simple_action_set_state(action, g_variant_new_string(value));

	OsdShowFlags flags = image_osd_get(lw->image);
	if (!(flags & OSD_SHOW_HISTOGRAM))
		{
		image_osd_set(lw->image, static_cast<OsdShowFlags>(flags | OSD_SHOW_INFO | OSD_SHOW_STATUS | OSD_SHOW_HISTOGRAM));
		}

	image_osd_histogram_set_channel(lw->image, channel);
}

static void layout_menu_histogram_mode_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	const gchar *value = g_variant_get_string(state, nullptr);
	gint mode = histogram_mode_from_string(value);
	if (mode < 0 || mode >= HMODE_COUNT) return;

	auto lw = get_current_layout();

	g_simple_action_set_state(action, g_variant_new_string(value));

	image_osd_histogram_set_mode(lw->image, mode);
}

static void layout_menu_refresh_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_refresh(lw);
}

static void layout_menu_bar_exif_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	layout_exif_window_new(lw);
}

static void layout_menu_search_and_run_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	layout_search_and_run_window_new(lw);
}


static void layout_menu_float_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();
	layout_exit_fullscreen(lw);
	layout_tools_float_toggle(lw);

	g_simple_action_set_state((action), state);
}

static void layout_menu_hide_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	layout_tools_hide_toggle(lw);
}

static void layout_menu_selectable_toolbars_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();

	if (lw->options.selectable_toolbars_hidden == g_variant_get_boolean(g_action_get_state(G_ACTION(action)))) return;

	layout_exit_fullscreen(lw);
	current_layout_selectable_toolbars_toggle();

	g_simple_action_set_state(action, state);
}

static void layout_menu_info_pixel_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();

	if (lw->options.show_info_pixel == g_variant_get_boolean(g_action_get_state(G_ACTION(action)))) return;

	layout_exit_fullscreen(lw);
	layout_info_pixel_set(lw, !lw->options.show_info_pixel);

	g_simple_action_set_state(action, state);
}

/* NOTE: these callbacks are called also from layout_util_sync_views */
static void layout_menu_bar_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);

	layout_bar_toggle(lw);

}

static void layout_menu_bar_sort_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	layout_bar_sort_toggle(lw);

}

static void layout_menu_hide_bars_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();

	layout_bars_hide_toggle(lw);

	bool enabled = g_variant_get_boolean(state);
	g_simple_action_set_state(action, g_variant_new_boolean(!enabled));
}

static void layout_menu_slideshow_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();

	layout_image_slideshow_toggle(lw);

	bool enabled = g_variant_get_boolean(state);
	g_simple_action_set_state(action, g_variant_new_boolean(!enabled));
}

static void layout_menu_slideshow_pause_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_image_slideshow_pause_toggle(lw);
}

static void layout_menu_slideshow_slower_cb(GSimpleAction *, GVariant *, gpointer)
{
	options->slideshow.delay = std::min<gdouble>(options->slideshow.delay + 5, SLIDESHOW_MAX_SECONDS);
}

static void layout_menu_slideshow_faster_cb(GSimpleAction *, GVariant *, gpointer)
{
	options->slideshow.delay = std::max<gdouble>(options->slideshow.delay - 5, SLIDESHOW_MIN_SECONDS * 10);
}

static void layout_menu_stereo_mode_next_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const char* next_value;

	/* 0->1, 1->2, 2->3, 3->1 - disable auto, then cycle */

	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-stereo");

	GVariant *state = g_action_get_state(G_ACTION(action));
	const gchar *value = g_variant_get_string(state, nullptr);


	if (g_str_equal(value, "auto"))
		next_value = "side-by-side";
	else if (g_str_equal(value, "side-by-side"))
		next_value = "cross";
	else if (g_str_equal(value, "cross"))
		next_value = "off";
	else if (g_str_equal(value, "off"))
		next_value = "auto";
	else
		next_value = "auto";

	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string(next_value));

	/*
	this is called via fallback in layout_menu_stereo_mode_cb
	layout_image_stereo_pixbuf_set(lw, mode);
	*/
}

static const char *stereo_mode_to_string(StereoPixbufData mode)
{
	switch (mode)
		{
		case STEREO_PIXBUF_DEFAULT: return "auto";
		case STEREO_PIXBUF_SBS:     return "side-by-side";
		case STEREO_PIXBUF_CROSS:   return "cross";
		case STEREO_PIXBUF_NONE:    return "none";
		}

	return "auto";
}

static void layout_menu_stereo_mode_cb(GSimpleAction *action, GVariant *state, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const char *value = g_variant_get_string(state, nullptr);
	StereoPixbufData mode;

	if (g_str_equal(value, "auto"))
		mode = STEREO_PIXBUF_DEFAULT;
	else if (g_str_equal(value, "side-by-side"))
		mode = STEREO_PIXBUF_SBS;
	else if (g_str_equal(value, "cross"))
		mode = STEREO_PIXBUF_CROSS;
	else if (g_str_equal(value, "off"))
		mode = STEREO_PIXBUF_NONE;
	else
		mode = STEREO_PIXBUF_DEFAULT;

	g_simple_action_set_state(action, g_variant_new_string(value));

	layout_image_stereo_pixbuf_set(lw, mode);
}

static void layout_menu_draw_rectangle_aspect_ratio_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	const char *value = g_variant_get_string(state, nullptr);

	RectangleDrawAspectRatio mode;

	if (g_str_equal(value, "none"))
		mode = RECTANGLE_DRAW_ASPECT_RATIO_NONE;
	else if (g_str_equal(value, "1-1"))
		mode = RECTANGLE_DRAW_ASPECT_RATIO_ONE_ONE;
	else if (g_str_equal(value, "4-3"))
		mode = RECTANGLE_DRAW_ASPECT_RATIO_FOUR_THREE;
	else if (g_str_equal(value, "3-2"))
		mode = RECTANGLE_DRAW_ASPECT_RATIO_THREE_TWO;
	else if (g_str_equal(value, "16-9"))
		mode = RECTANGLE_DRAW_ASPECT_RATIO_SIXTEEN_NINE;
	else
		mode = RECTANGLE_DRAW_ASPECT_RATIO_NONE;

	g_simple_action_set_state((action), state);

	options->rectangle_draw_aspect_ratio = mode;
}

static void overlay_screen_display_profile_set(gint i)
{
	g_free(options->image_overlay.template_string);
	g_free(options->image_overlay.font);

	options->image_overlay = options->image_overlay_n[i];

	options->image_overlay.template_string = g_strdup(options->image_overlay_n[i].template_string);
	options->image_overlay.font = g_strdup(options->image_overlay_n[i].font);
}

static void layout_menu_osd_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();

	const char *value = g_variant_get_string(state, nullptr);
	OverlayScreenDisplaySelectedTab mode;
	
	if (g_str_equal(value, "0"))
		mode = OVERLAY_SCREEN_DISPLAY_1;
	else if (g_str_equal(value, "1"))
		mode = OVERLAY_SCREEN_DISPLAY_2;
	else if (g_str_equal(value, "2"))
		mode = OVERLAY_SCREEN_DISPLAY_3;
	else if (g_str_equal(value, "3"))
		mode = OVERLAY_SCREEN_DISPLAY_4;
	else
		mode = OVERLAY_SCREEN_DISPLAY_1;

options->overlay_screen_display_selected_profile = mode;

		g_simple_action_set_state((action), state);

	overlay_screen_display_profile_set(options->overlay_screen_display_selected_profile);

	layout_image_refresh(lw);
}

static void layout_menu_help_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);

	GApplication *app = g_application_get_default();
	GtkWindow *win = gtk_application_get_active_window(GTK_APPLICATION(app));

	const char *type = static_cast<gchar *>(g_object_get_data(G_OBJECT(win), "window-type"));

	if (g_strcmp0(type, "search") == 0)
		{
		help_window_show("GuideImageSearchSearch.html");
		}
	else
		{
		help_window_show("index.html");
		}
}

static void layout_menu_help_search_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	help_search_window_show();
}

static void layout_menu_help_pdf_cb(GSimpleAction *, GVariant *, gpointer)
{
	help_pdf();
}

static void layout_menu_help_keys_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);

	shortcuts_window_new_from_keyfile();
}

static void layout_menu_notes_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	help_window_show("release_notes");
}

static void layout_menu_changelog_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

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

	return g_strconcat(pre_key[0], ">", menu_name, "<", post_key[1], "\n", nullptr);
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

static void layout_menu_kbd_map_cb(GSimpleAction *, GVariant *, gpointer)
{
	g_autofree gchar *tmp_file = nullptr;
	g_autoptr(GError) error = nullptr;

	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);

	const gint fd = g_file_open_tmp("geeqie_keymap_XXXXXX.svg", &tmp_file, &error);
	if (error)
		{
		log_printf("Error: Keyboard Map - cannot create file:%s\n", error->message);
		return;
		}

	get_actions_and_accelerators(get_keyfile_merged(), array);

	convert_keymap_template_to_file(fd, array);

	view_window_new(file_data_new_simple(tmp_file));
}

static void layout_menu_about_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	show_about_window(lw);
}

static void layout_menu_crop_selection_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	start_editor_from_file("org.geeqie.image-crop.desktop", lw->image->image_fd);
}

static void layout_menu_log_window_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_exit_fullscreen(lw);
	log_window_new(lw);
}


/*
 *-----------------------------------------------------------------------------
 * select menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_select_all_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_select_all(lw);
}

static void layout_menu_unselect_all_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_select_none(lw);
}

static void layout_menu_invert_selection_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	layout_select_invert(lw);
}

static void layout_menu_file_filter_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();

	bool active = g_variant_get_boolean(state);

	g_simple_action_set_state((action), state);

	layout_file_filter_set(lw, active);
}

static void layout_menu_marks_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();

	bool active = g_variant_get_boolean(state);

	g_simple_action_set_state( (action), state);

	layout_marks_set(lw, active);
}

template<SelectionToMarkMode mode, int mark>
static void layout_menu_selection_to_mark_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, mode);
}

template<MarkToSelectionMode mode, int mark>
static void layout_menu_mark_to_selection_cb(GSimpleAction  * , GVariant *, gpointer  )
{
	auto lw = get_current_layout();
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, mode);
}

template<int mark>
static void layout_menu_mark_filter_toggle_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_marks_set(lw, TRUE);
	layout_mark_filter_toggle(lw, mark);
}


/*
 *-----------------------------------------------------------------------------
 * go menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_image_first_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	layout_image_first(lw);
}

static void layout_menu_image_prev_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

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

static void layout_menu_image_next_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

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

static void layout_menu_page_first_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_set_page_num(fd, 1);
		}
}

static void layout_menu_page_last_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_set_page_num(fd, -1);
		}
}

static void layout_menu_page_next_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_inc_page_num(fd);
		}
}

static void layout_menu_page_previous_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	FileData *fd = layout_image_get_fd(lw);

	if (fd && fd->page_total > 0)
		{
		file_data_dec_page_num(fd);
		}
}

static void layout_menu_image_forward_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	/* Obtain next image */
	layout_set_path(lw, image_chain_forward());
}

static void layout_menu_image_back_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();

	/* Obtain previous image */
	layout_set_path(lw, image_chain_back());
}

static void layout_menu_split_pane_next_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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

static void layout_menu_split_pane_prev_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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

static void layout_menu_split_pane_updown_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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

static void layout_menu_image_last_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	layout_image_last(lw);
}

static void layout_menu_back_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	FileData *dir_fd;

	/* Obtain previous path */
	dir_fd = file_data_new_dir(history_chain_back());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_forward_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
	FileData *dir_fd;

	/* Obtain next path */
	dir_fd = file_data_new_dir(history_chain_forward());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_home_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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

static void layout_menu_up_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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




static void layout_menu_metadata_write_cb(GSimpleAction *, GVariant *, gpointer)
{
	metadata_write_queue_confirm(TRUE, nullptr);
}

static GtkWidget *last_focussed = nullptr;
static void layout_menu_keyword_autocomplete_cb(GSimpleAction *, GVariant *, gpointer)
{
	auto lw = get_current_layout();
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
static void layout_color_menu_enable_cb(GSimpleAction *action, GVariant *state, gpointer)
{
	auto lw = get_current_layout();


	layout_util_sync_color(lw);
	layout_image_refresh(lw);

	g_simple_action_set_state(action, state);
}

static void layout_color_menu_use_image_cb(GSimpleAction *action, GVariant *parameter, gpointer)
{
	auto lw = get_current_layout();
	gint input;
	gboolean use_image;

	gboolean active = g_variant_get_boolean(parameter);

	if (!layout_image_color_profile_get(lw, input, use_image)) return;

	if (use_image == active) return;

	layout_image_color_profile_set(lw, input, active);
	layout_util_sync_color(lw);
	layout_image_refresh(lw);

	g_simple_action_set_state(action, g_variant_new_boolean(active));
}

static void layout_color_menu_input_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint input;
	gboolean use_image;

	gint32 type = g_variant_get_int32(parameter);

	if (type < 0 || type >= COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS) return;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;
	if (type == input) return;

	layout_image_color_profile_set(lw, type, use_image);
	layout_image_refresh(lw);
}
#else
static void layout_color_menu_enable_cb(GSimpleAction *action, GVariant *parameter, gpointer)
{
}

static void layout_color_menu_use_image_cb(GSimpleAction *action, GVariant *parameter, gpointer)
{
}

static void layout_color_menu_input_cb(GSimpleAction *action, GVariant *parameter, gpointer)
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
			wn->path = g_build_filename(pathl, name_utf8, nullptr);

			list = g_list_prepend(list, wn);
			}
		}
	closedir(dp);

	return g_list_sort(list, layout_window_menu_list_sort_cb);
}

static void layout_menu_new_window_update(LayoutWindow *lw)
{
	if (!lw || !lw->builder) return;

	g_autolist(WindowNames) list = layout_window_menu_list();

	GMenu *layouts_section = G_MENU(gtk_builder_get_object(lw->builder, "new-window-layouts-section"));
	if (!layouts_section) return;

	g_menu_remove_all(layouts_section);
	for (GList *work = list; work; work = work->next)
		{
		auto *wn = static_cast<WindowNames *>(work->data);
		g_autoptr(GMenuItem) item = g_menu_item_new(wn->name, nullptr);
		g_menu_item_set_action_and_target_value(item, "win.main-win-new-window-layout", g_variant_new_string(wn->name));
		g_menu_append_item(layouts_section, item);
		}
}

static void connected_zoom_menu_cb(GSimpleAction *, GVariant *, gpointer)
{
	/* no-op */
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
	const char *new_id = gtk_editable_get_text(GTK_EDITABLE(rw->window_name_entry));

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
		g_autofree gchar *path = g_build_filename(get_window_layouts_dir(), xml_name, nullptr);

		if (isfile(path))
			{
			unlink_file(path);
			}

		g_free(rw->lw->options.id);
		rw->lw->options.id = g_strdup(new_id);
		layout_window_foreach(layout_menu_new_window_update);
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
	g_autofree gchar *path = g_build_filename(get_window_layouts_dir(), xml_name, nullptr);

	layout_close(dw->lw);
	g_free(dw);

	if (isfile(path))
		{
		unlink_file(path);
		}

	layout_window_foreach(layout_menu_new_window_update);
}

static void layout_menu_window_default_cb(GSimpleAction *, GVariant *, gpointer)
{
	layout_new_from_default();
}

static void layout_menu_window_layout_cb(GSimpleAction *, GVariant *parameter, gpointer)
{
	const gchar *id = g_variant_get_string(parameter, nullptr);
	if (!id || layout_window_is_displayed(id))
		{
		return;
		}

	g_autofree gchar *xml_name = g_strdup_printf("%s.xml", id);
	g_autofree gchar *path = g_build_filename(get_window_layouts_dir(), xml_name, nullptr);
	load_config_from_file(path, FALSE);
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

static void layout_menu_window_from_current_cb(GSimpleAction *, GVariant *, gpointer data)
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

static void layout_menu_window_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();

	layout_menu_new_window_update(lw);
}

static void layout_menu_window_rename_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();
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


void plugin_run_cb(GSimpleAction *, GVariant *parameter, gpointer user_data)
{
    /* parameter is a string key like "foo.desktop" */
    const char *key = g_variant_get_string(parameter, nullptr);

	auto lw = static_cast<LayoutWindow *>(user_data);

	if (!editor_window_flag_set(key))
		layout_exit_fullscreen(lw);

	lw = get_current_layout();

	file_util_start_editor_from_filelist(key, layout_selection_list(lw), layout_get_path(lw), lw->window);


    /* run ed->exec, etc. */
}

static void layout_actions_setup_editors(LayoutWindow *lw)
{
	GMenu *plugins_menu = G_MENU(gtk_builder_get_object(lw->builder, "plugins-submenu"));
	g_menu_remove_all(plugins_menu);

	plugins_menu_populate(plugins_menu, "win.main-win-plugin-run", nullptr);

	GtkApplication *app = GTK_APPLICATION(g_application_get_default());
	const char *empty_accels[] = {nullptr};

	if (!plugin_accel_actions)
		{
		plugin_accel_actions = g_ptr_array_new_with_free_func(g_free);
		}
	else
		{
		for (guint i = 0; i < plugin_accel_actions->len; i++)
			{
			auto *detailed_action = static_cast<gchar *>(g_ptr_array_index(plugin_accel_actions, i));
			register_accels_for_action(app, detailed_action, const_cast<GStrv>(empty_accels));
			}
		g_ptr_array_set_size(plugin_accel_actions, 0);
		}

	EditorsList editors_list = editor_list_get();

	for (EditorDescription *editor : editors_list)
		{
		if (!editor->hotkey || !*editor->hotkey)
			{
			continue;
			}

		g_autofree gchar *detailed_action = g_strdup_printf("win.main-win-plugin-run::%s", editor->key);
		g_auto(GStrv) accels = g_strsplit(editor->hotkey, ";", -1);
		register_accels_for_action(app, detailed_action, accels);
		g_ptr_array_add(plugin_accel_actions, g_strdup(detailed_action));
		}
}

void create_toolbars(LayoutWindow *lw)
{
	// creates first group called only from one place
	int i; 

	for (i = 0; i < TOOLBAR_COUNT; i++)
		{
		layout_actions_toolbar(lw, static_cast<ToolbarType>(i)); // creates the box
		layout_toolbar_add_default(lw, static_cast<ToolbarType>(i));
		}
}

static void layout_menu_window_delete_cb(GSimpleAction *, GVariant *, gpointer  )
{
	auto lw = get_current_layout();
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
	LayoutWindow *cur_lw = get_current_layout();

	const auto layout_toolbars_apply = [cur_lw](LayoutWindow *lw)
	{
		if (lw == cur_lw) return;

		for (int i = 0; i < TOOLBAR_COUNT; i++)
			{
			layout_toolbar_clear(lw, static_cast<ToolbarType>(i));

			for (GList *work = cur_lw->toolbar_actions[i]; work; work = work->next)
				{
				auto *action_name = static_cast<gchar *>(work->data);

				layout_toolbar_add(lw, static_cast<ToolbarType>(i), action_name);
				}
			}
	};

	layout_window_foreach(layout_toolbars_apply);

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
	GApplication *app = g_application_get_default();

	register_actions_from_table(GTK_APPLICATION(app), window, get_main_actions(), get_keyfile_merged(), lw);
}

GtkWidget *layout_actions_menu_bar(LayoutWindow *lw)
{
	if (lw->menu_bar) return lw->menu_bar;

	lw->menu_bar = gtk_popover_menu_bar_new_from_model(lw->menu_model);

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
	GtkWidget *toolbar;

	toolbar = layout_actions_toolbar(lw, TOOLBAR_MAIN);
	DEBUG_NAME(toolbar);

	if (lw->menu_tool_bar)
		{
		if (gtk_widget_get_parent(toolbar) != lw->menu_tool_bar)
			{
			widget_remove_from_parent(toolbar);
			gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), toolbar, FALSE, FALSE, 0);
			}

		return lw->menu_tool_bar;
		}

	lw->menu_tool_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), toolbar, FALSE, FALSE, 0);

	return g_object_ref(lw->menu_tool_bar);
}

void layout_actions_foreach(LayoutWindow *lw, GFunc func, gpointer data)
{
	if (!lw || !lw->window || !func) return;

	g_auto(GStrv) action_names = g_action_group_list_actions(G_ACTION_GROUP(lw->window));
	for (guint i = 0; action_names && action_names[i]; i++)
		{
		GAction *action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), action_names[i]);
		if (!action) continue;

		func(action, data);
		}
}

static void toolbar_clear_cb(GtkWidget *widget, gpointer)
{
	if (GTK_IS_BUTTON(widget))
		{
		/* Temporary GTK4 stub: legacy GtkAction signal bookkeeping has been removed. */
		auto *action = static_cast<GObject *>(g_object_get_data(G_OBJECT(widget), "action"));
		if (g_object_get_data(G_OBJECT(widget), "id") )
			{
			if (action)
				{
				g_signal_handler_disconnect(action, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "id")));
				}
			}
		}
	gq_gtk_widget_destroy(widget);
}

void layout_toolbar_clear(LayoutWindow *lw, ToolbarType type)
{
	g_list_free_full(lw->toolbar_actions[type], g_free);
	lw->toolbar_actions[type] = nullptr;

	if (lw->toolbar[type])
		{
		while (GtkWidget *child = gtk_widget_get_first_child(lw->toolbar[type]))
			{
			toolbar_clear_cb(child, nullptr);
			}
		}
}

void layout_toolbar_add(LayoutWindow *lw, ToolbarType type, const gchar *action_name)
{
	const gchar *tooltip_text = nullptr;
	GtkWidget *button = nullptr;

	if (!action_name)
		{
		g_abort();
		return;
		}

	if (!lw->toolbar[type])
		{
		g_abort();
		return;
		}

	if (g_str_has_suffix(action_name, ".desktop"))
		{
/** @FIXME GTK4
		/ this may be called before the external editors are read
		   create a dummy action for now /
		if (!lw->action_group_editors)
		{
		lw->action_group_editors = deprecated_gtk_action_group_new("MenuActionsExternal");
		deprecated_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);
		}
		if (!deprecated_gtk_action_group_get_action(lw->action_group_editors, action_name))
		{
		GtkActionEntry entry = { action_name,
		GQ_ICON_MISSING_IMAGE,
		action_name,
		nullptr,
		nullptr,
		nullptr
		};
		DEBUG_1("Creating temporary action %s", action_name);
		deprecated_gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);
		}
*/
		}


	if (g_strcmp0(action_name, "Separator") == 0)
		{
		button = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		}
	else
		{
		if (g_str_has_suffix(action_name, ".desktop"))
			{
/** FIXME GTK4
			action = deprecated_gtk_action_group_get_action(lw->action_group_editors, action_name);
			const gchar *kk = get_description_for_action_name("org.gnome.Evince.desktop");

			 @FIXME Using tootip as a flag to layout_actions_setup_editors()
			 is not a good way.
			
			tooltip_text = deprecated_gtk_action_get_label(action);
*/
			}
		else
			{
			GAction *action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), action_name+4);
			if (action)
				{
				const GVariantType *state_type =
				    g_action_get_state_type(action);

				if (state_type == nullptr)
					{
					/* stateless action */
					button = gtk_button_new(); // !!
					}
				else if (g_variant_type_equal(state_type, G_VARIANT_TYPE_BOOLEAN))
					{
					/* toggle */
					button = gtk_toggle_button_new(); // !!
					}
				else
					{
					/* radio / choice action */
					button = gtk_toggle_button_new(); // !!
					}
				tooltip_text = get_description_for_action_name(action_name);
				}

			else
				{
				GApplication *app = g_application_get_default();
				GAction *action = g_action_map_lookup_action(G_ACTION_MAP(app), action_name + 4);
				if (action)
					{
					const GVariantType *state_type = g_action_get_state_type(action);

					if (state_type == nullptr)
						{
						/* stateless action */
						button = gtk_button_new(); // !!
						}
					else if (g_variant_type_equal(state_type, G_VARIANT_TYPE_BOOLEAN))
						{
						/* toggle */
						button = gtk_toggle_button_new(); // !!
						}
					else
						{
						/* radio / choice action */
						button = gtk_toggle_button_new(); // !!
						}
					tooltip_text = get_description_for_action_name(action_name);
					}
				}
			}
		}

	if (button)
		{
		gtk_widget_add_css_class(button, "flat");
		gtk_widget_set_tooltip_text(button, tooltip_text);

		if (GTK_IS_ACTIONABLE(button))
			{
			gtk_actionable_set_detailed_action_name(GTK_ACTIONABLE(button), action_name);
			}

		if (GTK_IS_BUTTON(button))
			{
			GtkWidget *image = gtk_image_new_from_icon_name(get_icon_for_action_name(action_name));
			gtk_button_set_child(GTK_BUTTON(button), image);
			}

		gq_gtk_container_add(lw->toolbar[type], button);
		gq_gtk_widget_show_all(button);

		lw->toolbar_actions[type] = g_list_append(lw->toolbar_actions[type], g_strdup(action_name));
		}
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
			layout_toolbar_add(lw, type, "win.main-win-thumbnails");
			layout_toolbar_add(lw, type, "win.main-win-back");
			layout_toolbar_add(lw, type, "win.main-win-forward");
			layout_toolbar_add(lw, type, "win.main-win-up");
			layout_toolbar_add(lw, type, "win.main-win-home");
			layout_toolbar_add(lw, type, "win.main-win-refresh");
			layout_toolbar_add(lw, type, "win.main-win-zoom-in");
			layout_toolbar_add(lw, type, "win.main-win-zoom-out");
			layout_toolbar_add(lw, type, "win.main-win-zoom-fit");
			layout_toolbar_add(lw, type, "win.main-win-zoom-1-1");
			layout_toolbar_add(lw, type, "app.preferences");
			layout_toolbar_add(lw, type, "win.main-win-float-tools");
			break;
		case TOOLBAR_STATUS:
			layout_toolbar_add(lw, type, "win.main-win-exif-rotate");
			layout_toolbar_add(lw, type, "win.main-win-show-info-pixel");
			layout_toolbar_add(lw, type, "win.main-win-use-color-profiles");
			layout_toolbar_add(lw, type, "win.main-win-save-metadata");
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
		WRITE_CHAR_FULL("action", action);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_FORMAT_STRING("</%s>", name);
}

void layout_toolbar_add_from_config(LayoutWindow *lw, ToolbarType type, const char **attribute_names, const char **attribute_values)
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
	gint n = metadata_queue_length();

	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-save-metadata");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), (n > 0));

	const gchar *icon_name = n > 0 ? GQ_ICON_SAVE_AS : GQ_ICON_SAVE;
	g_autofree const gchar *icon_tooltip= n > 0 ? g_strdup_printf("Number of files with unsaved metadata: %d",n) : g_strdup("No unsaved metadata");

	for (int i = 0; i < TOOLBAR_COUNT; i++) // NOLINT(modernize-loop-convert)
		{
		if (!lw->toolbar[i]) continue;

		for (GtkWidget *widget = gtk_widget_get_first_child(lw->toolbar[i]);
		     widget;
		     widget = gtk_widget_get_next_sibling(widget))
			{
			if (!GTK_IS_BUTTON(widget)) continue;

			auto *waction = static_cast<GAction *>(g_object_get_data(G_OBJECT(widget), "action"));
			if (waction != action) continue;

			GtkWidget *image = gtk_button_get_child(GTK_BUTTON(widget));
			if (GTK_IS_IMAGE(image))
				{
				gtk_image_set_from_icon_name(GTK_IMAGE(image), icon_name);
				}

			gtk_widget_set_tooltip_text(widget, icon_tooltip);
			}
		}
}

void layout_util_status_update_write_all()
{
	layout_window_foreach(layout_util_status_update_write);
}

void layout_util_sync_color(LayoutWindow *lw)
{
	GAction *action;
	gint input = 0;
	gboolean use_color;
	gboolean use_image = FALSE;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;

	use_color = layout_image_color_profile_get_use(lw);

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-use-color-profiles");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(use_color));

#if HAVE_LCMS
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(use_color));

/** @FIXME GTK4
	if (const auto status = layout_image_color_profile_get_status(lw); status.has_value())
		{
		g_autofree gchar *buf = g_strdup_printf(_("Image profile: %s\nScreen profile: %s"),
		                                        status->image_profile.c_str(), status->screen_profile.c_str());

		deprecated_gtk_action_set_tooltip(action, buf);
		}
	else
		{
		deprecated_gtk_action_set_tooltip(action, _("Click to enable color management"));
		}
*/
#else
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(FALSE));
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), FALSE);
/** @FIXME GTK4
	deprecated_gtk_action_set_tooltip(action, _("Color profiles not supported"));
*/
#endif

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-use-image-profile");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(use_image));
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), use_color);

	color_profiles_menu_populate(lw, "win.main-win-color-profile");

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-grayscale");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(layout_image_get_desaturate(lw)));
}

void layout_util_sync_file_filter(LayoutWindow *lw)
{
	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-show-file-filter");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.show_file_filter));
}

void layout_util_sync_marks(LayoutWindow *lw)
{
	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-show-marks");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.show_marks));
}

static void layout_util_sync_views(LayoutWindow *lw)
{
	GAction *action;
	OsdShowFlags osd_flags = image_osd_get(lw->image);

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-folder-tree");
		if (lw->options.dir_view_type == DIRVIEW_LIST)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(FALSE));
			}
		else
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(TRUE));
			}

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-split-mode");
		if (lw->split_mode == SPLIT_NONE)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("none"));
			}
		else if (lw->split_mode == SPLIT_VERT)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("vertical"));
			}
		else if (lw->split_mode == SPLIT_HOR)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("horizontal"));
			}
		else if (lw->split_mode == SPLIT_TRIPLE)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("triple"));
			}
		else if (lw->split_mode == SPLIT_QUAD)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("quad"));
			}
		else
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("none"));
			}
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-split-next-pane");

		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !(lw->split_mode == SPLIT_NONE));
		action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-split-previous-pane");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !(lw->split_mode == SPLIT_NONE));
		action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-split-up-pane");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !(lw->split_mode == SPLIT_NONE));
		action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-split-down-pane");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !(lw->split_mode == SPLIT_NONE));


	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-connected-zoom-menu");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), FALSE);

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-split-pane-sync");
		g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.split_pane_sync));
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-view-mode");

		if (lw->options.file_view_type == FILEVIEW_LIST)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("main-win-list"));
			}
		else if (lw->split_mode == SPLIT_VERT)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("main-win-icons"));
			}
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-crop");
		if (options->rectangle_draw_aspect_ratio == RECTANGLE_DRAW_ASPECT_RATIO_NONE)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("none"));
			}
		else if (options->rectangle_draw_aspect_ratio == RECTANGLE_DRAW_ASPECT_RATIO_ONE_ONE)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("1-1"));
			}
		else if (options->rectangle_draw_aspect_ratio == RECTANGLE_DRAW_ASPECT_RATIO_FOUR_THREE)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("4-3"));
			}
		else if (options->rectangle_draw_aspect_ratio == RECTANGLE_DRAW_ASPECT_RATIO_THREE_TWO)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("3-2"));
			}
		else if (options->rectangle_draw_aspect_ratio == RECTANGLE_DRAW_ASPECT_RATIO_SIXTEEN_NINE)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("16-9"));
			}
		else
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("none"));
			}
			
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-osd");
		if (options->overlay_screen_display_selected_profile == OVERLAY_SCREEN_DISPLAY_1)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("1"));
			}
		else if (options->overlay_screen_display_selected_profile == OVERLAY_SCREEN_DISPLAY_2)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("2"));
			}
		else if (options->overlay_screen_display_selected_profile == OVERLAY_SCREEN_DISPLAY_3)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("3"));
			}
		else if (options->overlay_screen_display_selected_profile == OVERLAY_SCREEN_DISPLAY_4)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("4"));
			}
		else
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string("1"));
			}
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-float-tools");
		if (lw->options.tools_float)
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(TRUE));
			}
		else
			{
			g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(FALSE));
			}
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-info-sidebar");
		g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(layout_bar_enabled(lw)));
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-sort-manager");
		g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(layout_bar_enabled(lw)));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-hide-selectable-toolbars");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.selectable_toolbars_hidden));
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-show-info-pixel");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.show_info_pixel));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-slideshow");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(layout_image_slideshow_active(lw)));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-ignore-alpha");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.ignore_alpha));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-animate");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.animate));
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-image-overlay");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(osd_flags != OSD_SHOW_NOTHING));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-image-histogram");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(osd_flags != OSD_SHOW_HISTOGRAM));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-exif-rotate");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->image.exif_rotate_enable));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-over-under-exposed");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->overunderexposed));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-draw-rectangle");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->draw_rectangle));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-rectangular-selection");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(options->collections.rectangular_selection));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-show-file-filter");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.show_file_filter));

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-hide-bars");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.bars_state.hidden));

	if (osd_flags & OSD_SHOW_HISTOGRAM)
		{
		action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-histogram-channel");
		g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string(histogram_channel_to_string(image_osd_histogram_get_channel(lw->image))));

		action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-histogram-mode");
		g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string(histogram_mode_to_string(image_osd_histogram_get_mode(lw->image))));
		}

//	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-connect-zoom-menu");
//	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), (lw->split_mode != SPLIT_NONE));

	g_autofree char *exiftran = g_find_program_in_path("exiftran");
	g_autofree char *mogrify  = g_find_program_in_path("mogrify");

	bool is_write_rotation =
	    (exiftran != nullptr) &&
	    (mogrify  != nullptr) &&
	    !options->metadata.write_orientation;

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-write-rotation");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), is_write_rotation);
	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-write-rotation-keep-date");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), is_write_rotation);

	action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-stereo");
	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string(stereo_mode_to_string(layout_image_stereo_pixbuf_get(lw))));

	layout_util_sync_marks(lw);
	layout_util_sync_color(lw);
	layout_image_set_ignore_alpha(lw, lw->options.ignore_alpha);

	overlay_screen_display_profile_set(options->overlay_screen_display_selected_profile);
}

void layout_util_sync_thumb(LayoutWindow *lw)
{
	if (!lw || !lw->window) return;

	GAction *action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-thumbnails");
	if (!action) return;

	g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(lw->options.show_thumbnails));
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), lw->options.file_view_type == FILEVIEW_LIST);
}

void layout_util_sync(LayoutWindow *lw)
{
	layout_util_sync_views(lw);
	layout_util_sync_thumb(lw);
}

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

	gtk_paned_set_end_child(GTK_PANED(lw->utility_paned), lw->bar);

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


static void layout_bar_sort_destroyed(GtkWidget *, gpointer  )
{
	auto lw = get_current_layout();

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

	gtk_paned_set_start_child(GTK_PANED(lw->utility_paned), image);

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

/* const ActionDef app_actions[] =
 */
#include "layout-util-app-actions.inc"

/* const ActionDef main_actions[] =
 */
#include "layout-util-main-actions.inc"

void register_main_window_actions(GtkApplication *app,  LayoutWindow *lw)
{
	GKeyFile *accels_keyfile = get_keyfile_merged();

	register_actions_from_table(app, lw->window, main_actions, accels_keyfile, lw);
	layout_menu_new_window_update(lw);
}

GStrv get_tooltips()
{
	GPtrArray *array = g_ptr_array_new_with_free_func(g_free);

	for (const auto & app_action : app_actions)
		{
		if (app_action.description)
			{
			g_ptr_array_add(array, g_strdup(app_action.description));
			}
		}

	g_ptr_array_add(array, nullptr);  /* NULL terminate */

	return (GStrv) g_ptr_array_free(array, FALSE);
}

struct ActionLine
{
	const char *action_name;
	const char *description;
};

struct ActionLine2
{
	const char *action_name;
};

GStrv get_actions_for_toolbar()
{
	size_t n_app_actions = G_N_ELEMENTS(app_actions);

	std::vector<ActionLine2> rows;
	rows.reserve(n_app_actions);

	size_t max_entry_len = 0;

	for (size_t i = 0; i < n_app_actions; i++)
		{
		if (!app_actions[i].icon_name)
			{
			continue;
			}

		if (!app_actions[i].action_name || !app_actions[i].description || (g_strcmp0(app_actions[i].icon_name, GQ_ICON_MISSING_IMAGE) == 0) || app_actions[i].parameter_type || !app_actions[i].icon_name)
			{
			continue;
			}

		rows.push_back({ app_actions[i].action_name});

		size_t len = strlen(app_actions[i].description);
		max_entry_len = std::max(len, max_entry_len);
		}

	std::sort(rows.begin(), rows.end(), [](const ActionLine2 & a, const ActionLine2 & b)
		{
		return g_strcmp0(a.action_name, b.action_name) < 0;
		});

	GPtrArray *array = g_ptr_array_new_with_free_func(g_free);

	for (const auto &row : rows)
		{
		g_ptr_array_add(array, g_strdup_printf("%s", row.action_name));
		}

	g_ptr_array_add(array, nullptr);

	return (GStrv) g_ptr_array_free(array, FALSE);
}

const ActionDef *get_app_actions()
{
	return app_actions;
}


const ActionDef *get_main_actions()
{
	return main_actions;
}

void register_app_actions(GtkApplication *app)
{
	register_actions_from_table(app, nullptr, app_actions, get_keyfile_merged(), nullptr);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
