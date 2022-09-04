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
#include "layout-util.h"

#include "advanced-exif.h"
#include "bar-sort.h"
#include "bar.h"
#include "bar-keywords.h"
#include "cache-maint.h"
#include "collect.h"
#include "collect-dlg.h"
#include "compat.h"
#include "color-man.h"
#include "dupe.h"
#include "editors.h"
#include "filedata.h"
#include "history-list.h"
#include "image.h"
#include "image-overlay.h"
#include "histogram.h"
#include "img-view.h"
#include "layout-image.h"
#include "logwindow.h"
#include "misc.h"
#include "pan-view.h"
#include "pixbuf-util.h"
#include "preferences.h"
#include "print.h"
#include "rcfile.h"
#include "search.h"
#include "search-and-run.h"
#include "slideshow.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "utilops.h"
#include "view-dir.h"
#include "view-file.h"
#include "window.h"
#include "metadata.h"
#include "desktop-file.h"

#include <sys/wait.h>
#include <gdk/gdkkeysyms.h> /* for keyboard values */
#include "keymap-template.h"

#define MENU_EDIT_ACTION_OFFSET 16
#define FILE_COLUMN_POINTER 0

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
	guint i;

	for (i = 0; i < sizeof(tree_key_overrides) / sizeof(guint); i++)
		{
		if (keyval == tree_key_overrides[i]) return TRUE;
		}

	return FALSE;
}

gboolean layout_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	LayoutWindow *lw = data;
	GtkWidget *focused;
	gboolean stop_signal = FALSE;
	gint x = 0;
	gint y = 0;

	if (lw->path_entry && gtk_widget_has_focus(lw->path_entry))
		{
		if (event->keyval == GDK_KEY_Escape && lw->dir_fd)
			{
			gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);
			}

		/* the gtkaccelgroup of the window is stealing presses before they get to the entry (and more),
		 * so when the some widgets have focus, give them priority (HACK)
		 */
		if (gtk_widget_event(lw->path_entry, (GdkEvent *)event))
			{
			return TRUE;
			}
		}

	if (lw->vf->file_filter.combo && gtk_widget_has_focus(gtk_bin_get_child(GTK_BIN(lw->vf->file_filter.combo))))
		{
		if (gtk_widget_event(gtk_bin_get_child(GTK_BIN(lw->vf->file_filter.combo)), (GdkEvent *)event))
			{
			return TRUE;
			}
		}

	if (lw->vd && lw->options.dir_view_type == DIRVIEW_TREE && gtk_widget_has_focus(lw->vd->view) &&
	    !layout_key_match(event->keyval) &&
	    gtk_widget_event(lw->vd->view, (GdkEvent *)event))
		{
		return TRUE;
		}
	if (lw->bar &&
	    bar_event(lw->bar, (GdkEvent *)event))
		{
		return TRUE;
		}

	focused = gtk_container_get_focus_child(GTK_CONTAINER(lw->image->widget));
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
		if (event->state & GDK_SHIFT_MASK)
			{
			x *= 3;
			y *= 3;
			}
		keyboard_scroll_calc(&x, &y, event);
		layout_image_scroll(lw, x, y, (event->state & GDK_SHIFT_MASK));
		}

	return stop_signal;
}

void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window)
{
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(layout_key_press_cb), lw);
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

static void clear_marks_cancel_cb(GenericDialog *gd, gpointer UNUSED(data))
{
	generic_dialog_close(gd);
}

static void clear_marks_help_cb(GenericDialog *UNUSED(gd), gpointer UNUSED(data))
{
	help_window_show("GuideMainWindowMenus.html");
}

void layout_menu_clear_marks_ok_cb(GenericDialog *gd, gpointer UNUSED(data))
{
	marks_clear_all();
	generic_dialog_close(gd);
}

static void layout_menu_clear_marks_cb(GtkAction *UNUSED(action), gpointer UNUSED(data))
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Clear Marks"),
				"marks_clear", NULL, FALSE, clear_marks_cancel_cb, NULL);
	generic_dialog_add_message(gd, GTK_STOCK_DIALOG_QUESTION, "Clear all marks?",
				"This will clear all marks for all images,\nincluding those linked to keywords",
				TRUE);
	generic_dialog_add_button(gd, GTK_STOCK_OK, NULL, layout_menu_clear_marks_ok_cb, TRUE);
	generic_dialog_add_button(gd, GTK_STOCK_HELP, NULL,
				clear_marks_help_cb, FALSE);

	gtk_widget_show(gd->dialog);
}

static void layout_menu_new_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	collection_window_new(NULL);
}

static void layout_menu_open_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	collection_dialog_load(NULL);
}

static void layout_menu_search_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	search_new(lw->dir_fd, layout_image_get_fd(lw));
}

static void layout_menu_dupes_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	dupe_window_new();
}

static void layout_menu_pan_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	pan_window_new(lw->dir_fd);
}

static void layout_menu_print_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	print_window_new(layout_image_get_fd(lw), layout_selection_list(lw), layout_list(lw), layout_window(lw));
}

static void layout_menu_dir_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->vd) vd_new_folder(lw->vd, lw->dir_fd);
}

static void layout_menu_copy_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	file_util_copy(NULL, layout_selection_list(lw), NULL, layout_window(lw));
}

static void layout_menu_copy_path_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	file_util_copy_path_list_to_clipboard(layout_selection_list(lw), TRUE);
}

static void layout_menu_copy_path_unquoted_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	file_util_copy_path_list_to_clipboard(layout_selection_list(lw), FALSE);
}

static void layout_menu_move_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	file_util_move(NULL, layout_selection_list(lw), NULL, layout_window(lw));
}

static void layout_menu_rename_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	file_util_rename(NULL, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_delete_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	options->file_ops.safe_delete_enable = FALSE;
	file_util_delete(NULL, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_move_to_trash_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	options->file_ops.safe_delete_enable = TRUE;
	file_util_delete(NULL, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_move_to_trash_key_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	if (options->file_ops.enable_delete_key)
		{
		options->file_ops.safe_delete_enable = TRUE;
		file_util_delete(NULL, layout_selection_list(lw), layout_window(lw));
		}
}

static void layout_menu_disable_grouping_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	file_data_disable_grouping_list(layout_selection_list(lw), TRUE);
}

static void layout_menu_enable_grouping_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	file_data_disable_grouping_list(layout_selection_list(lw), FALSE);
}

void layout_menu_close_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_close(lw);
}

static void layout_menu_exit_cb(GtkAction *UNUSED(action), gpointer UNUSED(data))
{
	exit_program();
}

static void layout_menu_alter_90_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter_orientation(lw, ALTER_ROTATE_90);
}

static void layout_menu_rating_0_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_rating(lw, "0");
}

static void layout_menu_rating_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_rating(lw, "1");
}

static void layout_menu_rating_2_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_rating(lw, "2");
}

static void layout_menu_rating_3_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_rating(lw, "3");
}

static void layout_menu_rating_4_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_rating(lw, "4");
}

static void layout_menu_rating_5_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_rating(lw, "5");
}

static void layout_menu_rating_m1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_rating(lw, "-1");
}

static void layout_menu_alter_90cc_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter_orientation(lw, ALTER_ROTATE_90_CC);
}

static void layout_menu_alter_180_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter_orientation(lw, ALTER_ROTATE_180);
}

static void layout_menu_alter_mirror_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter_orientation(lw, ALTER_MIRROR);
}

static void layout_menu_alter_flip_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter_orientation(lw, ALTER_FLIP);
}

static void layout_menu_alter_desaturate_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_set_desaturate(lw, gtk_toggle_action_get_active(action));
}

static void layout_menu_alter_ignore_alpha_cb(GtkToggleAction *action, gpointer data)
{
   LayoutWindow *lw = data;

	if (lw->options.ignore_alpha == gtk_toggle_action_get_active(action)) return;

   layout_image_set_ignore_alpha(lw, gtk_toggle_action_get_active(action));
}

static void layout_menu_alter_none_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_alter_orientation(lw, ALTER_NONE);
}

static void layout_menu_exif_rotate_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	options->image.exif_rotate_enable = gtk_toggle_action_get_active(action);
	layout_image_reset_orientation(lw);
}

static void layout_menu_select_rectangle_cb(GtkToggleAction *action, gpointer UNUSED(data))
{
	options->draw_rectangle = gtk_toggle_action_get_active(action);
}

static void layout_menu_split_pane_sync_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	lw->options.split_pane_sync = gtk_toggle_action_get_active(action);
}

static void layout_menu_select_overunderexposed_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_set_overunderexposed(lw, gtk_toggle_action_get_active(action));
}

static void layout_menu_write_rotate(GtkToggleAction *UNUSED(action), gpointer data, gboolean keep_date)
{
	LayoutWindow *lw = data;
	GtkTreeModel *store;
	GList *work;
	GtkTreeSelection *selection;
	GtkTreePath *tpath;
	FileData *fd_n;
	GtkTreeIter iter;
	gchar *rotation;
	gchar *command;
	gint run_result;
	GenericDialog *gd;
	GString *message;
	int cmdstatus;

	if (!layout_valid(&lw)) return;

	if (!lw || !lw->vf) return;

	if (lw->vf->type == FILEVIEW_ICON)
		{
		if (!VFICON(lw->vf)->selection) return;
		work = VFICON(lw->vf)->selection;
		}
	else
		{
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(lw->vf->listview));
		work = gtk_tree_selection_get_selected_rows(selection, &store);
		}

	while (work)
		{
		if (lw->vf->type == FILEVIEW_ICON)
			{
			fd_n = work->data;
			work = work->next;
			}
		else
			{
			tpath = work->data;
			gtk_tree_model_get_iter(store, &iter, tpath);
			gtk_tree_model_get(store, &iter, FILE_COLUMN_POINTER, &fd_n, -1);
			work = work->next;
			}

		rotation = g_strdup_printf("%d", fd_n->user_orientation);
		command = g_strconcat(gq_bindir, "/geeqie-rotate -r ", rotation,
								keep_date ? " -t \"" : " \"", fd_n->path, "\"", NULL);
		cmdstatus = runcmd(command);
		run_result = WEXITSTATUS(cmdstatus);
		if (!run_result)
			{
			fd_n->user_orientation = 0;
			}
		else
			{
			message = g_string_new("");
			message = g_string_append(message, _("Operation failed:\n"));

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

			gd = generic_dialog_new(_("Image orientation"),
			"Image orientation", NULL, TRUE, NULL, NULL);
			generic_dialog_add_message(gd, GTK_STOCK_DIALOG_ERROR,
			"Image orientation", message->str, TRUE);
			generic_dialog_add_button(gd, GTK_STOCK_OK, NULL, NULL, TRUE);

			gtk_widget_show(gd->dialog);

			g_string_free(message, TRUE);
			}

		g_free(rotation);
		g_free(command);
		}
}

static void layout_menu_write_rotate_keep_date_cb(GtkToggleAction *action, gpointer data)
{
	layout_menu_write_rotate(action, data, TRUE);
}

static void layout_menu_write_rotate_cb(GtkToggleAction *action, gpointer data)
{
	layout_menu_write_rotate(action, data, FALSE);
}

static void layout_menu_config_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	show_config_window(lw);
}

static void layout_menu_editors_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	show_editor_list_window();
}

static void layout_menu_layout_config_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_show_config_window(lw);
}

static void layout_menu_remove_thumb_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	cache_manager_show();
}

static void layout_menu_wallpaper_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_to_root(lw);
}

/* single window zoom */
static void layout_menu_zoom_in_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, get_zoom_increment(), FALSE);
}

static void layout_menu_zoom_out_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, -get_zoom_increment(), FALSE);
}

static void layout_menu_zoom_1_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 1.0, FALSE);
}

static void layout_menu_zoom_fit_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 0.0, FALSE);
}

static void layout_menu_zoom_fit_hor_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, FALSE, FALSE);
}

static void layout_menu_zoom_fit_vert_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, TRUE, FALSE);
}

static void layout_menu_zoom_2_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 2.0, FALSE);
}

static void layout_menu_zoom_3_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 3.0, FALSE);
}
static void layout_menu_zoom_4_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 4.0, FALSE);
}

static void layout_menu_zoom_1_2_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -2.0, FALSE);
}

static void layout_menu_zoom_1_3_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -3.0, FALSE);
}

static void layout_menu_zoom_1_4_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -4.0, FALSE);
}

/* connected zoom */
static void layout_menu_connect_zoom_in_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, get_zoom_increment(), TRUE);
}

static void layout_menu_connect_zoom_out_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_adjust(lw, -get_zoom_increment(), TRUE);
}

static void layout_menu_connect_zoom_1_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 1.0, TRUE);
}

static void layout_menu_connect_zoom_fit_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 0.0, TRUE);
}

static void layout_menu_connect_zoom_fit_hor_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, FALSE, TRUE);
}

static void layout_menu_connect_zoom_fit_vert_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set_fill_geometry(lw, TRUE, TRUE);
}

static void layout_menu_connect_zoom_2_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 2.0, TRUE);
}

static void layout_menu_connect_zoom_3_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 3.0, TRUE);
}
static void layout_menu_connect_zoom_4_1_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, 4.0, TRUE);
}

static void layout_menu_connect_zoom_1_2_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -2.0, TRUE);
}

static void layout_menu_connect_zoom_1_3_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -3.0, TRUE);
}

static void layout_menu_connect_zoom_1_4_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_zoom_set(lw, -4.0, TRUE);
}


static void layout_menu_split_cb(GtkRadioAction *action, GtkRadioAction *UNUSED(current), gpointer data)
{
	LayoutWindow *lw = data;
	ImageSplitMode mode;

	layout_exit_fullscreen(lw);
	mode = gtk_radio_action_get_current_value(action);
	layout_split_change(lw, mode);
}


static void layout_menu_thumb_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_thumb_set(lw, gtk_toggle_action_get_active(action));
}


static void layout_menu_list_cb(GtkRadioAction *action, GtkRadioAction *UNUSED(current), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_views_set(lw, lw->options.dir_view_type, (FileViewType) gtk_radio_action_get_current_value(action));
}

static void layout_menu_view_dir_as_cb(GtkToggleAction *action,  gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);

	if (gtk_toggle_action_get_active(action))
		{
		layout_views_set(lw, DIRVIEW_TREE, lw->options.file_view_type);
		}
	else
		{
		layout_views_set(lw, DIRVIEW_LIST, lw->options.file_view_type);
		}
}

static void layout_menu_view_in_new_window_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	view_window_new(layout_image_get_fd(lw));
}

static void layout_menu_open_archive_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	LayoutWindow *lw_new;
	gchar *dest_dir;
	FileData *fd;

	layout_exit_fullscreen(lw);
	fd = layout_image_get_fd(lw);

	if (fd->format_class == FORMAT_CLASS_ARCHIVE)
		{
		dest_dir = open_archive(layout_image_get_fd(lw));
		if (dest_dir)
			{
			lw_new = layout_new_from_default();
			layout_set_path(lw_new, dest_dir);
			g_free(dest_dir);
			}
		else
			{
			warning_dialog(_("Cannot open archive file"), _("See the Log Window"), GTK_STOCK_DIALOG_WARNING, NULL);
			}
		}
}

