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

#include "main.h"
#include "preferences.h"

#include "bar-keywords.h"
#include "cache.h"
#include "editors.h"
#include "filedata.h"
#include "filefilter.h"
#include "fullscreen.h"
#include "image.h"
#include "image-overlay.h"
#include "color-man.h"
#include "img-view.h"
#include "layout-util.h"
#include "metadata.h"
#include "misc.h"
#include "osd.h"
#include "pixbuf-util.h"
#include "rcfile.h"
#include "slideshow.h"
#include "toolbar.h"
#include "trash.h"
#include "utilops.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-spinner.h"
#include "ui-tabcomp.h"
#include "window.h"
#include "zonedetect.h"

#ifdef HAVE_LCMS
#ifdef HAVE_LCMS2
#include <lcms2.h>
#else
#include <lcms.h>
#endif
#endif

#ifdef HAVE_SPELL
#include <gspell/gspell.h>
#endif

#define EDITOR_NAME_MAX_LENGTH 32
#define EDITOR_COMMAND_MAX_LENGTH 1024

static void image_overlay_set_text_colours();

GtkWidget *keyword_text;
static void config_tab_keywords_save();

struct ThumbSize
{
	gint w;
	gint h;
};

static ThumbSize thumb_size_list[] =
{
	{ 24, 24 },
	{ 32, 32 },
	{ 48, 48 },
	{ 64, 64 },
	{ 96, 72 },
	{ 96, 96 },
	{ 128, 96 },
	{ 128, 128 },
	{ 160, 120 },
	{ 160, 160 },
	{ 192, 144 },
	{ 192, 192 },
	{ 256, 192 },
	{ 256, 256 }
};

enum {
	FE_ENABLE,
	FE_EXTENSION,
	FE_DESCRIPTION,
	FE_CLASS,
	FE_WRITABLE,
	FE_ALLOW_SIDECAR
};

enum {
	AE_ACTION,
	AE_KEY,
	AE_TOOLTIP,
	AE_ACCEL
};

enum {
	FILETYPES_COLUMN_ENABLED = 0,
	FILETYPES_COLUMN_FILTER,
	FILETYPES_COLUMN_DESCRIPTION,
	FILETYPES_COLUMN_CLASS,
	FILETYPES_COLUMN_WRITABLE,
	FILETYPES_COLUMN_SIDECAR,
	FILETYPES_COLUMN_COUNT
};

const gchar *format_class_list[] = {
	N_("Unknown"),
	N_("Image"),
	N_("RAW Image"),
	N_("Metadata"),
	N_("Video"),
	N_("Collection"),
	N_("Document"),
	N_("Archive")
	};

/* config memory values */
static ConfOptions *c_options = nullptr;


#ifdef DEBUG
static gint debug_c;
#endif

static GtkWidget *configwindow = nullptr;
static GtkListStore *filter_store = nullptr;
static GtkTreeStore *accel_store = nullptr;

static GtkWidget *safe_delete_path_entry;

static GtkWidget *color_profile_input_file_entry[COLOR_PROFILE_INPUTS];
static GtkWidget *color_profile_input_name_entry[COLOR_PROFILE_INPUTS];
static GtkWidget *color_profile_screen_file_entry;
static GtkWidget *external_preview_select_entry;
static GtkWidget *external_preview_extract_entry;

static GtkWidget *sidecar_ext_entry;
static GtkWidget *help_search_engine_entry;


#define CONFIG_WINDOW_DEF_WIDTH		700
#define CONFIG_WINDOW_DEF_HEIGHT	600

/*
 *-----------------------------------------------------------------------------
 * option widget callbacks (private)
 *-----------------------------------------------------------------------------
 */

static void zoom_increment_cb(GtkWidget *spin, gpointer)
{
	c_options->image.zoom_increment = static_cast<gint>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) * 100.0 + 0.01);
}

static void slideshow_delay_hours_cb(GtkWidget *spin, gpointer)
{
	gint mins_secs_tenths, delay;

	mins_secs_tenths = c_options->slideshow.delay %
						(3600 * SLIDESHOW_SUBSECOND_PRECISION);

	delay = (gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) *
								(3600 * SLIDESHOW_SUBSECOND_PRECISION) +
								mins_secs_tenths);

	c_options->slideshow.delay = delay > 0 ? delay : SLIDESHOW_MIN_SECONDS *
													SLIDESHOW_SUBSECOND_PRECISION;
}

static void slideshow_delay_minutes_cb(GtkWidget *spin, gpointer)
{
	gint hours, secs_tenths, delay;

	hours = c_options->slideshow.delay / (3600 * SLIDESHOW_SUBSECOND_PRECISION);
	secs_tenths = c_options->slideshow.delay % (60 * SLIDESHOW_SUBSECOND_PRECISION);

	delay = hours * (3600 * SLIDESHOW_SUBSECOND_PRECISION) +
					(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) *
					(60 * SLIDESHOW_SUBSECOND_PRECISION) + secs_tenths);

	c_options->slideshow.delay = delay > 0 ? delay : SLIDESHOW_MIN_SECONDS *
													SLIDESHOW_SUBSECOND_PRECISION;
}

static void slideshow_delay_seconds_cb(GtkWidget *spin, gpointer)
{
	gint hours_mins, delay;

	hours_mins = c_options->slideshow.delay / (60 * SLIDESHOW_SUBSECOND_PRECISION);

	delay = (hours_mins * (60 * SLIDESHOW_SUBSECOND_PRECISION)) +
							static_cast<gint>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) *
							static_cast<gdouble>(SLIDESHOW_SUBSECOND_PRECISION) + 0.01);

	c_options->slideshow.delay = delay > 0 ? delay : SLIDESHOW_MIN_SECONDS *
													SLIDESHOW_SUBSECOND_PRECISION;
}

/*
 *-----------------------------------------------------------------------------
 * sync program to config window routine (private)
 *-----------------------------------------------------------------------------
 */

void config_entry_to_option(GtkWidget *entry, gchar **option, gchar *(*func)(const gchar *))
{
	const gchar *buf;

	g_free(*option);
	*option = nullptr;
	buf = gtk_entry_get_text(GTK_ENTRY(entry));
	if (buf && strlen(buf) > 0)
		{
		if (func)
			*option = func(buf);
		else
			*option = g_strdup(buf);
		}
}


static gboolean accel_apply_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	gchar *accel_path, *accel;

	gtk_tree_model_get(model, iter, AE_ACCEL, &accel_path, AE_KEY, &accel, -1);

	if (accel_path && accel_path[0])
		{
		GtkAccelKey key;
		gtk_accelerator_parse(accel, &key.accel_key, &key.accel_mods);
		gtk_accel_map_change_entry(accel_path, key.accel_key, key.accel_mods, TRUE);
		}

	g_free(accel_path);
	g_free(accel);

	return FALSE;
}


static void config_window_apply()
{
	gboolean refresh = FALSE;
#ifdef HAVE_LCMS2
	int i = 0;
#endif

	config_entry_to_option(safe_delete_path_entry, &options->file_ops.safe_delete_path, remove_trailing_slash);

	if (options->file_filter.show_hidden_files != c_options->file_filter.show_hidden_files) refresh = TRUE;
	if (options->file_filter.show_parent_directory != c_options->file_filter.show_parent_directory) refresh = TRUE;
	if (options->file_filter.show_dot_directory != c_options->file_filter.show_dot_directory) refresh = TRUE;
	if (options->file_sort.case_sensitive != c_options->file_sort.case_sensitive) refresh = TRUE;
	if (options->file_sort.natural != c_options->file_sort.natural) refresh = TRUE;
	if (options->file_filter.disable_file_extension_checks != c_options->file_filter.disable_file_extension_checks) refresh = TRUE;
	if (options->file_filter.disable != c_options->file_filter.disable) refresh = TRUE;

	options->file_ops.confirm_delete = c_options->file_ops.confirm_delete;
	options->file_ops.enable_delete_key = c_options->file_ops.enable_delete_key;
	options->file_ops.confirm_move_to_trash = c_options->file_ops.confirm_move_to_trash;
	options->file_ops.use_system_trash = c_options->file_ops.use_system_trash;
	options->file_ops.no_trash = c_options->file_ops.no_trash;
	options->file_ops.safe_delete_folder_maxsize = c_options->file_ops.safe_delete_folder_maxsize;
	options->tools_restore_state = c_options->tools_restore_state;
	options->save_window_positions = c_options->save_window_positions;
	options->use_saved_window_positions_for_new_windows = c_options->use_saved_window_positions_for_new_windows;
	options->save_window_workspace = c_options->save_window_workspace;
	options->save_dialog_window_positions = c_options->save_dialog_window_positions;
	options->show_window_ids = c_options->show_window_ids;
	options->image.scroll_reset_method = c_options->image.scroll_reset_method;
	options->image.zoom_2pass = c_options->image.zoom_2pass;
	options->image.fit_window_to_image = c_options->image.fit_window_to_image;
	options->image.limit_window_size = c_options->image.limit_window_size;
	options->image.zoom_to_fit_allow_expand = c_options->image.zoom_to_fit_allow_expand;
	options->image.max_window_size = c_options->image.max_window_size;
	options->image.limit_autofit_size = c_options->image.limit_autofit_size;
	options->image.max_autofit_size = c_options->image.max_autofit_size;
	options->image.max_enlargement_size = c_options->image.max_enlargement_size;
	options->image.tile_size = c_options->image.tile_size;
	options->progressive_key_scrolling = c_options->progressive_key_scrolling;
	options->keyboard_scroll_step = c_options->keyboard_scroll_step;

	if (options->thumbnails.max_width != c_options->thumbnails.max_width
	    || options->thumbnails.max_height != c_options->thumbnails.max_height
	    || options->thumbnails.quality != c_options->thumbnails.quality)
	        {
	    	thumb_format_changed = TRUE;
		refresh = TRUE;
		options->thumbnails.max_width = c_options->thumbnails.max_width;
		options->thumbnails.max_height = c_options->thumbnails.max_height;
		options->thumbnails.quality = c_options->thumbnails.quality;
		}
	options->thumbnails.enable_caching = c_options->thumbnails.enable_caching;
	options->thumbnails.cache_into_dirs = c_options->thumbnails.cache_into_dirs;
	options->thumbnails.use_exif = c_options->thumbnails.use_exif;
	options->thumbnails.use_color_management = c_options->thumbnails.use_color_management;
	options->thumbnails.collection_preview = c_options->thumbnails.collection_preview;
	options->thumbnails.use_ft_metadata = c_options->thumbnails.use_ft_metadata;
	options->thumbnails.spec_standard = c_options->thumbnails.spec_standard;
	options->metadata.enable_metadata_dirs = c_options->metadata.enable_metadata_dirs;
	options->file_filter.show_hidden_files = c_options->file_filter.show_hidden_files;
	options->file_filter.show_parent_directory = c_options->file_filter.show_parent_directory;
	options->file_filter.show_dot_directory = c_options->file_filter.show_dot_directory;
	options->file_filter.disable_file_extension_checks = c_options->file_filter.disable_file_extension_checks;

	options->file_sort.case_sensitive = c_options->file_sort.case_sensitive;
	options->file_sort.natural = c_options->file_sort.natural;
	options->file_filter.disable = c_options->file_filter.disable;

	config_entry_to_option(sidecar_ext_entry, &options->sidecar.ext, nullptr);
	sidecar_ext_parse(options->sidecar.ext);

	options->slideshow.random = c_options->slideshow.random;
	options->slideshow.repeat = c_options->slideshow.repeat;
	options->slideshow.delay = c_options->slideshow.delay;

	options->mousewheel_scrolls = c_options->mousewheel_scrolls;
	options->image_lm_click_nav = c_options->image_lm_click_nav;
	options->image_l_click_archive = c_options->image_l_click_archive;
	options->image_l_click_video = c_options->image_l_click_video;
	options->image_l_click_video_editor = c_options->image_l_click_video_editor;

	options->file_ops.enable_in_place_rename = c_options->file_ops.enable_in_place_rename;

	options->image.tile_cache_max = c_options->image.tile_cache_max;
	options->image.image_cache_max = c_options->image.image_cache_max;

	options->image.zoom_quality = c_options->image.zoom_quality;

	options->image.zoom_increment = c_options->image.zoom_increment;

	options->image.zoom_style = c_options->image.zoom_style;

	options->image.enable_read_ahead = c_options->image.enable_read_ahead;

	options->appimage_notifications = c_options->appimage_notifications;


	if (options->image.use_custom_border_color != c_options->image.use_custom_border_color
	    || options->image.use_custom_border_color_in_fullscreen != c_options->image.use_custom_border_color_in_fullscreen
	    || !gdk_color_equal(&options->image.border_color, &c_options->image.border_color))
		{
		options->image.use_custom_border_color_in_fullscreen = c_options->image.use_custom_border_color_in_fullscreen;
		options->image.use_custom_border_color = c_options->image.use_custom_border_color;
		options->image.border_color = c_options->image.border_color;
		layout_colors_update();
		view_window_colors_update();
		}

	options->image.alpha_color_1 = c_options->image.alpha_color_1;
	options->image.alpha_color_2 = c_options->image.alpha_color_2;

	options->fullscreen.screen = c_options->fullscreen.screen;
	options->fullscreen.clean_flip = c_options->fullscreen.clean_flip;
	options->fullscreen.disable_saver = c_options->fullscreen.disable_saver;
	options->fullscreen.above = c_options->fullscreen.above;
	if (c_options->image_overlay.template_string)
		set_image_overlay_template_string(&options->image_overlay.template_string,
						  c_options->image_overlay.template_string);
	if (c_options->image_overlay.font)
		set_image_overlay_font_string(&options->image_overlay.font,
						  c_options->image_overlay.font);
	options->image_overlay.text_red = c_options->image_overlay.text_red;
	options->image_overlay.text_green = c_options->image_overlay.text_green;
	options->image_overlay.text_blue = c_options->image_overlay.text_blue;
	options->image_overlay.text_alpha = c_options->image_overlay.text_alpha;
	options->image_overlay.background_red = c_options->image_overlay.background_red;
	options->image_overlay.background_green = c_options->image_overlay.background_green;
	options->image_overlay.background_blue = c_options->image_overlay.background_blue;
	options->image_overlay.background_alpha = c_options->image_overlay.background_alpha;
	options->update_on_time_change = c_options->update_on_time_change;

	options->duplicates_similarity_threshold = c_options->duplicates_similarity_threshold;
	options->rot_invariant_sim = c_options->rot_invariant_sim;

	options->tree_descend_subdirs = c_options->tree_descend_subdirs;

	options->view_dir_list_single_click_enter = c_options->view_dir_list_single_click_enter;
	options->circular_selection_lists = c_options->circular_selection_lists;

	options->open_recent_list_maxsize = c_options->open_recent_list_maxsize;
	options->recent_folder_image_list_maxsize = c_options->recent_folder_image_list_maxsize;
	options->dnd_icon_size = c_options->dnd_icon_size;
	options->clipboard_selection = c_options->clipboard_selection;
	options->dnd_default_action = c_options->dnd_default_action;

	options->metadata.save_in_image_file = c_options->metadata.save_in_image_file;
	options->metadata.save_legacy_IPTC = c_options->metadata.save_legacy_IPTC;
	options->metadata.warn_on_write_problems = c_options->metadata.warn_on_write_problems;
	options->metadata.save_legacy_format = c_options->metadata.save_legacy_format;
	options->metadata.sync_grouped_files = c_options->metadata.sync_grouped_files;
	options->metadata.confirm_write = c_options->metadata.confirm_write;
	options->metadata.sidecar_extended_name = c_options->metadata.sidecar_extended_name;
	options->metadata.confirm_timeout = c_options->metadata.confirm_timeout;
	options->metadata.confirm_after_timeout = c_options->metadata.confirm_after_timeout;
	options->metadata.confirm_on_image_change = c_options->metadata.confirm_on_image_change;
	options->metadata.confirm_on_dir_change = c_options->metadata.confirm_on_dir_change;
	options->metadata.keywords_case_sensitive = c_options->metadata.keywords_case_sensitive;
	options->metadata.write_orientation = c_options->metadata.write_orientation;
	options->metadata.check_spelling = c_options->metadata.check_spelling;
	options->stereo.mode = (c_options->stereo.mode & (PR_STEREO_HORIZ | PR_STEREO_VERT | PR_STEREO_FIXED | PR_STEREO_ANAGLYPH | PR_STEREO_HALF)) |
	                       (c_options->stereo.tmp.mirror_right ? PR_STEREO_MIRROR_RIGHT : 0) |
	                       (c_options->stereo.tmp.flip_right   ? PR_STEREO_FLIP_RIGHT : 0) |
	                       (c_options->stereo.tmp.mirror_left  ? PR_STEREO_MIRROR_LEFT : 0) |
	                       (c_options->stereo.tmp.flip_left    ? PR_STEREO_FLIP_LEFT : 0) |
	                       (c_options->stereo.tmp.swap         ? PR_STEREO_SWAP : 0) |
	                       (c_options->stereo.tmp.temp_disable ? PR_STEREO_TEMP_DISABLE : 0);
	options->stereo.fsmode = (c_options->stereo.fsmode & (PR_STEREO_HORIZ | PR_STEREO_VERT | PR_STEREO_FIXED | PR_STEREO_ANAGLYPH | PR_STEREO_HALF)) |
	                       (c_options->stereo.tmp.fs_mirror_right ? PR_STEREO_MIRROR_RIGHT : 0) |
	                       (c_options->stereo.tmp.fs_flip_right   ? PR_STEREO_FLIP_RIGHT : 0) |
	                       (c_options->stereo.tmp.fs_mirror_left  ? PR_STEREO_MIRROR_LEFT : 0) |
	                       (c_options->stereo.tmp.fs_flip_left    ? PR_STEREO_FLIP_LEFT : 0) |
	                       (c_options->stereo.tmp.fs_swap         ? PR_STEREO_SWAP : 0) |
	                       (c_options->stereo.tmp.fs_temp_disable ? PR_STEREO_TEMP_DISABLE : 0);
	options->stereo.enable_fsmode = c_options->stereo.enable_fsmode;
	options->stereo.fixed_w = c_options->stereo.fixed_w;
	options->stereo.fixed_h = c_options->stereo.fixed_h;
	options->stereo.fixed_x1 = c_options->stereo.fixed_x1;
	options->stereo.fixed_y1 = c_options->stereo.fixed_y1;
	options->stereo.fixed_x2 = c_options->stereo.fixed_x2;
	options->stereo.fixed_y2 = c_options->stereo.fixed_y2;

	options->info_keywords.height = c_options->info_keywords.height;
	options->info_title.height = c_options->info_title.height;
	options->info_comment.height = c_options->info_comment.height;
	options->info_rating.height = c_options->info_rating.height;

	options->show_predefined_keyword_tree = c_options->show_predefined_keyword_tree;
	options->expand_menu_toolbar = c_options->expand_menu_toolbar;

	options->marks_save = c_options->marks_save;
	options->with_rename = c_options->with_rename;
	options->collections_on_top = c_options->collections_on_top;
	options->hide_window_in_fullscreen = c_options->hide_window_in_fullscreen;
	config_entry_to_option(help_search_engine_entry, &options->help_search_engine, nullptr);

	options->external_preview.enable = c_options->external_preview.enable;
	config_entry_to_option(external_preview_select_entry, &options->external_preview.select, nullptr);
	config_entry_to_option(external_preview_extract_entry, &options->external_preview.extract, nullptr);

	options->read_metadata_in_idle = c_options->read_metadata_in_idle;

	options->star_rating.star = c_options->star_rating.star;
	options->star_rating.rejected = c_options->star_rating.rejected;

	options->threads.duplicates = c_options->threads.duplicates > 0 ? c_options->threads.duplicates : -1;

#ifdef DEBUG
	set_debug_level(debug_c);
#endif

#ifdef HAVE_LCMS
	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		config_entry_to_option(color_profile_input_name_entry[i], &options->color_profile.input_name[i], nullptr);
		config_entry_to_option(color_profile_input_file_entry[i], &options->color_profile.input_file[i], nullptr);
		}
	config_entry_to_option(color_profile_screen_file_entry, &options->color_profile.screen_file, nullptr);
	options->color_profile.use_x11_screen_profile = c_options->color_profile.use_x11_screen_profile;
	if (options->color_profile.render_intent != c_options->color_profile.render_intent)
		{
		options->color_profile.render_intent = c_options->color_profile.render_intent;
		color_man_update();
		}
