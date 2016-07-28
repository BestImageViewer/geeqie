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

#include "bar_exif.h"
#include "cache.h"
#include "cache_maint.h"
#include "editors.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "fullscreen.h"
#include "image.h"
#include "image-overlay.h"
#include "color-man.h"
#include "img-view.h"
#include "layout_config.h"
#include "layout_util.h"
#include "pixbuf_util.h"
#include "slideshow.h"
#include "trash.h"
#include "utilops.h"
#include "ui_fileops.h"
#include "ui_misc.h"
#include "ui_tabcomp.h"
#include "ui_utildlg.h"
#include "window.h"

#include <math.h>

#ifdef HAVE_LCMS
#ifdef HAVE_LCMS2
#include <lcms2.h>
#else
#include <lcms.h>
#endif
#endif

#define EDITOR_NAME_MAX_LENGTH 32
#define EDITOR_COMMAND_MAX_LENGTH 1024

static void image_overlay_set_text_colours();

typedef struct _ThumbSize ThumbSize;
struct _ThumbSize
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

static gchar *format_class_list[] = {
	N_("Unknown"),
	N_("Image"),
	N_("RAW Image"),
	N_("Metadata")
	};

/* config memory values */
static ConfOptions *c_options = NULL;


#ifdef DEBUG
static gint debug_c;
#endif

static GtkWidget *configwindow = NULL;
static GtkListStore *filter_store = NULL;
static GtkTreeStore *accel_store = NULL;

static GtkWidget *safe_delete_path_entry;

static GtkWidget *color_profile_input_file_entry[COLOR_PROFILE_INPUTS];
static GtkWidget *color_profile_input_name_entry[COLOR_PROFILE_INPUTS];
static GtkWidget *color_profile_screen_file_entry;

static GtkWidget *sidecar_ext_entry;


#define CONFIG_WINDOW_DEF_WIDTH		700
#define CONFIG_WINDOW_DEF_HEIGHT	600

/*
 *-----------------------------------------------------------------------------
 * option widget callbacks (private)
 *-----------------------------------------------------------------------------
 */

static void zoom_mode_cb(GtkWidget *widget, gpointer data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		c_options->image.zoom_mode = GPOINTER_TO_INT(data);
}

static void scroll_reset_cb(GtkWidget *widget, gpointer data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		c_options->image.scroll_reset_method = GPOINTER_TO_INT(data);
}

static void zoom_increment_cb(GtkWidget *spin, gpointer data)
{
	c_options->image.zoom_increment = (gint)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) * 100.0 + 0.01);
}

static void slideshow_delay_cb(GtkWidget *spin, gpointer data)
{
	c_options->slideshow.delay = (gint)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) *
				   (gdouble)SLIDESHOW_SUBSECOND_PRECISION + 0.01);
}

/*
 *-----------------------------------------------------------------------------
 * sync progam to config window routine (private)
 *-----------------------------------------------------------------------------
 */

void config_entry_to_option(GtkWidget *entry, gchar **option, gchar *(*func)(const gchar *))
{
	const gchar *buf;

	g_free(*option);
	*option = NULL;
	buf = gtk_entry_get_text(GTK_ENTRY(entry));
	if (buf && strlen(buf) > 0)
		{
		if (func)
			*option = func(buf);
		else
			*option = g_strdup(buf);
		}
}


static gboolean accel_apply_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
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


static void config_window_apply(void)
{
	gint i;
	gboolean refresh = FALSE;

	config_entry_to_option(safe_delete_path_entry, &options->file_ops.safe_delete_path, remove_trailing_slash);

	if (options->file_filter.show_hidden_files != c_options->file_filter.show_hidden_files) refresh = TRUE;
	if (options->file_filter.show_parent_directory != c_options->file_filter.show_parent_directory) refresh = TRUE;
	if (options->file_filter.show_dot_directory != c_options->file_filter.show_dot_directory) refresh = TRUE;
	if (options->file_sort.case_sensitive != c_options->file_sort.case_sensitive) refresh = TRUE;
	if (options->file_filter.disable_file_extension_checks != c_options->file_filter.disable_file_extension_checks) refresh = TRUE;
	if (options->file_filter.disable != c_options->file_filter.disable) refresh = TRUE;

	options->file_ops.confirm_delete = c_options->file_ops.confirm_delete;
	options->file_ops.enable_delete_key = c_options->file_ops.enable_delete_key;
	options->file_ops.safe_delete_enable = c_options->file_ops.safe_delete_enable;
	options->file_ops.safe_delete_folder_maxsize = c_options->file_ops.safe_delete_folder_maxsize;
	options->tools_restore_state = c_options->tools_restore_state;
	options->save_window_positions = c_options->save_window_positions;
	options->use_saved_window_positions_for_new_windows = c_options->use_saved_window_positions_for_new_windows;
	options->image.zoom_mode = c_options->image.zoom_mode;
	options->image.scroll_reset_method = c_options->image.scroll_reset_method;
	options->image.zoom_2pass = c_options->image.zoom_2pass;
	options->image.fit_window_to_image = c_options->image.fit_window_to_image;
	options->image.limit_window_size = c_options->image.limit_window_size;
	options->image.zoom_to_fit_allow_expand = c_options->image.zoom_to_fit_allow_expand;
	options->image.max_window_size = c_options->image.max_window_size;
	options->image.limit_autofit_size = c_options->image.limit_autofit_size;
	options->image.max_autofit_size = c_options->image.max_autofit_size;
	options->image.use_clutter_renderer = c_options->image.use_clutter_renderer;
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
	options->thumbnails.spec_standard = c_options->thumbnails.spec_standard;
	options->metadata.enable_metadata_dirs = c_options->metadata.enable_metadata_dirs;
	options->file_filter.show_hidden_files = c_options->file_filter.show_hidden_files;
	options->file_filter.show_parent_directory = c_options->file_filter.show_parent_directory;
	options->file_filter.show_dot_directory = c_options->file_filter.show_dot_directory;
	options->file_filter.disable_file_extension_checks = c_options->file_filter.disable_file_extension_checks;

	options->file_sort.case_sensitive = c_options->file_sort.case_sensitive;
	options->file_filter.disable = c_options->file_filter.disable;

	config_entry_to_option(sidecar_ext_entry, &options->sidecar.ext, NULL);
	sidecar_ext_parse(options->sidecar.ext);

	options->slideshow.random = c_options->slideshow.random;
	options->slideshow.repeat = c_options->slideshow.repeat;
	options->slideshow.delay = c_options->slideshow.delay;

	options->mousewheel_scrolls = c_options->mousewheel_scrolls;
	options->image_lm_click_nav = c_options->image_lm_click_nav;

	options->file_ops.enable_in_place_rename = c_options->file_ops.enable_in_place_rename;

	options->collections.rectangular_selection = c_options->collections.rectangular_selection;

	options->image.tile_cache_max = c_options->image.tile_cache_max;
	options->image.image_cache_max = c_options->image.image_cache_max;

	options->image.zoom_quality = c_options->image.zoom_quality;

	options->image.zoom_increment = c_options->image.zoom_increment;

	options->image.enable_read_ahead = c_options->image.enable_read_ahead;


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
	options->image.exif_rotate_enable = c_options->image.exif_rotate_enable;
	options->image.exif_proof_rotate_enable = c_options->image.exif_proof_rotate_enable;

	options->duplicates_similarity_threshold = c_options->duplicates_similarity_threshold;
	options->rot_invariant_sim = c_options->rot_invariant_sim;

	options->tree_descend_subdirs = c_options->tree_descend_subdirs;

	options->view_dir_list_single_click_enter = c_options->view_dir_list_single_click_enter;

	options->open_recent_list_maxsize = c_options->open_recent_list_maxsize;
	options->dnd_icon_size = c_options->dnd_icon_size;

	options->metadata.save_in_image_file = c_options->metadata.save_in_image_file;
	options->metadata.save_legacy_IPTC = c_options->metadata.save_legacy_IPTC;
	options->metadata.warn_on_write_problems = c_options->metadata.warn_on_write_problems;
	options->metadata.save_legacy_format = c_options->metadata.save_legacy_format;
	options->metadata.sync_grouped_files = c_options->metadata.sync_grouped_files;
	options->metadata.confirm_write = c_options->metadata.confirm_write;
	options->metadata.confirm_timeout = c_options->metadata.confirm_timeout;
	options->metadata.confirm_after_timeout = c_options->metadata.confirm_after_timeout;
	options->metadata.confirm_on_image_change = c_options->metadata.confirm_on_image_change;
	options->metadata.confirm_on_dir_change = c_options->metadata.confirm_on_dir_change;
	options->metadata.keywords_case_sensitive = c_options->metadata.keywords_case_sensitive;
	options->metadata.write_orientation = c_options->metadata.write_orientation;
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

#ifdef DEBUG
	set_debug_level(debug_c);
#endif

#ifdef HAVE_LCMS
	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		config_entry_to_option(color_profile_input_name_entry[i], &options->color_profile.input_name[i], NULL);
		config_entry_to_option(color_profile_input_file_entry[i], &options->color_profile.input_file[i], NULL);
		}
	config_entry_to_option(color_profile_screen_file_entry, &options->color_profile.screen_file, NULL);
	options->color_profile.use_x11_screen_profile = c_options->color_profile.use_x11_screen_profile;
	if (options->color_profile.render_intent != c_options->color_profile.render_intent)
		{
		options->color_profile.render_intent = c_options->color_profile.render_intent;
		color_man_update();
		}