static void layout_menu_fullscreen_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_full_screen_toggle(lw);
}

static void layout_menu_escape_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
}

static void layout_menu_overlay_toggle_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	image_osd_toggle(lw->image);
	layout_util_sync_views(lw);
}


static void layout_menu_overlay_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (gtk_toggle_action_get_active(action))
		{
		OsdShowFlags flags = image_osd_get(lw->image);

		if ((flags | OSD_SHOW_INFO | OSD_SHOW_STATUS) != flags)
			image_osd_set(lw->image, flags | OSD_SHOW_INFO | OSD_SHOW_STATUS);
		}
	else
		{
		GtkToggleAction *histogram_action = GTK_TOGGLE_ACTION(gtk_action_group_get_action(lw->action_group, "ImageHistogram"));

		image_osd_set(lw->image, OSD_SHOW_NOTHING);
		gtk_toggle_action_set_active(histogram_action, FALSE); /* this calls layout_menu_histogram_cb */
		}
}

static void layout_menu_histogram_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (gtk_toggle_action_get_active(action))
		{
		image_osd_set(lw->image, OSD_SHOW_INFO | OSD_SHOW_STATUS | OSD_SHOW_HISTOGRAM);
		layout_util_sync_views(lw); /* show the overlay state, default channel and mode in the menu */
		}
	else
		{
		OsdShowFlags flags = image_osd_get(lw->image);
		if (flags & OSD_SHOW_HISTOGRAM)
			image_osd_set(lw->image, flags & ~OSD_SHOW_HISTOGRAM);
		}
}

static void layout_menu_animate_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.animate == gtk_toggle_action_get_active(action)) return;
	layout_image_animate_toggle(lw);
}

static void layout_menu_rectangular_selection_cb(GtkToggleAction *action, gpointer UNUSED(data))
{
	options->collections.rectangular_selection = gtk_toggle_action_get_active(action);
}

static void layout_menu_histogram_toggle_channel_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	image_osd_histogram_toggle_channel(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_histogram_toggle_mode_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	image_osd_histogram_toggle_mode(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_histogram_channel_cb(GtkRadioAction *action, GtkRadioAction *UNUSED(current), gpointer data)
{
	LayoutWindow *lw = data;
	gint channel = gtk_radio_action_get_current_value(action);
	GtkToggleAction *histogram_action = GTK_TOGGLE_ACTION(gtk_action_group_get_action(lw->action_group, "ImageHistogram"));

	if (channel < 0 || channel >= HCHAN_COUNT) return;

	gtk_toggle_action_set_active(histogram_action, TRUE); /* this calls layout_menu_histogram_cb */
	image_osd_histogram_set_channel(lw->image, channel);
}

static void layout_menu_histogram_mode_cb(GtkRadioAction *action, GtkRadioAction *UNUSED(current), gpointer data)
{
	LayoutWindow *lw = data;
	gint mode = gtk_radio_action_get_current_value(action);
	GtkToggleAction *histogram_action = GTK_TOGGLE_ACTION(gtk_action_group_get_action(lw->action_group, "ImageHistogram"));

	if (mode < 0 || mode > 1) return;

	gtk_toggle_action_set_active(histogram_action, TRUE); /* this calls layout_menu_histogram_cb */
	image_osd_histogram_set_mode(lw->image, mode);
}

static void layout_menu_refresh_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_refresh(lw);
}

static void layout_menu_bar_exif_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_exif_window_new(lw);
}

static void layout_menu_search_and_run_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_search_and_run_window_new(lw);
}


static void layout_menu_float_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.tools_float == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_tools_float_toggle(lw);
}

static void layout_menu_hide_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	layout_tools_hide_toggle(lw);
}

static void layout_menu_toolbar_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.toolbar_hidden == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_toolbar_toggle(lw);
}

static void layout_menu_info_pixel_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.show_info_pixel == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_info_pixel_set(lw, !lw->options.show_info_pixel);
}

/* NOTE: these callbacks are called also from layout_util_sync_views */
static void layout_menu_bar_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (layout_bar_enabled(lw) == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_toggle(lw);
}

static void layout_menu_bar_sort_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (layout_bar_sort_enabled(lw) == gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_sort_toggle(lw);
}

static void layout_menu_hide_bars_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (lw->options.bars_state.hidden == gtk_toggle_action_get_active(action))
		{
		return;
		}
	layout_bars_hide_toggle(lw);
}

static void layout_menu_slideshow_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (layout_image_slideshow_active(lw) == gtk_toggle_action_get_active(action)) return;
	layout_image_slideshow_toggle(lw);
}

static void layout_menu_slideshow_pause_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_image_slideshow_pause_toggle(lw);
}

static void layout_menu_slideshow_slower_cb(GtkAction *UNUSED(action), gpointer UNUSED(data))
{
	options->slideshow.delay = options->slideshow.delay + 5;
	if (options->slideshow.delay > SLIDESHOW_MAX_SECONDS)
		options->slideshow.delay = SLIDESHOW_MAX_SECONDS;
}

static void layout_menu_slideshow_faster_cb(GtkAction *UNUSED(action), gpointer UNUSED(data))
{
	options->slideshow.delay = options->slideshow.delay - 5;
	if (options->slideshow.delay < SLIDESHOW_MIN_SECONDS * 10)
		options->slideshow.delay = SLIDESHOW_MIN_SECONDS * 10;
}


static void layout_menu_stereo_mode_next_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	gint mode = layout_image_stereo_pixbuf_get(lw);

	/* 0->1, 1->2, 2->3, 3->1 - disable auto, then cycle */
	mode = mode % 3 + 1;

	GtkAction *radio = gtk_action_group_get_action(lw->action_group, "StereoAuto");
	gtk_radio_action_set_current_value(GTK_RADIO_ACTION(radio), mode);

	/*
	this is called via fallback in layout_menu_stereo_mode_cb
	layout_image_stereo_pixbuf_set(lw, mode);
	*/

}

static void layout_menu_stereo_mode_cb(GtkRadioAction *action, GtkRadioAction *UNUSED(current), gpointer data)
{
	LayoutWindow *lw = data;
	gint mode = gtk_radio_action_get_current_value(action);
	layout_image_stereo_pixbuf_set(lw, mode);
}

static void layout_menu_help_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	help_window_show("index.html");
}

static void layout_menu_help_search_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	help_search_window_show();
}

static void layout_menu_help_keys_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	help_window_show("GuideReferenceKeyboardShortcuts.html");
}

static void layout_menu_notes_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	help_window_show("release_notes");
}

static void layout_menu_changelog_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	help_window_show("changelog");
}

static char *keyboard_map_hardcoded[][2] = {
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
	{NULL, NULL}
};

static void layout_menu_foreach_func(
					gpointer data,
					const gchar *accel_path,
					guint accel_key,
					GdkModifierType accel_mods,
					gboolean UNUSED(changed))
{
	gchar *path, *name;
	gchar *key_name, *menu_name;
	gchar **subset_lt_arr, **subset_gt_arr;
	gchar *subset_lt, *converted_name;
	GPtrArray *array = data;

	path = g_strescape(accel_path, NULL);
	name = gtk_accelerator_name(accel_key, accel_mods);

	menu_name = g_strdup(g_strrstr(path, "/")+1);

	if (g_strrstr(name, ">"))
		{
		subset_lt_arr = g_strsplit_set(name,"<", 4);
		subset_lt = g_strjoinv("&lt;", subset_lt_arr);
		subset_gt_arr = g_strsplit_set(subset_lt,">", 4);
		converted_name = g_strjoinv("&gt;", subset_gt_arr);
		key_name = g_strdup(converted_name);

		g_free(converted_name);
		g_free(subset_lt);
		g_strfreev(subset_lt_arr);
		g_strfreev(subset_gt_arr);
		}
	else
		key_name = g_strdup(name);

	g_ptr_array_add(array, (gpointer)menu_name);
	g_ptr_array_add(array, (gpointer)key_name);

	g_free(name);
	g_free(path);
}

static void layout_menu_kbd_map_cb(GtkAction *UNUSED(action), gpointer UNUSED(data))
{
	gint fd = -1;
	GPtrArray *array;
	char * tmp_file;
	GError *error = NULL;
	GIOChannel *channel;
	char **pre_key, **post_key;
	char *key_name, *converted_line;
	int keymap_index;
	guint index;

	fd = g_file_open_tmp("geeqie_keymap_XXXXXX.svg", &tmp_file, &error);
	if (error)
		{
		log_printf("Error: Keyboard Map - cannot create file:%s\n",error->message);
		g_error_free(error);
		}
	else
		{
		array = g_ptr_array_new();

		gtk_accel_map_foreach(array, layout_menu_foreach_func);

		channel = g_io_channel_unix_new(fd);

		keymap_index = 0;
		while (keymap_template[keymap_index])
			{
			if (g_strrstr(keymap_template[keymap_index], ">key:"))
				{
				pre_key = g_strsplit(keymap_template[keymap_index],">key:",2);
				post_key = g_strsplit(pre_key[1],"<",2);

				index=0;
				key_name = " ";
				for (index=0; index < array->len-2; index=index+2)
					{
					if (!(g_ascii_strcasecmp(g_ptr_array_index(array,index+1), post_key[0])))
						{
						key_name = g_ptr_array_index(array,index+0);
						break;
						}
					}

				index=0;
				while (keyboard_map_hardcoded[index][0])
					{
					if (!(g_strcmp0(keyboard_map_hardcoded[index][1], post_key[0])))
						{
						key_name = keyboard_map_hardcoded[index][0];
						break;
						}
					index++;
					}

				converted_line = g_strconcat(pre_key[0], ">", key_name, "<", post_key[1], "\n", NULL);
				g_io_channel_write_chars(channel, converted_line, -1, NULL, &error);
				if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}

				g_free(converted_line);
				g_strfreev(pre_key);
				g_strfreev(post_key);
				}
			else
				{
				g_io_channel_write_chars(channel, keymap_template[keymap_index], -1, NULL, &error);
				if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}
				g_io_channel_write_chars(channel, "\n", -1, NULL, &error);
				if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}
				}
			keymap_index++;
			}

		g_io_channel_flush(channel, &error);
		if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}
		g_io_channel_unref(channel);

		index=0;
		for (index=0; index < array->len-2; index=index+2)
			{
			g_free(g_ptr_array_index(array,index));
			g_free(g_ptr_array_index(array,index+1));
			}
		g_ptr_array_unref(array);

		view_window_new(file_data_new_simple(tmp_file));
		g_free(tmp_file);
		}
}

static void layout_menu_about_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	show_about_window(lw);
}

static void layout_menu_log_window_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_exit_fullscreen(lw);
	log_window_new(lw);
}


/*
 *-----------------------------------------------------------------------------
 * select menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_select_all_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_select_all(lw);
}

static void layout_menu_unselect_all_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_select_none(lw);
}

static void layout_menu_invert_selection_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	layout_select_invert(lw);
}

static void layout_menu_file_filter_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_file_filter_set(lw, gtk_toggle_action_get_active(action));
}

static void layout_menu_marks_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	layout_marks_set(lw, gtk_toggle_action_get_active(action));
}


static void layout_menu_set_mark_sel_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_SET);
}

static void layout_menu_res_mark_sel_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_RESET);
}

static void layout_menu_toggle_mark_sel_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_TOGGLE);
}

static void layout_menu_sel_mark_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_SET);
}

static void layout_menu_sel_mark_or_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_OR);
}

static void layout_menu_sel_mark_and_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_AND);
}

static void layout_menu_sel_mark_minus_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_MINUS);
}

static void layout_menu_mark_filter_toggle_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
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

static void layout_menu_image_first_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	layout_image_first(lw);
}

static void layout_menu_image_prev_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	gint i;

	if (lw->options.split_pane_sync)
		{
		for (i = 0; i < MAX_SPLIT_IMAGES; i++)
			{
			if (lw->split_images[i])
				{
				if (i != -1)
					{
					DEBUG_1("image activate scroll %d", i);
					layout_image_activate(lw, i, FALSE);
					layout_image_prev(lw);
					}
				}
			}
		}
	else
		{
		layout_image_prev(lw);
		}
}

static void layout_menu_image_next_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	gint i;

	if (lw->options.split_pane_sync)
		{
		for (i = 0; i < MAX_SPLIT_IMAGES; i++)
			{
			if (lw->split_images[i])
				{
				if (i != -1)
					{
					DEBUG_1("image activate scroll %d", i);
					layout_image_activate(lw, i, FALSE);
					layout_image_next(lw);
					}
				}
			}
		}
	else
		{
		layout_image_next(lw);
		}
}

static void layout_menu_page_first_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_set_page_num(fd, 1);
		}
}

static void layout_menu_page_last_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_set_page_num(fd, -1);
		}
}

static void layout_menu_page_next_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_inc_page_num(fd);
		}
}

static void layout_menu_page_previous_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_dec_page_num(fd);
		}
}

static void layout_menu_image_forward_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	/* Obtain next image */
	layout_set_path(lw, image_chain_forward());
}

static void layout_menu_image_back_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;

	/* Obtain previous image */
	layout_set_path(lw, image_chain_back());
}

static void layout_menu_split_pane_next_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
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

static void layout_menu_split_pane_prev_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
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

static void layout_menu_split_pane_updown_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
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

static void layout_menu_image_last_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	layout_image_last(lw);
}

static void layout_menu_back_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	FileData *dir_fd;

	/* Obtain previous path */
	dir_fd = file_data_new_dir(history_chain_back());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_forward_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	FileData *dir_fd;

	/* Obtain next path */
	dir_fd = file_data_new_dir(history_chain_forward());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_home_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
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

static void layout_menu_up_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
	ViewDir *vd = lw->vd;
	gchar *path;

	if (!vd->dir_fd || strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) == 0) return;
	path = remove_level_from_path(vd->dir_fd->path);

	if (vd->select_func)
		{
		FileData *fd = file_data_new_dir(path);
		vd->select_func(vd, fd, vd->select_data);
		file_data_unref(fd);
		}

	g_free(path);
}


