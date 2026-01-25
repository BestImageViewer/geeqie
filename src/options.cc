/*
 * Copyright (C) 2008, 2016 The Geeqie Team -
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#include "options.h"

#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "intl.h"
#include "layout-image.h"
#include "layout.h"
#include "main-defines.h"
#include "misc.h"
#include "pixbuf-renderer.h"
#include "print.h"
#include "rcfile.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"

namespace
{

constexpr gint DEFAULT_THUMB_WIDTH = 96;
constexpr gint DEFAULT_THUMB_HEIGHT = 72;

} // namespace

ConfOptions *options;
CommandLine *command_line;

ConfOptions *init_options(ConfOptions *options)
{
	gint i;

	if (!options) options = g_new0(ConfOptions, 1);

	options->collections.rectangular_selection = FALSE;

	options->color_profile.enabled = TRUE;
	options->color_profile.input_type = 0;
	options->color_profile.screen_file = nullptr;
	options->color_profile.use_image = TRUE;
	options->color_profile.use_x11_screen_profile = TRUE;
	options->color_profile.render_intent = 0;

	options->dnd_icon_size = 48;
	options->dnd_default_action = DND_ACTION_ASK;
	options->duplicates_similarity_threshold = 99;
	options->rot_invariant_sim = TRUE;
	options->sort_totals = FALSE;
	options->rectangle_draw_aspect_ratio = RECTANGLE_DRAW_ASPECT_RATIO_NONE;

	options->file_filter.disable = FALSE;
	options->file_filter.show_dot_directory = FALSE;
	options->file_filter.show_hidden_files = FALSE;
	options->file_filter.show_parent_directory = TRUE;
	options->file_filter.disable_file_extension_checks = FALSE;

	options->save_window_positions = TRUE;
	options->use_saved_window_positions_for_new_windows = FALSE;
	options->save_window_workspace = FALSE;
	options->tools_restore_state = TRUE;
	options->save_dialog_window_positions = TRUE;
	options->hide_window_decorations = FALSE;
	options->show_window_ids = FALSE;

	options->file_ops.confirm_delete = TRUE;
	options->file_ops.confirm_move_to_trash = TRUE;
	options->file_ops.enable_delete_key = TRUE;
	options->file_ops.use_system_trash = TRUE;
	options->file_ops.enable_in_place_rename = TRUE;
	options->file_ops.safe_delete_enable = TRUE;
	options->file_ops.safe_delete_folder_maxsize = 128;
	options->file_ops.safe_delete_path = nullptr;
	options->file_ops.no_trash = FALSE;

	options->file_sort.case_sensitive = FALSE;

	options->fullscreen.clean_flip = FALSE;
	options->fullscreen.disable_saver = TRUE;
	options->fullscreen.screen = -1;

	options->appimage_notifications = TRUE;
	options->marks_save = TRUE;
	options->with_rename = FALSE;
	options->collections_duplicates = FALSE;
	options->collections_on_top = FALSE;
	options->hide_window_in_fullscreen = TRUE;
	options->hide_osd_in_fullscreen = FALSE;

	memset(&options->image.border_color, 0, sizeof(options->image.border_color));
	memset(&options->image.alpha_color_1, 0, sizeof(options->image.alpha_color_1));
	memset(&options->image.alpha_color_2, 0, sizeof(options->image.alpha_color_2));
	options->image.border_color.red = static_cast<gdouble>(0x009999) / 65535;
	options->image.border_color.green = static_cast<gdouble>(0x009999) / 65535;
	options->image.border_color.blue = static_cast<gdouble>(0x009999) / 65535;
/* alpha channel checkerboard background (same as gimp) */
	options->image.alpha_color_1.red = static_cast<gdouble>(0x009999) / 65535;
	options->image.alpha_color_1.green = static_cast<gdouble>(0x009999) / 65535;
	options->image.alpha_color_1.blue = static_cast<gdouble>(0x009999) / 65535;
	options->image.alpha_color_2.red = static_cast<gdouble>(0x006666) / 65535;
	options->image.alpha_color_2.green = static_cast<gdouble>(0x006666) / 65535;
	options->image.alpha_color_2.blue = static_cast<gdouble>(0x006666) / 65535;
	options->image.enable_read_ahead = TRUE;
	options->image.exif_rotate_enable = TRUE;
	options->image.fit_window_to_image = FALSE;
	options->image.limit_autofit_size = FALSE;
	options->image.limit_window_size = TRUE;
	options->image.max_autofit_size = 100;
	options->image.max_enlargement_size = 900;
	options->image.max_window_size = 90;
	options->image.scroll_reset_method = ScrollReset::NOCHANGE;
	options->image.tile_cache_max = 10;
	options->image.image_cache_max = 128; /* 4 x 10MPix */
	options->image.use_custom_border_color = FALSE;
	options->image.use_custom_border_color_in_fullscreen = TRUE;
	options->image.zoom_2pass = TRUE;
	options->image.zoom_increment = 5;
	options->image.zoom_mode = ZOOM_RESET_NONE;
	options->image.zoom_quality = GDK_INTERP_BILINEAR;
	options->image.zoom_to_fit_allow_expand = FALSE;
	options->image.zoom_style = ZOOM_GEOMETRIC;
	options->image.tile_size = 128;

	options->image_overlay.template_string = nullptr;
	options->image_overlay.x = 10;
	options->image_overlay.y = -10;
	options->image_overlay.font = g_strdup("Sans 10");
	options->image_overlay.text_red = 0;
	options->image_overlay.text_green = 0;
	options->image_overlay.text_blue = 0;
	options->image_overlay.text_alpha = 255;
	options->image_overlay.background_red = 240;
	options->image_overlay.background_green = 240;
	options->image_overlay.background_blue = 240;
	options->image_overlay.background_alpha = 210;

	for (gint i = 0; i < OVERLAY_SCREEN_DISPLAY_PROFILE_COUNT; i++)
		{
		options->image_overlay_n.template_string[i] = nullptr;
		options->image_overlay_n.x[i] = 10;
		options->image_overlay_n.y[i] = -10;
		options->image_overlay_n.font[i] = g_strdup("Sans 10");
		options->image_overlay_n.text_red[i] = 0;
		options->image_overlay_n.text_green[i] = 0;
		options->image_overlay_n.text_blue[i] = 0;
		options->image_overlay_n.text_alpha[i] = 255;
		options->image_overlay_n.background_red[i] = 240;
		options->image_overlay_n.background_green[i] = 240;
		options->image_overlay_n.background_blue[i] = 240;
		options->image_overlay_n.background_alpha[i] = 210;
		}

	options->lazy_image_sync = FALSE;
	options->mousewheel_scrolls = FALSE;
	options->image_lm_click_nav = TRUE;
	options->image_l_click_archive = FALSE;
	options->image_l_click_video = TRUE;
	options->image_l_click_video_editor = g_strdup("video-player.desktop");
	options->open_recent_list_maxsize = 10;
	options->recent_folder_image_list_maxsize = 10;
	options->place_dialogs_under_mouse = FALSE;

	options->progressive_key_scrolling = TRUE;
	options->keyboard_scroll_step = 1;

	options->metadata.enable_metadata_dirs = FALSE;
	options->metadata.save_in_image_file = FALSE;
	options->metadata.save_legacy_IPTC = FALSE;
	options->metadata.warn_on_write_problems = TRUE;
	options->metadata.save_legacy_format = FALSE;
	options->metadata.sync_grouped_files = TRUE;
	options->metadata.confirm_write = TRUE;
	options->metadata.confirm_after_timeout = FALSE;
	options->metadata.confirm_timeout = 10;
	options->metadata.confirm_on_image_change = FALSE;
	options->metadata.confirm_on_dir_change = TRUE;
	options->metadata.keywords_case_sensitive = FALSE;
	options->metadata.write_orientation = TRUE;
	options->metadata.sidecar_extended_name = FALSE;
	options->metadata.check_spelling = TRUE;

	options->show_icon_names = TRUE;
	options->show_star_rating = FALSE;
	options->show_collection_infotext = FALSE;
	options->show_predefined_keyword_tree = TRUE;
	options->expand_menu_toolbar = FALSE;
	options->hamburger_menu = FALSE;

	options->slideshow.delay = 50;
	options->slideshow.random = FALSE;
	options->slideshow.repeat = FALSE;

	options->thumbnails.cache_into_dirs = FALSE;
	options->thumbnails.enable_caching = TRUE;
	options->thumbnails.max_width = DEFAULT_THUMB_WIDTH;
	options->thumbnails.max_height = DEFAULT_THUMB_HEIGHT;
	options->thumbnails.quality = GDK_INTERP_TILES;
	options->thumbnails.spec_standard = TRUE;
	options->thumbnails.use_xvpics = TRUE;
	options->thumbnails.use_exif = FALSE;
	options->thumbnails.use_color_management = FALSE;
	options->thumbnails.use_ft_metadata = TRUE;
	options->thumbnails.collection_preview = 20;

	options->tree_descend_subdirs = FALSE;
	options->view_dir_list_single_click_enter = TRUE;
	options->circular_selection_lists = TRUE;
	options->update_on_time_change = TRUE;
	options->clipboard_selection = CLIPBOARD_BOTH;

	options->stereo.fixed_w = 1920;
	options->stereo.fixed_h = 1080;
	options->stereo.fixed_x1 = 0;
	options->stereo.fixed_y1 = 0;
	options->stereo.fixed_x2 = 0;
	options->stereo.fixed_y2 = 1125;

	options->log_window_lines = 1000;
	options->log_window.line_wrap = FALSE;
	options->log_window.paused = FALSE;
	options->log_window.timer_data = TRUE;
	options->log_window.action = g_strdup("echo");

	options->read_metadata_in_idle = FALSE;
	options->star_rating.star = STAR_RATING_STAR;
	options->star_rating.rejected = STAR_RATING_REJECTED;

	options->printer.template_string = nullptr;
	options->printer.image_font = g_strdup("Serif 10");
	options->printer.page_font = g_strdup("Serif 10");
	options->printer.page_text = nullptr;
	options->printer.image_text_position = FOOTER_1;
	options->printer.page_text_position = HEADER_1;

	options->threads.duplicates = get_cpu_cores() - 1;

	options->disabled_plugins.clear();

	options->mouse_button_8 = g_strdup("Back");
	options->mouse_button_9 = g_strdup("Forward");

	options->disable_gpu = FALSE;
	options->override_disable_gpu = FALSE;

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		options->class_filter[i] = TRUE;
		}
	return options;
}