#endif

	image_options_sync();

	if (refresh)
		{
		filter_rebuild();
		layout_refresh(NULL);
		}

	if (accel_store) gtk_tree_model_foreach(GTK_TREE_MODEL(accel_store), accel_apply_cb, NULL);
}

/*
 *-----------------------------------------------------------------------------
 * config window main button callbacks (private)
 *-----------------------------------------------------------------------------
 */

static void config_window_close_cb(GtkWidget *widget, gpointer data)
{
	gtk_widget_destroy(configwindow);
	configwindow = NULL;
	filter_store = NULL;
}

static gboolean config_window_delete(GtkWidget *widget, GdkEventAny *event, gpointer data)
{
	config_window_close_cb(NULL, NULL);
	return TRUE;
}

static void config_window_ok_cb(GtkWidget *widget, gpointer data)
{
	config_window_apply();
	config_window_close_cb(NULL, NULL);
}

static void config_window_apply_cb(GtkWidget *widget, gpointer data)
{
	config_window_apply();
}

static void config_window_save_cb(GtkWidget *widget, gpointer data)
{
	config_window_apply();
	save_options(options);
}

/*
 *-----------------------------------------------------------------------------
 * config window setup (private)
 *-----------------------------------------------------------------------------
 */

static void quality_menu_cb(GtkWidget *combo, gpointer data)
{
	gint *option = data;

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
		case 3:
			*option = GDK_INTERP_HYPER;
			break;
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
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Bilinear"));
	if (option == GDK_INTERP_BILINEAR) current = 2;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Hyper (best, but slowest)"));
	if (option == GDK_INTERP_HYPER) current = 3;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(quality_menu_cb), option_c);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show(combo);
}

static void thumb_size_menu_cb(GtkWidget *combo, gpointer data)
{
	gint n;

	n = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	if (n < 0) return;

	if ((guint) n < sizeof(thumb_size_list) / sizeof(ThumbSize))
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
	for (i = 0; (guint) i < sizeof(thumb_size_list) / sizeof(ThumbSize); i++)
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

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show(combo);
}

static void stereo_mode_menu_cb(GtkWidget *combo, gpointer data)
{
	gint *option = data;

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

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show(combo);
}

static void filter_store_populate(void)
{
	GList *work;

	if (!filter_store) return;

	gtk_list_store_clear(filter_store);

	work = filter_get_list();
	while (work)
		{
		FilterEntry *fe;
		GtkTreeIter iter;

		fe = work->data;
		work = work->next;

		gtk_list_store_append(filter_store, &iter);
		gtk_list_store_set(filter_store, &iter, 0, fe, -1);
		}
}

static void filter_store_ext_edit_cb(GtkCellRendererText *cell, gchar *path_str,
				     gchar *new_text, gpointer data)
{
	GtkWidget *model = data;
	FilterEntry *fe = data;
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

static void filter_store_class_edit_cb(GtkCellRendererText *cell, gchar *path_str,
				       gchar *new_text, gpointer data)
{
	GtkWidget *model = data;
	FilterEntry *fe = data;
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
			fe->file_class = i;
			break;
			}
		}

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_store_desc_edit_cb(GtkCellRendererText *cell, gchar *path_str,
				      gchar *new_text, gpointer data)
{
	GtkWidget *model = data;
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

static void filter_store_enable_cb(GtkCellRendererToggle *renderer,
				   gchar *path_str, gpointer data)
{
	GtkWidget *model = data;
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

static void filter_store_writable_cb(GtkCellRendererToggle *renderer,
				     gchar *path_str, gpointer data)
{
	GtkWidget *model = data;
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

static void filter_store_sidecar_cb(GtkCellRendererToggle *renderer,
				    gchar *path_str, gpointer data)
{
	GtkWidget *model = data;
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

static void filter_set_func(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
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
	GList *list_columns;
	const gchar *title;
	guint i = 0;
	gint rows;

	rows = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(filter_store), NULL);
	path = gtk_tree_path_new_from_indices(rows-1, -1);

	list_columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(data));
	do {
		column = g_list_nth(list_columns,i)->data;
		title = gtk_tree_view_column_get_title(GTK_TREE_VIEW_COLUMN(column));
		i++;
		} while (strcmp(title, "Filter") !=0 );

	list_cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
	cell = g_list_last(list_cells)->data;

	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(data),
								path, column, FALSE, 0.0, 0.0 );
	gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(data),
								path, column, cell, TRUE);

	gtk_tree_path_free(path);
	g_list_free(list_cells);
	g_list_free(list_columns);

	return(FALSE);
}