/*
 *-----------------------------------------------------------------------------
 * edit menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_edit_cb(GtkAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	const gchar *key = gtk_action_get_name(action);

	if (!editor_window_flag_set(key))
		layout_exit_fullscreen(lw);

	file_util_start_editor_from_filelist(key, layout_selection_list(lw), layout_get_path(lw), lw->window);
}


static void layout_menu_metadata_write_cb(GtkAction *UNUSED(action), gpointer UNUSED(data))
{
	metadata_write_queue_confirm(TRUE, NULL, NULL);
}

static GtkWidget *last_focussed = NULL;
static void layout_menu_keyword_autocomplete_cb(GtkAction *UNUSED(action), gpointer data)
{
	LayoutWindow *lw = data;
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
#ifdef HAVE_LCMS
static void layout_color_menu_enable_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;

	if (layout_image_color_profile_get_use(lw) == gtk_toggle_action_get_active(action)) return;

	layout_image_color_profile_set_use(lw, gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}
#else
static void layout_color_menu_enable_cb()
{
}
#endif

#ifdef HAVE_LCMS
static void layout_color_menu_use_image_cb(GtkToggleAction *action, gpointer data)
{
	LayoutWindow *lw = data;
	gint input;
	gboolean use_image;

	if (!layout_image_color_profile_get(lw, &input, &use_image)) return;
	if (use_image == gtk_toggle_action_get_active(action)) return;
	layout_image_color_profile_set(lw, input, gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}
#else
static void layout_color_menu_use_image_cb()
{
}
#endif

#ifdef HAVE_LCMS
static void layout_color_menu_input_cb(GtkRadioAction *action, GtkRadioAction *UNUSED(current), gpointer data)
{
	LayoutWindow *lw = data;
	gint type;
	gint input;
	gboolean use_image;

	type = gtk_radio_action_get_current_value(action);
	if (type < 0 || type >= COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS) return;

	if (!layout_image_color_profile_get(lw, &input, &use_image)) return;
	if (type == input) return;

	layout_image_color_profile_set(lw, type, use_image);
	layout_image_refresh(lw);
}
#else
static void layout_color_menu_input_cb()
{
}
#endif


/*
 *-----------------------------------------------------------------------------
 * recent menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_recent_cb(GtkWidget *widget, gpointer UNUSED(data))
{
	gint n;
	gchar *path;

	n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "recent_index"));

	path = g_list_nth_data(history_list_get_by_key("recent"), n);

	if (!path) return;

	/* make a copy of it */
	path = g_strdup(path);
	collection_window_new(path);
	g_free(path);
}

static void layout_menu_recent_update(LayoutWindow *lw)
{
	GtkWidget *menu;
	GtkWidget *recent;
	GtkWidget *item;
	GList *list;
	gint n;

	if (!lw->ui_manager) return;

	list = history_list_get_by_key("recent");
	n = 0;

	menu = gtk_menu_new();

	while (list)
		{
		const gchar *filename = filename_from_path((gchar *)list->data);
		gchar *name;
		gboolean free_name = FALSE;

		if (file_extension_match(filename, GQ_COLLECTION_EXT))
			{
			name = remove_extension_from_path(filename);
			free_name = TRUE;
			}
		else
			{
			name = (gchar *) filename;
			}

		item = menu_item_add_simple(menu, name, G_CALLBACK(layout_menu_recent_cb), lw);
		if (free_name) g_free(name);
		g_object_set_data(G_OBJECT(item), "recent_index", GINT_TO_POINTER(n));
		list = list->next;
		n++;
		}

	if (n == 0)
		{
		menu_item_add(menu, _("Empty"), NULL, NULL);
		}

	recent = gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu/FileMenu/OpenRecent");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), menu);
	gtk_widget_set_sensitive(recent, (n != 0));
}

void layout_recent_update_all(void)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		layout_menu_recent_update(lw);
		}
}

void layout_recent_add_path(const gchar *path)
{
	if (!path) return;

	history_list_add_to_key("recent", path, options->open_recent_list_maxsize);

	layout_recent_update_all();
}

/*
 *-----------------------------------------------------------------------------
 * window layout menu
 *-----------------------------------------------------------------------------
 */
typedef struct _WindowNames WindowNames;
struct _WindowNames
{
	gboolean displayed;
	gchar *name;
	gchar *path;
};

typedef struct _RenameWindow RenameWindow;
struct _RenameWindow
{
	GenericDialog *gd;
	LayoutWindow *lw;

	GtkWidget *button_ok;
	GtkWidget *window_name_entry;
};

typedef struct _DeleteWindow DeleteWindow;
struct _DeleteWindow
{
	GenericDialog *gd;
	LayoutWindow *lw;

	GtkWidget *button_ok;
	GtkWidget *group;
};

static gint layout_window_menu_list_sort_cb(gconstpointer a, gconstpointer b)
{
	const WindowNames *wna = a;
	const WindowNames *wnb = b;

	return g_strcmp0((gchar *)wna->name, (gchar *)wnb->name);
}

static GList *layout_window_menu_list(GList *listin)
{
	GList *list;
	WindowNames *wn;
	gboolean dupe;
	DIR *dp;
	struct dirent *dir;
	gchar *pathl;

	pathl = path_from_utf8(get_window_layouts_dir());
	dp = opendir(pathl);
	if (!dp)
		{
		/* dir not found */
		g_free(pathl);
		return listin;
		}

	while ((dir = readdir(dp)) != NULL)
		{
		gchar *name_file = dir->d_name;

		if (g_str_has_suffix(name_file, ".xml"))
			{
			LayoutWindow *lw_tmp ;
			gchar *name_utf8 = path_to_utf8(name_file);
			gchar *name_base = g_strndup(name_utf8, strlen(name_utf8) - 4);
			list = layout_window_list;
			dupe = FALSE;
			while (list)
				{
				lw_tmp = list->data;
				if (g_strcmp0(lw_tmp->options.id, name_base) == 0)
					{
					dupe = TRUE;
					}
				list = list->next;
				}
			gchar *dpath = g_build_filename(pathl, name_utf8, NULL);
			wn  = g_new0(WindowNames, 1);
			wn->displayed = dupe;
			wn->name = g_strdup(name_base);
			wn->path = g_strdup(dpath);
			listin = g_list_append(listin, wn);

			g_free(dpath);
			g_free(name_utf8);
			g_free(name_base);
			}
		}
	closedir(dp);

	g_free(pathl);

	return g_list_sort(listin, layout_window_menu_list_sort_cb);
}

static void layout_menu_new_window_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	gint n;

	n = GPOINTER_TO_INT(data);
	GList *menulist = NULL;

	menulist = layout_window_menu_list(menulist);
	WindowNames *wn = g_list_nth(menulist, n )->data;

	if (wn->path)
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
	GtkWidget *menu;
	GtkWidget *sub_menu;
	GtkWidget *item;
	GList *children, *iter;
	gint n;
	GList *list = NULL;
	gint i = 0;
	WindowNames *wn;

	if (!lw->ui_manager) return;

	list = layout_window_menu_list(list);

	menu = gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu/WindowsMenu/NewWindow");
	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	children = gtk_container_get_children(GTK_CONTAINER(sub_menu));
	for (iter = children; iter != NULL; iter = g_list_next(iter), i++)
		{
		if (i >= 4) // separator, default, from current, separator
			{
			gtk_widget_destroy(GTK_WIDGET(iter->data));
			}
		}
	g_list_free(children);

	menu_item_add_divider(sub_menu);

	n = 0;
	while (list)
		{
		wn = list->data;
		item = menu_item_add_simple(sub_menu, wn->name, G_CALLBACK(layout_menu_new_window_cb), GINT_TO_POINTER(n));
		if (wn->displayed)
			{
			gtk_widget_set_sensitive(item, FALSE);
			}
		list = list->next;
		n++;
		}
}

static void window_rename_cancel_cb(GenericDialog *UNUSED(gd), gpointer data)
{
	RenameWindow *rw = data;

	generic_dialog_close(rw->gd);
	g_free(rw);
}

static void window_rename_ok(GenericDialog *UNUSED(gd), gpointer data)
{
	RenameWindow *rw = data;
	gchar *path;
	gboolean window_layout_name_exists = FALSE;
	GList *list = NULL;
	gchar *xml_name;
	gchar *new_id;

	new_id = g_strdup(gtk_entry_get_text(GTK_ENTRY(rw->window_name_entry)));

	list = layout_window_menu_list(list);
	while (list)
		{
		WindowNames *ln = list->data;
		if (g_strcmp0(ln->name, new_id) == 0)
			{
			gchar *buf;
			buf = g_strdup_printf(_("Window layout name \"%s\" already exists."), new_id);
			warning_dialog(_("Rename window"), buf, GTK_STOCK_DIALOG_WARNING, rw->gd->dialog);
			g_free(buf);
			window_layout_name_exists = TRUE;
			break;
			}
		list = list->next;
		}

	if (!window_layout_name_exists)
		{
		xml_name = g_strdup_printf("%s.xml", rw->lw->options.id);
		path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);

		if (isfile(path))
			{
			unlink_file(path);
			}
		g_free(xml_name);
		g_free(path);

		g_free(rw->lw->options.id);
		rw->lw->options.id = g_strdup(new_id);
		layout_menu_new_window_update(rw->lw);
		layout_refresh(rw->lw);
		image_update_title(rw->lw->image);
		}

	save_layout(rw->lw);

	g_free(new_id);
	generic_dialog_close(rw->gd);
	g_free(rw);
}

static void window_rename_ok_cb(GenericDialog *gd, gpointer data)
{
	RenameWindow *rw = data;

	window_rename_ok(gd, rw);
}

static void window_rename_entry_activate_cb(GenericDialog *gd, gpointer data)
{
	RenameWindow *rw = data;

	window_rename_ok(gd, rw);
}

static void window_delete_cancel_cb(GenericDialog *UNUSED(gd), gpointer data)
{
	DeleteWindow *dw = data;

	g_free(dw);
}

static void window_delete_ok_cb(GenericDialog *UNUSED(gd), gpointer data)
{
	DeleteWindow *dw = data;
	gchar *path;
	gchar *xml_name;

	xml_name = g_strdup_printf("%s.xml", dw->lw->options.id);
	path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);

	layout_close(dw->lw);
	g_free(dw);

	if (isfile(path))
		{
		unlink_file(path);
		}
	g_free(xml_name);
	g_free(path);
}

static void layout_menu_window_default_cb(GtkWidget *UNUSED(widget), gpointer UNUSED(data))
{
	layout_new_from_default();
}

static void layout_menu_windows_menu_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;
	GtkWidget *menu;
	GtkWidget *sub_menu;
	gchar *menu_label;
	GList *children, *iter;
	gint i;

	menu = gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu/WindowsMenu/");
	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	/* disable Delete for temporary windows */
	if (g_str_has_prefix(lw->options.id, "lw"))
		{
		i = 0;
		children = gtk_container_get_children(GTK_CONTAINER(sub_menu));
		for (iter = children; iter != NULL; iter = g_list_next(iter), i++)
			{
			menu_label = g_strdup(gtk_menu_item_get_label(GTK_MENU_ITEM(iter->data)));
			if (g_strcmp0(menu_label, _("Delete window")) == 0)
				{
				gtk_widget_set_sensitive(GTK_WIDGET(iter->data), FALSE);
				}
			g_free(menu_label);
			}
		g_list_free(children);
		}
}

static void layout_menu_view_menu_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;
	GtkWidget *menu;
	GtkWidget *sub_menu;
	gchar *menu_label;
	GList *children, *iter;
	gint i;
	FileData *fd;

	menu = gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu/ViewMenu/");
	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	fd = layout_image_get_fd(lw);

	i = 0;
	children = gtk_container_get_children(GTK_CONTAINER(sub_menu));
	for (iter = children; iter != NULL; iter = g_list_next(iter), i++)
		{
		menu_label = g_strdup(gtk_menu_item_get_label(GTK_MENU_ITEM(iter->data)));
		if (g_strcmp0(menu_label, _("Open archive")) == 0)
			{
			if (fd && fd->format_class == FORMAT_CLASS_ARCHIVE)
				{
				gtk_widget_set_sensitive(GTK_WIDGET(iter->data), TRUE);
				}
			else
				{
				gtk_widget_set_sensitive(GTK_WIDGET(iter->data), FALSE);
				}
			}
		g_free(menu_label);
		}
	g_list_free(children);
}

static void change_window_id(const gchar *infile, const gchar *outfile)
{
	GFile *in_file;
	GFile *out_file;
	GFileInputStream *in_file_stream;
	GFileOutputStream *out_file_stream;
	GDataInputStream *in_data_stream;
	GDataOutputStream *out_data_stream;
	gchar *line;
	gchar *id_name;

	id_name = layout_get_unique_id();

	in_file = g_file_new_for_path(infile);
	in_file_stream = g_file_read(in_file, NULL, NULL);
	in_data_stream = g_data_input_stream_new(G_INPUT_STREAM(in_file_stream));

	out_file = g_file_new_for_path(outfile);
	out_file_stream = g_file_append_to(out_file, G_FILE_CREATE_PRIVATE, NULL, NULL);
	out_data_stream = g_data_output_stream_new(G_OUTPUT_STREAM(out_file_stream));

	while ((line = g_data_input_stream_read_line(in_data_stream, NULL, NULL, NULL)))
		{
		if (g_str_has_suffix(line, "<layout"))
			{
			g_data_output_stream_put_string(out_data_stream, line, NULL, NULL);
			g_data_output_stream_put_string(out_data_stream, "\n", NULL, NULL);
			g_free(line);

			line = g_data_input_stream_read_line(in_data_stream, NULL, NULL, NULL);
			g_data_output_stream_put_string(out_data_stream, "id = \"", NULL, NULL);
			g_data_output_stream_put_string(out_data_stream, id_name, NULL, NULL);
			g_data_output_stream_put_string(out_data_stream, "\"\n", NULL, NULL);
			}
		else
			{
			g_data_output_stream_put_string(out_data_stream, line, NULL, NULL);
			g_data_output_stream_put_string(out_data_stream, "\n", NULL, NULL);
			}
		g_free(line);
		}

	g_free(id_name);
	g_object_unref(out_data_stream);
	g_object_unref(in_data_stream);
	g_object_unref(out_file_stream);
	g_object_unref(in_file_stream);
	g_object_unref(out_file);
	g_object_unref(in_file);
}

static void layout_menu_window_from_current_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;
	gint fd_in = -1;
	gint fd_out = -1;
	char * tmp_file_in;
	char * tmp_file_out;
	GError *error = NULL;

	fd_in = g_file_open_tmp("geeqie_layout_name_XXXXXX.xml", &tmp_file_in, &error);
	if (error)
		{
		log_printf("Error: Window layout - cannot create file:%s\n",error->message);
		g_error_free(error);
		return;
		}
	close(fd_in);
	fd_out = g_file_open_tmp("geeqie_layout_name_XXXXXX.xml", &tmp_file_out, &error);
	if (error)
		{
		log_printf("Error: Window layout - cannot create file:%s\n",error->message);
		g_error_free(error);
		return;
		}
	close(fd_out);

	save_config_to_file(tmp_file_in, options, lw);
	change_window_id(tmp_file_in, tmp_file_out);
	load_config_from_file(tmp_file_out, FALSE);

	unlink_file(tmp_file_in);
	unlink_file(tmp_file_out);
	g_free(tmp_file_in);
	g_free(tmp_file_out);
}

static void layout_menu_window_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;

	layout_menu_new_window_update(lw);
}