#endif

	options->mouse_button_8 = c_options->mouse_button_8;
	options->mouse_button_9 = c_options->mouse_button_9;

	options->override_disable_gpu = c_options->override_disable_gpu;

	config_tab_keywords_save();

	image_options_sync();

	if (refresh)
		{
		filter_rebuild();
		layout_refresh(nullptr);
		}

	if (accel_store) gtk_tree_model_foreach(GTK_TREE_MODEL(accel_store), accel_apply_cb, nullptr);

	toolbar_apply(TOOLBAR_MAIN);
	toolbar_apply(TOOLBAR_STATUS);
}

/*
 *-----------------------------------------------------------------------------
 * config window main button callbacks (private)
 *-----------------------------------------------------------------------------
 */

static void config_window_close_cb(GtkWidget *, gpointer)
{
	gtk_widget_destroy(configwindow);
	configwindow = nullptr;
	filter_store = nullptr;
}

static void config_window_help_cb(GtkWidget *, gpointer data)
{
	auto notebook = static_cast<GtkWidget *>(data);
	gint i;

	static const gchar *html_section[] =
	{
	"GuideOptionsGeneral.html",
	"GuideOptionsImage.html",
	"GuideOptionsOSD.html",
	"GuideOptionsWindow.html",
	"GuideOptionsKeyboard.html",
	"GuideOptionsFiltering.html",
	"GuideOptionsMetadata.html",
	"GuideOptionsKeywords.html",
	"GuideOptionsColor.html",
	"GuideOptionsStereo.html",
	"GuideOptionsBehavior.html",
	"GuideOptionsToolbar.html",
	"GuideOptionsToolbar.html",
	"GuideOptionsAdvanced.html"
	};

	i = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
	help_window_show(html_section[i]);
}

static gboolean config_window_delete(GtkWidget *, GdkEventAny *, gpointer)
{
	config_window_close_cb(nullptr, nullptr);
	return TRUE;
}

static void config_window_ok_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw;
	auto notebook = static_cast<GtkNotebook *>(data);
	GdkWindow *window;
	gint x;
	gint y;
	gint w;
	gint h;
	gint page_number;

	lw = static_cast<LayoutWindow *>(layout_window_list->data);

	window = gtk_widget_get_window(widget);
	gdk_window_get_root_origin(window, &x, &y);
	w = gdk_window_get_width(window);
	h = gdk_window_get_height(window);
	page_number = gtk_notebook_get_current_page(notebook);

	lw->options.preferences_window.x = x;
	lw->options.preferences_window.y = y;
	lw->options.preferences_window.w = w;
	lw->options.preferences_window.h = h;
	lw->options.preferences_window.page_number = page_number;

	config_window_apply();
	layout_util_sync(lw);
	save_options(options);
	config_window_close_cb(nullptr, nullptr);
}

/*
 *-----------------------------------------------------------------------------
 * config window setup (private)
 *-----------------------------------------------------------------------------
 */

static void quality_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
		default:
			*option = GDK_INTERP_NEAREST;
			break;
		case 1:
			*option = GDK_INTERP_TILES;
			break;
		case 2:
			*option = GDK_INTERP_BILINEAR;
			break;
		}
}

static void dnd_default_action_selection_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
		default:
			*option = DND_ACTION_ASK;
			break;
		case 1:
			*option = DND_ACTION_COPY;
			break;
		case 2:
			*option = DND_ACTION_MOVE;
			break;
		}
}
static void clipboard_selection_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
			*option = CLIPBOARD_PRIMARY;
			break;
		case 1:
			*option = CLIPBOARD_CLIPBOARD;
			break;
		case 2:
			*option = CLIPBOARD_BOTH;
			break;
		default:
			*option = CLIPBOARD_BOTH;
		}
}

static void add_quality_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     guint option, guint *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Nearest (worst, but fastest)"));
	if (option == GDK_INTERP_NEAREST) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Tiles"));
	if (option == GDK_INTERP_TILES) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Bilinear (best, but slowest)"));
	if (option == GDK_INTERP_BILINEAR) current = 2;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(quality_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void add_dnd_default_action_selection_menu(GtkWidget *table, gint column, gint row, const gchar *text, DnDAction option, DnDAction *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Ask"));
	if (option == DND_ACTION_ASK) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Copy"));
	if (option == DND_ACTION_COPY) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Move"));
	if (option == DND_ACTION_MOVE) current = 2;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(dnd_default_action_selection_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void add_clipboard_selection_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     gint option, gint *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Primary"));
	if (option == CLIPBOARD_PRIMARY) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Clipboard"));
	if (option == CLIPBOARD_CLIPBOARD) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Both"));
	if (option == CLIPBOARD_BOTH) current = 2;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(clipboard_selection_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void zoom_style_selection_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
			*option = ZOOM_GEOMETRIC;
			break;
		case 1:
			*option = ZOOM_ARITHMETIC;
			break;
		default:
			*option = ZOOM_GEOMETRIC;
		}
}

static void add_zoom_style_selection_menu(GtkWidget *table, gint column, gint row, const gchar *text, ZoomStyle option, ZoomStyle *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Geometric"));
	if (option == ZOOM_GEOMETRIC) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Arithmetic"));
	if (option == ZOOM_ARITHMETIC) current = 1;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(zoom_style_selection_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

struct UseableMouseItems
{
	const gchar *name; /* GtkActionEntry terminology */
	const gchar *label;
	const gchar *icon_name;
};

static const UseableMouseItems useable_mouse_items[] = {
	{"", "", nullptr},
	{"FirstImage",	N_("First Image"), GQ_ICON_GO_TOP},
	{"PrevImage",	N_("Previous Image"), GQ_ICON_GO_UP},
	{"NextImage",	N_("Next Image"), GQ_ICON_GO_DOWN},
	{"LastImage",	N_("Last Image"), GQ_ICON_GO_BOTTOM},
	{"Back",	N_("Back"), GQ_ICON_GO_PREV},
	{"Forward",	N_("Forward"), GQ_ICON_GO_NEXT},
	{"Home",	N_("Home"), GQ_ICON_HOME},
	{"Up",	N_("Up"), GQ_ICON_GO_UP},
	{"FirstPage",	N_("First page"), GQ_ICON_PREV_PAGE},
	{"LastPage",	N_("Last Page"), GQ_ICON_NEXT_PAGE},
	{"NextPage",	N_("Next page"), GQ_ICON_FORWARD_PAGE},
	{"PrevPage",	N_("Previous Page"), GQ_ICON_BACK_PAGE},
	{"NewWindow",	N_("New _window"), GQ_ICON_NEW},
	{"NewCollection",	N_("New collection"), GQ_ICON_COLLECTION},
	{"OpenCollection",	N_("Open collection"), GQ_ICON_OPEN},
	{"Search",	N_("Search"), GQ_ICON_FIND},
	{"FindDupes",	N_("Find duplicates"), GQ_ICON_FIND},
	{"NewFolder",	N_("New folder"),GQ_ICON_DIRECTORY},
	{"Copy",	N_("Copy"), GQ_ICON_COPY},
	{"Move",	N_("Move"), PIXBUF_INLINE_ICON_MOVE},
	{"Rename",	N_("Rename"), PIXBUF_INLINE_ICON_RENAME},
	{"Delete",	N_("Delete"), GQ_ICON_DELETE},
	{"CloseWindow",	N_("Close Window"), GQ_ICON_CLOSE},
	{"PanView",	N_("Pan view"), PIXBUF_INLINE_ICON_PANORAMA},
	{"SelectAll",	N_("Select all"), PIXBUF_INLINE_ICON_SELECT_ALL},
	{"SelectNone",	N_("Select none"), PIXBUF_INLINE_ICON_SELECT_NONE},
	{"SelectInvert",	N_("Select invert"), PIXBUF_INLINE_ICON_SELECT_INVERT},
	{"ShowFileFilter",	N_("Show file filter"), PIXBUF_INLINE_ICON_FILE_FILTER},
	{"RectangularSelection",	N_("Select rectangle"), PIXBUF_INLINE_ICON_SELECT_RECTANGLE},
	{"Print",	N_("Print"), GQ_ICON_PRINT},
	{"Preferences",	N_("Preferences"), GQ_ICON_PREFERENCES},
	{"LayoutConfig",	N_("Configure this window"), GQ_ICON_PREFERENCES},
	{"Maintenance",	N_("Cache maintenance"), PIXBUF_INLINE_ICON_MAINTENANCE},
	{"RotateCW",	N_("Rotate clockwise 90°"), PIXBUF_INLINE_ICON_CW},
	{"RotateCCW",	N_("Rotate counterclockwise 90°"), PIXBUF_INLINE_ICON_CCW},
	{"Rotate180",	N_("Rotate 180°"), PIXBUF_INLINE_ICON_180},
	{"Mirror",	N_("Mirror"), PIXBUF_INLINE_ICON_MIRROR},
	{"Flip",	N_("Flip"), PIXBUF_INLINE_ICON_FLIP},
	{"AlterNone",	N_("Original state"), PIXBUF_INLINE_ICON_ORIGINAL},
	{"ZoomIn",	N_("Zoom in"), GQ_ICON_ZOOM_IN},
	{"ZoomOut",	N_("Zoom out"), GQ_ICON_ZOOM_OUT},
	{"Zoom100",	N_("Zoom 1:1"), GQ_ICON_ZOOM_100},
	{"ZoomFit",	N_("Zoom to fit"), GQ_ICON_ZOOM_FIT},
	{"ZoomFillHor",	N_("Fit Horizontaly"), PIXBUF_INLINE_ICON_ZOOMFILLHOR},
	{"ZoomFillVert",	N_("Fit vertically"), PIXBUF_INLINE_ICON_ZOOMFILLVERT},
	{"Zoom200",	N_("Zoom 2:1"), GQ_ICON_GENERIC},
	{"Zoom300",	N_("Zoom 3:1"), GQ_ICON_GENERIC},
	{"Zoom400",	N_("Zoom 4:1"), GQ_ICON_GENERIC},
	{"Zoom50",	N_("Zoom 1:2"), GQ_ICON_GENERIC},
	{"Zoom33",	N_("Zoom 1:3"), GQ_ICON_GENERIC},
	{"Zoom25",	N_("Zoom 1:4"), GQ_ICON_GENERIC},
	{"ConnectZoomIn",	N_("Connected Zoom in"), GQ_ICON_ZOOM_IN},
	{"SplitPaneSync",	N_("Split Pane Sync"), PIXBUF_INLINE_SPLIT_PANE_SYNC},
	{"Grayscale",	N_("Grayscale"), PIXBUF_INLINE_ICON_GRAYSCALE},
	{"OverUnderExposed",	N_("Over Under Exposed"), PIXBUF_INLINE_ICON_EXPOSURE},
	{"HideTools",	N_("Hide file list"), PIXBUF_INLINE_ICON_HIDETOOLS},
	{"SlideShowPause",	N_("Pause slideshow"), GQ_ICON_PAUSE},
	{"SlideShowFaster",	N_("Slideshow Faster"), GQ_ICON_GENERIC},
	{"SlideShowSlower",	N_("Slideshow Slower"), GQ_ICON_GENERIC},
	{"Refresh",	N_("Refresh"), GQ_ICON_REFRESH},
	{"HelpContents",	N_("Help"), GQ_ICON_HELP},
	{"ExifWin",	N_("Exif window"), PIXBUF_INLINE_ICON_EXIF},
	{"Thumbnails",	N_("Show thumbnails"), PIXBUF_INLINE_ICON_THUMB},
	{"ShowMarks",	N_("Show marks"), PIXBUF_INLINE_ICON_MARKS},
	{"DrawRectangle",	N_("Draw Rectangle"), PIXBUF_INLINE_ICON_DRAW_RECTANGLE},
	{"FloatTools",	N_("Float file list"), PIXBUF_INLINE_ICON_FLOAT},
	{"SBar",	N_("Info sidebar"), PIXBUF_INLINE_ICON_INFO},
	{"SBarSort",	N_("Sort manager"), PIXBUF_INLINE_ICON_SORT},
	{"Quit",	N_("Quit"), GQ_ICON_QUIT},
	{nullptr,		nullptr, nullptr}
};

static void mouse_buttons_selection_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gchar **>(data);
	gchar *label;

	label = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));

	const UseableMouseItems *list = useable_mouse_items;

	while (list->name)
		{
		if (g_strcmp0(list->label, label) == 0)
			{
			break;
			}
		list++;
		}

	g_free(*option);
	*option = g_strdup(list->name);
	g_free(label);
}

static void add_mouse_selection_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     gchar *option, gchar **option_c)
{
	GtkWidget *combo;
	gint current = 0;
	gint i = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	const UseableMouseItems *list = useable_mouse_items;

	while (list->name)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), list->label);
		if (g_strcmp0(list->name, option) == 0)
			{
			current = i;
			}
		i++;
		list++;
		}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(mouse_buttons_selection_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void thumb_size_menu_cb(GtkWidget *combo, gpointer)
{
	gint n;

	n = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	if (n < 0) return;

	if (static_cast<guint>(n) < sizeof(thumb_size_list) / sizeof(ThumbSize))
		{
		c_options->thumbnails.max_width = thumb_size_list[n].w;
		c_options->thumbnails.max_height = thumb_size_list[n].h;
		}
	else
		{
		c_options->thumbnails.max_width = options->thumbnails.max_width;
		c_options->thumbnails.max_height = options->thumbnails.max_height;
		}
}

static void add_thumb_size_menu(GtkWidget *table, gint column, gint row, gchar *text)
{
	GtkWidget *combo;
	gint current;
	gint i;

	c_options->thumbnails.max_width = options->thumbnails.max_width;
	c_options->thumbnails.max_height = options->thumbnails.max_height;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	current = -1;
	for (i = 0; static_cast<guint>(i) < sizeof(thumb_size_list) / sizeof(ThumbSize); i++)
		{
		gint w, h;
		gchar *buf;

		w = thumb_size_list[i].w;
		h = thumb_size_list[i].h;

		buf = g_strdup_printf("%d x %d", w, h);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), buf);
		g_free(buf);

		if (w == options->thumbnails.max_width && h == options->thumbnails.max_height) current = i;
		}

	if (current == -1)
		{
		gchar *buf;

		buf = g_strdup_printf("%s %d x %d", _("Custom"), options->thumbnails.max_width, options->thumbnails.max_height);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), buf);
		g_free(buf);

		current = i;
		}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);
	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(thumb_size_menu_cb), NULL);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void stereo_mode_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
		default:
			*option = PR_STEREO_NONE;
			break;
		case 1:
			*option = PR_STEREO_ANAGLYPH_RC;
			break;
		case 2:
			*option = PR_STEREO_ANAGLYPH_GM;
			break;
		case 3:
			*option = PR_STEREO_ANAGLYPH_YB;
			break;
		case 4:
			*option = PR_STEREO_ANAGLYPH_GRAY_RC;
			break;
		case 5:
			*option = PR_STEREO_ANAGLYPH_GRAY_GM;
			break;
		case 6:
			*option = PR_STEREO_ANAGLYPH_GRAY_YB;
			break;
		case 7:
			*option = PR_STEREO_ANAGLYPH_DB_RC;
			break;
		case 8:
			*option = PR_STEREO_ANAGLYPH_DB_GM;
			break;
		case 9:
			*option = PR_STEREO_ANAGLYPH_DB_YB;
			break;
		case 10:
			*option = PR_STEREO_HORIZ;
			break;
		case 11:
			*option = PR_STEREO_HORIZ | PR_STEREO_HALF;
			break;
		case 12:
			*option = PR_STEREO_VERT;
			break;
		case 13:
			*option = PR_STEREO_VERT | PR_STEREO_HALF;
			break;
		case 14:
			*option = PR_STEREO_FIXED;
			break;
		}
}