static void filter_add_cb(GtkWidget *widget, gpointer data)
{
	filter_add_unique("description", ".new", FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
	filter_store_populate();

	g_idle_add((GSourceFunc)filter_add_scroll, data);
}

static void filter_remove_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *filter_view = data;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	FilterEntry *fe;

	if (!filter_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(filter_view));
	if (!gtk_tree_selection_get_selected(selection, NULL, &iter)) return;
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

	return(FALSE);
}

static void filter_default_ok_cb(GenericDialog *gd, gpointer data)
{
	filter_reset();
	filter_add_defaults();
	filter_rebuild();
	filter_store_populate();

	g_idle_add((GSourceFunc)filter_default_ok_scroll, gd->data);
}

static void dummy_cancel_cb(GenericDialog *gd, gpointer data)
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
				   _("This will reset the file filters to the defaults.\nContinue?"));
	generic_dialog_add_button(gd, GTK_STOCK_OK, NULL, filter_default_ok_cb, TRUE);
	gtk_widget_show(gd->dialog);
}

static void filter_disable_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *frame = data;

	gtk_widget_set_sensitive(frame,
				 !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

static void safe_delete_view_cb(GtkWidget *widget, gpointer data)
{
	layout_set_path(NULL, gtk_entry_get_text(GTK_ENTRY(safe_delete_path_entry)));
}

static void safe_delete_clear_ok_cb(GenericDialog *gd, gpointer data)
{
	file_util_trash_clear();
}

static void safe_delete_clear_cb(GtkWidget *widget, gpointer data)
{
	GenericDialog *gd;
	GtkWidget *entry;
	gd = generic_dialog_new(_("Clear trash"),
				"clear_trash", widget, TRUE,
				dummy_cancel_cb, NULL);
	generic_dialog_add_message(gd, GTK_STOCK_DIALOG_QUESTION, _("Clear trash"),
				    _("This will remove the trash contents."));
	generic_dialog_add_button(gd, GTK_STOCK_OK, NULL, safe_delete_clear_ok_cb, TRUE);
	entry = gtk_entry_new();
	gtk_widget_set_can_focus(entry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
	if (options->file_ops.safe_delete_path) gtk_entry_set_text(GTK_ENTRY(entry), options->file_ops.safe_delete_path);
	gtk_box_pack_start(GTK_BOX(gd->vbox), entry, FALSE, FALSE, 0);
	gtk_widget_show(entry);
	gtk_widget_show(gd->dialog);
}

static void image_overlay_template_view_changed_cb(GtkWidget *widget, gpointer data)
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

static void image_overlay_default_template_ok_cb(GenericDialog *gd, gpointer data)
{
	GtkTextView *text_view = data;
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
				   _("This will reset the image overlay template string to the default.\nContinue?"));
	generic_dialog_add_button(gd, GTK_STOCK_OK, NULL, image_overlay_default_template_ok_cb, TRUE);
	gtk_widget_show(gd->dialog);
}

static void image_overlay_help_cb(GtkWidget *widget, gpointer data)
{
	help_window_show("overlay");
}

static void image_overlay_set_font_cb(GtkWidget *widget, gpointer data)
{
#if GTK_CHECK_VERSION(3,4,0)
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
#else
	const char *font;

	font = gtk_font_button_get_font_name(GTK_FONT_BUTTON(widget));
	c_options->image_overlay.font = g_strdup(font);
#endif
}

static void image_overlay_set_text_colour_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
#if GTK_CHECK_VERSION(3,4,0)
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
#else
	GdkColor colour;
	GtkColorSelection *colorsel;

	dialog = gtk_color_selection_dialog_new("Image Overlay Text Colour");
	gtk_window_set_keep_above(GTK_WINDOW(dialog),TRUE);
	colour.red = options->image_overlay.text_red*257;
	colour.green = options->image_overlay.text_green*257;
	colour.blue = options->image_overlay.text_blue*257;
	colorsel = GTK_COLOR_SELECTION(gtk_color_selection_dialog_get_color_selection(GTK_COLOR_SELECTION_DIALOG(dialog)));
	gtk_color_selection_set_has_opacity_control(colorsel, TRUE);
	gtk_color_selection_set_current_color(colorsel, &colour);
	gtk_color_selection_set_current_alpha(colorsel, options->image_overlay.text_alpha*257);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
		{
		gtk_color_selection_get_current_color(colorsel, &colour);
		c_options->image_overlay.text_red = colour.red/257;
		c_options->image_overlay.text_green = colour.green/257;
		c_options->image_overlay.text_blue = colour.blue/257;
		c_options->image_overlay.text_alpha = gtk_color_selection_get_current_alpha(colorsel)/257;
		}
	gtk_widget_destroy (dialog);
#endif
}


static void image_overlay_set_background_colour_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
#if GTK_CHECK_VERSION(3,4,0)
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
#else
	GdkColor colour;
	GtkColorSelection *colorsel;

	dialog = gtk_color_selection_dialog_new("Image Overlay Background Colour");
	gtk_window_set_keep_above(GTK_WINDOW(dialog),TRUE);
	colour.red = options->image_overlay.background_red*257;
	colour.green = options->image_overlay.background_green*257;
	colour.blue = options->image_overlay.background_blue*257;
	colorsel = GTK_COLOR_SELECTION(gtk_color_selection_dialog_get_color_selection(GTK_COLOR_SELECTION_DIALOG(dialog)));
	gtk_color_selection_set_has_opacity_control(colorsel, TRUE);
	gtk_color_selection_set_current_color(colorsel, &colour);
	gtk_color_selection_set_current_alpha(colorsel, options->image_overlay.background_alpha*257);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
		{
		gtk_color_selection_get_current_color(colorsel, &colour);
		c_options->image_overlay.background_red = colour.red/257;
		c_options->image_overlay.background_green = colour.green/257;
		c_options->image_overlay.background_blue = colour.blue/257;
		c_options->image_overlay.background_alpha = gtk_color_selection_get_current_alpha(colorsel)/257;
		}
	gtk_widget_destroy(dialog);
#endif
}