static void layout_menu_window_rename_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;
	RenameWindow *rw;
	GtkWidget *hbox;

	rw = g_new0(RenameWindow, 1);
	rw->lw = lw;

	rw->gd = generic_dialog_new(_("Rename window"), "rename_window", NULL, FALSE, window_rename_cancel_cb, rw);
	rw->button_ok = generic_dialog_add_button(rw->gd, GTK_STOCK_OK, _("OK"), window_rename_ok_cb, TRUE);

	generic_dialog_add_message(rw->gd, NULL, _("rename window"), NULL, FALSE);

	hbox = pref_box_new(rw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);

	hbox = pref_box_new(rw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	rw->window_name_entry = gtk_entry_new();
	gtk_widget_set_can_focus(rw->window_name_entry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(rw->window_name_entry), TRUE);
	gtk_entry_set_text(GTK_ENTRY(rw->window_name_entry), lw->options.id);
	gtk_box_pack_start(GTK_BOX(hbox), rw->window_name_entry, TRUE, TRUE, 0);
	gtk_widget_grab_focus(GTK_WIDGET(rw->window_name_entry));
	gtk_widget_show(rw->window_name_entry);
	g_signal_connect(rw->window_name_entry, "activate", G_CALLBACK(window_rename_entry_activate_cb), rw);

	gtk_widget_show(rw->gd->dialog);
}

static void layout_menu_window_delete_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;
	DeleteWindow *dw;
	GtkWidget *hbox;

	dw = g_new0(DeleteWindow, 1);
	dw->lw = lw;

	dw->gd = generic_dialog_new(_("Delete window"), "delete_window", NULL, TRUE, window_delete_cancel_cb, dw);
	dw->button_ok = generic_dialog_add_button(dw->gd, GTK_STOCK_OK, _("OK"), window_delete_ok_cb, TRUE);

	generic_dialog_add_message(dw->gd, NULL, _("Delete window layout"), NULL, FALSE);

	hbox = pref_box_new(dw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	dw->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(dw->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, (lw->options.id));

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
  { "FileMenu",		NULL,			N_("_File"),				NULL,			NULL,					NULL },
  { "GoMenu",		NULL,			N_("_Go"),				NULL,			NULL,					NULL },
  { "EditMenu",		NULL,			N_("_Edit"),				NULL,			NULL,					NULL },
  { "SelectMenu",	NULL,			N_("_Select"),				NULL,			NULL,					NULL },
  { "OrientationMenu",	NULL,			N_("_Orientation"),			NULL,			NULL,					NULL },
  { "RatingMenu",	NULL,			N_("_Rating"),					NULL,			NULL,					NULL },
  { "PreferencesMenu",	NULL,			N_("P_references"),			NULL,			NULL,					NULL },
  { "ViewMenu",		NULL,			N_("_View"),				NULL,			NULL,					CB(layout_menu_view_menu_cb)  },
  { "FileDirMenu",	NULL,			N_("_Files and Folders"),		NULL,			NULL,					NULL },
  { "ZoomMenu",		NULL,			N_("_Zoom"),				NULL,			NULL,					NULL },
  { "ColorMenu",	NULL,			N_("_Color Management"),		NULL,			NULL,					NULL },
  { "ConnectZoomMenu",	NULL,			N_("_Connected Zoom"),			NULL,			NULL,					NULL },
  { "SplitMenu",	NULL,			N_("Spli_t"),				NULL,			NULL,					NULL },
  { "StereoMenu",	NULL,			N_("Stere_o"),				NULL,			NULL,					NULL },
  { "OverlayMenu",	NULL,			N_("Image _Overlay"),			NULL,			NULL,					NULL },
  { "PluginsMenu",	NULL,			N_("_Plugins"),				NULL,			NULL,					NULL },
  { "WindowsMenu",		NULL,		N_("_Windows"),				NULL,			NULL,					CB(layout_menu_windows_menu_cb)  },
  { "HelpMenu",		NULL,			N_("_Help"),				NULL,			NULL,					NULL },

  { "Copy",		GTK_STOCK_COPY,		N_("_Copy..."),				"<control>C",		N_("Copy..."),				CB(layout_menu_copy_cb) },
  { "Move",	PIXBUF_INLINE_ICON_MOVE,			N_("_Move..."),				"<control>M",		N_("Move..."),				CB(layout_menu_move_cb) },
  { "Rename",	PIXBUF_INLINE_ICON_RENAME,	N_("_Rename..."),			"<control>R",		N_("Rename..."),			CB(layout_menu_rename_cb) },
  { "Delete",	PIXBUF_INLINE_ICON_TRASH,	N_("Move to Trash..."),		"<control>D",	N_("Move to Trash..."),		CB(layout_menu_move_to_trash_cb) },
  { "DeleteAlt1",	PIXBUF_INLINE_ICON_TRASH,N_("Move to Trash..."),	"Delete",		N_("Move to Trash..."),		CB(layout_menu_move_to_trash_key_cb) },
  { "DeleteAlt2",	PIXBUF_INLINE_ICON_TRASH,N_("Move to Trash..."),	"KP_Delete",	N_("Move to Trash..."),		CB(layout_menu_move_to_trash_key_cb) },
  { "PermanentDelete",	GTK_STOCK_DELETE,	N_("Delete..."),			"<shift>Delete",N_("Delete..."),			CB(layout_menu_delete_cb) }, 
  { "SelectAll",	PIXBUF_INLINE_ICON_SELECT_ALL,			N_("Select _all"),			"<control>A",		N_("Select all"),			CB(layout_menu_select_all_cb) },
  { "SelectNone",	PIXBUF_INLINE_ICON_SELECT_NONE,			N_("Select _none"),			"<control><shift>A",	N_("Select none"),			CB(layout_menu_unselect_all_cb) },
  { "SelectInvert",	PIXBUF_INLINE_ICON_SELECT_INVERT,			N_("_Invert Selection"),		"<control><shift>I",	N_("Invert Selection"),			CB(layout_menu_invert_selection_cb) },
  { "CloseWindow",	GTK_STOCK_CLOSE,	N_("C_lose window"),			"<control>W",		N_("Close window"),			CB(layout_menu_close_cb) },
  { "Quit",		GTK_STOCK_QUIT, 	N_("_Quit"),				"<control>Q",		N_("Quit"),				CB(layout_menu_exit_cb) },
  { "FirstImage",	GTK_STOCK_GOTO_TOP,	N_("_First Image"),			"Home",			N_("First Image"),			CB(layout_menu_image_first_cb) },
  { "PrevImage",	GTK_STOCK_GO_UP,	N_("_Previous Image"),			"BackSpace",		N_("Previous Image"),			CB(layout_menu_image_prev_cb) },
  { "PrevImageAlt1",	GTK_STOCK_GO_UP,	N_("_Previous Image"),			"Page_Up",		N_("Previous Image"),			CB(layout_menu_image_prev_cb) },
  { "PrevImageAlt2",	GTK_STOCK_GO_UP,	N_("_Previous Image"),			"KP_Page_Up",		N_("Previous Image"),			CB(layout_menu_image_prev_cb) },
  { "NextImage",	GTK_STOCK_GO_DOWN,	N_("_Next Image"),			"space",		N_("Next Image"),			CB(layout_menu_image_next_cb) },
  { "NextImageAlt1",	GTK_STOCK_GO_DOWN,	N_("_Next Image"),			"Page_Down",		N_("Next Image"),			CB(layout_menu_image_next_cb) },

  { "ImageForward",	GTK_STOCK_GOTO_LAST,	N_("Image Forward"),	NULL,	N_("Forward in image history"),	CB(layout_menu_image_forward_cb) },
  { "ImageBack",	GTK_STOCK_GOTO_FIRST,	N_("Image Back"),		NULL,	N_("Back in image history"),		CB(layout_menu_image_back_cb) },

  { "FirstPage",GTK_STOCK_MEDIA_PREVIOUS,	N_("_First Page"),		NULL,	N_( "First Page of multi-page image"),	CB(layout_menu_page_first_cb) },
  { "LastPage",	GTK_STOCK_MEDIA_NEXT,		N_("_Last Page"),		NULL,	N_("Last Page of multi-page image"),	CB(layout_menu_page_last_cb) },
  { "NextPage",	GTK_STOCK_MEDIA_FORWARD,	N_("_Next Page"),		NULL,	N_("Next Page of multi-page image"),	CB(layout_menu_page_next_cb) },
  { "PrevPage",	GTK_STOCK_MEDIA_REWIND,		N_("_Previous Page"),	NULL,	N_("Previous Page of multi-page image"),	CB(layout_menu_page_previous_cb) },


  { "NextImageAlt2",	GTK_STOCK_GO_DOWN,	N_("_Next Image"),			"KP_Page_Down",		N_("Next Image"),		CB(layout_menu_image_next_cb) },
  { "LastImage",	GTK_STOCK_GOTO_BOTTOM,	N_("_Last Image"),			"End",			N_("Last Image"),			CB(layout_menu_image_last_cb) },
  { "Back",		GTK_STOCK_GO_BACK,	N_("_Back"),			NULL,	N_("Back in folder history"),		CB(layout_menu_back_cb) },
  { "Forward",	GTK_STOCK_GO_FORWARD,	N_("_Forward"),		NULL,	N_("Forward in folder history"),	CB(layout_menu_forward_cb) },
  { "Home",		GTK_STOCK_HOME,		N_("_Home"),			NULL,	N_("Home"),				CB(layout_menu_home_cb) },
  { "Up",		GTK_STOCK_GO_UP,	N_("_Up"),				NULL,	N_("Up one folder"),				CB(layout_menu_up_cb) },
  { "NewWindow",	NULL,		N_("New window"),			NULL,		N_("New window"),	CB(layout_menu_window_cb) },
  { "NewWindowDefault",	NULL,	N_("default"),			"<control>N",		N_("New window (default)"),	CB(layout_menu_window_default_cb)  },
  { "NewWindowFromCurrent",	NULL,	N_("from current"),			NULL,		N_("from current"),	CB(layout_menu_window_from_current_cb)  },
  { "RenameWindow",	GTK_STOCK_EDIT,		N_("Rename window"),	NULL,	N_("Rename window"),	CB(layout_menu_window_rename_cb) },
  { "DeleteWindow",	GTK_STOCK_DELETE,		N_("Delete window"),	NULL,	N_("Delete window"),	CB(layout_menu_window_delete_cb) },
  { "NewCollection",	GTK_STOCK_INDEX,	N_("_New collection"),			"C",			N_("New collection"),			CB(layout_menu_new_cb) },
  { "OpenCollection",	GTK_STOCK_OPEN,		N_("_Open collection..."),		"O",			N_("Open collection..."),		CB(layout_menu_open_cb) },
  { "OpenRecent",	NULL,			N_("Open recen_t"),			NULL,			N_("Open recent collection"),			NULL },
  { "Search",		GTK_STOCK_FIND,		N_("_Search..."),			"F3",			N_("Search..."),			CB(layout_menu_search_cb) },
  { "FindDupes",	GTK_STOCK_FIND,		N_("_Find duplicates..."),		"D",			N_("Find duplicates..."),		CB(layout_menu_dupes_cb) },
  { "PanView",	PIXBUF_INLINE_ICON_PANORAMA,	N_("Pa_n view"),			"<control>J",		N_("Pan view"),				CB(layout_menu_pan_cb) },
  { "Print",		GTK_STOCK_PRINT,	N_("_Print..."),			"<shift>P",		N_("Print..."),				CB(layout_menu_print_cb) },
  { "NewFolder",	GTK_STOCK_DIRECTORY,	N_("N_ew folder..."),			"<control>F",		N_("New folder..."),			CB(layout_menu_dir_cb) },
  { "EnableGrouping",	NULL,			N_("Enable file _grouping"),		NULL,			N_("Enable file grouping"),		CB(layout_menu_enable_grouping_cb) },
  { "DisableGrouping",	NULL,			N_("Disable file groupi_ng"),		NULL,			N_("Disable file grouping"),		CB(layout_menu_disable_grouping_cb) },
  { "CopyPath",		NULL,			N_("_Copy path to clipboard"),		NULL,			N_("Copy path to clipboard"),		CB(layout_menu_copy_path_cb) },
  { "CopyPathUnquoted",		NULL,			N_("_Copy path unquoted to clipboard"),		NULL,			N_("Copy path unquoted to clipboard"),		CB(layout_menu_copy_path_unquoted_cb) },
  { "Rating0",		NULL,			N_("_Rating 0"),	"<alt>KP_0",	N_("Rating 0"),			CB(layout_menu_rating_0_cb) },
  { "Rating1",		NULL,			N_("_Rating 1"),	"<alt>KP_1",	N_("Rating 1"),			CB(layout_menu_rating_1_cb) },
  { "Rating2",		NULL,			N_("_Rating 2"),	"<alt>KP_2",	N_("Rating 2"),			CB(layout_menu_rating_2_cb) },
  { "Rating3",		NULL,			N_("_Rating 3"),	"<alt>KP_3",	N_("Rating 3"),			CB(layout_menu_rating_3_cb) },
  { "Rating4",		NULL,			N_("_Rating 4"),	"<alt>KP_4",	N_("Rating 4"),			CB(layout_menu_rating_4_cb) },
  { "Rating5",		NULL,			N_("_Rating 5"),	"<alt>KP_5",	N_("Rating 5"),			CB(layout_menu_rating_5_cb) },
  { "RatingM1",		NULL,			N_("_Rating -1"),	"<alt>KP_Subtract",	N_("Rating -1"),	CB(layout_menu_rating_m1_cb) },
  { "RotateCW",		PIXBUF_INLINE_ICON_CW,			N_("_Rotate clockwise 90°"),		"bracketright",		N_("Image Rotate clockwise 90°"),			CB(layout_menu_alter_90_cb) },
  { "RotateCCW",	PIXBUF_INLINE_ICON_CCW,	N_("Rotate _counterclockwise 90°"),		"bracketleft",		N_("Rotate counterclockwise 90°"),		CB(layout_menu_alter_90cc_cb) },
  { "Rotate180",	PIXBUF_INLINE_ICON_180,	N_("Rotate 1_80°"),	"<shift>R",		N_("Image Rotate 180°"),			CB(layout_menu_alter_180_cb) },
  { "Mirror",		PIXBUF_INLINE_ICON_MIRROR,	N_("_Mirror"),	"<shift>M",		N_("Image Mirror"),				CB(layout_menu_alter_mirror_cb) },
  { "Flip",		PIXBUF_INLINE_ICON_FLIP,	N_("_Flip"),	"<shift>F",		N_("Image Flip"),				CB(layout_menu_alter_flip_cb) },
  { "AlterNone",	PIXBUF_INLINE_ICON_ORIGINAL,	N_("_Original state"), 	"<shift>O",		N_("Image rotate Original state"),			CB(layout_menu_alter_none_cb) },
  { "Preferences",	GTK_STOCK_PREFERENCES,	N_("P_references..."),			"<control>O",		N_("Preferences..."),			CB(layout_menu_config_cb) },
  { "Plugins",		GTK_STOCK_PREFERENCES,	N_("Configure _Plugins..."),		NULL,			N_("Configure Plugins..."),		CB(layout_menu_editors_cb) },
  { "LayoutConfig",	GTK_STOCK_PREFERENCES,	N_("_Configure this window..."),	NULL,			N_("Configure this window..."),		CB(layout_menu_layout_config_cb) },
  { "Maintenance",	PIXBUF_INLINE_ICON_MAINTENANCE,	N_("_Cache maintenance..."),	NULL,			N_("Cache maintenance..."),		CB(layout_menu_remove_thumb_cb) },
  { "Wallpaper",	NULL,			N_("Set as _wallpaper"),		NULL,			N_("Set as wallpaper"),			CB(layout_menu_wallpaper_cb) },
  { "SaveMetadata",	GTK_STOCK_SAVE,		N_("_Save metadata"),			"<control>S",		N_("Save metadata"),			CB(layout_menu_metadata_write_cb) },
  { "KeywordAutocomplete",	NULL,	N_("Keyword autocomplete"),		"<alt>K",		N_("Keyword Autocomplete"),			CB(layout_menu_keyword_autocomplete_cb) },
  { "ZoomInAlt1",	GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),				"KP_Add",		N_("Zoom in"),				CB(layout_menu_zoom_in_cb) },
  { "ZoomIn",		GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),				"equal",		N_("Zoom in"),				CB(layout_menu_zoom_in_cb) },
  { "ZoomOut",		GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),			"minus",		N_("Zoom out"),				CB(layout_menu_zoom_out_cb) },
  { "ZoomOutAlt1",	GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),			"KP_Subtract",		N_("Zoom out"),				CB(layout_menu_zoom_out_cb) },
  { "Zoom100",		GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),			"Z",			N_("Zoom 1:1"),				CB(layout_menu_zoom_1_1_cb) },
  { "Zoom100Alt1",	GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),			"KP_Divide",		N_("Zoom 1:1"),				CB(layout_menu_zoom_1_1_cb) },
  { "ZoomFitAlt1",	GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),			"KP_Multiply",		N_("Zoom to fit"),			CB(layout_menu_zoom_fit_cb) },
  { "ZoomFit",		GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),			"X",			N_("Zoom to fit"),			CB(layout_menu_zoom_fit_cb) },
  { "ZoomFillHor",	PIXBUF_INLINE_ICON_ZOOMFILLHOR,	N_("Fit _Horizontally"),		"H",			N_("Fit Horizontally"),			CB(layout_menu_zoom_fit_hor_cb) },
  { "ZoomFillVert",	PIXBUF_INLINE_ICON_ZOOMFILLVERT,	N_("Fit _Vertically"),			"W",			N_("Fit Vertically"),			CB(layout_menu_zoom_fit_vert_cb) },
  { "Zoom200",	        GTK_STOCK_FILE,			N_("Zoom _2:1"),			NULL,			N_("Zoom 2:1"),				CB(layout_menu_zoom_2_1_cb) },
  { "Zoom300",	        GTK_STOCK_FILE,			N_("Zoom _3:1"),			NULL,			N_("Zoom 3:1"),				CB(layout_menu_zoom_3_1_cb) },
  { "Zoom400",		GTK_STOCK_FILE,			N_("Zoom _4:1"),			NULL,			N_("Zoom 4:1"),				CB(layout_menu_zoom_4_1_cb) },
  { "Zoom50",		GTK_STOCK_FILE,			N_("Zoom 1:2"),				NULL,			N_("Zoom 1:2"),				CB(layout_menu_zoom_1_2_cb) },
  { "Zoom33",		GTK_STOCK_FILE,			N_("Zoom 1:3"),				NULL,			N_("Zoom 1:3"),				CB(layout_menu_zoom_1_3_cb) },
  { "Zoom25",		GTK_STOCK_FILE,			N_("Zoom 1:4"),				NULL,			N_("Zoom 1:4"),				CB(layout_menu_zoom_1_4_cb) },
  { "ConnectZoomIn",	GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),				"plus",			N_("Connected Zoom in"),		CB(layout_menu_connect_zoom_in_cb) },
  { "ConnectZoomInAlt1",GTK_STOCK_ZOOM_IN,	N_("Zoom _in"),				"<shift>KP_Add",	N_("Connected Zoom in"),		CB(layout_menu_connect_zoom_in_cb) },
  { "ConnectZoomOut",	GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),			"underscore",		N_("Connected Zoom out"),		CB(layout_menu_connect_zoom_out_cb) },
  { "ConnectZoomOutAlt1",GTK_STOCK_ZOOM_OUT,	N_("Zoom _out"),			"<shift>KP_Subtract",	N_("Connected Zoom out"),		CB(layout_menu_connect_zoom_out_cb) },
  { "ConnectZoom100",	GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),			"<shift>Z",		N_("Connected Zoom 1:1"),		CB(layout_menu_connect_zoom_1_1_cb) },
  { "ConnectZoom100Alt1",GTK_STOCK_ZOOM_100,	N_("Zoom _1:1"),			"<shift>KP_Divide",	N_("Connected Zoom 1:1"),		CB(layout_menu_connect_zoom_1_1_cb) },
  { "ConnectZoomFit",	GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),			"<shift>X",		N_("Connected Zoom to fit"),		CB(layout_menu_connect_zoom_fit_cb) },
  { "ConnectZoomFitAlt1",GTK_STOCK_ZOOM_FIT,	N_("_Zoom to fit"),			"<shift>KP_Multiply",	N_("Connected Zoom to fit"),		CB(layout_menu_connect_zoom_fit_cb) },
  { "ConnectZoomFillHor",NULL,			N_("Fit _Horizontally"),		"<shift>H",		N_("Connected Fit Horizontally"),	CB(layout_menu_connect_zoom_fit_hor_cb) },
  { "ConnectZoomFillVert",NULL,			N_("Fit _Vertically"),			"<shift>W",		N_("Connected Fit Vertically"),		CB(layout_menu_connect_zoom_fit_vert_cb) },
  { "ConnectZoom200",	NULL,			N_("Zoom _2:1"),			NULL,			N_("Connected Zoom 2:1"),		CB(layout_menu_connect_zoom_2_1_cb) },
  { "ConnectZoom300",	NULL,			N_("Zoom _3:1"),			NULL,			N_("Connected Zoom 3:1"),		CB(layout_menu_connect_zoom_3_1_cb) },
  { "ConnectZoom400",	NULL,			N_("Zoom _4:1"),			NULL,			N_("Connected Zoom 4:1"),		CB(layout_menu_connect_zoom_4_1_cb) },
  { "ConnectZoom50",	NULL,			N_("Zoom 1:2"),				NULL,			N_("Connected Zoom 1:2"),		CB(layout_menu_connect_zoom_1_2_cb) },
  { "ConnectZoom33",	NULL,			N_("Zoom 1:3"),				NULL,			N_("Connected Zoom 1:3"),		CB(layout_menu_connect_zoom_1_3_cb) },
  { "ConnectZoom25",	NULL,			N_("Zoom 1:4"),				NULL,			N_("Connected Zoom 1:4"),		CB(layout_menu_connect_zoom_1_4_cb) },
  { "ViewInNewWindow",	NULL,			N_("_View in new window"),		"<control>V",		N_("View in new window"),		CB(layout_menu_view_in_new_window_cb) },
  { "OpenArchive",	GTK_STOCK_OPEN,			N_("Open archive"),		NULL,		N_("Open archive"),		CB(layout_menu_open_archive_cb) },
  { "FullScreen",	GTK_STOCK_FULLSCREEN,	N_("F_ull screen"),			"F",			N_("Full screen"),			CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt1",	GTK_STOCK_FULLSCREEN,	N_("F_ull screen"),			"V",			N_("Full screen"),			CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt2",	GTK_STOCK_FULLSCREEN,	N_("F_ull screen"),			"F11",			N_("Full screen"),			CB(layout_menu_fullscreen_cb) },
  { "Escape",		GTK_STOCK_LEAVE_FULLSCREEN,N_("_Leave full screen"),		"Escape",		N_("Leave full screen"),		CB(layout_menu_escape_cb) },
  { "EscapeAlt1",	GTK_STOCK_LEAVE_FULLSCREEN,N_("_Leave full screen"),		"Q",			N_("Leave full screen"),		CB(layout_menu_escape_cb) },
  { "ImageOverlayCycle",NULL,			N_("_Cycle through overlay modes"),	"I",			N_("Cycle through Overlay modes"),	CB(layout_menu_overlay_toggle_cb) },
  { "HistogramChanCycle",NULL,			N_("Cycle through histogram ch_annels"),"K",			N_("Cycle through histogram channels"),	CB(layout_menu_histogram_toggle_channel_cb) },
  { "HistogramModeCycle",NULL,			N_("Cycle through histogram mo_des"),	"J",			N_("Cycle through histogram modes"),	CB(layout_menu_histogram_toggle_mode_cb) },
  { "HideTools",	PIXBUF_INLINE_ICON_HIDETOOLS,	N_("_Hide file list"),			"<control>H",		N_("Hide file list"),			CB(layout_menu_hide_cb) },
  { "SlideShowPause",	GTK_STOCK_MEDIA_PAUSE,	N_("_Pause slideshow"), 		"P",			N_("Pause slideshow"), 			CB(layout_menu_slideshow_pause_cb) },
  { "SlideShowFaster",	GTK_STOCK_FILE,	N_("Faster"), 		"<control>equal",			N_("Slideshow Faster"), 			CB(layout_menu_slideshow_faster_cb) },
  { "SlideShowSlower",	GTK_STOCK_FILE,	N_("Slower"), 		"<control>minus",			N_("Slideshow Slower"), 			CB(layout_menu_slideshow_slower_cb) },
  { "Refresh",		GTK_STOCK_REFRESH,	N_("_Refresh"),				"R",			N_("Refresh"),				CB(layout_menu_refresh_cb) },
  { "HelpContents",	GTK_STOCK_HELP,		N_("_Help manual"),			"F1",			N_("Help manual"),				CB(layout_menu_help_cb) },
  { "HelpSearch",	NULL,		N_("On-line help search"),			NULL,			N_("On-line help search"),				CB(layout_menu_help_search_cb) },
  { "HelpShortcuts",	NULL,			N_("_Keyboard shortcuts"),		NULL,			N_("Keyboard shortcuts"),		CB(layout_menu_help_keys_cb) },
  { "HelpKbd",		NULL,			N_("_Keyboard map"),			NULL,			N_("Keyboard map"),			CB(layout_menu_kbd_map_cb) },
  { "HelpNotes",	NULL,			N_("_Readme"),			NULL,			N_("Readme"),			CB(layout_menu_notes_cb) },
  { "HelpChangeLog",	NULL,			N_("_ChangeLog"),			NULL,			N_("ChangeLog notes"),			CB(layout_menu_changelog_cb) },
  { "SearchAndRunCommand",	GTK_STOCK_FIND,		N_("Search and Run command"),	"slash",	N_("Search commands by keyword and run them"),	CB(layout_menu_search_and_run_cb) },
  { "About",		GTK_STOCK_ABOUT,	N_("_About"),				NULL,			N_("About"),				CB(layout_menu_about_cb) },
  { "LogWindow",	NULL,			N_("_Log Window"),			NULL,			N_("Log Window"),			CB(layout_menu_log_window_cb) },
  { "ExifWin",		PIXBUF_INLINE_ICON_EXIF,	N_("_Exif window"),			"<control>E",		N_("Exif window"),			CB(layout_menu_bar_exif_cb) },
  { "StereoCycle",	NULL,			N_("_Cycle through stereo modes"),	NULL,			N_("Cycle through stereo modes"),	CB(layout_menu_stereo_mode_next_cb) },
  { "SplitNextPane",	NULL,			N_("_Next Pane"),	"<alt>Right",			N_("Next Split Pane"),	CB(layout_menu_split_pane_next_cb) },
  { "SplitPreviousPane",	NULL,			N_("_Previous Pane"),	"<alt>Left",			N_("Previous Split Pane"),	CB(layout_menu_split_pane_prev_cb) },
  { "SplitUpPane",	NULL,			N_("_Up Pane"),	"<alt>Up",			N_("Up Split Pane"),	CB(layout_menu_split_pane_updown_cb) },
  { "SplitDownPane",	NULL,			N_("_Down Pane"),	"<alt>Down",			N_("Down Split Pane"),	CB(layout_menu_split_pane_updown_cb) },
  { "WriteRotation",	NULL,			N_("_Write orientation to file"),  		NULL,		N_("Write orientation to file"),			CB(layout_menu_write_rotate_cb) },
  { "WriteRotationKeepDate",	NULL,			N_("_Write orientation to file (preserve timestamp)"),  		NULL,		N_("Write orientation to file (preserve timestamp)"),			CB(layout_menu_write_rotate_keep_date_cb) },
  { "ClearMarks",	NULL,		N_("Clear Marks..."),			NULL,		N_("Clear Marks"),			CB(layout_menu_clear_marks_cb) },
};