static void add_stereo_mode_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     gint option, gint *option_c, gboolean add_fixed)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Single image"));

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Red-Cyan"));
	if (option & PR_STEREO_ANAGLYPH_RC) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Green-Magenta"));
	if (option & PR_STEREO_ANAGLYPH_GM) current = 2;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Yellow-Blue"));
	if (option & PR_STEREO_ANAGLYPH_YB) current = 3;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Gray Red-Cyan"));
	if (option & PR_STEREO_ANAGLYPH_GRAY_RC) current = 4;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Gray Green-Magenta"));
	if (option & PR_STEREO_ANAGLYPH_GRAY_GM) current = 5;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Gray Yellow-Blue"));
	if (option & PR_STEREO_ANAGLYPH_GRAY_YB) current = 6;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Dubois Red-Cyan"));
	if (option & PR_STEREO_ANAGLYPH_DB_RC) current = 7;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Dubois Green-Magenta"));
	if (option & PR_STEREO_ANAGLYPH_DB_GM) current = 8;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Anaglyph Dubois Yellow-Blue"));
	if (option & PR_STEREO_ANAGLYPH_DB_YB) current = 9;

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Side by Side"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Side by Side Half size"));
	if (option & PR_STEREO_HORIZ)
		{
		current = 10;
		if (option & PR_STEREO_HALF) current = 11;
		}

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Top - Bottom"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Top - Bottom Half size"));
	if (option & PR_STEREO_VERT)
		{
		current = 12;
		if (option & PR_STEREO_HALF) current = 13;
		}

	if (add_fixed)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Fixed position"));
		if (option & PR_STEREO_FIXED) current = 14;
		}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(stereo_mode_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void video_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gchar **>(data);

	auto ed = static_cast<EditorDescription *>(g_list_nth_data(editor_list_get(), gtk_combo_box_get_active(GTK_COMBO_BOX(combo))));
	*option = ed->key;
}

static void video_menu_populate(gpointer data, gpointer user_data)
{
	auto combo = static_cast<GtkWidget *>(user_data);
	auto ed = static_cast<EditorDescription *>(data);

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), ed->name);
}

static void add_video_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     gchar *option, gchar **option_c)
{
	GtkWidget *combo;
	gint current;
/* use lists since they are sorted */
	GList *eds = editor_list_get();

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();
	g_list_foreach(eds,video_menu_populate,combo);
	current = option ? g_list_index(eds,g_hash_table_lookup(editors,option)): -1;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(video_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void filter_store_populate()
{
	GList *work;

	if (!filter_store) return;

	gtk_list_store_clear(filter_store);

	work = filter_get_list();
	while (work)
		{
		FilterEntry *fe;
		GtkTreeIter iter;

		fe = static_cast<FilterEntry *>(work->data);
		work = work->next;

		gtk_list_store_append(filter_store, &iter);
		gtk_list_store_set(filter_store, &iter, 0, fe, -1);
		}
}

static void filter_store_ext_edit_cb(GtkCellRendererText *, gchar *path_str, gchar *new_text, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	auto fe = static_cast<FilterEntry *>(data);
	GtkTreePath *tpath;
	GtkTreeIter iter;

	if (!new_text || strlen(new_text) < 1) return;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	g_free(fe->extensions);
	fe->extensions = g_strdup(new_text);

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_store_class_edit_cb(GtkCellRendererText *, gchar *path_str, gchar *new_text, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	auto fe = static_cast<FilterEntry *>(data);
	GtkTreePath *tpath;
	GtkTreeIter iter;
	gint i;

	if (!new_text || !new_text[0]) return;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		if (strcmp(new_text, _(format_class_list[i])) == 0)
			{
			fe->file_class = static_cast<FileFormatClass>(i);
			break;
			}
		}

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_store_desc_edit_cb(GtkCellRendererText *, gchar *path_str, gchar *new_text, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	FilterEntry *fe;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	if (!new_text || !new_text[0]) return;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	g_free(fe->description);
	fe->description = g_strdup(new_text);

	gtk_tree_path_free(tpath);
}

static void filter_store_enable_cb(GtkCellRendererToggle *, gchar *path_str, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	FilterEntry *fe;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	fe->enabled = !fe->enabled;

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_store_writable_cb(GtkCellRendererToggle *, gchar *path_str, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	FilterEntry *fe;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	fe->writable = !fe->writable;
	if (fe->writable) fe->allow_sidecar = FALSE;

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_store_sidecar_cb(GtkCellRendererToggle *, gchar *path_str, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	FilterEntry *fe;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	fe->allow_sidecar = !fe->allow_sidecar;
	if (fe->allow_sidecar) fe->writable = FALSE;

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_set_func(GtkTreeViewColumn *, GtkCellRenderer *cell,
			    GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	FilterEntry *fe;

	gtk_tree_model_get(tree_model, iter, 0, &fe, -1);

	switch (GPOINTER_TO_INT(data))
		{
		case FE_ENABLE:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "active", fe->enabled, NULL);
			break;
		case FE_EXTENSION:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "text", fe->extensions, NULL);
			break;
		case FE_DESCRIPTION:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "text", fe->description, NULL);
			break;
		case FE_CLASS:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "text", _(format_class_list[fe->file_class]), NULL);
			break;
		case FE_WRITABLE:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "active", fe->writable, NULL);
			break;
		case FE_ALLOW_SIDECAR:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "active", fe->allow_sidecar, NULL);
			break;
		}
}

static gboolean filter_add_scroll(gpointer data)
{
	GtkTreePath *path;
	GList *list_cells;
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	gint rows;
	GtkTreeIter iter;
	GtkTreeModel *store;
	gboolean valid;
	FilterEntry *filter;

	rows = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(filter_store), nullptr);
	path = gtk_tree_path_new_from_indices(rows-1, -1);

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(data), 0);

	list_cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
	cell = static_cast<GtkCellRenderer *>(g_list_last(list_cells)->data);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(data));
	valid = gtk_tree_model_get_iter_first(store, &iter);

	while (valid)
		{
		gtk_tree_model_get(store, &iter, 0, &filter, -1);

		if (g_strcmp0(filter->extensions, ".new") == 0)
			{
			path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
			break;
			}

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}

	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(data),
								path, column, FALSE, 0.0, 0.0 );
	gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(data),
								path, column, cell, TRUE);

	gtk_tree_path_free(path);
	g_list_free(list_cells);

	return(G_SOURCE_REMOVE);
}

static void filter_add_cb(GtkWidget *, gpointer data)
{
	filter_add_unique("description", ".new", FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
	filter_store_populate();

	g_idle_add(static_cast<GSourceFunc>(filter_add_scroll), data);
}

static void filter_remove_cb(GtkWidget *, gpointer data)
{
	auto filter_view = static_cast<GtkWidget *>(data);
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	FilterEntry *fe;

	if (!filter_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(filter_view));
	if (!gtk_tree_selection_get_selected(selection, nullptr, &iter)) return;
	gtk_tree_model_get(GTK_TREE_MODEL(filter_store), &iter, 0, &fe, -1);
	if (!fe) return;

	filter_remove_entry(fe);
	filter_rebuild();
	filter_store_populate();
}

static gboolean filter_default_ok_scroll(GtkTreeView *data)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;

	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(filter_store), &iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(filter_store), &iter);
	column = gtk_tree_view_get_column(GTK_TREE_VIEW(data),0);

	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(data),
				     path, column,
				     FALSE, 0.0, 0.0);

	gtk_tree_path_free(path);

	return(G_SOURCE_REMOVE);
}

static void filter_default_ok_cb(GenericDialog *gd, gpointer)
{
	filter_reset();
	filter_add_defaults();
	filter_rebuild();
	filter_store_populate();

	g_idle_add(reinterpret_cast<GSourceFunc>(filter_default_ok_scroll), gd->data);
}

static void dummy_cancel_cb(GenericDialog *, gpointer)
{
	/* no op, only so cancel button appears */
}

static void filter_default_cb(GtkWidget *widget, gpointer data)
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Reset filters"),
				"reset_filter", widget, TRUE,
				dummy_cancel_cb, data);
	generic_dialog_add_message(gd, GTK_STOCK_DIALOG_QUESTION, _("Reset filters"),
				   _("This will reset the file filters to the defaults.\nContinue?"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", filter_default_ok_cb, TRUE);
	gtk_widget_show(gd->dialog);
}

static void filter_disable_cb(GtkWidget *widget, gpointer data)
{
	auto frame = static_cast<GtkWidget *>(data);

	gtk_widget_set_sensitive(frame,
				 !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

static void safe_delete_view_cb(GtkWidget *, gpointer)
{
	layout_set_path(nullptr, gtk_entry_get_text(GTK_ENTRY(safe_delete_path_entry)));
}

static void safe_delete_clear_ok_cb(GenericDialog *, gpointer)
{
	file_util_trash_clear();
}

static void safe_delete_clear_cb(GtkWidget *widget, gpointer)
{
	GenericDialog *gd;
	GtkWidget *entry;
	gd = generic_dialog_new(_("Clear trash"),
				"clear_trash", widget, TRUE,
				dummy_cancel_cb, nullptr);
	generic_dialog_add_message(gd, GTK_STOCK_DIALOG_QUESTION, _("Clear trash"),
				    _("This will remove the trash contents."), FALSE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", safe_delete_clear_ok_cb, TRUE);
	entry = gtk_entry_new();
	gtk_widget_set_can_focus(entry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
	if (options->file_ops.safe_delete_path) gtk_entry_set_text(GTK_ENTRY(entry), options->file_ops.safe_delete_path);
	gtk_box_pack_start(GTK_BOX(gd->vbox), entry, FALSE, FALSE, 0);
	gtk_widget_show(entry);
	gtk_widget_show(gd->dialog);
}

static void image_overlay_template_view_changed_cb(GtkWidget *, gpointer data)
{
	GtkWidget *pTextView;
	GtkTextBuffer *pTextBuffer;
	GtkTextIter iStart;
	GtkTextIter iEnd;

	pTextView = GTK_WIDGET(data);

	pTextBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pTextView));
	gtk_text_buffer_get_start_iter(pTextBuffer, &iStart);
	gtk_text_buffer_get_end_iter(pTextBuffer, &iEnd);

	set_image_overlay_template_string(&c_options->image_overlay.template_string,
					  gtk_text_buffer_get_text(pTextBuffer, &iStart, &iEnd, TRUE));
}

static void image_overlay_default_template_ok_cb(GenericDialog *, gpointer data)
{
	auto text_view = static_cast<GtkTextView *>(data);
	GtkTextBuffer *buffer;

	set_default_image_overlay_template_string(&options->image_overlay.template_string);
	if (!configwindow) return;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
	gtk_text_buffer_set_text(buffer, options->image_overlay.template_string, -1);
}

static void image_overlay_default_template_cb(GtkWidget *widget, gpointer data)
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Reset image overlay template string"),
				"reset_image_overlay_template_string", widget, TRUE,
				dummy_cancel_cb, data);
	generic_dialog_add_message(gd, GTK_STOCK_DIALOG_QUESTION, _("Reset image overlay template string"),
				   _("This will reset the image overlay template string to the default.\nContinue?"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", image_overlay_default_template_ok_cb, TRUE);
	gtk_widget_show(gd->dialog);
}

static void image_overlay_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuideOptionsOSD.html");
}

static void image_overlay_set_font_cb(GtkWidget *widget, gpointer)
{
	GtkWidget *dialog;
	char *font;
	PangoFontDescription *font_desc;

	dialog = gtk_font_chooser_dialog_new("Image Overlay Font", GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	gtk_font_chooser_set_font(GTK_FONT_CHOOSER(dialog), options->image_overlay.font);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_CANCEL)
		{
		font_desc = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(dialog));
		font = pango_font_description_to_string(font_desc);
		g_free(c_options->image_overlay.font);
		c_options->image_overlay.font = g_strdup(font);
		g_free(font);
		}

	gtk_widget_destroy(dialog);
}

static void image_overlay_set_text_colour_cb(GtkWidget *widget, gpointer)
{
	GtkWidget *dialog;
	GdkRGBA colour;

	dialog = gtk_color_chooser_dialog_new("Image Overlay Text Colour", GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	colour.red = options->image_overlay.text_red;
	colour.green = options->image_overlay.text_green;
	colour.blue = options->image_overlay.text_blue;
	colour.alpha = options->image_overlay.text_alpha;
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &colour);
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dialog), TRUE);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_CANCEL)
		{
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &colour);
		c_options->image_overlay.text_red = colour.red*255;
		c_options->image_overlay.text_green = colour.green*255;
		c_options->image_overlay.text_blue = colour.blue*255;
		c_options->image_overlay.text_alpha = colour.alpha*255;
		}
	gtk_widget_destroy(dialog);
}


static void image_overlay_set_background_colour_cb(GtkWidget *widget, gpointer)
{
	GtkWidget *dialog;
	GdkRGBA colour;

	dialog = gtk_color_chooser_dialog_new("Image Overlay Background Colour", GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	colour.red = options->image_overlay.background_red;
	colour.green = options->image_overlay.background_green;
	colour.blue = options->image_overlay.background_blue;
	colour.alpha = options->image_overlay.background_alpha;
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &colour);
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dialog), TRUE);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_CANCEL)
		{
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &colour);
		c_options->image_overlay.background_red = colour.red*255;
		c_options->image_overlay.background_green = colour.green*255;
		c_options->image_overlay.background_blue = colour.blue*255;
		c_options->image_overlay.background_alpha = colour.alpha*255;
		}
	gtk_widget_destroy(dialog);
}

static void accel_store_populate()
{
	LayoutWindow *lw;
	GList *groups, *actions;
	GtkAction *action;
	const gchar *accel_path;
	GtkAccelKey key;
	GtkTreeIter iter;

	if (!accel_store || !layout_window_list || !layout_window_list->data) return;

	gtk_tree_store_clear(accel_store);
	lw = static_cast<LayoutWindow *>(layout_window_list->data); /* get the actions from the first window, it should not matter, they should be the same in all windows */

	g_assert(lw && lw->ui_manager);
	groups = gtk_ui_manager_get_action_groups(lw->ui_manager);
	while (groups)
		{
		actions = gtk_action_group_list_actions(GTK_ACTION_GROUP(groups->data));
		while (actions)
			{
			action = GTK_ACTION(actions->data);
			accel_path = gtk_action_get_accel_path(action);
			if (accel_path && gtk_accel_map_lookup_entry(accel_path, &key))
				{
				gchar *label, *label2, *tooltip, *accel;
				g_object_get(action,
					     "tooltip", &tooltip,
					     "label", &label,
					     NULL);

				if (pango_parse_markup(label, -1, '_', nullptr, &label2, nullptr, nullptr) && label2)
					{
					g_free(label);
					label = label2;
					}

				accel = gtk_accelerator_name(key.accel_key, key.accel_mods);

				if (tooltip)
					{
					gtk_tree_store_append(accel_store, &iter, nullptr);
					gtk_tree_store_set(accel_store, &iter,
							   AE_ACTION, label,
							   AE_KEY, accel,
							   AE_TOOLTIP, tooltip ? tooltip : "",
							   AE_ACCEL, accel_path,
							   -1);
					}

				g_free(accel);
				g_free(label);
				g_free(tooltip);
				}
			actions = actions->next;
			}

		groups = groups->next;
		}
}

static void accel_store_cleared_cb(GtkCellRendererAccel *, gchar *, gpointer)
{

}

static gboolean accel_remove_key_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
	auto accel1 = static_cast<gchar *>(data);
	gchar *accel2;
	GtkAccelKey key1;
	GtkAccelKey key2;

	gtk_tree_model_get(model, iter, AE_KEY, &accel2, -1);

	gtk_accelerator_parse(accel1, &key1.accel_key, &key1.accel_mods);
	gtk_accelerator_parse(accel2, &key2.accel_key, &key2.accel_mods);

	if (key1.accel_key == key2.accel_key && key1.accel_mods == key2.accel_mods)
		{
		gtk_tree_store_set(accel_store, iter, AE_KEY, "",  -1);
		DEBUG_1("accelerator key '%s' is already used, removing.", accel1);
		}

	g_free(accel2);

	return FALSE;
}


static void accel_store_edited_cb(GtkCellRendererAccel *, gchar *path_string, guint accel_key, GdkModifierType accel_mods, guint, gpointer)
{
	auto model = reinterpret_cast<GtkTreeModel *>(accel_store);
	GtkTreeIter iter;
	gchar *acc;
	gchar *accel_path;
	GtkAccelKey old_key, key;
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);

	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, AE_ACCEL, &accel_path, -1);

	/* test if the accelerator can be stored without conflicts*/
	gtk_accel_map_lookup_entry(accel_path, &old_key);

	/* change the key and read it back (change may fail on keys hardcoded in gtk)*/
	gtk_accel_map_change_entry(accel_path, accel_key, accel_mods, TRUE);
	gtk_accel_map_lookup_entry(accel_path, &key);

	/* restore the original for now, the key will be really changed when the changes are confirmed */
	gtk_accel_map_change_entry(accel_path, old_key.accel_key, old_key.accel_mods, TRUE);

	acc = gtk_accelerator_name(key.accel_key, key.accel_mods);
	gtk_tree_model_foreach(GTK_TREE_MODEL(accel_store), accel_remove_key_cb, acc);

	gtk_tree_store_set(accel_store, &iter, AE_KEY, acc, -1);
	gtk_tree_path_free(path);
	g_free(acc);
}

static gboolean accel_default_scroll(GtkTreeView *data)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;

	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(accel_store), &iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(accel_store), &iter);
	column = gtk_tree_view_get_column(GTK_TREE_VIEW(data),0);

	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(data),
				     path, column,
				     FALSE, 0.0, 0.0);

	gtk_tree_path_free(path);

	return(G_SOURCE_REMOVE);
}

static void accel_default_cb(GtkWidget *, gpointer data)
{
	accel_store_populate();

	g_idle_add(reinterpret_cast<GSourceFunc>(accel_default_scroll), data);
}

void accel_clear_selection(GtkTreeModel *, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	gtk_tree_store_set(accel_store, iter, AE_KEY, "", -1);
}

