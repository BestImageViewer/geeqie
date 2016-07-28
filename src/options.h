/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
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
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef OPTIONS_H
#define OPTIONS_H

typedef struct _ConfOptions ConfOptions;

struct _ConfOptions
{
	/* ui */
	gboolean progressive_key_scrolling;
	guint keyboard_scroll_step;
	gboolean place_dialogs_under_mouse;
	gboolean mousewheel_scrolls;
	gboolean image_lm_click_nav;
	gboolean show_icon_names;

	/* various */
	gboolean tree_descend_subdirs;
	gboolean view_dir_list_single_click_enter;

	gboolean lazy_image_sync;
	gboolean update_on_time_change;

	guint duplicates_similarity_threshold;
	gboolean rot_invariant_sim;

	gint open_recent_list_maxsize;
	gint dnd_icon_size;

	gboolean save_window_positions;
	gboolean use_saved_window_positions_for_new_windows;
	gboolean tools_restore_state;

	/* file ops */
	struct {
		gboolean enable_in_place_rename;

		gboolean confirm_delete;
		gboolean enable_delete_key;
		gboolean safe_delete_enable;
		gchar *safe_delete_path;
		gint safe_delete_folder_maxsize;
	} file_ops;

	/* image */
	struct {
		gboolean exif_rotate_enable;
		gboolean exif_proof_rotate_enable;
		guint scroll_reset_method;
		gboolean fit_window_to_image;
		gboolean limit_window_size;
		gint max_window_size;
		gboolean limit_autofit_size;
		gint max_autofit_size;

		gint tile_cache_max;	/* in megabytes */
		gint image_cache_max;   /* in megabytes */
		gboolean enable_read_ahead;

		ZoomMode zoom_mode;
		gboolean zoom_2pass;
		gboolean zoom_to_fit_allow_expand;
		guint zoom_quality;
		gint zoom_increment;	/* 100 is 1.0, 5 is 0.05, 200 is 2.0, etc. */
		gboolean use_clutter_renderer;

		gboolean use_custom_border_color_in_fullscreen;
		gboolean use_custom_border_color;
		GdkColor border_color;
	} image;

	/* thumbnails */
	struct {
		gint max_width;
		gint max_height;
		gboolean enable_caching;
		gboolean cache_into_dirs;
		gboolean use_xvpics;
		gboolean spec_standard;
		guint quality;
		gboolean use_exif;
	} thumbnails;

	/* file filtering */
	struct {
		gboolean show_hidden_files;
		gboolean show_parent_directory;
		gboolean show_dot_directory;
		gboolean disable_file_extension_checks;
		gboolean disable;
	} file_filter;

	struct {
		gchar *ext;
	} sidecar;

	/* collections */
	struct {
		gboolean rectangular_selection;
	} collections;

	/* shell */
	struct {
		gchar *path;
		gchar *options;
	} shell;

	/* file sorting */
	struct {
		SortType method;
		gboolean ascending;
		gboolean case_sensitive; /* file sorting method (case) */
	} file_sort;

	/* slideshow */
	struct {
		gint delay;	/* in tenths of a second */
		gboolean random;
		gboolean repeat;
	} slideshow;

	/* fullscreen */
	struct {
		gint screen;
		gboolean clean_flip;
		gboolean disable_saver;
		gboolean above;
	} fullscreen;

	/* image overlay */
	struct {
		gchar *template_string;
		gint x;
		gint y;
		guint16 text_red;
		guint16 text_green;
		guint16 text_blue;
		guint16 text_alpha;
		guint16 background_red;
		guint16 background_green;
		guint16 background_blue;
		guint16 background_alpha;
		gchar *font;
	} image_overlay;

	/* properties dialog */
	struct {
		gchar *tabs_order;
	} properties;

	/* color profiles */
	struct {
		gboolean enabled;
		gint input_type;
		gchar *input_file[COLOR_PROFILE_INPUTS];
		gchar *input_name[COLOR_PROFILE_INPUTS];
		gchar *screen_file;
		gboolean use_image;
		gboolean use_x11_screen_profile;
		gint render_intent;
	} color_profile;

	/* Helpers programs */
	struct {
		struct {
			gchar *command_name;
			gchar *command_line;
		} html_browser;
	} helpers;

	/* Metadata */
	struct {
		gboolean enable_metadata_dirs;

		gboolean save_in_image_file;
		gboolean save_legacy_IPTC;
		gboolean warn_on_write_problems;

		gboolean save_legacy_format;

		gboolean sync_grouped_files;

		gboolean confirm_write;
		gint confirm_timeout;
		gboolean confirm_after_timeout;
		gboolean confirm_on_image_change;
		gboolean confirm_on_dir_change;
		gboolean keywords_case_sensitive;
		gboolean write_orientation;
	} metadata;

	/* Stereo */
	struct {
		gint mode;;
		gint fsmode;
		gboolean enable_fsmode;
		gint fixed_w, fixed_h;
		gint fixed_x1, fixed_y1;
		gint fixed_x2, fixed_y2;
		struct { /* options in this struct are packed to mode and fsmode entries */
			gboolean mirror_right;
			gboolean mirror_left;
			gboolean flip_right;
			gboolean flip_left;
			gboolean swap;
			gboolean temp_disable;
			gboolean fs_mirror_right;
			gboolean fs_mirror_left;
			gboolean fs_flip_right;
			gboolean fs_flip_left;
			gboolean fs_swap;
			gboolean fs_temp_disable;
		} tmp;
	} stereo;
};

ConfOptions *options;
CommandLine *command_line;

ConfOptions *init_options(ConfOptions *options);
void setup_default_options(ConfOptions *options);
void save_options(ConfOptions *options);
gboolean load_options(ConfOptions *options);

void copy_layout_options(LayoutOptions *dest, const LayoutOptions *src);
void free_layout_options_content(LayoutOptions *dest);
LayoutOptions *init_layout_options(LayoutOptions *options);

#endif /* OPTIONS_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