static GtkToggleActionEntry menu_toggle_entries[] = {
  { "Thumbnails",	PIXBUF_INLINE_ICON_THUMB,N_("Show _Thumbnails"),		"T",			N_("Show Thumbnails"),			CB(layout_menu_thumb_cb),	 FALSE },
  { "ShowMarks",        PIXBUF_INLINE_ICON_MARKS,	N_("Show _Marks"),			"M",			N_("Show Marks"),			CB(layout_menu_marks_cb),	 FALSE  },
  { "ShowFileFilter", PIXBUF_INLINE_ICON_FILE_FILTER,	N_("Show File Filter"),	NULL,	N_("Show File Filter"),	CB(layout_menu_file_filter_cb),	 FALSE  },
  { "ShowInfoPixel",	GTK_STOCK_COLOR_PICKER,	N_("Pi_xel Info"),			NULL,			N_("Show Pixel Info"),			CB(layout_menu_info_pixel_cb),	 FALSE  },
  { "IgnoreAlpha", GTK_STOCK_STRIKETHROUGH,           N_("Hide _alpha"),          "<shift>A",     N_("Hide alpha channel"),       CB(layout_menu_alter_ignore_alpha_cb), FALSE},
  { "FloatTools",	PIXBUF_INLINE_ICON_FLOAT,N_("_Float file list"),		"L",			N_("Float file list"),			CB(layout_menu_float_cb),	 FALSE  },
  { "HideToolbar",	NULL,			N_("Hide tool_bar"),			NULL,			N_("Hide toolbar"),			CB(layout_menu_toolbar_cb),	 FALSE  },
  { "SBar",	PIXBUF_INLINE_ICON_INFO,	N_("_Info sidebar"),			"<control>K",		N_("Info sidebar"),			CB(layout_menu_bar_cb),		 FALSE  },
  { "SBarSort",	PIXBUF_INLINE_ICON_SORT,	N_("Sort _manager"),			"<shift>S",		N_("Sort manager"),			CB(layout_menu_bar_sort_cb),	 FALSE  },
  { "HideBars",		NULL,			N_("Hide Bars"),			"grave",		N_("Hide Bars"),			CB(layout_menu_hide_bars_cb),	 FALSE  },
  { "SlideShow",	GTK_STOCK_MEDIA_PLAY,	N_("Toggle _slideshow"),		"S",			N_("Toggle slideshow"),			CB(layout_menu_slideshow_cb),	 FALSE  },
  { "UseColorProfiles",	GTK_STOCK_SELECT_COLOR,	N_("Use _color profiles"), 		NULL,			N_("Use color profiles"), 		CB(layout_color_menu_enable_cb), FALSE},
  { "UseImageProfile",	NULL,			N_("Use profile from _image"),		NULL,			N_("Use profile from image"),		CB(layout_color_menu_use_image_cb), FALSE},
  { "Grayscale",	PIXBUF_INLINE_ICON_GRAYSCALE,	N_("Toggle _grayscale"),	"<shift>G",		N_("Toggle grayscale"),		CB(layout_menu_alter_desaturate_cb), FALSE},
  { "ImageOverlay",	NULL,			N_("Image _Overlay"),			NULL,			N_("Image Overlay"),			CB(layout_menu_overlay_cb),	 FALSE },
  { "ImageHistogram",	NULL,			N_("_Show Histogram"),			NULL,			N_("Show Histogram"),			CB(layout_menu_histogram_cb),	 FALSE },
  { "RectangularSelection",	PIXBUF_INLINE_ICON_SELECT_RECTANGLE,	N_("Rectangular Selection"),			"<alt>R",			N_("Rectangular Selection"),			CB(layout_menu_rectangular_selection_cb),	 FALSE },
  { "Animate",	NULL,	N_("GIF _animation"),		"A",			N_("Toggle GIF animation"),			CB(layout_menu_animate_cb),	 FALSE  },
  { "ExifRotate",	GTK_STOCK_ORIENTATION_PORTRAIT,			N_("_Exif rotate"),  		"<alt>X",		N_("Toggle Exif rotate"),			CB(layout_menu_exif_rotate_cb), FALSE },
  { "DrawRectangle",	PIXBUF_INLINE_ICON_DRAW_RECTANGLE,			N_("Draw Rectangle"),  		NULL,		N_("Draw Rectangle"),			CB(layout_menu_select_rectangle_cb), FALSE },
  { "OverUnderExposed",	PIXBUF_INLINE_ICON_EXPOSURE,	N_("Over/Under Exposed"),  	"<shift>E",		N_("Highlight over/under exposed"),		CB(layout_menu_select_overunderexposed_cb), FALSE },
  { "SplitPaneSync",	PIXBUF_INLINE_SPLIT_PANE_SYNC,			N_("Split Pane Sync"),	NULL,		N_("Split Pane Sync"),	CB(layout_menu_split_pane_sync_cb), FALSE },
};