void accel_reset_selection(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	GtkAccelKey key;
	gchar *accel_path, *accel;

	gtk_tree_model_get(model, iter, AE_ACCEL, &accel_path, -1);
	gtk_accel_map_lookup_entry(accel_path, &key);
	accel = gtk_accelerator_name(key.accel_key, key.accel_mods);

	gtk_tree_model_foreach(GTK_TREE_MODEL(accel_store), accel_remove_key_cb, accel);

	gtk_tree_store_set(accel_store, iter, AE_KEY, accel, -1);
	g_free(accel_path);
	g_free(accel);
}

static void accel_clear_cb(GtkWidget *, gpointer data)
{
	GtkTreeSelection *selection;

	if (!accel_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
	gtk_tree_selection_selected_foreach(selection, &accel_clear_selection, nullptr);
}

static void accel_reset_cb(GtkWidget *, gpointer data)
{
	GtkTreeSelection *selection;

	if (!accel_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
	gtk_tree_selection_selected_foreach(selection, &accel_reset_selection, nullptr);
}



static GtkWidget *scrolled_notebook_page(GtkWidget *notebook, const gchar *title)
{
	GtkWidget *label;
	GtkWidget *vbox;
	GtkWidget *scrolled;
	GtkWidget *viewport;

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled), PREF_PAD_BORDER);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	label = gtk_label_new(title);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, label);
	gtk_widget_show(scrolled);

	viewport = gtk_viewport_new(nullptr, nullptr);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), viewport);
	gtk_widget_show(viewport);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(viewport), vbox);
	gtk_widget_show(vbox);

	return vbox;
}

static void cache_standard_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->thumbnails.spec_standard =TRUE;
		c_options->thumbnails.cache_into_dirs = FALSE;
		}
}

static void cache_geeqie_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->thumbnails.spec_standard =FALSE;
		c_options->thumbnails.cache_into_dirs = FALSE;
		}
}

static void cache_local_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->thumbnails.cache_into_dirs = TRUE;
		c_options->thumbnails.spec_standard =FALSE;
		}
}

static void help_search_engine_entry_icon_cb(GtkEntry *, GtkEntryIconPosition pos, GdkEvent *, gpointer userdata)
{
	if (pos == GTK_ENTRY_ICON_PRIMARY)
		{
		gtk_entry_set_text(GTK_ENTRY(userdata), HELP_SEARCH_ENGINE);
		}
	else
		{
		gtk_entry_set_text(GTK_ENTRY(userdata), "");
		}
}

static void star_rating_star_icon_cb(GtkEntry *, GtkEntryIconPosition pos, GdkEvent *, gpointer userdata)
{
	gchar *rating_symbol;

	if (pos == GTK_ENTRY_ICON_PRIMARY)
		{
		rating_symbol = g_strdup_printf("U+%X", STAR_RATING_STAR);
		gtk_entry_set_text(GTK_ENTRY(userdata), rating_symbol);
		g_free(rating_symbol);
		}
	else
		{
		gtk_entry_set_text(GTK_ENTRY(userdata), "U+");
		gtk_widget_grab_focus(GTK_WIDGET(userdata));
		gtk_editable_select_region(GTK_EDITABLE(userdata), 2, 2);
		}
}

static void star_rating_rejected_icon_cb(GtkEntry *, GtkEntryIconPosition pos, GdkEvent *, gpointer userdata)
{
	gchar *rating_symbol;

	if (pos == GTK_ENTRY_ICON_PRIMARY)
		{
		rating_symbol = g_strdup_printf("U+%X", STAR_RATING_REJECTED);
		gtk_entry_set_text(GTK_ENTRY(userdata), rating_symbol);
		g_free(rating_symbol);
		}
	else
		{
		gtk_entry_set_text(GTK_ENTRY(userdata), "U+");
		gtk_widget_grab_focus(GTK_WIDGET(userdata));
		gtk_editable_select_region(GTK_EDITABLE(userdata), 2, 2);
		}
}

static guint star_rating_symbol_test(GtkWidget *, gpointer data)
{
	auto hbox = static_cast<GtkContainer *>(data);
	GString *str = g_string_new(nullptr);
	GtkEntry *hex_code_entry;
	gchar *hex_code_full;
	gchar **hex_code;
	GList *list;
	guint64 hex_value = 0;

	list = gtk_container_get_children(hbox);

	hex_code_entry = static_cast<GtkEntry *>(g_list_nth_data(list, 2));
	hex_code_full = g_strdup(gtk_entry_get_text(hex_code_entry));

	hex_code = g_strsplit(hex_code_full, "+", 2);
	if (hex_code[0] && hex_code[1])
		{
		hex_value = strtoull(hex_code[1], nullptr, 16);
		}
	if (!hex_value || hex_value > 0x10FFFF)
		{
		hex_value = 0x003F; // Unicode 'Question Mark'
		}
	str = g_string_append_unichar(str, static_cast<gunichar>(hex_value));
	gtk_label_set_text(static_cast<GtkLabel *>(g_list_nth_data(list, 1)), str->str);

	g_strfreev(hex_code);
	g_string_free(str, TRUE);
	g_free(hex_code_full);

	return hex_value;
}

static void star_rating_star_test_cb(GtkWidget *widget, gpointer data)
{
	guint64 star_symbol;

	star_symbol = star_rating_symbol_test(widget, data);
	c_options->star_rating.star = star_symbol;
}

static void star_rating_rejected_test_cb(GtkWidget *widget, gpointer data)
{
	guint64 rejected_symbol;

	rejected_symbol = star_rating_symbol_test(widget, data);
	c_options->star_rating.rejected = rejected_symbol;
}

/* general options tab */
static void timezone_database_install_cb(GtkWidget *widget, gpointer data);
struct TZData
{
	GenericDialog *gd;
	GCancellable *cancellable;

	GtkWidget *progress;
	GFile *tmp_g_file;
	GFile *timezone_database_gq;
	gchar *timezone_database_user;
};

static void config_tab_general(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *group;
	GtkWidget *group_frame;
	GtkWidget *subgroup;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *table;
	GtkWidget *spin;
	gint hours, minutes, remainder;
	gdouble seconds;
	GtkWidget *star_rating_entry;
	GString *str;
	gchar *rating_symbol;
	gchar *path;
	gchar *basename;
	gchar *download_locn;
	GNetworkMonitor *net_mon;
	GSocketConnectable *tz_org;
	gboolean internet_available = FALSE;
	TZData *tz;

	vbox = scrolled_notebook_page(notebook, _("General"));

	group = pref_group_new(vbox, FALSE, _("Thumbnails"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 2, FALSE, FALSE);
	add_thumb_size_menu(table, 0, 0, _("Size:"));
	add_quality_menu(table, 0, 1, _("Quality:"), options->thumbnails.quality, &c_options->thumbnails.quality);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Custom size: "));
	pref_spin_new_int(hbox, _("Width:"), nullptr, 1, 512, 1, options->thumbnails.max_width, &c_options->thumbnails.max_width);
	pref_spin_new_int(hbox, _("Height:"), nullptr, 1, 512, 1, options->thumbnails.max_height, &c_options->thumbnails.max_height);

	ct_button = pref_checkbox_new_int(group, _("Cache thumbnails and sim. files"),
					  options->thumbnails.enable_caching, &c_options->thumbnails.enable_caching);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_checkbox_link_sensitivity(ct_button, subgroup);

	c_options->thumbnails.spec_standard = options->thumbnails.spec_standard;
	c_options->thumbnails.cache_into_dirs = options->thumbnails.cache_into_dirs;
	group_frame = pref_frame_new(subgroup, TRUE, _("Use Geeqie thumbnail style and cache"),
										GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	button = pref_radiobutton_new(group_frame, nullptr,  get_thumbnails_cache_dir(),
							!options->thumbnails.spec_standard && !options->thumbnails.cache_into_dirs,
							G_CALLBACK(cache_geeqie_cb), nullptr);

	group_frame = pref_frame_new(subgroup, TRUE,
							_("Store thumbnails local to image folder (non-standard)"),
							GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_radiobutton_new(group_frame, button, "*/.thumbnails",
							!options->thumbnails.spec_standard && options->thumbnails.cache_into_dirs,
							G_CALLBACK(cache_local_cb), nullptr);

	group_frame = pref_frame_new(subgroup, TRUE,
							_("Use standard thumbnail style and cache, shared with other applications"),
							GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_radiobutton_new(group_frame, button, get_thumbnails_standard_cache_dir(),
							options->thumbnails.spec_standard && !options->thumbnails.cache_into_dirs,
							G_CALLBACK(cache_standard_cb), nullptr);

	pref_checkbox_new_int(group, _("Use EXIF thumbnails when available (EXIF thumbnails may be outdated)"),
			      options->thumbnails.use_exif, &c_options->thumbnails.use_exif);

	pref_checkbox_new_int(group, _("Thumbnail color management"),
				options->thumbnails.use_color_management, &c_options->thumbnails.use_color_management);

	spin = pref_spin_new_int(group, _("Collection preview:"), nullptr,
				 1, 999, 1,
				 options->thumbnails.collection_preview, &c_options->thumbnails.collection_preview);
	gtk_widget_set_tooltip_text(spin, _("The maximum number of thumbnails shown in a Collection preview montage"));

#ifdef HAVE_FFMPEGTHUMBNAILER_METADATA
	pref_checkbox_new_int(group, _("Use embedded metadata in video files as thumbnails when available"),
			      options->thumbnails.use_ft_metadata, &c_options->thumbnails.use_ft_metadata);
#endif

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Star Rating"), GTK_ORIENTATION_VERTICAL);

	c_options->star_rating.star = options->star_rating.star;
	c_options->star_rating.rejected = options->star_rating.rejected;

	str = g_string_new(nullptr);
	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Star character: "));
	str = g_string_append_unichar(str, options->star_rating.star);
	pref_label_new(hbox, g_strdup(str->str));
	rating_symbol = g_strdup_printf("U+%X", options->star_rating.star);
	star_rating_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(star_rating_entry), rating_symbol);
	gtk_box_pack_start(GTK_BOX(hbox), star_rating_entry, FALSE, FALSE, 0);
	gtk_entry_set_width_chars(GTK_ENTRY(star_rating_entry), 15);
	gtk_widget_show(star_rating_entry);
	button = pref_button_new(nullptr, nullptr, _("Set"),
					G_CALLBACK(star_rating_star_test_cb), hbox);
	gtk_widget_set_tooltip_text(button, _("Display selected character"));
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	gtk_widget_set_tooltip_text(star_rating_entry, _("Hexadecimal representation of a Unicode character. A list of all Unicode characters may be found on the Internet."));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_PRIMARY, GQ_ICON_REVERT);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_PRIMARY, _("Default"));
	g_signal_connect(GTK_ENTRY(star_rating_entry), "icon-press",
						G_CALLBACK(star_rating_star_icon_cb),
						star_rating_entry);

	g_string_free(str, TRUE);
	g_free(rating_symbol);

	str = g_string_new(nullptr);
	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Rejected character: "));
	str = g_string_append_unichar(str, options->star_rating.rejected);
	pref_label_new(hbox, g_strdup(str->str));
	rating_symbol = g_strdup_printf("U+%X", options->star_rating.rejected);
	star_rating_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(star_rating_entry), rating_symbol);
	gtk_box_pack_start(GTK_BOX(hbox), star_rating_entry, FALSE, FALSE, 0);
	gtk_entry_set_width_chars(GTK_ENTRY(star_rating_entry), 15);
	gtk_widget_show(star_rating_entry);
	button = pref_button_new(nullptr, nullptr, _("Set"),
					G_CALLBACK(star_rating_rejected_test_cb), hbox);
	gtk_widget_set_tooltip_text(button, _("Display selected character"));
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	gtk_widget_set_tooltip_text(star_rating_entry, _("Hexadecimal representation of a Unicode character. A list of all Unicode characters may be found on the Internet."));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_PRIMARY, GQ_ICON_REVERT);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(star_rating_entry),
						GTK_ENTRY_ICON_PRIMARY, _("Default"));
	g_signal_connect(GTK_ENTRY(star_rating_entry), "icon-press",
						G_CALLBACK(star_rating_rejected_icon_cb),
						star_rating_entry);

	g_string_free(str, TRUE);
	g_free(rating_symbol);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Slide show"), GTK_ORIENTATION_VERTICAL);

	c_options->slideshow.delay = options->slideshow.delay;
	hours = options->slideshow.delay / (3600 * SLIDESHOW_SUBSECOND_PRECISION);
	remainder = options->slideshow.delay % (3600 * SLIDESHOW_SUBSECOND_PRECISION);
	minutes = remainder / (60 * SLIDESHOW_SUBSECOND_PRECISION);
	seconds = static_cast<gdouble>(remainder % (60 * SLIDESHOW_SUBSECOND_PRECISION)) /
											SLIDESHOW_SUBSECOND_PRECISION;

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	spin = pref_spin_new(hbox, _("Delay between image change hrs:mins:secs.dec"), nullptr,
										0, 23, 1.0, 0,
										options->slideshow.delay ? hours : 0.0,
										G_CALLBACK(slideshow_delay_hours_cb), nullptr);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);
	spin = pref_spin_new(hbox, ":" , nullptr,
										0, 59, 1.0, 0,
										options->slideshow.delay ? minutes: 0.0,
										G_CALLBACK(slideshow_delay_minutes_cb), nullptr);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);
	spin = pref_spin_new(hbox, ":", nullptr,
										SLIDESHOW_MIN_SECONDS, 59, 1.0, 1,
										options->slideshow.delay ? seconds : 10.0,
										G_CALLBACK(slideshow_delay_seconds_cb), nullptr);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);

	pref_checkbox_new_int(group, _("Random"), options->slideshow.random, &c_options->slideshow.random);
	pref_checkbox_new_int(group, _("Repeat"), options->slideshow.repeat, &c_options->slideshow.repeat);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Image loading and caching"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new_int(group, _("Decoded image cache size (MiB):"), nullptr,
			  0, 99999, 1, options->image.image_cache_max, &c_options->image.image_cache_max);
	pref_checkbox_new_int(group, _("Preload next image"),
			      options->image.enable_read_ahead, &c_options->image.enable_read_ahead);

	pref_checkbox_new_int(group, _("Refresh on file change"),
			      options->update_on_time_change, &c_options->update_on_time_change);


	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Expand menu and toolbar"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Expand menu and toolbar (NOTE! Geeqie must be restarted for change to take effect)"),
				options->expand_menu_toolbar, &c_options->expand_menu_toolbar);
	gtk_widget_set_tooltip_text(group, _("Expand the menu and toolbar to the full width of the window"));

	pref_spacer(group, PREF_PAD_GROUP);

	if (g_getenv("APPDIR") && strstr(g_getenv("APPDIR"), "/tmp/.mount_Geeqie"))
		{
		group = pref_group_new(vbox, FALSE, _("AppImage updates notifications"), GTK_ORIENTATION_VERTICAL);
		hbox = pref_box_new(group, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
		pref_checkbox_new_int(group, _("Enable"), options->appimage_notifications, &c_options->appimage_notifications);
		gtk_widget_set_tooltip_text(group, _("Show a notification on start-up if the server has a newer version than the current. Requires an Internet connection"));

		pref_spacer(group, PREF_PAD_GROUP);
		}


	net_mon = g_network_monitor_get_default();
	tz_org = g_network_address_parse_uri(TIMEZONE_DATABASE_WEB, 80, nullptr);
	if (tz_org)
		{
		internet_available = g_network_monitor_can_reach(net_mon, tz_org, nullptr, nullptr);
		g_object_unref(tz_org);
		}

	group = pref_group_new(vbox, FALSE, _("Timezone database"), GTK_ORIENTATION_VERTICAL);
	hbox = pref_box_new(group, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	if (!internet_available)
		{
		gtk_widget_set_sensitive(group, FALSE);
		}

	tz = g_new0(TZData, 1);

	path = path_from_utf8(TIMEZONE_DATABASE_WEB);
	basename = g_path_get_basename(path);
	tz->timezone_database_user = g_build_filename(get_rc_dir(), TIMEZONE_DATABASE_FILE, NULL);
	g_free(path);
	g_free(basename);

	if (isfile(tz->timezone_database_user))
		{
		button = pref_button_new(GTK_WIDGET(hbox), nullptr, _("Update"), G_CALLBACK(timezone_database_install_cb), tz);
		}
	else
		{
		button = pref_button_new(GTK_WIDGET(hbox), nullptr, _("Install"), G_CALLBACK(timezone_database_install_cb), tz);
		}

	download_locn = g_strconcat(_("Download database from: "), TIMEZONE_DATABASE_WEB, NULL);
	pref_label_new(GTK_WIDGET(hbox), download_locn);
	g_free(download_locn);

	if (!internet_available)
		{
		gtk_widget_set_tooltip_text(button, _("No Internet connection!\nThe timezone database is used to display exif time and date\ncorrected for UTC offset and Daylight Saving Time"));
		}
	else
		{
		gtk_widget_set_tooltip_text(button, _("The timezone database is used to display exif time and date\ncorrected for UTC offset and Daylight Saving Time"));
		}
	gtk_widget_show(button);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("On-line help search engine"), GTK_ORIENTATION_VERTICAL);

	help_search_engine_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(help_search_engine_entry), options->help_search_engine);
	gtk_box_pack_start(GTK_BOX(group), help_search_engine_entry, FALSE, FALSE, 0);
	gtk_widget_show(help_search_engine_entry);

	gtk_widget_set_tooltip_text(help_search_engine_entry, _("The format varies between search engines, e.g the format may be:\nhttps://www.search_engine.com/search?q=site:geeqie.org/help\nhttps://www.search_engine.com/?q=site:geeqie.org/help"));

	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(help_search_engine_entry),
						GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(help_search_engine_entry),
						GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(help_search_engine_entry),
						GTK_ENTRY_ICON_PRIMARY, GQ_ICON_REVERT);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(help_search_engine_entry),
						GTK_ENTRY_ICON_PRIMARY, _("Default"));
	g_signal_connect(GTK_ENTRY(help_search_engine_entry), "icon-press",
						G_CALLBACK(help_search_engine_entry_icon_cb),
						help_search_engine_entry);
}