static void accel_store_populate(void)
{
	LayoutWindow *lw;
	GList *groups, *actions;
	GtkAction *action;
	const gchar *accel_path;
	GtkAccelKey key;
	GtkTreeIter iter;

	if (!accel_store || !layout_window_list || !layout_window_list->data) return;

	gtk_tree_store_clear(accel_store);
	lw = layout_window_list->data; /* get the actions from the first window, it should not matter, they should be the same in all windows */

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

				if (pango_parse_markup(label, -1, '_', NULL, &label2, NULL, NULL) && label2)
					{
					g_free(label);
					label = label2;
					}

				accel = gtk_accelerator_name(key.accel_key, key.accel_mods);

				if (tooltip)
					{
					gtk_tree_store_append(accel_store, &iter, NULL);
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

static void accel_store_cleared_cb(GtkCellRendererAccel *accel, gchar *path_string, gpointer user_data)
{

}

static gboolean accel_remove_key_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	gchar *accel1 = data;
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


static void accel_store_edited_cb(GtkCellRendererAccel *accel, gchar *path_string, guint accel_key, GdkModifierType accel_mods, guint hardware_keycode, gpointer user_data)
{
	GtkTreeModel *model = (GtkTreeModel *)accel_store;
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

	return(FALSE);
}

static void accel_default_cb(GtkWidget *widget, gpointer data)
{
	accel_store_populate();

	g_idle_add((GSourceFunc)accel_default_scroll, data);
}

void accel_remove_selection(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	gtk_tree_store_set(accel_store, iter, AE_KEY, "", -1);
}

void accel_reset_selection(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
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

static void accel_reset_cb(GtkWidget *widget, gpointer data)
{
	GtkTreeSelection *selection;

	if (!accel_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
	gtk_tree_selection_selected_foreach(selection, &accel_reset_selection, NULL);
}



static GtkWidget *scrolled_notebook_page(GtkWidget *notebook, const gchar *title)
{
	GtkWidget *label;
	GtkWidget *vbox;
	GtkWidget *scrolled;
	GtkWidget *viewport;

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled), PREF_PAD_BORDER);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	label = gtk_label_new(title);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, label);
	gtk_widget_show(scrolled);

	viewport = gtk_viewport_new(NULL, NULL);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), viewport);
	gtk_widget_show(viewport);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(viewport), vbox);
	gtk_widget_show(vbox);

	return vbox;
}