static GtkRadioActionEntry menu_radio_entries[] = {
  { "ViewList",		NULL,			N_("Images as _List"),			"<control>L",		N_("View Images as List"),		FILEVIEW_LIST },
  { "ViewIcons",	NULL,			N_("Images as I_cons"),			"<control>I",		N_("View Images as Icons"),		FILEVIEW_ICON }
};

static GtkToggleActionEntry menu_view_dir_toggle_entries[] = {
  { "FolderTree",	NULL,			N_("T_oggle Folder View"),			"<control>T",		N_("Toggle Folders View"), 		CB(layout_menu_view_dir_as_cb),FALSE },
};

static GtkRadioActionEntry menu_split_radio_entries[] = {
  { "SplitHorizontal",	NULL,			N_("_Horizontal"),			"E",			N_("Split panes horizontal."),			SPLIT_HOR },
  { "SplitVertical",	NULL,			N_("_Vertical"),			"U",			N_("Split panes vertical"),				SPLIT_VERT },
  { "SplitQuad",	NULL,			N_("_Quad"),				NULL,			N_("Split panes quad"),				SPLIT_QUAD },
  { "SplitSingle",	NULL,			N_("_Single"),				"Y",			N_("Single pane"),				SPLIT_NONE }
};

static GtkRadioActionEntry menu_color_radio_entries[] = {
  { "ColorProfile0",	NULL,			N_("Input _0: sRGB"),			NULL,			N_("Input 0: sRGB"),			COLOR_PROFILE_SRGB },
  { "ColorProfile1",	NULL,			N_("Input _1: AdobeRGB compatible"),	NULL,			N_("Input 1: AdobeRGB compatible"),	COLOR_PROFILE_ADOBERGB },
  { "ColorProfile2",	NULL,			N_("Input _2"),				NULL,			N_("Input 2"),				COLOR_PROFILE_FILE },
  { "ColorProfile3",	NULL,			N_("Input _3"),				NULL,			N_("Input 3"),				COLOR_PROFILE_FILE + 1 },
  { "ColorProfile4",	NULL,			N_("Input _4"),				NULL,			N_("Input 4"),				COLOR_PROFILE_FILE + 2 },
  { "ColorProfile5",	NULL,			N_("Input _5"),				NULL,			N_("Input 5"),				COLOR_PROFILE_FILE + 3 }
};

static GtkRadioActionEntry menu_histogram_channel[] = {
  { "HistogramChanR",	NULL,			N_("Histogram on _Red"),		NULL,			N_("Histogram on Red"),		HCHAN_R },
  { "HistogramChanG",	NULL,			N_("Histogram on _Green"),		NULL,			N_("Histogram on Green"),	HCHAN_G },
  { "HistogramChanB",	NULL,			N_("Histogram on _Blue"),		NULL,			N_("Histogram on Blue"),	HCHAN_B },
  { "HistogramChanRGB",	NULL,			N_("_Histogram on RGB"),			NULL,			N_("Histogram on RGB"),		HCHAN_RGB },
  { "HistogramChanV",	NULL,			N_("Histogram on _Value"),		NULL,			N_("Histogram on Value"),	HCHAN_MAX }
};

static GtkRadioActionEntry menu_histogram_mode[] = {
  { "HistogramModeLin",	NULL,			N_("Li_near Histogram"),		NULL,			N_("Linear Histogram"),		0 },
  { "HistogramModeLog",	NULL,			N_("_Log Histogram"),			NULL,			N_("Log Histogram"),		1 },
};

static GtkRadioActionEntry menu_stereo_mode_entries[] = {
  { "StereoAuto",	NULL,			N_("_Auto"),				NULL,			N_("Stereo Auto"),		STEREO_PIXBUF_DEFAULT },
  { "StereoSBS",	NULL,			N_("_Side by Side"),			NULL,			N_("Stereo Side by Side"),	STEREO_PIXBUF_SBS },
  { "StereoCross",	NULL,			N_("_Cross"),				NULL,			N_("Stereo Cross"),		STEREO_PIXBUF_CROSS },
  { "StereoOff",	NULL,			N_("_Off"),				NULL,			N_("Stereo Off"),		STEREO_PIXBUF_NONE }
};


#undef CB

static const gchar *menu_ui_description =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='NewCollection'/>"
"      <menuitem action='OpenCollection'/>"
"      <menuitem action='OpenRecent'/>"
"      <placeholder name='OpenSection'/>"
"      <separator/>"
"      <menuitem action='Search'/>"
"      <menuitem action='FindDupes'/>"
"      <placeholder name='SearchSection'/>"
"      <separator/>"
"      <menuitem action='Print'/>"
"      <placeholder name='PrintSection'/>"
"      <separator/>"
"      <menuitem action='NewFolder'/>"
"      <menuitem action='Copy'/>"
"      <menuitem action='Move'/>"
"      <menuitem action='Rename'/>"
"      <separator/>"
"      <menuitem action='Delete'/>"
"      <menuitem action='PermanentDelete'/>"
"      <separator/>"
"      <placeholder name='FileOpsSection'/>"
"      <separator/>"
"      <placeholder name='QuitSection'/>"
"      <menuitem action='Quit'/>"
"      <separator/>"
"    </menu>"
"    <menu action='GoMenu'>"
"      <menuitem action='FirstImage'/>"
"      <menuitem action='PrevImage'/>"
"      <menuitem action='NextImage'/>"
"      <menuitem action='LastImage'/>"
"      <menuitem action='ImageBack'/>"
"      <menuitem action='ImageForward'/>"
"      <separator/>"
"      <menuitem action='Back'/>"
"      <menuitem action='Forward'/>"
"      <menuitem action='Up'/>"
"      <menuitem action='Home'/>"
"      <separator/>"
"      <menuitem action='FirstPage'/>"
"      <menuitem action='LastPage'/>"
"      <menuitem action='NextPage'/>"
"      <menuitem action='PrevPage'/>"
"    </menu>"
"    <menu action='SelectMenu'>"
"      <menuitem action='SelectAll'/>"
"      <menuitem action='SelectNone'/>"
"      <menuitem action='SelectInvert'/>"
"      <menuitem action='RectangularSelection'/>"
"      <menuitem action='ShowFileFilter'/>"
"      <placeholder name='SelectSection'/>"
"      <separator/>"
"      <menuitem action='CopyPath'/>"
"      <menuitem action='CopyPathUnquoted'/>"
"      <placeholder name='ClipboardSection'/>"
"      <separator/>"
"      <menuitem action='ShowMarks'/>"
"      <menuitem action='ClearMarks'/>"
"      <placeholder name='MarksSection'/>"
"      <separator/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <placeholder name='EditSection'/>"
"      <separator/>"
"      <menu action='OrientationMenu'>"
"        <menuitem action='RotateCW'/>"
"        <menuitem action='RotateCCW'/>"
"        <menuitem action='Rotate180'/>"
"        <menuitem action='Mirror'/>"
"        <menuitem action='Flip'/>"
"        <menuitem action='AlterNone'/>"
"        <separator/>"
"        <menuitem action='ExifRotate'/>"
"        <separator/>"
"        <menuitem action='WriteRotation'/>"
"        <menuitem action='WriteRotationKeepDate'/>"
"        <separator/>"
"      </menu>"
"      <menu action='RatingMenu'>"
"        <menuitem action='Rating0'/>"
"        <menuitem action='Rating1'/>"
"        <menuitem action='Rating2'/>"
"        <menuitem action='Rating3'/>"
"        <menuitem action='Rating4'/>"
"        <menuitem action='Rating5'/>"
"        <menuitem action='RatingM1'/>"
"        <separator/>"
"      </menu>"
"      <menuitem action='SaveMetadata'/>"
"      <menuitem action='KeywordAutocomplete'/>"
"      <placeholder name='PropertiesSection'/>"
"      <separator/>"
"      <menuitem action='DrawRectangle'/>"
"      <separator/>"
"      <menuitem action='Preferences'/>"
"      <menuitem action='Plugins'/>"
"      <menuitem action='LayoutConfig'/>"
"      <menuitem action='Maintenance'/>"
"      <placeholder name='PreferencesSection'/>"
"      <separator/>"
"      <separator/>"
"    </menu>"
"    <menu action='PluginsMenu'>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='ViewInNewWindow'/>"
"      <menuitem action='PanView'/>"
"      <menuitem action='ExifWin'/>"
"      <menuitem action='OpenArchive'/>"
"      <placeholder name='WindowSection'/>"
"      <separator/>"
"      <menu action='FileDirMenu'>"
"        <menuitem action='FolderTree'/>"
"        <placeholder name='FolderSection'/>"
"        <separator/>"
"        <menuitem action='ViewList'/>"
"        <menuitem action='ViewIcons'/>"
"        <menuitem action='Thumbnails'/>"
"        <placeholder name='ListSection'/>"
"        <separator/>"
"        <menuitem action='FloatTools'/>"
"        <menuitem action='HideTools'/>"
"        <menuitem action='HideToolbar'/>"
"      </menu>"
"      <placeholder name='DirSection'/>"
"      <separator/>"
"      <menu action='ZoomMenu'>"
"        <menu action='ConnectZoomMenu'>"
"          <menuitem action='ConnectZoomIn'/>"
"          <menuitem action='ConnectZoomOut'/>"
"          <menuitem action='ConnectZoomFit'/>"
"          <menuitem action='ConnectZoomFillHor'/>"
"          <menuitem action='ConnectZoomFillVert'/>"
"          <menuitem action='ConnectZoom100'/>"
"          <menuitem action='ConnectZoom200'/>"
"          <menuitem action='ConnectZoom300'/>"
"          <menuitem action='ConnectZoom400'/>"
"          <menuitem action='ConnectZoom50'/>"
"          <menuitem action='ConnectZoom33'/>"
"          <menuitem action='ConnectZoom25'/>"
"        </menu>"
"        <menuitem action='ZoomIn'/>"
"        <menuitem action='ZoomOut'/>"
"        <menuitem action='ZoomFit'/>"
"        <menuitem action='ZoomFillHor'/>"
"        <menuitem action='ZoomFillVert'/>"
"        <menuitem action='Zoom100'/>"
"        <menuitem action='Zoom200'/>"
"        <menuitem action='Zoom300'/>"
"        <menuitem action='Zoom400'/>"
"        <menuitem action='Zoom50'/>"
"        <menuitem action='Zoom33'/>"
"        <menuitem action='Zoom25'/>"
"      </menu>"
"      <menu action='SplitMenu'>"
"        <menuitem action='SplitHorizontal'/>"
"        <menuitem action='SplitVertical'/>"
"        <menuitem action='SplitQuad'/>"
"        <menuitem action='SplitSingle'/>"
"        <separator/>"
"        <menuitem action='SplitNextPane'/>"
"        <menuitem action='SplitPreviousPane'/>"
"        <menuitem action='SplitUpPane'/>"
"        <menuitem action='SplitDownPane'/>"
"        <separator/>"
"        <menuitem action='SplitPaneSync'/>"
"      </menu>"
"      <menu action='StereoMenu'>"
"        <menuitem action='StereoAuto'/>"
"        <menuitem action='StereoSBS'/>"
"        <menuitem action='StereoCross'/>"
"        <menuitem action='StereoOff'/>"
"        <separator/>"
"        <menuitem action='StereoCycle'/>"
"      </menu>"
"      <menu action='ColorMenu'>"
"        <menuitem action='UseColorProfiles'/>"
"        <menuitem action='UseImageProfile'/>"
"        <menuitem action='ColorProfile0'/>"
"        <menuitem action='ColorProfile1'/>"
"        <menuitem action='ColorProfile2'/>"
"        <menuitem action='ColorProfile3'/>"
"        <menuitem action='ColorProfile4'/>"
"        <menuitem action='ColorProfile5'/>"
"        <separator/>"
"        <menuitem action='Grayscale'/>"
"      </menu>"
"      <menu action='OverlayMenu'>"
"        <menuitem action='ImageOverlay'/>"
"        <menuitem action='ImageHistogram'/>"
"        <menuitem action='ImageOverlayCycle'/>"
"        <separator/>"
"        <menuitem action='HistogramChanR'/>"
"        <menuitem action='HistogramChanG'/>"
"        <menuitem action='HistogramChanB'/>"
"        <menuitem action='HistogramChanRGB'/>"
"        <menuitem action='HistogramChanV'/>"
"        <menuitem action='HistogramChanCycle'/>"
"        <separator/>"
"        <menuitem action='HistogramModeLin'/>"
"        <menuitem action='HistogramModeLog'/>"
"        <menuitem action='HistogramModeCycle'/>"
"      </menu>"
"      <menuitem action='OverUnderExposed'/>"
"      <menuitem action='FullScreen'/>"
"      <placeholder name='ViewSection'/>"
"      <separator/>"
"      <menuitem action='SBar'/>"
"      <menuitem action='SBarSort'/>"
"      <menuitem action='HideBars'/>"
"      <menuitem action='ShowInfoPixel'/>"
"      <menuitem action='IgnoreAlpha'/>"
"      <placeholder name='ToolsSection'/>"
"      <separator/>"
"      <menuitem action='Animate'/>"
"      <menuitem action='SlideShow'/>"
"      <menuitem action='SlideShowPause'/>"
"      <menuitem action='SlideShowFaster'/>"
"      <menuitem action='SlideShowSlower'/>"
"      <separator/>"
"      <menuitem action='Refresh'/>"
"      <placeholder name='SlideShowSection'/>"
"      <separator/>"
"    </menu>"
"    <menu action='WindowsMenu'>"
"      <menu action='NewWindow'>"
"        <menuitem action='NewWindowDefault'/>"
"        <menuitem action='NewWindowFromCurrent'/>"
"        <separator/>"
"       </menu>"
"      <menuitem action='RenameWindow'/>"
"      <menuitem action='DeleteWindow'/>"
"      <menuitem action='CloseWindow'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <separator/>"
"      <menuitem action='HelpContents'/>"
"      <menuitem action='SearchAndRunCommand'/>"
"      <menuitem action='HelpSearch'/>"
"      <menuitem action='HelpShortcuts'/>"
"      <menuitem action='HelpKbd'/>"
"      <menuitem action='HelpNotes'/>"
"      <menuitem action='HelpChangeLog'/>"
"      <placeholder name='HelpSection'/>"
"      <separator/>"
"      <menuitem action='About'/>"
"      <separator/>"
"      <menuitem action='LogWindow'/>"
"      <separator/>"
"    </menu>"
"  </menubar>"
"  <toolbar name='ToolBar'>"
"  </toolbar>"
"  <toolbar name='StatusBar'>"
"  </toolbar>"
"<accelerator action='PrevImageAlt1'/>"
"<accelerator action='PrevImageAlt2'/>"
"<accelerator action='NextImageAlt1'/>"
"<accelerator action='NextImageAlt2'/>"
"<accelerator action='DeleteAlt1'/>"
"<accelerator action='DeleteAlt2'/>"
"<accelerator action='FullScreenAlt1'/>"
"<accelerator action='FullScreenAlt2'/>"
"<accelerator action='Escape'/>"
"<accelerator action='EscapeAlt1'/>"

