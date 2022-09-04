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
	gboolean image_l_click_archive;
	gboolean image_l_click_video;
	gchar *image_l_click_video_editor;
	gboolean show_icon_names;
	gboolean show_star_rating;
	gboolean draw_rectangle;
	gboolean show_predefined_keyword_tree;
	gboolean overunderexposed;
	gboolean expand_menu_toolbar;

	/* various */
	gboolean tree_descend_subdirs;
	gboolean view_dir_list_single_click_enter;

	gboolean circular_selection_lists;

	gboolean lazy_image_sync;
	gboolean update_on_time_change;

	guint duplicates_similarity_threshold;
	guint duplicates_match;
	gboolean duplicates_thumbnails;
	guint duplicates_select_type;
	gboolean rot_invariant_sim;
	gboolean sort_totals;

	gint open_recent_list_maxsize;
	gint recent_folder_image_list_maxsize;
	gint dnd_icon_size;
	DnDAction dnd_default_action;
	gint clipboard_selection;

	gboolean save_window_positions;
	gboolean use_saved_window_positions_for_new_windows;
	gboolean save_window_workspace;
	gboolean tools_restore_state;
	gboolean save_dialog_window_positions;
	gboolean show_window_ids;

	gint log_window_lines;

	gboolean marks_save;		/**< save marks on exit */
	gchar *marks_tooltips[FILEDATA_MARKS_SIZE];

	gboolean with_rename;
	gboolean collections_on_top;
	gboolean hide_window_in_fullscreen;

	gchar *help_search_engine;

	/**
	 * @struct info_comment
	 * info sidebar component height
	 */
	struct {
		gint height;
	} info_comment;

	/**
	 * @struct info_keywords
	 * info sidebar component height
	 */
	struct {
		gint height;
	} info_keywords;

	/**
	 * @struct info_title
	 * info sidebar component height
	 */
	struct {
		gint height;
	} info_title;

	/**
	 * @struct info_rating
	 * info sidebar component height
	 */
	struct {
		gint height;
	} info_rating;

	/**
	 * @struct info_headline
	 * info sidebar component height
	 */
	struct {
		gint height;
	} info_headline;

	/* file ops */
	struct {
		gboolean enable_in_place_rename;

		gboolean confirm_delete;
		gboolean confirm_move_to_trash;
		gboolean enable_delete_key;
		gboolean safe_delete_enable;
		gboolean use_system_trash;
		gchar *safe_delete_path;
		gint safe_delete_folder_maxsize;
		gboolean no_trash;
	} file_ops;

	/* image */
	struct {
		gboolean exif_rotate_enable;
		guint scroll_reset_method;
		gboolean fit_window_to_image;
		gboolean limit_window_size;
		gint max_window_size;
		gboolean limit_autofit_size;
		gint max_autofit_size;
		gint max_enlargement_size;

		gint tile_cache_max;	/**< in megabytes */
		gint image_cache_max;   /**< in megabytes */
		gboolean enable_read_ahead;

		ZoomMode zoom_mode;
		gboolean zoom_2pass;
		gboolean zoom_to_fit_allow_expand;
		guint zoom_quality;
		gint zoom_increment;	/**< 100 is 1.0, 5 is 0.05, 200 is 2.0, etc. */
		ZoomStyle zoom_style;
		gboolean use_clutter_renderer;

		gboolean use_custom_border_color_in_fullscreen;
		gboolean use_custom_border_color;
		GdkColor border_color;
		GdkColor alpha_color_1;
		GdkColor alpha_color_2;

		gint tile_size;
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
		gboolean use_color_management;
		gboolean use_ft_metadata;
		gint collection_preview;
// 		gboolean use_ft_metadata_small;
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
		gboolean case_sensitive; /**< file sorting method (case) */
		gboolean natural;
	} file_sort;

	/* slideshow */
	struct {
		gint delay;	/**< in tenths of a second */
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
		gboolean sidecar_extended_name;

		gboolean check_spelling;
	} metadata;

	/* Stereo */
	struct {
		gint mode;
		gint fsmode;
		gboolean enable_fsmode;
		gint fixed_w, fixed_h;
		gint fixed_x1, fixed_y1;
		gint fixed_x2, fixed_y2;
		/**
		 * @struct tmp
		 * options in this struct are packed to mode and fsmode entries
		 */
		struct {
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

	/* External preview extraction */
	struct {
		gboolean enable;
		gchar *select; /**< path to executable */
		gchar *extract; /**< path to executable */
	} external_preview;

	/**
	 * @struct cp_mv_rn
	 * copy move rename
	 */
	struct {
		gint auto_start;
		gchar *auto_end;
		gint auto_padding;
		gint formatted_start;
	} cp_mv_rn;

	/* log window */
	struct {
		gboolean paused;
		gboolean line_wrap;
		gboolean timer_data;
	} log_window;

	/* star rating */
	struct {
		gunichar star;
		gunichar rejected;
	} star_rating;

	/* Printer */
	struct {
		gchar *image_font;
		gchar *page_font;
		gboolean show_image_text;
		gboolean show_page_text;
		gchar *page_text;
		gint image_text_position;
		gint page_text_position;
		gchar *template_string;
	} printer;

	/* Threads */
	struct {
		gint duplicates;
	} threads;

	gchar *mouse_button_8; /**< user-definable mouse buttons */
	gchar *mouse_button_9; /**< user-definable mouse buttons */

	gboolean class_filter[FILE_FORMAT_CLASSES]; /**< class file filter */

	gboolean read_metadata_in_idle;

	gboolean disable_gpu; /**< GPU - see main.cc */
	gboolean override_disable_gpu; /**< GPU - see main.cc */

	GList *disabled_plugins;
};

extern ConfOptions *options;
extern CommandLine *command_line;

ConfOptions *init_options(ConfOptions *options);
void setup_default_options(ConfOptions *options);
void save_options(ConfOptions *options);
gboolean load_options(ConfOptions *options);

void copy_layout_options(LayoutOptions *dest, const LayoutOptions *src);
void free_layout_options_content(LayoutOptions *dest);
LayoutOptions *init_layout_options(LayoutOptions *options);

#endif /* OPTIONS_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