/* image tab */
static void config_tab_image(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *ct_button;
	GtkWidget *enlargement_button;
	GtkWidget *table;
	GtkWidget *spin;

	vbox = scrolled_notebook_page(notebook, _("Image"));

	group = pref_group_new(vbox, FALSE, _("Zoom"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_quality_menu(table, 0, 0, _("Quality:"), options->image.zoom_quality, &c_options->image.zoom_quality);

	pref_checkbox_new_int(group, _("Two pass rendering (apply HQ zoom and color correction in second pass)"),
			      options->image.zoom_2pass, &c_options->image.zoom_2pass);

	c_options->image.zoom_increment = options->image.zoom_increment;
	spin = pref_spin_new(group, _("Zoom increment:"), nullptr,
			     0.01, 4.0, 0.01, 2, static_cast<gdouble>(options->image.zoom_increment) / 100.0,
			     G_CALLBACK(zoom_increment_cb), nullptr);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);

	c_options->image.zoom_style = options->image.zoom_style;
	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_zoom_style_selection_menu(table, 0, 0, _("Zoom style:"), options->image.zoom_style, &c_options->image.zoom_style);

	group = pref_group_new(vbox, FALSE, _("Fit image to window"), GTK_ORIENTATION_VERTICAL);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	enlargement_button = pref_checkbox_new_int(hbox, _("Allow enlargement of image (max. size in %)"),
			      options->image.zoom_to_fit_allow_expand, &c_options->image.zoom_to_fit_allow_expand);
	spin = pref_spin_new_int(hbox, nullptr, nullptr,
				 100, 999, 1,
				 options->image.max_enlargement_size, &c_options->image.max_enlargement_size);
	pref_checkbox_link_sensitivity(enlargement_button, spin);
	gtk_widget_set_tooltip_text(GTK_WIDGET(hbox), _("Enable this to allow Geeqie to increase the image size for images that are smaller than the current view area when the zoom is set to \"Fit image to window\". This value sets the maximum expansion permitted in percent i.e. 100% is full-size."));

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	ct_button = pref_checkbox_new_int(hbox, _("Virtual window size (% of actual window):"),
					  options->image.limit_autofit_size, &c_options->image.limit_autofit_size);
	spin = pref_spin_new_int(hbox, nullptr, nullptr,
				 10, 150, 1,
				 options->image.max_autofit_size, &c_options->image.max_autofit_size);
	pref_checkbox_link_sensitivity(ct_button, spin);
	gtk_widget_set_tooltip_text(GTK_WIDGET(hbox), _("This value will set the virtual size of the window when \"Fit image to window\" is set. Instead of using the actual size of the window, the specified percentage of the window will be used. It allows one to keep a border around the image (values lower than 100%) or to auto zoom the image (values greater than 100%). It affects fullscreen mode too."));

	group = pref_group_new(vbox, FALSE, _("Tile size"), GTK_ORIENTATION_VERTICAL);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	spin = pref_spin_new_int(hbox, _("Pixels"), _("(Requires restart)"),
				 128, 4096, 128,
				 options->image.tile_size, &c_options->image.tile_size);
	gtk_widget_set_tooltip_text(GTK_WIDGET(hbox), _("This value changes the size of the tiles large images are split into. Increasing the size of the tiles will reduce the tiling effect seen on image changes, but will also slightly increase the delay before the first part of a large image is seen."));

	group = pref_group_new(vbox, FALSE, _("Appearance"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Use custom border color in window mode"),
			      options->image.use_custom_border_color, &c_options->image.use_custom_border_color);

	pref_checkbox_new_int(group, _("Use custom border color in fullscreen mode"),
			      options->image.use_custom_border_color_in_fullscreen, &c_options->image.use_custom_border_color_in_fullscreen);

	pref_color_button_new(group, _("Border color"), &options->image.border_color,
			      G_CALLBACK(pref_color_button_set_cb), &c_options->image.border_color);

	c_options->image.border_color = options->image.border_color;

	pref_color_button_new(group, _("Alpha channel color 1"), &options->image.alpha_color_1,
			      G_CALLBACK(pref_color_button_set_cb), &c_options->image.alpha_color_1);

	pref_color_button_new(group, _("Alpha channel color 2"), &options->image.alpha_color_2,
			      G_CALLBACK(pref_color_button_set_cb), &c_options->image.alpha_color_2);

	c_options->image.alpha_color_1 = options->image.alpha_color_1;
	c_options->image.alpha_color_2 = options->image.alpha_color_2;
}

/* windows tab */

static void save_default_window_layout_cb(GtkWidget *, gpointer)
{
	LayoutWindow *lw = nullptr;
	gchar *default_path;
	gchar *tmp_id;

	/* Get current lw */
	layout_valid(&lw);

	tmp_id = lw->options.id;
	lw->options.id = g_strdup("lw_default");

	default_path = g_build_filename(get_rc_dir(), DEFAULT_WINDOW_LAYOUT, NULL);
	save_default_layout_options_to_file(default_path, options, lw);
	g_free(lw->options.id);
	lw->options.id = tmp_id;
	g_free(default_path);
}

static gboolean popover_cb(gpointer data)
{
	auto popover = static_cast<GtkPopover *>(data);

	gtk_popover_popdown(popover);

	return FALSE;
}

static void default_layout_changed_cb(GtkWidget *, GtkPopover *popover)
{
	gtk_popover_popup(popover);

	g_timeout_add(2000, popover_cb, popover);
}

static GtkWidget *create_popover(GtkWidget *parent, GtkWidget *child, GtkPositionType pos)
{
	GtkWidget *popover;

	popover = gtk_popover_new(parent);
	gtk_popover_set_position(GTK_POPOVER (popover), pos);
	gtk_container_add (GTK_CONTAINER(popover), child);
	gtk_container_set_border_width(GTK_CONTAINER (popover), 6);
	gtk_widget_show (child);

	return popover;
}

static void config_tab_windows(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *subgroup;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *spin;

	vbox = scrolled_notebook_page(notebook, _("Windows"));

	group = pref_group_new(vbox, FALSE, _("State"), GTK_ORIENTATION_VERTICAL);

	ct_button = pref_checkbox_new_int(group, _("Remember session"),
					  options->save_window_positions, &c_options->save_window_positions);

	button = pref_checkbox_new_int(group, _("Use saved window positions also for new windows"),
				       options->use_saved_window_positions_for_new_windows, &c_options->use_saved_window_positions_for_new_windows);
	pref_checkbox_link_sensitivity(ct_button, button);

	button = pref_checkbox_new_int(group, _("Remember window workspace"),
			      options->save_window_workspace, &c_options->save_window_workspace);
	pref_checkbox_link_sensitivity(ct_button, button);

	pref_checkbox_new_int(group, _("Remember tool state (float/hidden)"),
			      options->tools_restore_state, &c_options->tools_restore_state);

	pref_checkbox_new_int(group, _("Remember dialog window positions"),
			      options->save_dialog_window_positions, &c_options->save_dialog_window_positions);

	pref_checkbox_new_int(group, _("Show window IDs"),
			      options->show_window_ids, &c_options->show_window_ids);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(subgroup, _("Use current layout for default: "));
	button = pref_button_new(subgroup, nullptr, _("Set"), G_CALLBACK(save_default_window_layout_cb), nullptr);

	GtkWidget *popover;

	popover = create_popover(button, gtk_label_new(_("Current window layout\nhas been set as default")), GTK_POS_TOP);
	gtk_popover_set_modal(GTK_POPOVER (popover), FALSE);
	g_signal_connect(button, "clicked", G_CALLBACK(default_layout_changed_cb), popover);

	group = pref_group_new(vbox, FALSE, _("Size"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Fit window to image when tools are hidden/floating"),
			      options->image.fit_window_to_image, &c_options->image.fit_window_to_image);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	ct_button = pref_checkbox_new_int(hbox, _("Limit size when auto-sizing window (%):"),
					  options->image.limit_window_size, &c_options->image.limit_window_size);
	spin = pref_spin_new_int(hbox, nullptr, nullptr,
				 10, 150, 1,
				 options->image.max_window_size, &c_options->image.max_window_size);
	pref_checkbox_link_sensitivity(ct_button, spin);

	group = pref_group_new(vbox, FALSE, _("Full screen"), GTK_ORIENTATION_VERTICAL);

	c_options->fullscreen.screen = options->fullscreen.screen;
	c_options->fullscreen.above = options->fullscreen.above;
	hbox = fullscreen_prefs_selection_new(_("Location:"), &c_options->fullscreen.screen, &c_options->fullscreen.above);
	gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	pref_checkbox_new_int(group, _("Smooth image flip"),
			      options->fullscreen.clean_flip, &c_options->fullscreen.clean_flip);
	pref_checkbox_new_int(group, _("Disable screen saver"),
			      options->fullscreen.disable_saver, &c_options->fullscreen.disable_saver);
}

#define PRE_FORMATTED_COLUMNS 5
static void config_tab_osd(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *image_overlay_template_view;
	GtkWidget *scrolled;
	GtkWidget *scrolled_pre_formatted;
	GtkTextBuffer *buffer;
	GtkWidget *label;
	GtkWidget *subgroup;

	vbox = scrolled_notebook_page(notebook, _("OSD"));

	image_overlay_template_view = gtk_text_view_new();

	group = pref_group_new(vbox, FALSE, _("Overlay Screen Display"), GTK_ORIENTATION_VERTICAL);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	scrolled_pre_formatted = osd_new(PRE_FORMATTED_COLUMNS, image_overlay_template_view);
	gtk_widget_set_size_request(scrolled_pre_formatted, 200, 150);
	gtk_box_pack_start(GTK_BOX(subgroup), scrolled_pre_formatted, FALSE, FALSE, 0);
	gtk_widget_show(scrolled_pre_formatted);
	gtk_widget_show(subgroup);

	pref_line(group, PREF_PAD_GAP);

	pref_label_new(group, _("Image overlay template"));

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_widget_set_size_request(scrolled, 200, 150);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
									GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	gtk_widget_set_tooltip_markup(image_overlay_template_view,
					_("Extensive formatting options are shown in the Help file"));

	gtk_container_add(GTK_CONTAINER(scrolled), image_overlay_template_view);
	gtk_widget_show(image_overlay_template_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_FONT, _("Font"),
				 G_CALLBACK(image_overlay_set_font_cb), notebook);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_COLOR, _("Text"),
				 G_CALLBACK(image_overlay_set_text_colour_cb), nullptr);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_COLOR, _("Background"),
				 G_CALLBACK(image_overlay_set_background_colour_cb), nullptr);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	image_overlay_set_text_colours();

	button = pref_button_new(nullptr, nullptr, _("Defaults"),
				 G_CALLBACK(image_overlay_default_template_cb), image_overlay_template_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_HELP, _("Help"),
				 G_CALLBACK(image_overlay_help_cb), nullptr);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(image_overlay_template_view));
	if (options->image_overlay.template_string) gtk_text_buffer_set_text(buffer, options->image_overlay.template_string, -1);
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(image_overlay_template_view_changed_cb), image_overlay_template_view);

	pref_line(group, PREF_PAD_GAP);

	group = pref_group_new(vbox, FALSE, _("Exif, XMP or IPTC tags"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("%Exif.Image.Orientation%"));
	gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Field separators"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("Separator shown only if both fields are non-null:\n%formatted.ShutterSpeed%|%formatted.ISOSpeedRating%"));
	gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Field maximum length"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("%path:39%"));
	gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Pre- and post- text"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("Text shown only if the field is non-null:\n%formatted.Aperture:F no. * setting%\n %formatted.Aperture:10:F no. * setting%"));
	gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Pango markup"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("<b>bold</b>\n<u>underline</u>\n<i>italic</i>\n<s>strikethrough</s>"));
	gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
}

static GtkTreeModel *create_class_model()
{
	GtkListStore *model;
	GtkTreeIter iter;
	gint i;

	/* create list store */
	model = gtk_list_store_new(1, G_TYPE_STRING);
	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, _(format_class_list[i]), -1);
		}
	return GTK_TREE_MODEL (model);
}


/* filtering tab */
static gint filter_table_sort_cb(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gint n = GPOINTER_TO_INT(data);
	gint ret = 0;
	FilterEntry *filter_a;
	FilterEntry *filter_b;

	gtk_tree_model_get(model, a, 0, &filter_a, -1);
	gtk_tree_model_get(model, b, 0, &filter_b, -1);

	switch (n)
		{
		case FILETYPES_COLUMN_ENABLED:
			{
			ret = filter_a->enabled - filter_b->enabled;
			break;
			}
		case FILETYPES_COLUMN_FILTER:
			{
			ret = g_utf8_collate(filter_a->extensions, filter_b->extensions);
			break;
			}
		case FILETYPES_COLUMN_DESCRIPTION:
			{
			ret = g_utf8_collate(filter_a->description, filter_b->description);
			break;
			}
		case FILETYPES_COLUMN_CLASS:
			{
			ret = g_strcmp0(format_class_list[filter_a->file_class], format_class_list[filter_b->file_class]);
			break;
			}
		case FILETYPES_COLUMN_WRITABLE:
			{
			ret = filter_a->writable - filter_b->writable;
			break;
			}
		case FILETYPES_COLUMN_SIDECAR:
			{
			ret = filter_a->allow_sidecar - filter_b->allow_sidecar;
			break;
			}
		default:
			g_return_val_if_reached(0);
		}

	return ret;
}

static gboolean search_function_cb(GtkTreeModel *model, gint, const gchar *key, GtkTreeIter *iter, gpointer)
{
	FilterEntry *fe;
	gboolean ret = TRUE;

	gtk_tree_model_get(model, iter, 0, &fe, -1);

	if (g_strstr_len(fe->extensions, -1, key))
		{
		ret = FALSE;
		}

	return ret;
}

static void config_tab_files(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *frame;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *scrolled;
	GtkWidget *filter_view;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;

	vbox = scrolled_notebook_page(notebook, _("File Filters"));

	group = pref_box_new(vbox, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	pref_checkbox_new_int(group, _("Show hidden files or folders"),
			      options->file_filter.show_hidden_files, &c_options->file_filter.show_hidden_files);
	pref_checkbox_new_int(group, _("Show parent folder (..)"),
			      options->file_filter.show_parent_directory, &c_options->file_filter.show_parent_directory);
	pref_checkbox_new_int(group, _("Case sensitive sort"),
			      options->file_sort.case_sensitive, &c_options->file_sort.case_sensitive);
	pref_checkbox_new_int(group, _("Natural sort order - Requires restart"),
					  options->file_sort.natural, &c_options->file_sort.natural);
	pref_checkbox_new_int(group, _("Disable file extension checks"),
			      options->file_filter.disable_file_extension_checks, &c_options->file_filter.disable_file_extension_checks);

	ct_button = pref_checkbox_new_int(group, _("Disable File Filtering"),
					  options->file_filter.disable, &c_options->file_filter.disable);


	group = pref_group_new(vbox, FALSE, _("Grouping sidecar extensions"), GTK_ORIENTATION_VERTICAL);

	sidecar_ext_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(sidecar_ext_entry), options->sidecar.ext);
	gtk_box_pack_start(GTK_BOX(group), sidecar_ext_entry, FALSE, FALSE, 0);
	gtk_widget_show(sidecar_ext_entry);

	group = pref_group_new(vbox, TRUE, _("File types"), GTK_ORIENTATION_VERTICAL);

	frame = pref_group_parent(group);
	g_signal_connect(G_OBJECT(ct_button), "toggled",
			 G_CALLBACK(filter_disable_cb), frame);
	gtk_widget_set_sensitive(frame, !options->file_filter.disable);

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	filter_store = gtk_list_store_new(1, G_TYPE_POINTER);
	filter_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(filter_store));
	g_object_unref(filter_store);
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(filter_view));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_SINGLE);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(filter_view), FALSE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Enabled"));
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(filter_store_enable_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_ENABLE), nullptr);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_ENABLED, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_ENABLED), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_ENABLED);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Filter"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_FILTER, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_FILTER), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_FILTER);

	renderer = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_ext_edit_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	g_object_set(G_OBJECT(renderer), "editable", static_cast<gboolean>TRUE, NULL);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_EXTENSION), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(filter_view), TRUE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(filter_view), FILETYPES_COLUMN_FILTER);
	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(filter_view), search_function_cb, nullptr, nullptr);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Description"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_fixed_width(column, 200);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

	renderer = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_desc_edit_cb), filter_store);
	g_object_set(G_OBJECT(renderer), "editable", static_cast<gboolean>TRUE, NULL);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_DESCRIPTION), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_DESCRIPTION, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_DESCRIPTION), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_DESCRIPTION);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Class"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	renderer = gtk_cell_renderer_combo_new();
	g_object_set(G_OBJECT(renderer), "editable", static_cast<gboolean>TRUE,
					 "model", create_class_model(),
					 "text-column", 0,
					 "has-entry", FALSE,
					 NULL);

	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_class_edit_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_CLASS), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_CLASS, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_CLASS), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_CLASS);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Writable"));
	gtk_tree_view_column_set_resizable(column, FALSE);
	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(filter_store_writable_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_WRITABLE), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_WRITABLE, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_WRITABLE), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_WRITABLE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Sidecar is allowed"));
	gtk_tree_view_column_set_resizable(column, FALSE);
	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(filter_store_sidecar_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_ALLOW_SIDECAR), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_SIDECAR, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_SIDECAR), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_SIDECAR);

	filter_store_populate();
	gtk_container_add(GTK_CONTAINER(scrolled), filter_view);
	gtk_widget_show(filter_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, nullptr, _("Defaults"),
				 G_CALLBACK(filter_default_cb), filter_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_REMOVE, _("Remove"),
				 G_CALLBACK(filter_remove_cb), filter_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_ADD, _("Add"),
				 G_CALLBACK(filter_add_cb), filter_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
}