"<accelerator action='ZoomInAlt1'/>"
"<accelerator action='ZoomOutAlt1'/>"
"<accelerator action='Zoom100Alt1'/>"
"<accelerator action='ZoomFitAlt1'/>"

"<accelerator action='ConnectZoomInAlt1'/>"
"<accelerator action='ConnectZoomOutAlt1'/>"
"<accelerator action='ConnectZoom100Alt1'/>"
"<accelerator action='ConnectZoomFitAlt1'/>"
"</ui>";

static gchar *menu_translate(const gchar *path, gpointer UNUSED(data))
{
	return (gchar *)(_(path));
}

static void layout_actions_setup_mark(LayoutWindow *lw, gint mark, gchar *name_tmpl,
				      gchar *label_tmpl, gchar *accel_tmpl, gchar *tooltip_tmpl, GCallback cb)
{
	gchar name[50];
	gchar label[100];
	gchar accel[50];
	gchar tooltip[100];
	GtkActionEntry entry = { name, NULL, label, accel, tooltip, cb };
	GtkAction *action;

	g_snprintf(name, sizeof(name), name_tmpl, mark);
	g_snprintf(label, sizeof(label), label_tmpl, mark);

	if (accel_tmpl)
		g_snprintf(accel, sizeof(accel), accel_tmpl, mark % 10);
	else
		entry.accelerator = NULL;

	if (tooltip_tmpl)
		g_snprintf(tooltip, sizeof(tooltip), tooltip_tmpl, mark);
	else
		entry.tooltip = NULL;

	gtk_action_group_add_actions(lw->action_group, &entry, 1, lw);
	action = gtk_action_group_get_action(lw->action_group, name);
	g_object_set_data(G_OBJECT(action), "mark_num", GINT_TO_POINTER(mark > 0 ? mark : 10));
}

static void layout_actions_setup_marks(LayoutWindow *lw)
{
	gint mark;
	GError *error;
	GString *desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>"
				"    <menu action='SelectMenu'>");

	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		gint i = (mark < 10 ? mark : 0);

		layout_actions_setup_mark(lw, i, "Mark%d",		_("Mark _%d"), NULL, NULL, NULL);
		layout_actions_setup_mark(lw, i, "SetMark%d",	_("_Set mark %d"),			NULL,		_("Set mark %d"), G_CALLBACK(layout_menu_set_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "ResetMark%d",	_("_Reset mark %d"),			NULL,		_("Reset mark %d"), G_CALLBACK(layout_menu_res_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "ToggleMark%d",	_("_Toggle mark %d"),			"%d",		_("Toggle mark %d"), G_CALLBACK(layout_menu_toggle_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "ToggleMark%dAlt1",	_("_Toggle mark %d"),			"KP_%d",	_("Toggle mark %d"), G_CALLBACK(layout_menu_toggle_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "SelectMark%d",	_("Se_lect mark %d"),			"<control>%d",	_("Select mark %d"), G_CALLBACK(layout_menu_sel_mark_cb));
		layout_actions_setup_mark(lw, i, "SelectMark%dAlt1",	_("_Select mark %d"),			"<control>KP_%d", _("Select mark %d"), G_CALLBACK(layout_menu_sel_mark_cb));
		layout_actions_setup_mark(lw, i, "AddMark%d",	_("_Add mark %d"),			NULL,		_("Add mark %d"), G_CALLBACK(layout_menu_sel_mark_or_cb));
		layout_actions_setup_mark(lw, i, "IntMark%d",	_("_Intersection with mark %d"),	NULL,		_("Intersection with mark %d"), G_CALLBACK(layout_menu_sel_mark_and_cb));
		layout_actions_setup_mark(lw, i, "UnselMark%d",	_("_Unselect mark %d"),			NULL,		_("Unselect mark %d"), G_CALLBACK(layout_menu_sel_mark_minus_cb));
		layout_actions_setup_mark(lw, i, "FilterMark%d",	_("_Filter mark %d"),			NULL,		_("Filter mark %d"), G_CALLBACK(layout_menu_mark_filter_toggle_cb));

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
				"    </menu>"
				"  </menubar>");
	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		gint i = (mark < 10 ? mark : 0);

		g_string_append_printf(desc,
				"<accelerator action='ToggleMark%dAlt1'/>"
				"<accelerator action='SelectMark%dAlt1'/>",
				i, i);
		}
	g_string_append(desc,   "</ui>" );

	error = NULL;
	if (!gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	g_string_free(desc, TRUE);
}

static GList *layout_actions_editor_menu_path(EditorDescription *editor)
{
	gchar **split = g_strsplit(editor->menu_path, "/", 0);
	gint i = 0;
	GList *ret = NULL;

	if (split[0] == NULL)
		{
		g_strfreev(split);
		return NULL;
		}

	while (split[i])
		{
		ret = g_list_prepend(ret, g_strdup(split[i]));
		i++;
		}

	g_strfreev(split);

	ret = g_list_prepend(ret, g_strdup(editor->key));

	return g_list_reverse(ret);
}

static void layout_actions_editor_add(GString *desc, GList *path, GList *old_path)
{
	gint to_open, to_close, i;
	while (path && old_path && strcmp((gchar *)path->data, (gchar *)old_path->data) == 0)
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
		gchar *name = old_path->data;
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
		gchar *name = path->data;
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
		g_string_append_printf(desc, "      <menuitem action='%s'/>", (gchar *)path->data);
}

static void layout_actions_setup_editors(LayoutWindow *lw)
{
	GError *error;
	GList *editors_list;
	GList *work;
	GList *old_path;
	GString *desc;

	if (lw->ui_editors_id)
		{
		gtk_ui_manager_remove_ui(lw->ui_manager, lw->ui_editors_id);
		}

	if (lw->action_group_editors)
		{
		gtk_ui_manager_remove_action_group(lw->ui_manager, lw->action_group_editors);
		g_object_unref(lw->action_group_editors);
		}
	lw->action_group_editors = gtk_action_group_new("MenuActionsExternal");
	gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);

	/* lw->action_group_editors contains translated entries, no translate func is required */
	desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>");

	editors_list = editor_list_get();

	old_path = NULL;
	work = editors_list;
	while (work)
		{
		GList *path;
		EditorDescription *editor = work->data;
		GtkActionEntry entry = { editor->key,
		                         NULL,
		                         editor->name,
		                         editor->hotkey,
		                         editor->comment ? editor->comment : editor->name,
		                         G_CALLBACK(layout_menu_edit_cb) };

		if (editor->icon)
			{
			entry.stock_id = editor->key;
			}
		gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);

		path = layout_actions_editor_menu_path(editor);
		layout_actions_editor_add(desc, path, old_path);

		string_list_free(old_path);
		old_path = path;
		work = work->next;
		}

	layout_actions_editor_add(desc, NULL, old_path);
	string_list_free(old_path);

	g_string_append(desc,   "  </menubar>"
				"</ui>" );

	error = NULL;

	lw->ui_editors_id = gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error);
	if (!lw->ui_editors_id)
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	g_string_free(desc, TRUE);
	g_list_free(editors_list);
}

void layout_actions_setup(LayoutWindow *lw)
{
	GError *error;
	gint i;

	DEBUG_1("%s layout_actions_setup: start", get_exec_time());
	if (lw->ui_manager) return;

	lw->action_group = gtk_action_group_new("MenuActions");
	gtk_action_group_set_translate_func(lw->action_group, menu_translate, NULL, NULL);

	gtk_action_group_add_actions(lw->action_group,
				     menu_entries, G_N_ELEMENTS(menu_entries), lw);
	gtk_action_group_add_toggle_actions(lw->action_group,
					    menu_toggle_entries, G_N_ELEMENTS(menu_toggle_entries), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_radio_entries, G_N_ELEMENTS(menu_radio_entries),
					   0, G_CALLBACK(layout_menu_list_cb), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_split_radio_entries, G_N_ELEMENTS(menu_split_radio_entries),
					   0, G_CALLBACK(layout_menu_split_cb), lw);
	gtk_action_group_add_toggle_actions(lw->action_group,
					   menu_view_dir_toggle_entries, G_N_ELEMENTS(menu_view_dir_toggle_entries),
					    lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_color_radio_entries, COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS,
					   0, G_CALLBACK(layout_color_menu_input_cb), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_histogram_channel, G_N_ELEMENTS(menu_histogram_channel),
					   0, G_CALLBACK(layout_menu_histogram_channel_cb), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_histogram_mode, G_N_ELEMENTS(menu_histogram_mode),
					   0, G_CALLBACK(layout_menu_histogram_mode_cb), lw);
	gtk_action_group_add_radio_actions(lw->action_group,
					   menu_stereo_mode_entries, G_N_ELEMENTS(menu_stereo_mode_entries),
					   0, G_CALLBACK(layout_menu_stereo_mode_cb), lw);


	lw->ui_manager = gtk_ui_manager_new();
	gtk_ui_manager_set_add_tearoffs(lw->ui_manager, TRUE);
	gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group, 0);

	DEBUG_1("%s layout_actions_setup: add menu", get_exec_time());
	error = NULL;
	if (!gtk_ui_manager_add_ui_from_string(lw->ui_manager, menu_ui_description, -1, &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}

	DEBUG_1("%s layout_actions_setup: add toolbar", get_exec_time());
	for (i = 0; i < TOOLBAR_COUNT; i++)
		{
		layout_toolbar_clear(lw, i);
		layout_toolbar_add_default(lw, i);
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

static gint layout_editors_reload_idle_id = -1;
static GList *layout_editors_desktop_files = NULL;

static gboolean layout_editors_reload_idle_cb(gpointer UNUSED(data))
{
	if (!layout_editors_desktop_files)
		{
		DEBUG_1("%s layout_editors_reload_idle_cb: get_desktop_files", get_exec_time());
		layout_editors_desktop_files = editor_get_desktop_files();
		return TRUE;
		}

	editor_read_desktop_file(layout_editors_desktop_files->data);
	g_free(layout_editors_desktop_files->data);
	layout_editors_desktop_files = g_list_delete_link(layout_editors_desktop_files, layout_editors_desktop_files);


	if (!layout_editors_desktop_files)
		{
		GList *work;
		DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors", get_exec_time());
		editor_table_finish();

		work = layout_window_list;
		while (work)
			{
			LayoutWindow *lw = work->data;
			work = work->next;
			layout_actions_setup_editors(lw);
			if (lw->bar_sort_enabled)
				{
				layout_bar_sort_toggle(lw);
				}
			}

		DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors done", get_exec_time());

		layout_editors_reload_idle_id = -1;
		return FALSE;
		}
	return TRUE;
}

void layout_editors_reload_start(void)
{
	DEBUG_1("%s layout_editors_reload_start", get_exec_time());

	if (layout_editors_reload_idle_id != -1)
		{
		g_source_remove(layout_editors_reload_idle_id);
		string_list_free(layout_editors_desktop_files);
		}

	editor_table_clear();
	layout_editors_reload_idle_id = g_idle_add(layout_editors_reload_idle_cb, NULL);
}

void layout_editors_reload_finish(void)
{
	if (layout_editors_reload_idle_id != -1)
		{
		DEBUG_1("%s layout_editors_reload_finish", get_exec_time());
		g_source_remove(layout_editors_reload_idle_id);
		while (layout_editors_reload_idle_id != -1)
			{
			layout_editors_reload_idle_cb(NULL);
			}
		}
}

void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window)
{
	GtkAccelGroup *group;

	if (!lw->ui_manager) return;

	group = gtk_ui_manager_get_accel_group(lw->ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), group);
}

GtkWidget *layout_actions_menu_bar(LayoutWindow *lw)
{
	if (lw->menu_bar) return lw->menu_bar;
	lw->menu_bar = gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu");
	g_object_ref(lw->menu_bar);
	return lw->menu_bar;
}

GtkWidget *layout_actions_toolbar(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar[type]) return lw->toolbar[type];
	switch (type)
		{
		case TOOLBAR_MAIN:
			lw->toolbar[type] = gtk_ui_manager_get_widget(lw->ui_manager, "/ToolBar");
			gtk_toolbar_set_icon_size(GTK_TOOLBAR(lw->toolbar[type]), GTK_ICON_SIZE_SMALL_TOOLBAR);
			gtk_toolbar_set_style(GTK_TOOLBAR(lw->toolbar[type]), GTK_TOOLBAR_ICONS);
			break;
		case TOOLBAR_STATUS:
			lw->toolbar[type] = gtk_ui_manager_get_widget(lw->ui_manager, "/StatusBar");
			gtk_toolbar_set_icon_size(GTK_TOOLBAR(lw->toolbar[type]), GTK_ICON_SIZE_MENU);
			gtk_toolbar_set_style(GTK_TOOLBAR(lw->toolbar[type]), GTK_TOOLBAR_ICONS);
			gtk_toolbar_set_show_arrow(GTK_TOOLBAR(lw->toolbar[type]), FALSE);
			break;
		default:
			break;
		}
	g_object_ref(lw->toolbar[type]);
	return lw->toolbar[type];
}