void setup_default_options(ConfOptions *options)
{
	gint i;

	g_autofree gchar *current_path = get_current_dir();
	bookmark_add_default(".", current_path);
	bookmark_add_default(_("Home"), homedir());
	g_autofree gchar *desktop_path = g_build_filename(homedir(), "Desktop", NULL);
	bookmark_add_default(_("Desktop"), desktop_path);
	bookmark_add_default(_("Collections"), get_collections_dir());

	g_free(options->file_ops.safe_delete_path);
	options->file_ops.safe_delete_path = g_strdup(get_trash_dir());

	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		options->color_profile.input_file[i] = nullptr;
		options->color_profile.input_name[i] = nullptr;
		}

	set_default_image_overlay_template_string(options);
	options->sidecar.ext = g_strdup(".jpg;%raw;.gqv;.xmp;%unknown");

	options->shell.path = g_strdup(GQ_DEFAULT_SHELL_PATH);
	options->shell.options = g_strdup(GQ_DEFAULT_SHELL_OPTIONS);

	for (i = 0; i < FILEDATA_MARKS_SIZE; i++)
		{
		options->marks_tooltips[i] = g_strdup_printf("%s%d", _("Mark "), i + 1);
		}

	options->help_search_engine = g_strdup(HELP_SEARCH_ENGINE);
}