/* metadata tab */
static void config_tab_metadata(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *group;
	GtkWidget *ct_button;
	GtkWidget *label;
	GtkWidget *tmp_widget;
	char *markup;
	GtkWidget *text_label;

	vbox = scrolled_notebook_page(notebook, _("Metadata"));


	group = pref_group_new(vbox, FALSE, _("Metadata writing sequence"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_EXIV2
	label = pref_label_new(group, _("Warning: Geeqie is built without Exiv2. Some options are disabled."));
#endif
	label = pref_label_new(group, _("When writing metadata, Geeqie will follow these steps, if selected. This process will stop when the first successful write occurs."));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);

	gtk_widget_set_tooltip_text(label, _("A flowchart of the sequence is shown in the Help file"));

	ct_button = pref_checkbox_new_int(group, "", options->metadata.save_in_image_file, &c_options->metadata.save_in_image_file);
	text_label = gtk_bin_get_child(GTK_BIN(ct_button));
	markup = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>%s", _("Step 1"), _(") Save metadata in either the image file or the sidecar file, according to the XMP standard"));
	gtk_label_set_markup (GTK_LABEL(text_label), markup);
	g_free(markup);
	markup = g_markup_printf_escaped ("%s<span style=\"italic\">%s</span>%s<span style=\"italic\">%s</span>%s", _("The destination is dependent on the settings in the "), _("Writable"), _(" and "), _("Sidecar Is Allowed"), _(" columns of the File Filters tab)"));
	gtk_widget_set_tooltip_markup(ct_button, markup);
	g_free(markup);

#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(ct_button, FALSE);
#endif

	tmp_widget = pref_checkbox_new_int(group, "", options->metadata.enable_metadata_dirs, &c_options->metadata.enable_metadata_dirs);
	text_label = gtk_bin_get_child(GTK_BIN(tmp_widget));
	markup = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>%s<span style=\"italic\">%s</span>%s", _("Step 2"), _(") Save metadata in the folder "),".metadata,", _(" local to the image folder (non-standard)"));
	gtk_label_set_markup (GTK_LABEL(text_label), markup);
	g_free(markup);

	label = pref_label_new(group, "");
	markup = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>%s<span style=\"italic\">%s</span>%s", _("Step 3"), _(") Save metadata in Geeqie private directory "), get_metadata_cache_dir(), "/");
	gtk_label_set_markup (GTK_LABEL(label), markup);
	g_free(markup);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);
	gtk_misc_set_padding(GTK_MISC(label), 22, 0);
	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Step 1 Options:"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(group, FALSE);
#endif

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	tmp_widget=	pref_checkbox_new_int(hbox, _("Store metadata also in IPTC tags (converted according to the IPTC4XMP standard)"), options->metadata.save_legacy_IPTC, &c_options->metadata.save_legacy_IPTC);
	gtk_widget_set_tooltip_text(tmp_widget, _("A simplified conversion list is in the Help file"));

	pref_checkbox_new_int(hbox, _("Warn if the image or sidecar file is not writable"), options->metadata.warn_on_write_problems, &c_options->metadata.warn_on_write_problems);

	pref_checkbox_new_int(hbox, _("Ask before writing to image files"), options->metadata.confirm_write, &c_options->metadata.confirm_write);

	tmp_widget=	pref_checkbox_new_int(hbox, "", options->metadata.sidecar_extended_name, &c_options->metadata.sidecar_extended_name);
	gtk_widget_set_tooltip_text(tmp_widget, _("This file naming convention is used by Darktable"));
	text_label = gtk_bin_get_child(GTK_BIN(tmp_widget));
	markup = g_markup_printf_escaped ("%s<span style=\"italic\">%s</span>%s<span style=\"italic\">%s</span>%s", _("Create sidecar files named "), "image.ext.xmp", _(" (as opposed to the normal "), "image.xmp", ")");
	gtk_label_set_markup (GTK_LABEL(text_label), markup);
	g_free(markup);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Steps 2 and 3 Option:"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(group, FALSE);
#endif

	pref_checkbox_new_int(group, _("Use GQview legacy metadata format instead of XMP (supports only Keywords and Comments)"), options->metadata.save_legacy_format, &c_options->metadata.save_legacy_format);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Miscellaneous"), GTK_ORIENTATION_VERTICAL);
	tmp_widget = pref_checkbox_new_int(group, _("Write the same description tags to all grouped sidecars"), options->metadata.sync_grouped_files, &c_options->metadata.sync_grouped_files);
	gtk_widget_set_tooltip_text(tmp_widget, _("See the Help file for a list of the tags used"));

	tmp_widget = pref_checkbox_new_int(group, _("Permit Keywords to be case-sensitive"), options->metadata.keywords_case_sensitive, &c_options->metadata.keywords_case_sensitive);
	gtk_widget_set_tooltip_text(tmp_widget, _("When selected, \"Place\" and \"place\" are two different keywords"));

	ct_button = pref_checkbox_new_int(group, _("Write altered image orientation to the metadata"), options->metadata.write_orientation, &c_options->metadata.write_orientation);
	gtk_widget_set_tooltip_text(ct_button, _("If checked, the results of orientation commands (Rotate, Mirror and Flip) issued on an image will be written to metadata\nNote: If this option is not checked, the results of orientation commands will be lost when Geeqie closes"));

#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(ct_button, FALSE);
#endif

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Auto-save options"), GTK_ORIENTATION_VERTICAL);

	ct_button = pref_checkbox_new_int(group, _("Write metadata after timeout"), options->metadata.confirm_after_timeout, &c_options->metadata.confirm_after_timeout);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spin_new_int(hbox, _("Timeout (seconds):"), nullptr, 0, 900, 1, options->metadata.confirm_timeout, &c_options->metadata.confirm_timeout);

	pref_checkbox_new_int(group, _("Write metadata on image change"), options->metadata.confirm_on_image_change, &c_options->metadata.confirm_on_image_change);

	pref_checkbox_new_int(group, _("Write metadata on directory change"), options->metadata.confirm_on_dir_change, &c_options->metadata.confirm_on_dir_change);

	pref_spacer(group, PREF_PAD_GROUP);

#ifdef HAVE_SPELL
	group = pref_group_new(vbox, FALSE, _("Spelling checks"), GTK_ORIENTATION_VERTICAL);

	ct_button = pref_checkbox_new_int(group, _("Check spelling - Requires restart"), options->metadata.check_spelling, &c_options->metadata.check_spelling);
	gtk_widget_set_tooltip_text(ct_button, _("Spelling checks are performed on info sidebar panes Comment, Headline and Title"));
#endif

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Pre-load metadata"), GTK_ORIENTATION_VERTICAL);

	ct_button = pref_checkbox_new_int(group, _("Read metadata in background"), options->read_metadata_in_idle, &c_options->read_metadata_in_idle);
	gtk_widget_set_tooltip_text(ct_button,"On folder change, read DateTimeOriginal, DateTimeDigitized and Star Rating in the idle loop.\nIf this is not selected, initial loading of the folder will be faster but sorting on these items will be slower");
}

/* keywords tab */

struct KeywordFindData
{
	GenericDialog *gd;

	GList *list;
	GList *list_dir;

	GtkWidget *button_close;
	GtkWidget *button_stop;
	GtkWidget *button_start;
	GtkWidget *progress;
	GtkWidget *spinner;

	GtkWidget *group;
	GtkWidget *entry;

	gboolean recurse;

	guint idle_id; /* event source id */
};

#define KEYWORD_DIALOG_WIDTH 400

static void keywords_find_folder(KeywordFindData *kfd, FileData *dir_fd)
{
	GList *list_d = nullptr;
	GList *list_f = nullptr;

	if (kfd->recurse)
		{
		filelist_read(dir_fd, &list_f, &list_d);
		}
	else
		{
		filelist_read(dir_fd, &list_f, nullptr);
		}

	list_f = filelist_filter(list_f, FALSE);
	list_d = filelist_filter(list_d, TRUE);

	kfd->list = g_list_concat(list_f, kfd->list);
	kfd->list_dir = g_list_concat(list_d, kfd->list_dir);
}

static void keywords_find_reset(KeywordFindData *kfd)
{
	filelist_free(kfd->list);
	kfd->list = nullptr;

	filelist_free(kfd->list_dir);
	kfd->list_dir = nullptr;
}

static void keywords_find_close_cb(GenericDialog *, gpointer data)
{
	auto kfd = static_cast<KeywordFindData *>(data);

	if (!gtk_widget_get_sensitive(kfd->button_close)) return;

	keywords_find_reset(kfd);
	generic_dialog_close(kfd->gd);
	g_free(kfd);
}

static void keywords_find_finish(KeywordFindData *kfd)
{
	keywords_find_reset(kfd);

	gtk_entry_set_text(GTK_ENTRY(kfd->progress), _("done"));
	spinner_set_interval(kfd->spinner, -1);

	gtk_widget_set_sensitive(kfd->group, TRUE);
	gtk_widget_set_sensitive(kfd->button_start, TRUE);
	gtk_widget_set_sensitive(kfd->button_stop, FALSE);
	gtk_widget_set_sensitive(kfd->button_close, TRUE);
}

static void keywords_find_stop_cb(GenericDialog *, gpointer data)
{
	auto kfd = static_cast<KeywordFindData *>(data);

	g_idle_remove_by_data(kfd);

	keywords_find_finish(kfd);
}

static gboolean keywords_find_file(gpointer data)
{
	auto kfd = static_cast<KeywordFindData *>(data);
	GtkTextIter iter;
	GtkTextBuffer *buffer;
	gchar *tmp;
	GList *keywords;

	if (kfd->list)
		{
		FileData *fd;

		fd = static_cast<FileData *>(kfd->list->data);
		kfd->list = g_list_remove(kfd->list, fd);

		keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);
		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(keyword_text));

		while (keywords)
			{
			gtk_text_buffer_get_end_iter(buffer, &iter);
			tmp = g_strconcat(static_cast<const gchar *>(keywords->data), "\n", NULL);
			gtk_text_buffer_insert(buffer, &iter, tmp, -1);
			g_free(tmp);
			keywords = keywords->next;
			}

		gtk_entry_set_text(GTK_ENTRY(kfd->progress), fd->path);
		file_data_unref(fd);
		g_list_free_full(keywords, g_free);

		return (G_SOURCE_CONTINUE);
		}
	else if (kfd->list_dir)
		{
		FileData *fd;

		fd = static_cast<FileData *>(kfd->list_dir->data);
		kfd->list_dir = g_list_remove(kfd->list_dir, fd);

		keywords_find_folder(kfd, fd);

		file_data_unref(fd);

		return G_SOURCE_CONTINUE;
		}

	keywords_find_finish(kfd);

	return G_SOURCE_REMOVE;
}

static void keywords_find_start_cb(GenericDialog *, gpointer data)
{
	auto kfd = static_cast<KeywordFindData *>(data);
	gchar *path;

	if (kfd->list || !gtk_widget_get_sensitive(kfd->button_start)) return;

	path = remove_trailing_slash((gtk_entry_get_text(GTK_ENTRY(kfd->entry))));
	parse_out_relatives(path);

	if (!isdir(path))
		{
		warning_dialog(_("Invalid folder"),
				_("The specified folder can not be found."),
				GTK_STOCK_DIALOG_WARNING, kfd->gd->dialog);
		}
	else
		{
		FileData *dir_fd;

		gtk_widget_set_sensitive(kfd->group, FALSE);
		gtk_widget_set_sensitive(kfd->button_start, FALSE);
		gtk_widget_set_sensitive(kfd->button_stop, TRUE);
		gtk_widget_set_sensitive(kfd->button_close, FALSE);
		spinner_set_interval(kfd->spinner, SPINNER_SPEED);

		dir_fd = file_data_new_dir(path);
		keywords_find_folder(kfd, dir_fd);
		file_data_unref(dir_fd);
		kfd->idle_id = g_idle_add(keywords_find_file, kfd);
		}

	g_free(path);
}

static void keywords_find_dialog(GtkWidget *widget, const gchar *path)
{
	KeywordFindData *kfd;
	GtkWidget *hbox;
	GtkWidget *label;

	kfd = g_new0(KeywordFindData, 1);

	kfd->gd = generic_dialog_new(_("Search for keywords"),
									"search_for_keywords",
									widget, FALSE,
									nullptr, kfd);
	gtk_window_set_default_size(GTK_WINDOW(kfd->gd->dialog), KEYWORD_DIALOG_WIDTH, -1);
	kfd->gd->cancel_cb = keywords_find_close_cb;
	kfd->button_close = generic_dialog_add_button(kfd->gd, GQ_ICON_CLOSE, _("Close"),
						     keywords_find_close_cb, FALSE);
	kfd->button_start = generic_dialog_add_button(kfd->gd, GQ_ICON_OK, _("S_tart"),
						     keywords_find_start_cb, FALSE);
	kfd->button_stop = generic_dialog_add_button(kfd->gd, GQ_ICON_STOP, _("Stop"),
						    keywords_find_stop_cb, FALSE);
	gtk_widget_set_sensitive(kfd->button_stop, FALSE);

	generic_dialog_add_message(kfd->gd, nullptr, _("Search for keywords"), nullptr, FALSE);

	hbox = pref_box_new(kfd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	kfd->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(kfd->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	label = tab_completion_new(&kfd->entry, path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(kfd->entry,_("Select folder") , TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	pref_checkbox_new_int(kfd->group, _("Include subfolders"), FALSE, &kfd->recurse);

	pref_line(kfd->gd->vbox, PREF_PAD_SPACE);
	hbox = pref_box_new(kfd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	kfd->progress = gtk_entry_new();
	gtk_widget_set_can_focus(kfd->progress, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(kfd->progress), FALSE);
	gtk_entry_set_text(GTK_ENTRY(kfd->progress), _("click start to begin"));
	gtk_box_pack_start(GTK_BOX(hbox), kfd->progress, TRUE, TRUE, 0);
	gtk_widget_show(kfd->progress);

	kfd->spinner = spinner_new(nullptr, -1);
	gtk_box_pack_start(GTK_BOX(hbox), kfd->spinner, FALSE, FALSE, 0);
	gtk_widget_show(kfd->spinner);

	kfd->list = nullptr;

	gtk_widget_show(kfd->gd->dialog);
}

static void keywords_find_cb(GtkWidget *widget, gpointer)
{
	const gchar *path = layout_get_path(nullptr);

	if (!path || !*path) path = homedir();
	keywords_find_dialog(widget, path);
}

static void config_tab_keywords_save()
{
	GtkTextIter start, end;
	GtkTextBuffer *buffer;
	GList *kw_list = nullptr;
	GList *work;
	gchar *buffer_text;
	gchar *kw_split;
	gboolean found;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(keyword_text));
	gtk_text_buffer_get_bounds(buffer, &start, &end);

	buffer_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

	kw_split = strtok(buffer_text, "\n");
	while (kw_split != nullptr)
		{
		work = kw_list;
		found = FALSE;
		while (work)
			{
			if (g_strcmp0(static_cast<const gchar *>(work->data), kw_split) == 0)
				{
				found = TRUE;
				break;
				}
			work = work->next;
			}
		if (!found)
			{
			kw_list = g_list_append(kw_list, g_strdup(kw_split));
			}
		kw_split = strtok(nullptr, "\n");
		}

	keyword_list_set(kw_list);

	g_list_free_full(kw_list, g_free);
	g_free(buffer_text);
}

static void config_tab_keywords(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *scrolled;
	GtkTextIter iter;
	GtkTextBuffer *buffer;
	gchar *tmp;
#ifdef HAVE_SPELL
	GspellTextView *gspell_view;
#endif

	vbox = scrolled_notebook_page(notebook, _("Keywords"));

	group = pref_group_new(vbox, TRUE, _("Edit keywords autocompletion list"), GTK_ORIENTATION_VERTICAL);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(hbox, GQ_ICON_RUN, _("Search"),
				   G_CALLBACK(keywords_find_cb), keyword_text);
	gtk_widget_set_tooltip_text(button, "Search for existing keywords");


	keyword_text = gtk_text_view_new();
	gtk_widget_set_size_request(keyword_text, 20, 20);
	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

#ifdef HAVE_SPELL
	if (options->metadata.check_spelling)
		{
		gspell_view = gspell_text_view_get_from_gtk_text_view(GTK_TEXT_VIEW(keyword_text));
		gspell_text_view_basic_setup(gspell_view);
		}
#endif

	gtk_container_add(GTK_CONTAINER(scrolled), keyword_text);
	gtk_widget_show(keyword_text);

	gtk_text_view_set_editable(GTK_TEXT_VIEW(keyword_text), TRUE);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(keyword_text));
	gtk_text_buffer_create_tag(buffer, "monospace",
				"family", "monospace", NULL);

	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(keyword_text), GTK_WRAP_WORD);
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_buffer_create_mark(buffer, "end", &iter, FALSE);
	gchar *path;

	path = g_build_filename(get_rc_dir(), "keywords", NULL);

	GList *kwl = keyword_list_get();
	kwl = g_list_first(kwl);
	while (kwl)
	{
		gtk_text_buffer_get_end_iter (buffer, &iter);
	    tmp = g_strconcat(static_cast<const gchar *>(kwl->data), "\n", NULL);
		gtk_text_buffer_insert(buffer, &iter, tmp, -1);
		kwl = kwl->next;
		g_free(tmp);
	}

	gtk_text_buffer_set_modified(buffer, FALSE);

	g_free(path);
}

/* metadata tab */
#ifdef HAVE_LCMS
static void intent_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
		default:
			*option = INTENT_PERCEPTUAL;
			break;
		case 1:
			*option = INTENT_RELATIVE_COLORIMETRIC;
			break;
		case 2:
			*option = INTENT_SATURATION;
			break;
		case 3:
			*option = INTENT_ABSOLUTE_COLORIMETRIC;
			break;
		}
}

static void add_intent_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     gint option, gint *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, 0.0);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Perceptual"));
	if (option == INTENT_PERCEPTUAL) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Relative Colorimetric"));
	if (option == INTENT_RELATIVE_COLORIMETRIC) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Saturation"));
	if (option == INTENT_SATURATION) current = 2;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Absolute Colorimetric"));
	if (option == INTENT_ABSOLUTE_COLORIMETRIC) current = 3;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	gtk_widget_set_tooltip_text(combo,"Refer to the lcms documentation for the defaults used when the selected Intent is not available");

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(intent_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}
#endif