GtkWidget *layout_actions_menu_tool_bar(LayoutWindow *lw)
{
	GtkWidget *menu_bar;
	GtkWidget *toolbar;

	if (lw->menu_tool_bar) return lw->menu_tool_bar;

	menu_bar = layout_actions_menu_bar(lw);
	DEBUG_NAME(menu_bar);
	toolbar = layout_actions_toolbar(lw, TOOLBAR_MAIN);
	DEBUG_NAME(toolbar);
	lw->menu_tool_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), menu_bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), toolbar, FALSE, FALSE, 0);

	g_object_ref(lw->menu_tool_bar);
	return lw->menu_tool_bar;
}

void layout_toolbar_clear(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar_merge_id[type])
		{
		gtk_ui_manager_remove_ui(lw->ui_manager, lw->toolbar_merge_id[type]);
		gtk_ui_manager_ensure_update(lw->ui_manager);
		}
	string_list_free(lw->toolbar_actions[type]);
	lw->toolbar_actions[type] = NULL;

	lw->toolbar_merge_id[type] = gtk_ui_manager_new_merge_id(lw->ui_manager);
}

void layout_toolbar_add(LayoutWindow *lw, ToolbarType type, const gchar *action)
{
	const gchar *path = NULL;

	if (!action || !lw->ui_manager) return;

	if (g_list_find_custom(lw->toolbar_actions[type], action, (GCompareFunc)strcmp)) return;

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


	if (g_str_has_suffix(action, ".desktop"))
		{
		/* this may be called before the external editors are read
		   create a dummy action for now */
		if (!lw->action_group_editors)
			{
			lw->action_group_editors = gtk_action_group_new("MenuActionsExternal");
			gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);
			}
		if (!gtk_action_group_get_action(lw->action_group_editors, action))
			{
			GtkActionEntry entry = { action,
			                         GTK_STOCK_MISSING_IMAGE,
			                         action,
			                         NULL,
			                         NULL,
			                         NULL };
			DEBUG_1("Creating temporary action %s", action);
			gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);
			}
		}
	gtk_ui_manager_add_ui(lw->ui_manager, lw->toolbar_merge_id[type], path, action, action, GTK_UI_MANAGER_TOOLITEM, FALSE);
	lw->toolbar_actions[type] = g_list_append(lw->toolbar_actions[type], g_strdup(action));
}


void layout_toolbar_add_default(LayoutWindow *lw, ToolbarType type)
{
	LayoutWindow *lw_first;
	GList *work_action;

	switch (type)
		{
		case TOOLBAR_MAIN:
			if (layout_window_list)
				{
				lw_first = layout_window_list->data;
				if (lw_first->toolbar_actions[TOOLBAR_MAIN])
					{
					work_action = lw_first->toolbar_actions[type];
					while (work_action)
						{
						gchar *action = work_action->data;
						work_action = work_action->next;
						layout_toolbar_add(lw, type, action);
						}
					}
				else
					{
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
					}
				}
			else
				{
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
				}
			break;
		case TOOLBAR_STATUS:
			if (layout_window_list)
				{
				lw_first = layout_window_list->data;
				if (lw_first->toolbar_actions[TOOLBAR_MAIN])
					{
					work_action = lw_first->toolbar_actions[type];
					while (work_action)
						{
						gchar *action = work_action->data;
						work_action = work_action->next;
						layout_toolbar_add(lw, type, action);
						}
					}
				else
					{
					layout_toolbar_add(lw, type, "ExifRotate");
					layout_toolbar_add(lw, type, "ShowInfoPixel");
					layout_toolbar_add(lw, type, "UseColorProfiles");
					layout_toolbar_add(lw, type, "SaveMetadata");
					}
				}
			else
				{
				layout_toolbar_add(lw, type, "ExifRotate");
				layout_toolbar_add(lw, type, "ShowInfoPixel");
				layout_toolbar_add(lw, type, "UseColorProfiles");
				layout_toolbar_add(lw, type, "SaveMetadata");
				}
			break;
		default:
			break;
		}
}



void layout_toolbar_write_config(LayoutWindow *lw, ToolbarType type, GString *outstr, gint indent)
{
	const gchar *name = NULL;
	GList *work = lw->toolbar_actions[type];

	switch (type)
		{
		case TOOLBAR_MAIN:
			name = "toolbar";
			break;
		case TOOLBAR_STATUS:
			name = "statusbar";
			break;
		default:
			break;
		}

	WRITE_NL(); WRITE_STRING("<%s>", name);
	indent++;
	WRITE_NL(); WRITE_STRING("<clear/>");
	while (work)
		{
		gchar *action = work->data;
		work = work->next;
		WRITE_NL(); WRITE_STRING("<toolitem ");
		write_char_option(outstr, indent + 1, "action", action);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</%s>", name);
}

void layout_toolbar_add_from_config(LayoutWindow *lw, ToolbarType type, const char **attribute_names, const gchar **attribute_values)
{
	gchar *action = NULL;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("action", action)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	layout_toolbar_add(lw, type, action);
	g_free(action);
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
	action = gtk_action_group_get_action(lw->action_group, "SaveMetadata");
	gtk_action_set_sensitive(action, n > 0);
	if (n > 0)
		{
		gchar *buf = g_strdup_printf(_("Number of files with unsaved metadata: %d"), n);
		g_object_set(G_OBJECT(action), "tooltip", buf, NULL);
		g_free(buf);
		}
	else
		{
		g_object_set(G_OBJECT(action), "tooltip", _("No unsaved metadata"), NULL);
		}
}

void layout_util_status_update_write_all(void)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		LayoutWindow *lw = work->data;
		work = work->next;

		layout_util_status_update_write(lw);
		}
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
#ifdef HAVE_LCMS
	gchar *image_profile;
	gchar *screen_profile;
#endif

	if (!lw->action_group) return;
	if (!layout_image_color_profile_get(lw, &input, &use_image)) return;

	use_color = layout_image_color_profile_get_use(lw);

	action = gtk_action_group_get_action(lw->action_group, "UseColorProfiles");
#ifdef HAVE_LCMS
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), use_color);
	if (layout_image_color_profile_get_status(lw, &image_profile, &screen_profile))
		{
		gchar *buf;
		buf = g_strdup_printf(_("Image profile: %s\nScreen profile: %s"), image_profile, screen_profile);
		g_object_set(G_OBJECT(action), "tooltip", buf, NULL);
		g_free(image_profile);
		g_free(screen_profile);
		g_free(buf);
		}
	else
		{
		g_object_set(G_OBJECT(action), "tooltip", _("Click to enable color management"), NULL);
		}
#else
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);
	gtk_action_set_sensitive(action, FALSE);
	g_object_set(G_OBJECT(action), "tooltip", _("Color profiles not supported"), NULL);
#endif

	action = gtk_action_group_get_action(lw->action_group, "UseImageProfile");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), use_image);
	gtk_action_set_sensitive(action, use_color);

	for (i = 0; i < COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS; i++)
		{
		sprintf(action_name, "ColorProfile%d", i);
		action = gtk_action_group_get_action(lw->action_group, action_name);

		if (i >= COLOR_PROFILE_FILE)
			{
			const gchar *name = options->color_profile.input_name[i - COLOR_PROFILE_FILE];
			const gchar *file = options->color_profile.input_file[i - COLOR_PROFILE_FILE];
			gchar *end;
			gchar *buf;

			if (!name || !name[0]) name = filename_from_path(file);

			end = layout_color_name_parse(name);
			buf = g_strdup_printf(_("Input _%d: %s"), i, end);
			g_free(end);

			g_object_set(G_OBJECT(action), "label", buf, NULL);
			g_free(buf);

			gtk_action_set_visible(action, file && file[0]);
			}

		gtk_action_set_sensitive(action, !use_image);
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), (i == input));
		}

	action = gtk_action_group_get_action(lw->action_group, "Grayscale");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_image_get_desaturate(lw));
}

void layout_util_sync_file_filter(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);
}

void layout_util_sync_marks(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gtk_action_group_get_action(lw->action_group, "ShowMarks");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_marks);
}

static void layout_util_sync_views(LayoutWindow *lw)
{
	GtkAction *action;
	OsdShowFlags osd_flags = image_osd_get(lw->image);

	if (!lw->action_group) return;

	action = gtk_action_group_get_action(lw->action_group, "FolderTree");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.dir_view_type);

	action = gtk_action_group_get_action(lw->action_group, "SplitSingle");
	gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), lw->split_mode);

	action = gtk_action_group_get_action(lw->action_group, "SplitNextPane");
	gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gtk_action_group_get_action(lw->action_group, "SplitPreviousPane");
	gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gtk_action_group_get_action(lw->action_group, "SplitUpPane");
	gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gtk_action_group_get_action(lw->action_group, "SplitDownPane");
	gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));

	action = gtk_action_group_get_action(lw->action_group, "SplitPaneSync");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.split_pane_sync);

	action = gtk_action_group_get_action(lw->action_group, "ViewIcons");
	gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), lw->options.file_view_type);

	action = gtk_action_group_get_action(lw->action_group, "FloatTools");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.tools_float);

	action = gtk_action_group_get_action(lw->action_group, "SBar");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_bar_enabled(lw));

	action = gtk_action_group_get_action(lw->action_group, "SBarSort");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_bar_sort_enabled(lw));

	action = gtk_action_group_get_action(lw->action_group, "HideToolbar");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.toolbar_hidden);

	action = gtk_action_group_get_action(lw->action_group, "ShowInfoPixel");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_info_pixel);

	action = gtk_action_group_get_action(lw->action_group, "SlideShow");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_image_slideshow_active(lw));

	action = gtk_action_group_get_action(lw->action_group, "IgnoreAlpha");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.ignore_alpha);

	action = gtk_action_group_get_action(lw->action_group, "Animate");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.animate);

	action = gtk_action_group_get_action(lw->action_group, "ImageOverlay");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), osd_flags != OSD_SHOW_NOTHING);

	action = gtk_action_group_get_action(lw->action_group, "ImageHistogram");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), osd_flags & OSD_SHOW_HISTOGRAM);

	action = gtk_action_group_get_action(lw->action_group, "ExifRotate");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->image.exif_rotate_enable);

	action = gtk_action_group_get_action(lw->action_group, "OverUnderExposed");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->overunderexposed);

	action = gtk_action_group_get_action(lw->action_group, "DrawRectangle");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->draw_rectangle);

	action = gtk_action_group_get_action(lw->action_group, "RectangularSelection");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->collections.rectangular_selection);

	action = gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);

	action = gtk_action_group_get_action(lw->action_group, "HideBars");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), (lw->options.bars_state.hidden));

	if (osd_flags & OSD_SHOW_HISTOGRAM)
		{
		action = gtk_action_group_get_action(lw->action_group, "HistogramChanR");
		gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), image_osd_histogram_get_channel(lw->image));

		action = gtk_action_group_get_action(lw->action_group, "HistogramModeLin");
		gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), image_osd_histogram_get_mode(lw->image));
		}

	action = gtk_action_group_get_action(lw->action_group, "ConnectZoomMenu");
	gtk_action_set_sensitive(action, lw->split_mode != SPLIT_NONE);

	action = gtk_action_group_get_action(lw->action_group, "WriteRotation");
	gtk_action_set_sensitive(action, !(runcmd("which exiftran >/dev/null 2>&1") ||
							runcmd("which mogrify >/dev/null 2>&1") || options->metadata.write_orientation));
	action = gtk_action_group_get_action(lw->action_group, "WriteRotationKeepDate");
	gtk_action_set_sensitive(action, !(runcmd("which exiftran >/dev/null 2>&1") ||
							runcmd("which mogrify >/dev/null 2>&1") || options->metadata.write_orientation));

	action = gtk_action_group_get_action(lw->action_group, "StereoAuto");
	gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), layout_image_stereo_pixbuf_get(lw));

	layout_util_sync_marks(lw);
	layout_util_sync_color(lw);
	layout_image_set_ignore_alpha(lw, lw->options.ignore_alpha);
}

void layout_util_sync_thumb(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gtk_action_group_get_action(lw->action_group, "Thumbnails");
	gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_thumbnails);
	g_object_set(action, "sensitive", (lw->options.file_view_type == FILEVIEW_LIST), NULL);
}

void layout_util_sync(LayoutWindow *lw)
{
	layout_util_sync_views(lw);
	layout_util_sync_thumb(lw);
	layout_menu_recent_update(lw);
//	layout_menu_edit_update(lw);
}

/**
 * @brief Checks if event key is mapped to Help
 * @param event 
 * @returns 
 * 
 * Used to check if the user has re-mapped the Help key
 * in Preferences/Keyboard
 * 
 * Note: help_key.accel_mods and event->state
 * differ in the higher bits
 */
gboolean is_help_key(GdkEventKey *event)
{
	GtkAccelKey help_key;
	gboolean ret = FALSE;
	guint mask = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK;

	if (gtk_accel_map_lookup_entry("<Actions>/MenuActions/HelpContents", &help_key))
		{
		if (help_key.accel_key == event->keyval &&
					(help_key.accel_mods & mask) == (event->state & mask))
			{
			ret = TRUE;
			}
		}

	return ret;
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

static void layout_bar_destroyed(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;

	lw->bar = NULL;
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
		lw->bar = NULL;
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


//	gtk_box_pack_start(GTK_BOX(lw->utility_box), lw->bar, FALSE, FALSE, 0);
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


static void layout_bar_sort_destroyed(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;

	lw->bar_sort = NULL;

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
		lw->bar_sort = NULL;
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

	gtk_box_pack_end(GTK_BOX(lw->utility_box), lw->bar_sort, FALSE, FALSE, 0);
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
		metadata_write_queue_confirm(FALSE, NULL, NULL);
}

void layout_bars_new_selection(LayoutWindow *lw, gint count)
{
	layout_bar_new_selection(lw, count);
}

GtkWidget *layout_bars_prepare(LayoutWindow *lw, GtkWidget *image)
{
	if (lw->utility_box) return lw->utility_box;
	lw->utility_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	lw->utility_paned = gtk_hpaned_new();
	DEBUG_NAME(lw->utility_paned);
	gtk_box_pack_start(GTK_BOX(lw->utility_box), lw->utility_paned, TRUE, TRUE, 0);

	gtk_paned_pack1(GTK_PANED(lw->utility_paned), image, TRUE, FALSE);
	gtk_widget_show(lw->utility_paned);

	gtk_widget_show(image);

	g_object_ref(lw->utility_box);
	return lw->utility_box;
}

void layout_bars_close(LayoutWindow *lw)
{
	layout_bar_sort_close(lw);
	layout_bar_close(lw);
}

static gboolean layout_exif_window_destroy(GtkWidget *UNUSED(widget), gpointer data)
{
	LayoutWindow *lw = data;
	lw->exif_window = NULL;

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