static void sync_options_with_current_state(ConfOptions *options)
{
	LayoutWindow *lw = get_current_layout();

	if (lw)
		{
		layout_sync_options_with_current_state(lw);

		options->color_profile.enabled = layout_image_color_profile_get_use(lw);
		layout_image_color_profile_get(lw,
		                               options->color_profile.input_type,
		                               options->color_profile.use_image);
		}

}

void save_options(ConfOptions *options)
{
	sync_options_with_current_state(options);

	g_autofree gchar *rc_path = g_build_filename(get_rc_dir(), RC_FILE_NAME, NULL);
	save_config_to_file(rc_path, options, nullptr);
}

gboolean load_options(ConfOptions *)
{
	gboolean success;

	if (isdir(GQ_SYSTEM_WIDE_DIR))
		{
		g_autofree gchar *rc_path = g_build_filename(GQ_SYSTEM_WIDE_DIR, RC_FILE_NAME, NULL);
		success = load_config_from_file(rc_path, TRUE);
		DEBUG_1("Loading options from %s ... %s", rc_path, success ? "done" : "failed");
		}

	g_autofree gchar *rc_path = g_build_filename(get_rc_dir(), RC_FILE_NAME, NULL);
	success = load_config_from_file(rc_path, TRUE);
	DEBUG_1("Loading options from %s ... %s", rc_path, success ? "done" : "failed");
	return success;
}

void set_default_image_overlay_template_string(ConfOptions *options)
{
	g_free(options->image_overlay.template_string);
	options->image_overlay.template_string = g_strdup(DEFAULT_OVERLAY_INFO);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