static void config_tab_color(GtkWidget *notebook)
{
	GtkWidget *label;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *tabcomp;
	GtkWidget *table;
	gint i;

	vbox = scrolled_notebook_page(notebook, _("Color management"));

	group =  pref_group_new(vbox, FALSE, _("Input profiles"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_LCMS
	gtk_widget_set_sensitive(pref_group_parent(group), FALSE);
#endif

	table = pref_table_new(group, 3, COLOR_PROFILE_INPUTS + 1, FALSE, FALSE);
	gtk_table_set_col_spacings(GTK_TABLE(table), PREF_PAD_GAP);

	label = pref_table_label(table, 0, 0, _("Type"), 0.0);
	pref_label_bold(label, TRUE, FALSE);

	label = pref_table_label(table, 1, 0, _("Menu name"), 0.0);
	pref_label_bold(label, TRUE, FALSE);

	label = pref_table_label(table, 2, 0, _("File"), 0.0);
	pref_label_bold(label, TRUE, FALSE);

	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		GtkWidget *entry;
		gchar *buf;

		buf = g_strdup_printf(_("Input %d:"), i + COLOR_PROFILE_FILE);
		pref_table_label(table, 0, i + 1, buf, 1.0);
		g_free(buf);

		entry = gtk_entry_new();
		gtk_entry_set_max_length(GTK_ENTRY(entry), EDITOR_NAME_MAX_LENGTH);
		if (options->color_profile.input_name[i])
			{
			gtk_entry_set_text(GTK_ENTRY(entry), options->color_profile.input_name[i]);
			}
		gtk_table_attach(GTK_TABLE(table), entry, 1, 2, i + 1, i + 2,
				 static_cast<GtkAttachOptions>(GTK_FILL | GTK_EXPAND), static_cast<GtkAttachOptions>(0), 0, 0);
		gtk_widget_show(entry);
		color_profile_input_name_entry[i] = entry;

		tabcomp = tab_completion_new(&entry, options->color_profile.input_file[i], nullptr, ".icc", "ICC Files", nullptr);
		tab_completion_add_select_button(entry, _("Select color profile"), FALSE);
		gtk_widget_set_size_request(entry, 160, -1);
		gtk_table_attach(GTK_TABLE(table), tabcomp, 2, 3, i + 1, i + 2,
				 static_cast<GtkAttachOptions>(GTK_FILL | GTK_EXPAND), static_cast<GtkAttachOptions>(0), 0, 0);
		gtk_widget_show(tabcomp);
		color_profile_input_file_entry[i] = entry;
		}

	group =  pref_group_new(vbox, FALSE, _("Screen profile"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_LCMS
	gtk_widget_set_sensitive(pref_group_parent(group), FALSE);
#endif
	pref_checkbox_new_int(group, _("Use system screen profile if available"),
			      options->color_profile.use_x11_screen_profile, &c_options->color_profile.use_x11_screen_profile);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);

	pref_table_label(table, 0, 0, _("Screen:"), 1.0);
	tabcomp = tab_completion_new(&color_profile_screen_file_entry,
				     options->color_profile.screen_file, nullptr, ".icc", "ICC Files", nullptr);
	tab_completion_add_select_button(color_profile_screen_file_entry, _("Select color profile"), FALSE);
	gtk_widget_set_size_request(color_profile_screen_file_entry, 160, -1);
#ifdef HAVE_LCMS
	add_intent_menu(table, 0, 1, _("Render Intent:"), options->color_profile.render_intent, &c_options->color_profile.render_intent);
#endif
	gtk_table_attach(GTK_TABLE(table), tabcomp, 1, 2,
			 0, 1,
			 static_cast<GtkAttachOptions>(GTK_FILL | GTK_EXPAND), static_cast<GtkAttachOptions>(0), 0, 0);

	gtk_widget_show(tabcomp);
}

/* advanced entry tab */
static void use_geeqie_trash_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->file_ops.use_system_trash = FALSE;
		c_options->file_ops.no_trash = FALSE;
		}
}

static void use_system_trash_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->file_ops.use_system_trash = TRUE;
		c_options->file_ops.no_trash = FALSE;
		}
}

static void use_no_cache_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->file_ops.no_trash = TRUE;
		}
}

static void config_tab_behavior(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *tabcomp;
	GtkWidget *ct_button;
	GtkWidget *spin;
	GtkWidget *table;
	GtkWidget *marks;
	GtkWidget *with_rename;
	GtkWidget *collections_on_top;
	GtkWidget *hide_window_in_fullscreen;
	GtkWidget *checkbox;
	GtkWidget *tmp;

	vbox = scrolled_notebook_page(notebook, _("Behavior"));

	group = pref_group_new(vbox, FALSE, _("Delete"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Confirm permanent file delete"),
			      options->file_ops.confirm_delete, &c_options->file_ops.confirm_delete);
	pref_checkbox_new_int(group, _("Confirm move file to Trash"),
			      options->file_ops.confirm_move_to_trash, &c_options->file_ops.confirm_move_to_trash);
	pref_checkbox_new_int(group, _("Enable Delete key"),
			      options->file_ops.enable_delete_key, &c_options->file_ops.enable_delete_key);

	ct_button = pref_radiobutton_new(group, nullptr, _("Use Geeqie trash location"),
					!options->file_ops.use_system_trash && !options->file_ops.no_trash, G_CALLBACK(use_geeqie_trash_cb),nullptr);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spacer(hbox, PREF_PAD_INDENT - PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	tabcomp = tab_completion_new(&safe_delete_path_entry, options->file_ops.safe_delete_path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(safe_delete_path_entry, nullptr, TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spacer(hbox, PREF_PAD_INDENT - PREF_PAD_GAP);
	spin = pref_spin_new_int(hbox, _("Maximum size:"), _("MiB"),
				 0, 2048, 1, options->file_ops.safe_delete_folder_maxsize, &c_options->file_ops.safe_delete_folder_maxsize);
	gtk_widget_set_tooltip_markup(spin, _("Set to 0 for unlimited size"));
	button = pref_button_new(nullptr, nullptr, _("View"),
				 G_CALLBACK(safe_delete_view_cb), nullptr);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_CLEAR, nullptr,
				 G_CALLBACK(safe_delete_clear_cb), nullptr);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	c_options->file_ops.no_trash = options->file_ops.no_trash;
	c_options->file_ops.use_system_trash = options->file_ops.use_system_trash;

	pref_radiobutton_new(group, ct_button, _("Use system Trash bin"),
					options->file_ops.use_system_trash && !options->file_ops.no_trash, G_CALLBACK(use_system_trash_cb), nullptr);

	pref_radiobutton_new(group, ct_button, _("Use no trash at all"),
			options->file_ops.no_trash, G_CALLBACK(use_no_cache_cb), nullptr);

	gtk_widget_show(button);

	pref_spacer(group, PREF_PAD_GROUP);


	group = pref_group_new(vbox, FALSE, _("Behavior"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Descend folders in tree view"),
			      options->tree_descend_subdirs, &c_options->tree_descend_subdirs);

	pref_checkbox_new_int(group, _("In place renaming"),
			      options->file_ops.enable_in_place_rename, &c_options->file_ops.enable_in_place_rename);

	pref_checkbox_new_int(group, _("List directory view uses single click to enter"),
			      options->view_dir_list_single_click_enter, &c_options->view_dir_list_single_click_enter);

	tmp = pref_checkbox_new_int(group, _("Circular selection lists"),
			      options->circular_selection_lists, &c_options->circular_selection_lists);
	gtk_widget_set_tooltip_text(tmp, _("Traverse selection lists in a circular manner"));

	marks = pref_checkbox_new_int(group, _("Save marks on exit"),
				options->marks_save, &c_options->marks_save);
	gtk_widget_set_tooltip_text(marks,"Note that marks linked to a keyword will be saved irrespective of this setting");

	with_rename = pref_checkbox_new_int(group, _("Use \"With Rename\" as default for Copy/Move dialogs"),
				options->with_rename, &c_options->with_rename);
	gtk_widget_set_tooltip_text(with_rename,"Change the default button for Copy/Move dialogs");

	collections_on_top = pref_checkbox_new_int(group, _("Open collections on top"),
				options->collections_on_top, &c_options->collections_on_top);
	gtk_widget_set_tooltip_text(collections_on_top,"Open collections window on top");

	hide_window_in_fullscreen = pref_checkbox_new_int(group, _("Hide window in fullscreen"),
				options->hide_window_in_fullscreen, &c_options->hide_window_in_fullscreen);
	gtk_widget_set_tooltip_text(hide_window_in_fullscreen,"When alt-tabbing, prevent Geeqie window showing twice");

	pref_spin_new_int(group, _("Recent folder list maximum size"), nullptr,
			  1, 50, 1, options->open_recent_list_maxsize, &c_options->open_recent_list_maxsize);

	tmp = pref_spin_new_int(group, _("Recent folder-image list maximum size"), nullptr, 0, 50, 1, options->recent_folder_image_list_maxsize, &c_options->recent_folder_image_list_maxsize);
	gtk_widget_set_tooltip_text(tmp, _("List of the last image viewed in each recent folder.\nRe-opening a folder will set focus to the last image viewed."));

	pref_spin_new_int(group, _("Drag'n drop icon size"), nullptr,
			  16, 256, 16, options->dnd_icon_size, &c_options->dnd_icon_size);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_dnd_default_action_selection_menu(table, 0, 0, _("Drag`n drop default action:"), options->dnd_default_action, &c_options->dnd_default_action);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_clipboard_selection_menu(table, 0, 0, _("Copy path clipboard selection:"), options->clipboard_selection, &c_options->clipboard_selection);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Navigation"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Progressive keyboard scrolling"),
			      options->progressive_key_scrolling, &c_options->progressive_key_scrolling);
	pref_spin_new_int(group, _("Keyboard scrolling step multiplier:"), nullptr,
			  1, 32, 1, options->keyboard_scroll_step, reinterpret_cast<int *>(&c_options->keyboard_scroll_step));
	pref_checkbox_new_int(group, _("Mouse wheel scrolls image"),
			      options->mousewheel_scrolls, &c_options->mousewheel_scrolls);
	pref_checkbox_new_int(group, _("Navigation by left or middle click on image"),
			      options->image_lm_click_nav, &c_options->image_lm_click_nav);
	pref_checkbox_new_int(group, _("Open archive by left click on image"),
			      options->image_l_click_archive, &c_options->image_l_click_archive);
	pref_checkbox_new_int(group, _("Play video by left click on image"),
			      options->image_l_click_video, &c_options->image_l_click_video);
	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_video_menu(table, 0, 0, _("Play with:"), options->image_l_click_video_editor, &c_options->image_l_click_video_editor);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_mouse_selection_menu(table, 0, 0, _("Mouse button Back:"), options->mouse_button_8, &c_options->mouse_button_8);
	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_mouse_selection_menu(table, 0, 0, _("Mouse button Forward:"), options->mouse_button_9, &c_options->mouse_button_9);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("GPU"), GTK_ORIENTATION_VERTICAL);

	checkbox = pref_checkbox_new_int(group, _("Override disable GPU"),
				options->override_disable_gpu, &c_options->override_disable_gpu);
	gtk_widget_set_tooltip_text(checkbox, "Contact the developers for usage");

#ifdef DEBUG
	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Debugging"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new_int(group, _("Debug level:"), nullptr,
			  DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX, 1, get_debug_level(), &debug_c);

	pref_checkbox_new_int(group, _("Timer data"),
			options->log_window.timer_data, &c_options->log_window.timer_data);

	pref_spin_new_int(group, _("Log Window max. lines:"), nullptr,
			  1, 99999, 1, options->log_window_lines, &options->log_window_lines);
#endif
}

/* accelerators tab */

static gboolean accel_search_function_cb(GtkTreeModel *model, gint column, const gchar *key, GtkTreeIter *iter, gpointer)
{
	gboolean ret = TRUE;
	gchar *key_nocase;
	gchar *text;
	gchar *text_nocase;

	gtk_tree_model_get(model, iter, column, &text, -1);
	text_nocase = g_utf8_casefold(text, -1);
	key_nocase = g_utf8_casefold(key, -1);

	if (g_strstr_len(text_nocase, -1, key_nocase))
		{
		ret = FALSE;
		}

	g_free(key_nocase);
	g_free(text);
	g_free(text_nocase);

	return ret;
}

static void accel_row_activated_cb(GtkTreeView *tree_view, GtkTreePath *, GtkTreeViewColumn *column, gpointer)
{
	GList *list;
	gint col_num = 0;

	list = gtk_tree_view_get_columns(tree_view);
	col_num = g_list_index(list, column);

	g_list_free(list);

	gtk_tree_view_set_search_column(tree_view, col_num);
}

static void config_tab_accelerators(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *scrolled;
	GtkWidget *accel_view;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;

	vbox = scrolled_notebook_page(notebook, _("Keyboard"));

	group = pref_group_new(vbox, TRUE, _("Accelerators"), GTK_ORIENTATION_VERTICAL);

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	accel_store = gtk_tree_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	accel_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(accel_store));
	g_object_unref(accel_store);
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(accel_view));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_MULTIPLE);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(accel_view), FALSE);

	renderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new_with_attributes(_("Action"),
							  renderer,
							  "text", AE_ACTION,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_ACTION);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);


	renderer = gtk_cell_renderer_accel_new();
	g_signal_connect(G_OBJECT(renderer), "accel-cleared",
			 G_CALLBACK(accel_store_cleared_cb), accel_store);
	g_signal_connect(G_OBJECT(renderer), "accel-edited",
			 G_CALLBACK(accel_store_edited_cb), accel_store);


	g_object_set (renderer,
		      "editable", TRUE,
		      "accel-mode", GTK_CELL_RENDERER_ACCEL_MODE_OTHER,
		      NULL);

	column = gtk_tree_view_column_new_with_attributes(_("KEY"),
							  renderer,
							  "text", AE_KEY,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_KEY);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);

	renderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new_with_attributes(_("Tooltip"),
							  renderer,
							  "text", AE_TOOLTIP,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_TOOLTIP);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);

	renderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new_with_attributes("Accel",
							  renderer,
							  "text", AE_ACCEL,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_ACCEL);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);

	/* Search on text in column */
	gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(accel_view), TRUE);
	g_signal_connect(accel_view, "row_activated", G_CALLBACK(accel_row_activated_cb), accel_store);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(accel_view), TRUE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(accel_view), AE_TOOLTIP);
	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(accel_view), accel_search_function_cb, nullptr, nullptr);

	accel_store_populate();
	gtk_container_add(GTK_CONTAINER(scrolled), accel_view);
	gtk_widget_show(accel_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, nullptr, _("Defaults"),
				 G_CALLBACK(accel_default_cb), accel_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, nullptr, _("Reset selected"),
				 G_CALLBACK(accel_reset_cb), accel_view);
	gtk_widget_set_tooltip_text(button, _("Will only reset changes made before the settings are saved"));
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, nullptr, _("Clear selected"),
				 G_CALLBACK(accel_clear_cb), accel_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
}

/* toolbar main tab */
static void config_tab_toolbar_main(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *toolbardata;
	LayoutWindow *lw;

	lw = static_cast<LayoutWindow *>(layout_window_list->data);

	vbox = scrolled_notebook_page(notebook, _("Toolbar Main"));

	toolbardata = toolbar_select_new(lw, TOOLBAR_MAIN);
	gtk_box_pack_start(GTK_BOX(vbox), toolbardata, TRUE, TRUE, 0);
	gtk_widget_show(vbox);
}

/* toolbar status tab */
static void config_tab_toolbar_status(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *toolbardata;
	LayoutWindow *lw;

	lw = static_cast<LayoutWindow *>(layout_window_list->data);

	vbox = scrolled_notebook_page(notebook, _("Toolbar Status"));

	toolbardata = toolbar_select_new(lw, TOOLBAR_STATUS);
	gtk_box_pack_start(GTK_BOX(vbox), toolbardata, TRUE, TRUE, 0);
	gtk_widget_show(vbox);
}

/* advanced tab */
static gint extension_sort_cb(gconstpointer a, gconstpointer b)
{
	return g_strcmp0(static_cast<const gchar *>(a), static_cast<const gchar *>(b));
}