/* general options tab */
static void config_tab_general(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *subgroup;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *table;
	GtkWidget *spin;

	vbox = scrolled_notebook_page(notebook, _("General"));

	group = pref_group_new(vbox, FALSE, _("Thumbnails"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 2, FALSE, FALSE);
	add_thumb_size_menu(table, 0, 0, _("Size:"));
	add_quality_menu(table, 0, 1, _("Quality:"), options->thumbnails.quality, &c_options->thumbnails.quality);

	ct_button = pref_checkbox_new_int(group, _("Cache thumbnails"),
					  options->thumbnails.enable_caching, &c_options->thumbnails.enable_caching);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_checkbox_link_sensitivity(ct_button, subgroup);

	button = pref_checkbox_new_int(subgroup, _("Use standard thumbnail cache, shared with other applications"),
				       options->thumbnails.spec_standard, &c_options->thumbnails.spec_standard);

	subgroup = pref_box_new(subgroup, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_checkbox_link_sensitivity_swap(button, subgroup);

	pref_checkbox_new_int(subgroup, _("Store thumbnails in '.thumbnails' folder, local to image folder (non-standard)"),
			      options->thumbnails.cache_into_dirs, &c_options->thumbnails.cache_into_dirs);

	pref_checkbox_new_int(group, _("Use EXIF thumbnails when available (EXIF thumbnails may be outdated)"),
			      options->thumbnails.use_exif, &c_options->thumbnails.use_exif);

	group = pref_group_new(vbox, FALSE, _("Slide show"), GTK_ORIENTATION_VERTICAL);

	c_options->slideshow.delay = options->slideshow.delay;
	spin = pref_spin_new(group, _("Delay between image change:"), _("seconds"),
			     SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS, 1.0, 1,
			     options->slideshow.delay ? (gdouble)options->slideshow.delay / SLIDESHOW_SUBSECOND_PRECISION : 10.0,
			     G_CALLBACK(slideshow_delay_cb), NULL);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);

	pref_checkbox_new_int(group, _("Random"), options->slideshow.random, &c_options->slideshow.random);
	pref_checkbox_new_int(group, _("Repeat"), options->slideshow.repeat, &c_options->slideshow.repeat);

	group = pref_group_new(vbox, FALSE, _("Image loading and caching"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new_int(group, _("Decoded image cache size (Mb):"), NULL,
			  0, 1024, 1, options->image.image_cache_max, &c_options->image.image_cache_max);
	pref_checkbox_new_int(group, _("Preload next image"),
			      options->image.enable_read_ahead, &c_options->image.enable_read_ahead);

	pref_checkbox_new_int(group, _("Refresh on file change"),
			      options->update_on_time_change, &c_options->update_on_time_change);
}

/* image tab */
static void config_tab_image(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *vbox2;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *table;
	GtkWidget *spin;

	vbox = scrolled_notebook_page(notebook, _("Image"));

	group = pref_group_new(vbox, FALSE, _("Zoom"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_quality_menu(table, 0, 0, _("Quality:"), options->image.zoom_quality, &c_options->image.zoom_quality);

#ifdef HAVE_CLUTTER
	pref_checkbox_new_int(group, _("Use GPU acceleration via Clutter library"),
			      options->image.use_clutter_renderer, &c_options->image.use_clutter_renderer);
#endif

	pref_checkbox_new_int(group, _("Two pass rendering (apply HQ zoom and color correction in second pass)"),
			      options->image.zoom_2pass, &c_options->image.zoom_2pass);

	pref_checkbox_new_int(group, _("Allow enlargement of image for zoom to fit"),
			      options->image.zoom_to_fit_allow_expand, &c_options->image.zoom_to_fit_allow_expand);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	ct_button = pref_checkbox_new_int(hbox, _("Limit image size when autofitting (%):"),
					  options->image.limit_autofit_size, &c_options->image.limit_autofit_size);
	spin = pref_spin_new_int(hbox, NULL, NULL,
				 10, 150, 1,
				 options->image.max_autofit_size, &c_options->image.max_autofit_size);
	pref_checkbox_link_sensitivity(ct_button, spin);

	c_options->image.zoom_increment = options->image.zoom_increment;
	spin = pref_spin_new(group, _("Zoom increment:"), NULL,
			     0.01, 4.0, 0.01, 2, (gdouble)options->image.zoom_increment / 100.0,
			     G_CALLBACK(zoom_increment_cb), NULL);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);

	group = pref_group_new(vbox, FALSE, _("When new image is selected:"), GTK_ORIENTATION_HORIZONTAL);

	vbox2 = pref_box_new(group, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	c_options->image.zoom_mode = options->image.zoom_mode;
	button = pref_radiobutton_new(vbox2, NULL, _("Zoom to original size"),
				      (options->image.zoom_mode == ZOOM_RESET_ORIGINAL),
				      G_CALLBACK(zoom_mode_cb), GINT_TO_POINTER(ZOOM_RESET_ORIGINAL));
	button = pref_radiobutton_new(vbox2, button, _("Fit image to window"),
				      (options->image.zoom_mode == ZOOM_RESET_FIT_WINDOW),
				      G_CALLBACK(zoom_mode_cb), GINT_TO_POINTER(ZOOM_RESET_FIT_WINDOW));
	button = pref_radiobutton_new(vbox2, button, _("Leave Zoom at previous setting"),
				      (options->image.zoom_mode == ZOOM_RESET_NONE),
				      G_CALLBACK(zoom_mode_cb), GINT_TO_POINTER(ZOOM_RESET_NONE));

	vbox2 = pref_box_new(group, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	c_options->image.scroll_reset_method = options->image.scroll_reset_method;
	button = pref_radiobutton_new(vbox2, NULL, _("Scroll to top left corner"),
				      (options->image.scroll_reset_method == SCROLL_RESET_TOPLEFT),
				      G_CALLBACK(scroll_reset_cb), GINT_TO_POINTER(SCROLL_RESET_TOPLEFT));
	button = pref_radiobutton_new(vbox2, button, _("Scroll to image center"),
				      (options->image.scroll_reset_method == SCROLL_RESET_CENTER),
				      G_CALLBACK(scroll_reset_cb), GINT_TO_POINTER(SCROLL_RESET_CENTER));
	button = pref_radiobutton_new(vbox2, button, _("Keep the region from previous image"),
				      (options->image.scroll_reset_method == SCROLL_RESET_NOCHANGE),
				      G_CALLBACK(scroll_reset_cb), GINT_TO_POINTER(SCROLL_RESET_NOCHANGE));


	group = pref_group_new(vbox, FALSE, _("Appearance"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Use custom border color in window mode"),
			      options->image.use_custom_border_color, &c_options->image.use_custom_border_color);

	pref_checkbox_new_int(group, _("Use custom border color in fullscreen mode"),
			      options->image.use_custom_border_color_in_fullscreen, &c_options->image.use_custom_border_color_in_fullscreen);

	pref_color_button_new(group, _("Border color"), &options->image.border_color,
			      G_CALLBACK(pref_color_button_set_cb), &c_options->image.border_color);

	group = pref_group_new(vbox, FALSE, _("Convenience"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Auto rotate image using Exif information"),
			      options->image.exif_rotate_enable, &c_options->image.exif_rotate_enable);

	pref_checkbox_new_int(group, _("Auto rotate proofs using Exif information"),
			      options->image.exif_proof_rotate_enable, &c_options->image.exif_proof_rotate_enable);
}

/* windows tab */
static void config_tab_windows(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *spin;
	GtkWidget *image_overlay_template_view;
	GtkWidget *scrolled;
	GtkTextBuffer *buffer;

	vbox = scrolled_notebook_page(notebook, _("Windows"));

	group = pref_group_new(vbox, FALSE, _("State"), GTK_ORIENTATION_VERTICAL);

	ct_button = pref_checkbox_new_int(group, _("Remember window positions"),
					  options->save_window_positions, &c_options->save_window_positions);

	button = pref_checkbox_new_int(group, _("Use saved window positions also for new windows"),
				       options->use_saved_window_positions_for_new_windows, &c_options->use_saved_window_positions_for_new_windows);
	pref_checkbox_link_sensitivity(ct_button, button);

	pref_checkbox_new_int(group, _("Remember tool state (float/hidden)"),
			      options->tools_restore_state, &c_options->tools_restore_state);

	group = pref_group_new(vbox, FALSE, _("Size"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Fit window to image when tools are hidden/floating"),
			      options->image.fit_window_to_image, &c_options->image.fit_window_to_image);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	ct_button = pref_checkbox_new_int(hbox, _("Limit size when auto-sizing window (%):"),
					  options->image.limit_window_size, &c_options->image.limit_window_size);
	spin = pref_spin_new_int(hbox, NULL, NULL,
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


	group = pref_group_new(vbox, FALSE, _("Overlay Screen Display"), GTK_ORIENTATION_VERTICAL);

	pref_label_new(group, _("Image overlay template"));

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(scrolled, 200, 150);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	image_overlay_template_view = gtk_text_view_new();

	gtk_widget_set_tooltip_markup(image_overlay_template_view,
	_("<i>%name%</i> results in the filename of the picture.\n"
	  "Also available: <i>%collection%</i>, <i>%number%</i>, <i>%total%</i>, <i>%date%</i>,\n"
	  "<i>%size%</i> (filesize), <i>%width%</i>, <i>%height%</i>, <i>%res%</i> (resolution)\n"
	  "To access exif data use the exif name, e. g. <i>%formatted.Camera%</i> is the formatted camera name,\n"
	  "<i>%Exif.Photo.DateTimeOriginal%</i> the date of the original shot.\n"
	  "<i>%formatted.Camera:20</i> notation will truncate the displayed data to 20 characters and will add 3 dots at the end to denote the truncation.\n"
	  "If two or more variables are connected with the |-sign, it prints available variables with a separator.\n"
	  "<i>%formatted.ShutterSpeed%</i>|<i>%formatted.ISOSpeedRating%</i>|<i>%formatted.FocalLength%</i> could show \"1/20s - 400 - 80 mm\" or \"1/200 - 80 mm\",\n"
	  "if there's no ISO information in the Exif data.\n"
	  "If a line is empty, it is removed. This allows one to add lines that totally disappear when no data is available.\n"
	));
	gtk_container_add(GTK_CONTAINER(scrolled), image_overlay_template_view);
	gtk_widget_show(image_overlay_template_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

#if GTK_CHECK_VERSION(3,4,0)
	button = pref_button_new(NULL, GTK_STOCK_SELECT_FONT, _("Font"), FALSE,
				 G_CALLBACK(image_overlay_set_font_cb), notebook);
#else
	button = gtk_font_button_new();
	gtk_font_button_set_title(GTK_FONT_BUTTON(button), "Image Overlay Font");
	gtk_font_button_set_font_name(GTK_FONT_BUTTON(button), options->image_overlay.font);
	g_signal_connect(G_OBJECT(button), "font-set",
				 G_CALLBACK(image_overlay_set_font_cb),NULL);
#endif
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_COLOR_PICKER, _("Text"), FALSE,
				 G_CALLBACK(image_overlay_set_text_colour_cb), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_COLOR_PICKER, _("Background"), FALSE,
				 G_CALLBACK(image_overlay_set_background_colour_cb), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	image_overlay_set_text_colours();

	button = pref_button_new(NULL, NULL, _("Defaults"), FALSE,
				 G_CALLBACK(image_overlay_default_template_cb), image_overlay_template_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_HELP, NULL, FALSE,
				 G_CALLBACK(image_overlay_help_cb), NULL);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(image_overlay_template_view));
	if (options->image_overlay.template_string) gtk_text_buffer_set_text(buffer, options->image_overlay.template_string, -1);
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(image_overlay_template_view_changed_cb), image_overlay_template_view);


}

static GtkTreeModel *create_class_model(void)
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

	vbox = scrolled_notebook_page(notebook, _("Files"));

	group = pref_box_new(vbox, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	pref_checkbox_new_int(group, _("Show hidden files or folders"),
			      options->file_filter.show_hidden_files, &c_options->file_filter.show_hidden_files);
	pref_checkbox_new_int(group, _("Show parent folder (..)"),
			      options->file_filter.show_parent_directory, &c_options->file_filter.show_parent_directory);
	pref_checkbox_new_int(group, _("Case sensitive sort"),
			      options->file_sort.case_sensitive, &c_options->file_sort.case_sensitive);
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

	scrolled = gtk_scrolled_window_new(NULL, NULL);
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
	gtk_tree_view_column_set_title(column, _("Filter"));
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(filter_store_enable_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_ENABLE), NULL);

	renderer = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_ext_edit_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	g_object_set(G_OBJECT(renderer), "editable", (gboolean)TRUE, NULL);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_EXTENSION), NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Description"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_fixed_width(column, 200);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

	renderer = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_desc_edit_cb), filter_store);
	g_object_set(G_OBJECT(renderer), "editable", (gboolean)TRUE, NULL);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_DESCRIPTION), NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Class"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	renderer = gtk_cell_renderer_combo_new();
	g_object_set(G_OBJECT(renderer), "editable", (gboolean)TRUE,
					 "model", create_class_model(),
					 "text-column", 0,
					 "has-entry", FALSE,
					 NULL);

	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_class_edit_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_CLASS), NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Writable"));
	gtk_tree_view_column_set_resizable(column, FALSE);
	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(filter_store_writable_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_WRITABLE), NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Sidecar is allowed"));
	gtk_tree_view_column_set_resizable(column, FALSE);
	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(filter_store_sidecar_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_ALLOW_SIDECAR), NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);


	filter_store_populate();
	gtk_container_add(GTK_CONTAINER(scrolled), filter_view);
	gtk_widget_show(filter_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(NULL, NULL, _("Defaults"), FALSE,
				 G_CALLBACK(filter_default_cb), filter_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_REMOVE, NULL, FALSE,
				 G_CALLBACK(filter_remove_cb), filter_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_ADD, NULL, FALSE,
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
	gchar *text;

	vbox = scrolled_notebook_page(notebook, _("Metadata"));


	group = pref_group_new(vbox, FALSE, _("Metadata writing process"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_EXIV2
	label = pref_label_new(group, _("Warning: Geeqie is built without Exiv2. Some options are disabled."));
#endif
	label = pref_label_new(group, _("Metadata are written in the following order. The process ends after first success."));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

	ct_button = pref_checkbox_new_int(group, _("1) Save metadata in image files, resp. sidecar files, according to the XMP standard"),
			      options->metadata.save_in_image_file, &c_options->metadata.save_in_image_file);
#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(ct_button, FALSE);
#endif

	pref_checkbox_new_int(group, _("2) Save metadata in '.metadata' folder, local to image folder (non-standard)"),
			      options->metadata.enable_metadata_dirs, &c_options->metadata.enable_metadata_dirs);

	text = g_strdup_printf(_("3) Save metadata in Geeqie private directory '%s'"), get_metadata_cache_dir());
	label = pref_label_new(group, text);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_misc_set_padding(GTK_MISC(label), 22, 0);
	g_free(text);

	group = pref_group_new(vbox, FALSE, _("Step 1: Write to image files"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(group, FALSE);
#endif

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_checkbox_new_int(hbox, _("Store metadata also in legacy IPTC tags (converted according to IPTC4XMP standard)"),
			      options->metadata.save_legacy_IPTC, &c_options->metadata.save_legacy_IPTC);

	pref_checkbox_new_int(hbox, _("Warn if the image files are unwritable"),
			      options->metadata.warn_on_write_problems, &c_options->metadata.warn_on_write_problems);

	pref_checkbox_new_int(hbox, _("Ask before writing to image files"),
			      options->metadata.confirm_write, &c_options->metadata.confirm_write);

	group = pref_group_new(vbox, FALSE, _("Step 2 and 3: write to Geeqie private files"), GTK_ORIENTATION_VERTICAL);
#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(group, FALSE);
#endif

	pref_checkbox_new_int(group, _("Use GQview legacy metadata format (supports only keywords and comments) instead of XMP"),
			      options->metadata.save_legacy_format, &c_options->metadata.save_legacy_format);


	group = pref_group_new(vbox, FALSE, _("Miscellaneous"), GTK_ORIENTATION_VERTICAL);
	pref_checkbox_new_int(group, _("Write the same description tags (keywords, comment, etc.) to all grouped sidecars"),
			      options->metadata.sync_grouped_files, &c_options->metadata.sync_grouped_files);

	pref_checkbox_new_int(group, _("Allow keywords to differ only in case"),
			      options->metadata.keywords_case_sensitive, &c_options->metadata.keywords_case_sensitive);

	ct_button = pref_checkbox_new_int(group, _("Write altered image orientation to the metadata"),
			      options->metadata.write_orientation, &c_options->metadata.write_orientation);
#ifndef HAVE_EXIV2
	gtk_widget_set_sensitive(ct_button, FALSE);
#endif

	group = pref_group_new(vbox, FALSE, _("Auto-save options"), GTK_ORIENTATION_VERTICAL);

	ct_button = pref_checkbox_new_int(group, _("Write metadata after timeout"),
			      options->metadata.confirm_after_timeout, &c_options->metadata.confirm_after_timeout);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spin_new_int(hbox, _("Timeout (seconds):"), NULL, 0, 900, 1,
			      options->metadata.confirm_timeout, &c_options->metadata.confirm_timeout);

	pref_checkbox_new_int(group, _("Write metadata on image change"),
			      options->metadata.confirm_on_image_change, &c_options->metadata.confirm_on_image_change);

	pref_checkbox_new_int(group, _("Write metadata on directory change"),
			      options->metadata.confirm_on_dir_change, &c_options->metadata.confirm_on_dir_change);
}

/* metadata tab */
#ifdef HAVE_LCMS
static void intent_menu_cb(GtkWidget *combo, gpointer data)
{
	gint *option = data;

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
			     guint option, guint *option_c)
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

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, 0, 0, 0);
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
				 GTK_FILL | GTK_EXPAND, 0, 0, 0);
		gtk_widget_show(entry);
		color_profile_input_name_entry[i] = entry;

		tabcomp = tab_completion_new(&entry, options->color_profile.input_file[i], NULL, NULL);
		tab_completion_add_select_button(entry, _("Select color profile"), FALSE);
		gtk_widget_set_size_request(entry, 160, -1);
		gtk_table_attach(GTK_TABLE(table), tabcomp, 2, 3, i + 1, i + 2,
				 GTK_FILL | GTK_EXPAND, 0, 0, 0);
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
				     options->color_profile.screen_file, NULL, NULL);
	tab_completion_add_select_button(color_profile_screen_file_entry, _("Select color profile"), FALSE);
	gtk_widget_set_size_request(color_profile_screen_file_entry, 160, -1);
#ifdef HAVE_LCMS
	add_intent_menu(table, 0, 1, _("Render Intent:"), options->color_profile.render_intent, &c_options->color_profile.render_intent);
#endif
	gtk_table_attach(GTK_TABLE(table), tabcomp, 1, 2,
			 0, 1,
			 GTK_FILL | GTK_EXPAND, 0, 0, 0);

	gtk_widget_show(tabcomp);
}

/* advanced entry tab */
static void config_tab_behavior(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *tabcomp;
	GtkWidget *ct_button;
	GtkWidget *spin;

	vbox = scrolled_notebook_page(notebook, _("Behavior"));

	group = pref_group_new(vbox, FALSE, _("Delete"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Confirm file delete"),
			      options->file_ops.confirm_delete, &c_options->file_ops.confirm_delete);
	pref_checkbox_new_int(group, _("Enable Delete key"),
			      options->file_ops.enable_delete_key, &c_options->file_ops.enable_delete_key);

	ct_button = pref_checkbox_new_int(group, _("Safe delete"),
					  options->file_ops.safe_delete_enable, &c_options->file_ops.safe_delete_enable);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spacer(hbox, PREF_PAD_INDENT - PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	tabcomp = tab_completion_new(&safe_delete_path_entry, options->file_ops.safe_delete_path, NULL, NULL);
	tab_completion_add_select_button(safe_delete_path_entry, NULL, TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spacer(hbox, PREF_PAD_INDENT - PREF_PAD_GAP);
	spin = pref_spin_new_int(hbox, _("Maximum size:"), _("MB"),
				 0, 2048, 1, options->file_ops.safe_delete_folder_maxsize, &c_options->file_ops.safe_delete_folder_maxsize);
	gtk_widget_set_tooltip_markup(spin, _("Set to 0 for unlimited size"));
	button = pref_button_new(NULL, NULL, _("View"), FALSE,
				 G_CALLBACK(safe_delete_view_cb), NULL);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_CLEAR, NULL, FALSE,
				 G_CALLBACK(safe_delete_clear_cb), NULL);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);


	group = pref_group_new(vbox, FALSE, _("Behavior"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Rectangular selection in icon view"),
			      options->collections.rectangular_selection, &c_options->collections.rectangular_selection);

	pref_checkbox_new_int(group, _("Descend folders in tree view"),
			      options->tree_descend_subdirs, &c_options->tree_descend_subdirs);

	pref_checkbox_new_int(group, _("In place renaming"),
			      options->file_ops.enable_in_place_rename, &c_options->file_ops.enable_in_place_rename);

	pref_checkbox_new_int(group, _("List directory view uses single click to enter"),
			      options->view_dir_list_single_click_enter, &c_options->view_dir_list_single_click_enter);

	pref_spin_new_int(group, _("Open recent list maximum size"), NULL,
			  1, 50, 1, options->open_recent_list_maxsize, &c_options->open_recent_list_maxsize);

	pref_spin_new_int(group, _("Drag'n drop icon size"), NULL,
			  16, 256, 16, options->dnd_icon_size, &c_options->dnd_icon_size);

	group = pref_group_new(vbox, FALSE, _("Navigation"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Progressive keyboard scrolling"),
			      options->progressive_key_scrolling, &c_options->progressive_key_scrolling);
	pref_spin_new_int(group, _("Keyboard scrolling step multiplier:"), NULL,
			  1, 32, 1, options->keyboard_scroll_step, (int *)&c_options->keyboard_scroll_step);
	pref_checkbox_new_int(group, _("Mouse wheel scrolls image"),
			      options->mousewheel_scrolls, &c_options->mousewheel_scrolls);
	pref_checkbox_new_int(group, _("Navigation by left or middle click on image"),
			      options->image_lm_click_nav, &c_options->image_lm_click_nav);

	group = pref_group_new(vbox, FALSE, _("Similarities"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new_int(group, _("Custom similarity threshold:"), NULL,
			  0, 100, 1, options->duplicates_similarity_threshold, (int *)&c_options->duplicates_similarity_threshold);
	pref_checkbox_new_int(group, _("Rotation invariant duplicate check"),
			      options->rot_invariant_sim, &c_options->rot_invariant_sim);

#ifdef DEBUG
	group = pref_group_new(vbox, FALSE, _("Debugging"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new_int(group, _("Debug level:"), NULL,
			  DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX, 1, get_debug_level(), &debug_c);
#endif
}

/* accelerators tab */
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

	scrolled = gtk_scrolled_window_new(NULL, NULL);
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

	accel_store_populate();
	gtk_container_add(GTK_CONTAINER(scrolled), accel_view);
	gtk_widget_show(accel_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(NULL, NULL, _("Defaults"), FALSE,
				 G_CALLBACK(accel_default_cb), accel_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(NULL, NULL, _("Reset selected"), FALSE,
				 G_CALLBACK(accel_reset_cb), accel_view);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
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
	box = pref_table_box(table, 0, 0, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_checkbox_new_int(box, _("Mirror left image"),
			      options->stereo.mode & PR_STEREO_MIRROR_LEFT, &c_options->stereo.tmp.mirror_left);
	box = pref_table_box(table, 1, 0, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_checkbox_new_int(box, _("Flip left image"),
			      options->stereo.mode & PR_STEREO_FLIP_LEFT, &c_options->stereo.tmp.flip_left);
	box = pref_table_box(table, 0, 1, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_checkbox_new_int(box, _("Mirror right image"),
			      options->stereo.mode & PR_STEREO_MIRROR_RIGHT, &c_options->stereo.tmp.mirror_right);
	box = pref_table_box(table, 1, 1, GTK_ORIENTATION_HORIZONTAL, NULL);
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
	box = pref_table_box(table, 0, 0, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_checkbox_new_int(box, _("Mirror left image"),
			      options->stereo.fsmode & PR_STEREO_MIRROR_LEFT, &c_options->stereo.tmp.fs_mirror_left);
	box = pref_table_box(table, 1, 0, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_checkbox_new_int(box, _("Flip left image"),
			      options->stereo.fsmode & PR_STEREO_FLIP_LEFT, &c_options->stereo.tmp.fs_flip_left);
	box = pref_table_box(table, 0, 1, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_checkbox_new_int(box, _("Mirror right image"),
			      options->stereo.fsmode & PR_STEREO_MIRROR_RIGHT, &c_options->stereo.tmp.fs_mirror_right);
	box = pref_table_box(table, 1, 1, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_checkbox_new_int(box, _("Flip right image"),
			      options->stereo.fsmode & PR_STEREO_FLIP_RIGHT, &c_options->stereo.tmp.fs_flip_right);
	pref_checkbox_new_int(box2, _("Swap left and right images"),
			      options->stereo.fsmode & PR_STEREO_SWAP, &c_options->stereo.tmp.fs_swap);
	pref_checkbox_new_int(box2, _("Disable stereo mode on single image source"),
			      options->stereo.fsmode & PR_STEREO_TEMP_DISABLE, &c_options->stereo.tmp.fs_temp_disable);

	group2 = pref_group_new(box2, FALSE, _("Fixed position"), GTK_ORIENTATION_VERTICAL);
	table = pref_table_new(group2, 5, 3, FALSE, FALSE);
	pref_table_spin_new_int(table, 0, 0, _("Width"), NULL,
			  1, 5000, 1, options->stereo.fixed_w, &c_options->stereo.fixed_w);
	pref_table_spin_new_int(table, 3, 0,  _("Height"), NULL,
			  1, 5000, 1, options->stereo.fixed_h, &c_options->stereo.fixed_h);
	pref_table_spin_new_int(table, 0, 1,  _("Left X"), NULL,
			  0, 5000, 1, options->stereo.fixed_x1, &c_options->stereo.fixed_x1);
	pref_table_spin_new_int(table, 3, 1,  _("Left Y"), NULL,
			  0, 5000, 1, options->stereo.fixed_y1, &c_options->stereo.fixed_y1);
	pref_table_spin_new_int(table, 0, 2,  _("Right X"), NULL,
			  0, 5000, 1, options->stereo.fixed_x2, &c_options->stereo.fixed_x2);
	pref_table_spin_new_int(table, 3, 2,  _("Right Y"), NULL,
			  0, 5000, 1, options->stereo.fixed_y2, &c_options->stereo.fixed_y2);

}

/* Main preferences window */
static void config_window_create(void)
{
	GtkWidget *win_vbox;
	GtkWidget *hbox;
	GtkWidget *notebook;
	GtkWidget *button;
	GtkWidget *ct_button;

	if (!c_options) c_options = init_options(NULL);

	configwindow = window_new(GTK_WINDOW_TOPLEVEL, "preferences", PIXBUF_INLINE_ICON_CONFIG, NULL, _("Preferences"));
	gtk_window_set_type_hint(GTK_WINDOW(configwindow), GDK_WINDOW_TYPE_HINT_DIALOG);
	g_signal_connect(G_OBJECT(configwindow), "delete_event",
			 G_CALLBACK(config_window_delete), NULL);
	gtk_window_set_default_size(GTK_WINDOW(configwindow), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(configwindow), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(configwindow), PREF_PAD_BORDER);

	win_vbox = gtk_vbox_new(FALSE, PREF_PAD_SPACE);
	gtk_container_add(GTK_CONTAINER(configwindow), win_vbox);
	gtk_widget_show(win_vbox);

	hbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_BUTTON_GAP);
	gtk_box_pack_end(GTK_BOX(win_vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(NULL, GTK_STOCK_OK, NULL, FALSE,
				 G_CALLBACK(config_window_ok_cb), NULL);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	ct_button = button;

	button = pref_button_new(NULL, GTK_STOCK_SAVE, NULL, FALSE,
				 G_CALLBACK(config_window_save_cb), NULL);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_APPLY, NULL, FALSE,
				 G_CALLBACK(config_window_apply_cb), NULL);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_CANCEL, NULL, FALSE,
				 G_CALLBACK(config_window_close_cb), NULL);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	if (!generic_dialog_get_alternative_button_order(configwindow))
		{
		gtk_box_reorder_child(GTK_BOX(hbox), ct_button, -1);
		}

	notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	gtk_box_pack_start(GTK_BOX(win_vbox), notebook, TRUE, TRUE, 0);

	config_tab_general(notebook);
	config_tab_image(notebook);
	config_tab_windows(notebook);
	config_tab_accelerators(notebook);
	config_tab_files(notebook);
	config_tab_metadata(notebook);
	config_tab_color(notebook);
	config_tab_stereo(notebook);
	config_tab_behavior(notebook);

	gtk_widget_show(notebook);

	gtk_widget_show(configwindow);
}

/*
 *-----------------------------------------------------------------------------
 * config window show (public)
 *-----------------------------------------------------------------------------
 */

void show_config_window(void)
{
	if (configwindow)
		{
		gtk_window_present(GTK_WINDOW(configwindow));
		return;
		}

	config_window_create();
}

/*
 *-----------------
 * about window
 *-----------------
 */

static GtkWidget *about = NULL;

static gboolean about_delete_cb(GtkWidget *widget, GdkEventAny *event, gpointer data)
{
	gtk_widget_destroy(about);
	about = NULL;

	return TRUE;
}

static void about_window_close(GtkWidget *widget, gpointer data)
{
	if (!about) return;

	gtk_widget_destroy(about);
	about = NULL;
}

static void about_credits_cb(GtkWidget *widget, gpointer data)
{
	help_window_show("credits");
}

void show_about_window(void)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *button;
	GdkPixbuf *pixbuf;

	gchar *buf;

	if (about)
		{
		gtk_window_present(GTK_WINDOW(about));
		return;
		}

	about = window_new(GTK_WINDOW_TOPLEVEL, "about", NULL, NULL, _("About"));
	gtk_window_set_type_hint(GTK_WINDOW(about), GDK_WINDOW_TYPE_HINT_DIALOG);
	g_signal_connect(G_OBJECT(about), "delete_event",
			 G_CALLBACK(about_delete_cb), NULL);

	gtk_container_set_border_width(GTK_CONTAINER(about), PREF_PAD_BORDER);

	vbox = gtk_vbox_new(FALSE, PREF_PAD_SPACE);
	gtk_container_add(GTK_CONTAINER(about), vbox);
	gtk_widget_show(vbox);

	pixbuf = pixbuf_inline(PIXBUF_INLINE_LOGO);
	button = gtk_image_new_from_pixbuf(pixbuf);
	g_object_unref(pixbuf);
	gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
	gtk_widget_show(button);

	buf = g_strdup_printf(_("%s %s\n\nCopyright (c) 2006 John Ellis\nCopyright (c) %s The Geeqie Team\nwebsite: %s\nemail: %s\n\nReleased under the GNU General Public License"),
			      GQ_APPNAME,
			      VERSION,
			      "2008 - 2016",
			      GQ_WEBSITE,
			      GQ_EMAIL_ADDRESS);
	label = gtk_label_new(buf);
	g_free(buf);

	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	hbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_BUTTON_GAP);
	gtk_box_pack_end(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(NULL, NULL, _("Credits..."), FALSE,
				 G_CALLBACK(about_credits_cb), NULL);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(NULL, GTK_STOCK_CLOSE, NULL, FALSE,
				 G_CALLBACK(about_window_close), NULL);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	gtk_widget_show(about);
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
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