static void config_tab_advanced(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *group;
	GSList *formats_list;
	GList *extensions_list = nullptr;
	gchar **extensions;
	GtkWidget *tabcomp;
	GdkPixbufFormat *fm;
	gint i;
	GString *types_string = g_string_new(nullptr);
	GtkWidget *types_string_label;
	GtkWidget *threads_string_label;
	GtkWidget *dupes_threads_spin;

	vbox = scrolled_notebook_page(notebook, _("Advanced"));
	group = pref_group_new(vbox, FALSE, _("External preview extraction"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Use external preview extraction -  Requires restart"), options->external_preview.enable, &c_options->external_preview.enable);

	pref_spacer(group, PREF_PAD_GROUP);

	formats_list = gdk_pixbuf_get_formats();

	while (formats_list)
		{
		fm = static_cast<GdkPixbufFormat *>(formats_list->data);
		extensions = gdk_pixbuf_format_get_extensions(fm);

		i = 0;
		while (extensions[i])
			{
			extensions_list = g_list_insert_sorted(extensions_list, g_strdup(extensions[i]), extension_sort_cb);
			i++;
			}

		g_strfreev(extensions);
		formats_list = formats_list->next;
		}

	while (extensions_list)
		{
		if (types_string->len == 0)
			{
			types_string = g_string_append(types_string, static_cast<const gchar *>(extensions_list->data));
			}
		else
			{
			types_string = g_string_append(types_string, ", ");
			types_string = g_string_append(types_string, static_cast<const gchar *>(extensions_list->data));
			}

		extensions_list = extensions_list->next;
		}

	types_string = g_string_prepend(types_string, _("Usable file types:\n"));
	types_string_label = pref_label_new(group, types_string->str);
	gtk_label_set_line_wrap(GTK_LABEL(types_string_label), TRUE);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("File identification tool"), GTK_ORIENTATION_VERTICAL);
	external_preview_select_entry = gtk_entry_new();
	tabcomp = tab_completion_new(&external_preview_select_entry, options->external_preview.select, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(external_preview_select_entry, _("Select file identification tool"), FALSE);
	gtk_box_pack_start(GTK_BOX(group), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	group = pref_group_new(vbox, FALSE, _("Preview extraction tool"), GTK_ORIENTATION_VERTICAL);
	external_preview_extract_entry = gtk_entry_new();
	tabcomp = tab_completion_new(&external_preview_extract_entry, options->external_preview.extract, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(external_preview_extract_entry, _("Select preview extraction tool"), FALSE);
	gtk_box_pack_start(GTK_BOX(group), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	gtk_widget_show(vbox);

	g_slist_free(formats_list);
	g_list_free_full(extensions_list, g_free);
	g_string_free(types_string, TRUE);

	pref_spacer(group, PREF_PAD_GROUP);

	pref_line(vbox, PREF_PAD_SPACE);
	group = pref_group_new(vbox, FALSE, _("Thread pool limits"), GTK_ORIENTATION_VERTICAL);

	threads_string_label = pref_label_new(group, "This option limits the number of threads (or cpu cores)\nthat Geeqie will use when running duplicate checks. The default value is 0, which means all available cores will be used.");
	gtk_label_set_line_wrap(GTK_LABEL(threads_string_label), TRUE);

	pref_spacer(vbox, PREF_PAD_GROUP);

	dupes_threads_spin = pref_spin_new_int(vbox, _("Duplicate check:"), _("max. threads"), 0, get_cpu_cores(), 1, options->threads.duplicates, &c_options->threads.duplicates);
	gtk_widget_set_tooltip_markup(dupes_threads_spin, _("Set to 0 for unlimited"));
}

/* stereo tab */
static void config_tab_stereo(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *group2;
	GtkWidget *table;
	GtkWidget *box;
	GtkWidget *box2;
	GtkWidget *fs_button;
	vbox = scrolled_notebook_page(notebook, _("Stereo"));

	group = pref_group_new(vbox, FALSE, _("Windowed stereo mode"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_stereo_mode_menu(table, 0, 0, _("Windowed stereo mode"), options->stereo.mode, &c_options->stereo.mode, FALSE);

	table = pref_table_new(group, 2, 2, TRUE, FALSE);
	box = pref_table_box(table, 0, 0, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Mirror left image"),
			      options->stereo.mode & PR_STEREO_MIRROR_LEFT, &c_options->stereo.tmp.mirror_left);
	box = pref_table_box(table, 1, 0, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Flip left image"),
			      options->stereo.mode & PR_STEREO_FLIP_LEFT, &c_options->stereo.tmp.flip_left);
	box = pref_table_box(table, 0, 1, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Mirror right image"),
			      options->stereo.mode & PR_STEREO_MIRROR_RIGHT, &c_options->stereo.tmp.mirror_right);
	box = pref_table_box(table, 1, 1, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Flip right image"),
			      options->stereo.mode & PR_STEREO_FLIP_RIGHT, &c_options->stereo.tmp.flip_right);
	pref_checkbox_new_int(group, _("Swap left and right images"),
			      options->stereo.mode & PR_STEREO_SWAP, &c_options->stereo.tmp.swap);
	pref_checkbox_new_int(group, _("Disable stereo mode on single image source"),
			      options->stereo.mode & PR_STEREO_TEMP_DISABLE, &c_options->stereo.tmp.temp_disable);

	group = pref_group_new(vbox, FALSE, _("Fullscreen stereo mode"), GTK_ORIENTATION_VERTICAL);
	fs_button = pref_checkbox_new_int(group, _("Use different settings for fullscreen"),
			      options->stereo.enable_fsmode, &c_options->stereo.enable_fsmode);
	box2 = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(fs_button, box2);
	table = pref_table_new(box2, 2, 1, FALSE, FALSE);
	add_stereo_mode_menu(table, 0, 0, _("Fullscreen stereo mode"), options->stereo.fsmode, &c_options->stereo.fsmode, TRUE);
	table = pref_table_new(box2, 2, 2, TRUE, FALSE);
	box = pref_table_box(table, 0, 0, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Mirror left image"),
			      options->stereo.fsmode & PR_STEREO_MIRROR_LEFT, &c_options->stereo.tmp.fs_mirror_left);
	box = pref_table_box(table, 1, 0, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Flip left image"),
			      options->stereo.fsmode & PR_STEREO_FLIP_LEFT, &c_options->stereo.tmp.fs_flip_left);
	box = pref_table_box(table, 0, 1, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Mirror right image"),
			      options->stereo.fsmode & PR_STEREO_MIRROR_RIGHT, &c_options->stereo.tmp.fs_mirror_right);
	box = pref_table_box(table, 1, 1, GTK_ORIENTATION_HORIZONTAL, nullptr);
	pref_checkbox_new_int(box, _("Flip right image"),
			      options->stereo.fsmode & PR_STEREO_FLIP_RIGHT, &c_options->stereo.tmp.fs_flip_right);
	pref_checkbox_new_int(box2, _("Swap left and right images"),
			      options->stereo.fsmode & PR_STEREO_SWAP, &c_options->stereo.tmp.fs_swap);
	pref_checkbox_new_int(box2, _("Disable stereo mode on single image source"),
			      options->stereo.fsmode & PR_STEREO_TEMP_DISABLE, &c_options->stereo.tmp.fs_temp_disable);

	group2 = pref_group_new(box2, FALSE, _("Fixed position"), GTK_ORIENTATION_VERTICAL);
	table = pref_table_new(group2, 5, 3, FALSE, FALSE);
	pref_table_spin_new_int(table, 0, 0, _("Width"), nullptr,
			  1, 5000, 1, options->stereo.fixed_w, &c_options->stereo.fixed_w);
	pref_table_spin_new_int(table, 3, 0,  _("Height"), nullptr,
			  1, 5000, 1, options->stereo.fixed_h, &c_options->stereo.fixed_h);
	pref_table_spin_new_int(table, 0, 1,  _("Left X"), nullptr,
			  0, 5000, 1, options->stereo.fixed_x1, &c_options->stereo.fixed_x1);
	pref_table_spin_new_int(table, 3, 1,  _("Left Y"), nullptr,
			  0, 5000, 1, options->stereo.fixed_y1, &c_options->stereo.fixed_y1);
	pref_table_spin_new_int(table, 0, 2,  _("Right X"), nullptr,
			  0, 5000, 1, options->stereo.fixed_x2, &c_options->stereo.fixed_x2);
	pref_table_spin_new_int(table, 3, 2,  _("Right Y"), nullptr,
			  0, 5000, 1, options->stereo.fixed_y2, &c_options->stereo.fixed_y2);

}

/* Main preferences window */
static void config_window_create(LayoutWindow *lw)
{
	GtkWidget *win_vbox;
	GtkWidget *hbox;
	GtkWidget *notebook;
	GtkWidget *button;
	GtkWidget *ct_button;

	if (!c_options) c_options = init_options(nullptr);

	configwindow = window_new(GTK_WINDOW_TOPLEVEL, "preferences", PIXBUF_INLINE_ICON_CONFIG, nullptr, _("Preferences"));
	DEBUG_NAME(configwindow);
	gtk_window_set_type_hint(GTK_WINDOW(configwindow), GDK_WINDOW_TYPE_HINT_DIALOG);
	g_signal_connect(G_OBJECT(configwindow), "delete_event",
			 G_CALLBACK(config_window_delete), NULL);
	if (options->save_dialog_window_positions)
		{
		gtk_window_resize(GTK_WINDOW(configwindow), lw->options.preferences_window.w, lw->options.preferences_window.h);
		gtk_window_move(GTK_WINDOW(configwindow), lw->options.preferences_window.x, lw->options.preferences_window.y);
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(configwindow), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
		}
	gtk_window_set_resizable(GTK_WINDOW(configwindow), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(configwindow), PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gtk_container_add(GTK_CONTAINER(configwindow), win_vbox);
	gtk_widget_show(win_vbox);

	notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_LEFT);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_box_pack_start(GTK_BOX(win_vbox), notebook, TRUE, TRUE, 0);

	config_tab_general(notebook);
	config_tab_image(notebook);
	config_tab_osd(notebook);
	config_tab_windows(notebook);
	config_tab_accelerators(notebook);
	config_tab_files(notebook);
	config_tab_metadata(notebook);
	config_tab_keywords(notebook);
	config_tab_color(notebook);
	config_tab_stereo(notebook);
	config_tab_behavior(notebook);
	config_tab_toolbar_main(notebook);
	config_tab_toolbar_status(notebook);
	config_tab_advanced(notebook);

	gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), lw->options.preferences_window.page_number);

	hbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_BUTTON_GAP);
	gtk_box_pack_end(GTK_BOX(win_vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(nullptr, GQ_ICON_HELP, _("Help"),
				 G_CALLBACK(config_window_help_cb), notebook);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_OK, "OK",
				 G_CALLBACK(config_window_ok_cb), notebook);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	ct_button = button;

	button = pref_button_new(nullptr, GQ_ICON_CANCEL, _("Cancel"),
				 G_CALLBACK(config_window_close_cb), nullptr);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	if (!generic_dialog_get_alternative_button_order(configwindow))
		{
		gtk_box_reorder_child(GTK_BOX(hbox), ct_button, -1);
		}

	gtk_widget_show(notebook);

	gtk_widget_show(configwindow);
}

/*
 *-----------------------------------------------------------------------------
 * config window show (public)
 *-----------------------------------------------------------------------------
 */

void show_config_window(LayoutWindow *lw)
{
	if (configwindow)
		{
		gtk_window_present(GTK_WINDOW(configwindow));
		return;
		}

	config_window_create(lw);
}

/*
 *-----------------
 * about window
 *-----------------
 */

void show_about_window(LayoutWindow *lw)
{
	GDataInputStream *data_stream;
	GInputStream *in_stream_authors;
	GInputStream *in_stream_translators;
	GString *copyright;
	GdkPixbuf *pixbuf_icon;
	GdkPixbuf *pixbuf_logo;
	ZoneDetect *cd;
	gchar *artists[2];
	gchar *author_line;
	gchar *authors[1000];
	gchar *authors_path;
	gchar *comment;
	gchar *timezone_path;
	gchar *translators;
	gchar *translators_path;
	gint i_authors = 0;
	gint n = 0;
	gsize bytes_read;
	gsize length;
	gsize size;
	guint32 flags;

	copyright = g_string_new(nullptr);
	copyright = g_string_append(copyright, "This program comes with absolutely no warranty.\nGNU General Public License, version 2 or later.\nSee https://www.gnu.org/licenses/old-licenses/gpl-2.0.html\n\n");

	timezone_path = g_build_filename(get_rc_dir(), TIMEZONE_DATABASE_FILE, NULL);
	if (g_file_test(timezone_path, G_FILE_TEST_EXISTS))
		{
		cd = ZDOpenDatabase(timezone_path);
		if (cd)
			{
			copyright = g_string_append(copyright, ZDGetNotice(cd));
			}
		else
			{
			log_printf("Error: Init of timezone database %s failed\n", timezone_path);
			}
		ZDCloseDatabase(cd);
		}
	g_free(timezone_path);

	authors_path = g_build_filename(GQ_RESOURCE_PATH_CREDITS, "authors", NULL);

	in_stream_authors = g_resources_open_stream(authors_path, G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);

	data_stream = g_data_input_stream_new(in_stream_authors);

	authors[0] = nullptr;
	while ((author_line = g_data_input_stream_read_line(G_DATA_INPUT_STREAM(data_stream), &length, nullptr, nullptr)))
		{
		authors[i_authors] = g_strdup(author_line);
		i_authors++;
		g_free(author_line);
		}
	authors[i_authors] = nullptr;

	g_input_stream_close(in_stream_authors, nullptr, nullptr);

	translators_path = g_build_filename(GQ_RESOURCE_PATH_CREDITS, "translators", NULL);

	g_resources_get_info(translators_path, G_RESOURCE_LOOKUP_FLAGS_NONE, &size, &flags, nullptr);

	in_stream_translators = g_resources_open_stream(translators_path, G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
	translators = static_cast<gchar *>(g_malloc0(size));
	g_input_stream_read_all(in_stream_translators, translators, size, &bytes_read, nullptr, nullptr);
	g_input_stream_close(in_stream_translators, nullptr, nullptr);

	comment = g_strconcat("Project created by John Ellis\nGQview 1998\nGeeqie 2007\n\n\nDevelopment and bug reports:\n", GQ_EMAIL_ADDRESS, "\nhttps://github.com/BestImageViewer/geeqie/issues",NULL);

	artists[0] = g_strdup("Néstor Díaz Valencia <nestor@estudionexos.com>");
	artists[1] = nullptr;

	pixbuf_logo = pixbuf_inline(PIXBUF_INLINE_LOGO);
	pixbuf_icon = pixbuf_inline(PIXBUF_INLINE_ICON);
	gtk_show_about_dialog(GTK_WINDOW(lw->window),
		"title", _("About Geeqie"),
		"resizable", TRUE,
		"program-name", GQ_APPNAME,
		"version", VERSION,
		"logo", pixbuf_logo,
		"icon", pixbuf_icon,
		"website", GQ_WEBSITE,
		"website-label", "Website",
		"comments", comment,
		"artists", artists,
		"authors", authors,
		"translator-credits", translators,
		"wrap-license", TRUE,
		"license", copyright->str,
		NULL);

	g_string_free(copyright, TRUE);

	while(n < i_authors)
		{
		g_free(authors[n]);
		n++;
		}

	g_free(artists[0]);
	g_free(authors_path);
	g_free(comment);
	g_free(translators);
	g_free(translators_path);
	g_object_unref(data_stream);
	g_object_unref(in_stream_authors);
	g_object_unref(in_stream_translators);
}

static void image_overlay_set_text_colours()
{
	c_options->image_overlay.text_red = options->image_overlay.text_red;
	c_options->image_overlay.text_green = options->image_overlay.text_green;
	c_options->image_overlay.text_blue = options->image_overlay.text_blue;
	c_options->image_overlay.text_alpha = options->image_overlay.text_alpha;
	c_options->image_overlay.background_red = options->image_overlay.background_red;
	c_options->image_overlay.background_green = options->image_overlay.background_green;
	c_options->image_overlay.background_blue = options->image_overlay.background_blue;
	c_options->image_overlay.background_alpha = options->image_overlay.background_alpha;
}

/*
 *-----------------------------------------------------------------------------
 * timezone database routines
 *-----------------------------------------------------------------------------
 */

static void timezone_async_ready_cb(GObject *source_object, GAsyncResult *res, gpointer data)
{
	GError *error = nullptr;
	auto tz = static_cast<TZData *>(data);
	gchar *tmp_filename;
	gchar *timezone_bin;
	gchar *tmp_dir = nullptr;
	FileData *fd;

	if (!g_cancellable_is_cancelled(tz->cancellable))
		{
		generic_dialog_close(tz->gd);
		}


	if (g_file_copy_finish(G_FILE(source_object), res, &error))
		{
		tmp_filename = g_file_get_path(tz->tmp_g_file);
		fd = file_data_new_simple(tmp_filename);
		tmp_dir = open_archive(fd);

		if (tmp_dir)
			{
			timezone_bin = g_build_filename(tmp_dir, TIMEZONE_DATABASE_VERSION, TIMEZONE_DATABASE_FILE, NULL);
			if (isfile(timezone_bin))
				{
				move_file(timezone_bin, tz->timezone_database_user);
				}
			else
				{
				warning_dialog(_("Warning: Cannot open timezone database file"), _("See the Log Window"), GTK_STOCK_DIALOG_WARNING, nullptr);
				}

			g_free(timezone_bin);
			g_free(tmp_dir); // The folder in /tmp is deleted in exit_program_final()
			}
		else
			{
			warning_dialog(_("Warning: Cannot open timezone database file"), _("See the Log Window"), GTK_STOCK_DIALOG_WARNING, nullptr);
			}
		g_free(tmp_filename);
		file_data_unref(fd);
		}
	else
		{
		file_util_warning_dialog(_("Error: Timezone database download failed"), error->message, GTK_STOCK_DIALOG_ERROR, nullptr);
		}

	g_file_delete(tz->tmp_g_file, nullptr, &error);
	g_object_unref(tz->tmp_g_file);
	tz->tmp_g_file = nullptr;
	g_object_unref(tz->cancellable);
	g_object_unref(tz->timezone_database_gq);
}

static void timezone_progress_cb(goffset current_num_bytes, goffset total_num_bytes, gpointer data)
{
	auto tz = static_cast<TZData *>(data);

	if (!g_cancellable_is_cancelled(tz->cancellable))
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(tz->progress), static_cast<gdouble>(current_num_bytes) / total_num_bytes);
		}
}

static void timezone_cancel_button_cb(GenericDialog *, gpointer data)
{
	auto tz = static_cast<TZData *>(data);

	g_cancellable_cancel(tz->cancellable);
}

static void timezone_database_install_cb(GtkWidget *widget, gpointer data)
{
	auto tz = static_cast<TZData *>(data);
	GError *error = nullptr;
	GFileIOStream *io_stream;

	if (tz->tmp_g_file)
		{
		return;
		}

	tz->tmp_g_file = g_file_new_tmp("geeqie_timezone_XXXXXX", &io_stream, &error);

	if (error)
		{
		file_util_warning_dialog(_("Timezone database download failed"), error->message, GTK_STOCK_DIALOG_ERROR, nullptr);
		log_printf("Error: Download timezone database failed:\n%s", error->message);
		g_error_free(error);
		g_object_unref(tz->tmp_g_file);
		}
	else
		{
		tz->timezone_database_gq = g_file_new_for_uri(TIMEZONE_DATABASE_WEB);

		tz->gd = generic_dialog_new(_("Timezone database"), "download_timezone_database", nullptr, TRUE, timezone_cancel_button_cb, tz);

		generic_dialog_add_message(tz->gd, GTK_STOCK_DIALOG_INFO, _("Downloading timezone database"), nullptr, FALSE);

		tz->progress = gtk_progress_bar_new();
		gtk_box_pack_start(GTK_BOX(tz->gd->vbox), tz->progress, FALSE, FALSE, 0);
		gtk_widget_show(tz->progress);

		gtk_widget_show(tz->gd->dialog);
		tz->cancellable = g_cancellable_new();
		g_file_copy_async(tz->timezone_database_gq, tz->tmp_g_file, G_FILE_COPY_OVERWRITE, G_PRIORITY_LOW, tz->cancellable, timezone_progress_cb, tz, timezone_async_ready_cb, tz);

		gtk_button_set_label(GTK_BUTTON(widget), _("Update"));
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
