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

#include "rcfile.h"

#include <cstdlib>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include <config.h>

#include "bar-comment.h"
#include "bar-exif.h"
#include "bar-gps.h"
#include "bar-histogram.h"
#include "bar-keywords.h"
#include "bar-rating.h"
#include "bar-sort.h"
#include "bar.h"
#include "debug.h"
#include "dupe.h"
#include "editors.h"
#include "filefilter.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "metadata.h"
#include "options.h"
#include "secure-save.h"
#include "slideshow.h"
#include "typedefs.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"

/*
 *-----------------------------------------------------------------------------
 * line write/parse routines (public)
 *-----------------------------------------------------------------------------
 */

void write_indent(GString *str, gint indent)
{
	g_string_append_printf(str, "\n%*s", indent * 4, "");
}

void write_char_option(GString *str, gint, const gchar *label, const gchar *text)
{
	/* this is needed for overlay string, because g_markup_escape_text does not handle \n and such,
	   ideas for improvement are welcome
	*/
	static const unsigned char no_quote_utf[] = {
		0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b,
		0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
		0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3,
		0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
		0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb,
		0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3,
		0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
		0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb,
		0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
		0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
		'"',  0 /* '"' is handled in g_markup_escape_text */
	};

	gchar *escval1 = g_strescape(text ? text : "", reinterpret_cast<const gchar *>(no_quote_utf));
	gchar *escval2 = g_markup_escape_text(escval1, -1);
	g_string_append_printf(str, "%s = \"%s\" ", label, escval2);
	g_free(escval2);
	g_free(escval1);
}

/* dummy read for old/obsolete/futur/deprecated/unused options */
gboolean read_dummy_option(const gchar *option, const gchar *label, const gchar *message)
{
	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	log_printf(_("Option %s ignored: %s\n"), option, message);
	return TRUE;
}


gboolean read_char_option(const gchar *option, const gchar *label, const gchar *value, gchar **text)
{
	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	if (!text) return FALSE;

	g_free(*text);
	*text = g_strcompress(value);
	return TRUE;
}

void write_color_option(GString *str, gint indent, const gchar *label, GdkRGBA *color)
{
	if (color)
		{
		gchar *colorstring = gdk_rgba_to_string(color);

		write_char_option(str, indent, label, colorstring);
		g_free(colorstring);
		}
	else
		write_char_option(str, indent, label, "");
}

/**
 * @brief Read color option
 * @param option
 * @param label
 * @param value
 * @param color Returned RGBA value
 * @returns
 *
 * The change from GdkColor to GdkRGBA requires a color format change.
 * If the value string starts with #, it is a value stored as GdkColor,
 * which is "#666666666666".
 * The GdkRGBA style is "rgba(192,97,203,0)"
 */
gboolean read_color_option(const gchar *option, const gchar *label, const gchar *value, GdkRGBA *color)
{
	guint64 color_from_hex_string;

	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	if (!color) return FALSE;

	if (!*value) return FALSE;

	/* Convert from GTK3 compatible GdkColor to GTK4 compatible GdkRGBA */
	if (g_str_has_prefix(value, "#"))
		{
		color_from_hex_string = g_ascii_strtoll(value + 1, nullptr, 16);
		color->red = (gdouble)((color_from_hex_string & 0xffff00000000) >> 32) / 65535;
		color->green = (gdouble)((color_from_hex_string & 0x0000ffff0000) >> 16) / 65535;
		color->blue = (gdouble)(color_from_hex_string & 0x00000000ffff) / 65535;
		}
	else
		{
		gdk_rgba_parse(color, value);
		}

	return TRUE;
}

void write_int_option(GString *str, gint, const gchar *label, gint n)
{
	g_string_append_printf(str, "%s = \"%d\" ", label, n);
}

gboolean read_int_option(const gchar *option, const gchar *label, const gchar *value, gint *n)
{
	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	if (!n) return FALSE;

	if (g_ascii_isdigit(value[0]) || (value[0] == '-' && g_ascii_isdigit(value[1])))
		{
		*n = strtol(value, nullptr, 10);
		}
	else
		{
		if (g_ascii_strcasecmp(value, "true") == 0)
			*n = 1;
		else
			*n = 0;
		}

	return TRUE;
}

gboolean read_ushort_option(const gchar *option, const gchar *label, const gchar *value, guint16 *n)
{
	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	if (!n) return FALSE;

	if (g_ascii_isdigit(value[0]))
		{
		*n = strtoul(value, nullptr, 10);
		}
	else
		{
		if (g_ascii_strcasecmp(value, "true") == 0)
			*n = 1;
		else
			*n = 0;
		}

	return TRUE;
}

void write_uint_option(GString *str, gint, const gchar *label, guint n)
{
	g_string_append_printf(str, "%s = \"%u\" ", label, n);
}

gboolean read_uint_option(const gchar *option, const gchar *label, const gchar *value, guint *n)
{
	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	if (!n) return FALSE;

	if (g_ascii_isdigit(value[0]))
		{
		*n = strtoul(value, nullptr, 10);
		}
	else
		{
		if (g_ascii_strcasecmp(value, "true") == 0)
			*n = 1;
		else
			*n = 0;
		}

	return TRUE;
}

gboolean read_uint_option_clamp(const gchar *option, const gchar *label, const gchar *value, guint *n, guint min, guint max)
{
	gboolean ret;

	ret = read_uint_option(option, label, value, n);
	if (ret) *n = CLAMP(*n, min, max);

	return ret;
}


gboolean read_int_option_clamp(const gchar *option, const gchar *label, const gchar *value, gint *n, gint min, gint max)
{
	gboolean ret;

	ret = read_int_option(option, label, value, n);
	if (ret) *n = CLAMP(*n, min, max);

	return ret;
}

void write_int_unit_option(GString *str, gint, const gchar *label, gint n, gint subunits)
{
	gint l;
	gint r;

	if (subunits > 0)
		{
		l = n / subunits;
		r = n % subunits;
		}
	else
		{
		l = n;
		r = 0;
		}

	g_string_append_printf(str, "%s = \"%d.%d\" ", label, l, r);
}

gboolean read_int_unit_option(const gchar *option, const gchar *label, const gchar *value, gint *n, gint subunits)
{
	gint l;
	gint r;
	gchar *ptr;
	gchar *buf;

	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	if (!n) return FALSE;

	buf = g_strdup(value);
	ptr = buf;
	while (*ptr != '\0' && *ptr != '.') ptr++;
	if (*ptr == '.')
		{
		*ptr = '\0';
		l = strtol(value, nullptr, 10);
		*ptr = '.';
		ptr++;
		r = strtol(ptr, nullptr, 10);
		}
	else
		{
		l = strtol(value, nullptr, 10);
		r = 0;
		}

	*n = l * subunits + r;
	g_free(buf);

	return TRUE;
}

void write_bool_option(GString *str, gint, const gchar *label, gint n)
{
	g_string_append_printf(str, "%s = \"%s\" ", label, n ? "true" : "false");
}

gboolean read_bool_option(const gchar *option, const gchar *label, const gchar *value, gint *n)
{
	if (g_ascii_strcasecmp(option, label) != 0) return FALSE;
	if (!n) return FALSE;

	if (g_ascii_strcasecmp(value, "true") == 0 || atoi(value) != 0)
		*n = TRUE;
	else
		*n = FALSE;

	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * write functions for elements (private)
 *-----------------------------------------------------------------------------
 */

static void write_global_attributes(GString *outstr, gint indent)
{
	/* General Options */
	WRITE_NL(); WRITE_BOOL(*options, show_icon_names);
	WRITE_NL(); WRITE_BOOL(*options, show_star_rating);
	WRITE_NL(); WRITE_BOOL(*options, show_predefined_keyword_tree);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*options, tree_descend_subdirs);
	WRITE_NL(); WRITE_BOOL(*options, view_dir_list_single_click_enter);
	WRITE_NL(); WRITE_BOOL(*options, circular_selection_lists);
	WRITE_NL(); WRITE_BOOL(*options, lazy_image_sync);
	WRITE_NL(); WRITE_BOOL(*options, update_on_time_change);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*options, progressive_key_scrolling);
	WRITE_NL(); WRITE_UINT(*options, keyboard_scroll_step);

	WRITE_NL(); WRITE_UINT(*options, duplicates_similarity_threshold);
	WRITE_NL(); WRITE_UINT(*options, duplicates_match);
	WRITE_NL(); WRITE_UINT(*options, duplicates_select_type);
	WRITE_NL(); WRITE_BOOL(*options, duplicates_thumbnails);
	WRITE_NL(); WRITE_BOOL(*options, rot_invariant_sim);
	WRITE_NL(); WRITE_BOOL(*options, sort_totals);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*options, mousewheel_scrolls);
	WRITE_NL(); WRITE_BOOL(*options, image_lm_click_nav);
	WRITE_NL(); WRITE_BOOL(*options, image_l_click_archive);
	WRITE_NL(); WRITE_BOOL(*options, image_l_click_video);
	WRITE_NL(); WRITE_CHAR(*options, image_l_click_video_editor);
	WRITE_NL(); WRITE_INT(*options, open_recent_list_maxsize);
	WRITE_NL(); WRITE_INT(*options, recent_folder_image_list_maxsize);
	WRITE_NL(); WRITE_INT(*options, dnd_icon_size);
	WRITE_NL(); WRITE_UINT(*options, dnd_default_action);
	WRITE_NL(); WRITE_BOOL(*options, place_dialogs_under_mouse);
	WRITE_NL(); WRITE_INT(*options, clipboard_selection);
	WRITE_NL(); WRITE_UINT(*options, rectangle_draw_aspect_ratio);

	WRITE_NL(); WRITE_BOOL(*options, save_window_positions);
	WRITE_NL(); WRITE_BOOL(*options, use_saved_window_positions_for_new_windows);
	WRITE_NL(); WRITE_BOOL(*options, save_window_workspace);
	WRITE_NL(); WRITE_BOOL(*options, tools_restore_state);
	WRITE_NL(); WRITE_BOOL(*options, save_dialog_window_positions);
	WRITE_NL(); WRITE_BOOL(*options, hide_window_decorations);
	WRITE_NL(); WRITE_BOOL(*options, show_window_ids);
	WRITE_NL(); WRITE_BOOL(*options, expand_menu_toolbar);
	WRITE_NL(); WRITE_BOOL(*options, hamburger_menu);

	WRITE_NL(); WRITE_UINT(*options, log_window_lines);
	WRITE_NL(); WRITE_BOOL(*options, log_window.timer_data);
	WRITE_NL(); WRITE_CHAR(*options, log_window.action);

	WRITE_NL(); WRITE_BOOL(*options, appimage_notifications);
	WRITE_NL(); WRITE_BOOL(*options, marks_save);
	WRITE_NL(); WRITE_CHAR(*options, help_search_engine);

	WRITE_NL(); WRITE_BOOL(*options, external_preview.enable);
	WRITE_NL(); WRITE_CHAR(*options, external_preview.select);
	WRITE_NL(); WRITE_CHAR(*options, external_preview.extract);

	WRITE_NL(); WRITE_BOOL(*options, with_rename);
	WRITE_NL(); WRITE_BOOL(*options, collections_duplicates);
	WRITE_NL(); WRITE_BOOL(*options, collections_on_top);
	WRITE_NL(); WRITE_BOOL(*options, hide_window_in_fullscreen);
	WRITE_NL(); WRITE_BOOL(*options, hide_osd_in_fullscreen);

	WRITE_NL(); WRITE_BOOL(*options, selectable_bars.menu_bar);
	WRITE_NL(); WRITE_BOOL(*options, selectable_bars.status_bar);
	WRITE_NL(); WRITE_BOOL(*options, selectable_bars.tool_bar);

	/* File operations Options */
	WRITE_NL(); WRITE_BOOL(*options, file_ops.enable_in_place_rename);
	WRITE_NL(); WRITE_BOOL(*options, file_ops.confirm_delete);
	WRITE_NL(); WRITE_BOOL(*options, file_ops.confirm_move_to_trash);
	WRITE_NL(); WRITE_BOOL(*options, file_ops.enable_delete_key);
	WRITE_NL(); WRITE_BOOL(*options, file_ops.use_system_trash);
	WRITE_NL(); WRITE_BOOL(*options, file_ops.safe_delete_enable);
	WRITE_NL(); WRITE_CHAR(*options, file_ops.safe_delete_path);
	WRITE_NL(); WRITE_INT(*options, file_ops.safe_delete_folder_maxsize);
	WRITE_NL(); WRITE_BOOL(*options, file_ops.no_trash);

	/* Properties dialog Options */
	WRITE_NL(); WRITE_CHAR(*options, properties.tabs_order);

	/* Image Options */
	WRITE_NL(); WRITE_UINT(*options, image.zoom_mode);

	WRITE_SEPARATOR();
	WRITE_NL(); WRITE_BOOL(*options, image.zoom_2pass);
	WRITE_NL(); WRITE_BOOL(*options, image.zoom_to_fit_allow_expand);
	WRITE_NL(); WRITE_UINT(*options, image.zoom_quality);
	WRITE_NL(); WRITE_INT(*options, image.zoom_increment);
	WRITE_NL(); WRITE_UINT(*options, image.zoom_style);
	WRITE_NL(); WRITE_BOOL(*options, image.fit_window_to_image);
	WRITE_NL(); WRITE_BOOL(*options, image.limit_window_size);
	WRITE_NL(); WRITE_INT(*options, image.max_window_size);
	WRITE_NL(); WRITE_BOOL(*options, image.limit_autofit_size);
	WRITE_NL(); WRITE_INT(*options, image.max_autofit_size);
	WRITE_NL(); WRITE_INT(*options, image.max_enlargement_size);
	WRITE_NL(); WRITE_UINT(*options, image.scroll_reset_method);
	WRITE_NL(); WRITE_INT(*options, image.tile_cache_max);
	WRITE_NL(); WRITE_INT(*options, image.image_cache_max);
	WRITE_NL(); WRITE_BOOL(*options, image.enable_read_ahead);
	WRITE_NL(); WRITE_BOOL(*options, image.exif_rotate_enable);
	WRITE_NL(); WRITE_BOOL(*options, image.use_custom_border_color);
	WRITE_NL(); WRITE_BOOL(*options, image.use_custom_border_color_in_fullscreen);
	WRITE_NL(); WRITE_COLOR(*options, image.border_color);
	WRITE_NL(); WRITE_COLOR(*options, image.alpha_color_1);
	WRITE_NL(); WRITE_COLOR(*options, image.alpha_color_2);
	WRITE_NL(); WRITE_INT(*options, image.tile_size);

	/* Thumbnails Options */
	WRITE_NL(); WRITE_INT(*options, thumbnails.max_width);
	WRITE_NL(); WRITE_INT(*options, thumbnails.max_height);
	WRITE_NL(); WRITE_BOOL(*options, thumbnails.enable_caching);
	WRITE_NL(); WRITE_BOOL(*options, thumbnails.cache_into_dirs);
	WRITE_NL(); WRITE_BOOL(*options, thumbnails.use_xvpics);
	WRITE_NL(); WRITE_BOOL(*options, thumbnails.spec_standard);
	WRITE_NL(); WRITE_UINT(*options, thumbnails.quality);
	WRITE_NL(); WRITE_BOOL(*options, thumbnails.use_exif);
	WRITE_NL(); WRITE_BOOL(*options, thumbnails.use_color_management);
	WRITE_NL(); WRITE_BOOL(*options, thumbnails.use_ft_metadata);
	WRITE_NL(); WRITE_INT(*options, thumbnails.collection_preview);

	/* File sorting Options */
	WRITE_NL(); WRITE_BOOL(*options, file_sort.case_sensitive);

	/* Fullscreen Options */
	WRITE_NL(); WRITE_INT(*options, fullscreen.screen);
	WRITE_NL(); WRITE_BOOL(*options, fullscreen.clean_flip);
	WRITE_NL(); WRITE_BOOL(*options, fullscreen.disable_saver);
	WRITE_NL(); WRITE_BOOL(*options, fullscreen.above);

	WRITE_SEPARATOR();

	/* Image Overlay Options */
	WRITE_NL(); WRITE_CHAR(*options, image_overlay.template_string);

	WRITE_NL(); WRITE_INT(*options, image_overlay.x);
	WRITE_NL(); WRITE_INT(*options, image_overlay.y);
	WRITE_NL(); WRITE_INT(*options, image_overlay.text_red);
	WRITE_NL(); WRITE_INT(*options, image_overlay.text_green);
	WRITE_NL(); WRITE_INT(*options, image_overlay.text_blue);
	WRITE_NL(); WRITE_INT(*options, image_overlay.text_alpha);
	WRITE_NL(); WRITE_INT(*options, image_overlay.background_red);
	WRITE_NL(); WRITE_INT(*options, image_overlay.background_green);
	WRITE_NL(); WRITE_INT(*options, image_overlay.background_blue);
	WRITE_NL(); WRITE_INT(*options, image_overlay.background_alpha);
	WRITE_NL(); WRITE_CHAR(*options, image_overlay.font);

	/* Slideshow Options */
	WRITE_NL(); WRITE_INT_UNIT(*options, slideshow.delay, SLIDESHOW_SUBSECOND_PRECISION);
	WRITE_NL(); WRITE_BOOL(*options, slideshow.random);
	WRITE_NL(); WRITE_BOOL(*options, slideshow.repeat);

	/* Collection Options */
	WRITE_NL(); WRITE_BOOL(*options, collections.rectangular_selection);

	/* Filtering Options */
	WRITE_NL(); WRITE_BOOL(*options, file_filter.show_hidden_files);
	WRITE_NL(); WRITE_BOOL(*options, file_filter.show_parent_directory);
	WRITE_NL(); WRITE_BOOL(*options, file_filter.show_dot_directory);
	WRITE_NL(); WRITE_BOOL(*options, file_filter.disable_file_extension_checks);
	WRITE_NL(); WRITE_BOOL(*options, file_filter.disable);
	WRITE_SEPARATOR();

	/* Sidecars Options */
	WRITE_NL(); WRITE_CHAR(*options, sidecar.ext);

	/* Shell command */
	WRITE_NL(); WRITE_CHAR(*options, shell.path);
	WRITE_NL(); WRITE_CHAR(*options, shell.options);

	/* Helpers */
	WRITE_NL(); WRITE_CHAR(*options, helpers.html_browser.command_name);
	WRITE_NL(); WRITE_CHAR(*options, helpers.html_browser.command_line);

	/* Metadata Options */
	WRITE_NL(); WRITE_BOOL(*options, metadata.enable_metadata_dirs);
	WRITE_NL(); WRITE_BOOL(*options, metadata.save_in_image_file);
	WRITE_NL(); WRITE_BOOL(*options, metadata.save_legacy_IPTC);
	WRITE_NL(); WRITE_BOOL(*options, metadata.warn_on_write_problems);
	WRITE_NL(); WRITE_BOOL(*options, metadata.save_legacy_format);
	WRITE_NL(); WRITE_BOOL(*options, metadata.sync_grouped_files);
	WRITE_NL(); WRITE_BOOL(*options, metadata.confirm_write);
	WRITE_NL(); WRITE_BOOL(*options, metadata.sidecar_extended_name);
	WRITE_NL(); WRITE_INT(*options, metadata.confirm_timeout);
	WRITE_NL(); WRITE_BOOL(*options, metadata.confirm_after_timeout);
	WRITE_NL(); WRITE_BOOL(*options, metadata.confirm_on_image_change);
	WRITE_NL(); WRITE_BOOL(*options, metadata.confirm_on_dir_change);
	WRITE_NL(); WRITE_BOOL(*options, metadata.keywords_case_sensitive);
	WRITE_NL(); WRITE_BOOL(*options, metadata.write_orientation);
	WRITE_NL(); WRITE_BOOL(*options, metadata.check_spelling);

	WRITE_NL(); WRITE_INT(*options, stereo.mode);
	WRITE_NL(); WRITE_INT(*options, stereo.fsmode);
	WRITE_NL(); WRITE_BOOL(*options, stereo.enable_fsmode);
	WRITE_NL(); WRITE_INT(*options, stereo.fixed_w);
	WRITE_NL(); WRITE_INT(*options, stereo.fixed_h);
	WRITE_NL(); WRITE_INT(*options, stereo.fixed_x1);
	WRITE_NL(); WRITE_INT(*options, stereo.fixed_y1);
	WRITE_NL(); WRITE_INT(*options, stereo.fixed_x2);
	WRITE_NL(); WRITE_INT(*options, stereo.fixed_y2);

	WRITE_NL(); WRITE_BOOL(*options, read_metadata_in_idle);

	WRITE_NL(); WRITE_UINT(*options, star_rating.star);
	WRITE_NL(); WRITE_UINT(*options, star_rating.rejected);

	/* copy move rename */
	WRITE_NL(); WRITE_INT(*options, cp_mv_rn.auto_start);
	WRITE_NL(); WRITE_INT(*options, cp_mv_rn.auto_padding);
	WRITE_NL(); WRITE_CHAR(*options, cp_mv_rn.auto_end);
	WRITE_NL(); WRITE_INT(*options, cp_mv_rn.formatted_start);

	WRITE_SEPARATOR();

	/* Print Text */
	WRITE_NL(); WRITE_CHAR(*options, printer.template_string);
	WRITE_NL(); WRITE_CHAR(*options, printer.image_font);
	WRITE_NL(); WRITE_CHAR(*options, printer.page_font);
	WRITE_NL(); WRITE_CHAR(*options, printer.page_text);
	WRITE_NL(); WRITE_INT(*options, printer.image_text_position);
	WRITE_NL(); WRITE_INT(*options, printer.page_text_position);
	WRITE_NL(); WRITE_BOOL(*options, printer.show_image_text);
	WRITE_NL(); WRITE_BOOL(*options, printer.show_page_text);
	WRITE_SEPARATOR();

	/* Threads */
	WRITE_NL(); WRITE_INT(*options, threads.duplicates);
	WRITE_SEPARATOR();

	/* user-definable mouse buttons */
	WRITE_NL(); WRITE_CHAR(*options, mouse_button_8);
	WRITE_NL(); WRITE_CHAR(*options, mouse_button_9);
	WRITE_SEPARATOR();

	/* GPU - see main.cc */
	WRITE_NL(); WRITE_BOOL(*options, override_disable_gpu);
	WRITE_SEPARATOR();

	/* Alternate similarity algorithm */
	WRITE_NL(); WRITE_BOOL(*options, alternate_similarity_algorithm.enabled);
	WRITE_NL(); WRITE_BOOL(*options, alternate_similarity_algorithm.grayscale);
	WRITE_SEPARATOR();
}

static void write_color_profile(GString *outstr, gint indent)
{
	gint i;
#if !HAVE_LCMS
	g_string_append_printf(outstr, "<!-- NOTICE: %s was not built with support for color profiles,\n"
				"		 color profile options will have no effect.\n-->\n", GQ_APPNAME);
#endif

	WRITE_NL(); WRITE_STRING("<color_profiles ");
	WRITE_CHAR(options->color_profile, screen_file);
	WRITE_BOOL(options->color_profile, enabled);
	WRITE_BOOL(options->color_profile, use_image);
	WRITE_INT(options->color_profile, input_type);
	WRITE_BOOL(options->color_profile, use_x11_screen_profile);
	WRITE_INT(options->color_profile, render_intent);
	WRITE_STRING(">");

	indent++;
	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		WRITE_NL(); WRITE_STRING("<profile ");
		write_char_option(outstr, indent, "input_file", options->color_profile.input_file[i]);
		write_char_option(outstr, indent, "input_name", options->color_profile.input_name[i]);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</color_profiles>");
}

static void write_marks_tooltips(GString *outstr, gint indent)
{
	gint i;

	WRITE_NL(); WRITE_STRING("<marks_tooltips>");
	indent++;
	for (i = 0; i < FILEDATA_MARKS_SIZE; i++)
		{
		WRITE_NL();
		write_char_option(outstr, indent, "<tooltip text", options->marks_tooltips[i]);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</marks_tooltips>");
}

static void write_class_filter(GString *outstr, gint indent)
{
	gint i;

	WRITE_NL(); WRITE_STRING("<class_filter>");
	indent++;
	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		WRITE_NL(); WRITE_STRING("<filter_type ");
		write_char_option(outstr, indent, "filter", format_class_list[i]);
		write_bool_option(outstr, indent, "enabled", options->class_filter[i]);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</class_filter>");
}

static void write_disabled_plugins(GString *outstr, gint indent)
{
	GtkTreeIter iter;
	gboolean valid;
	gboolean disabled;
	gchar *desktop_path;

	WRITE_NL(); WRITE_STRING("<disabled_plugins>");
	indent++;

	if (desktop_file_list)
		{
		valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(desktop_file_list), &iter);
		while (valid)
			{
			gtk_tree_model_get(GTK_TREE_MODEL(desktop_file_list), &iter, DESKTOP_FILE_COLUMN_DISABLED, &disabled, -1);
			gtk_tree_model_get(GTK_TREE_MODEL(desktop_file_list), &iter, DESKTOP_FILE_COLUMN_PATH, &desktop_path, -1);

			if (disabled)
				{
				WRITE_NL();
				write_char_option(outstr, indent, "<plugin path", desktop_path);
				WRITE_STRING("/>");
				}
			g_free(desktop_path);
			valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(desktop_file_list), &iter);
			}
		}

	indent--;
	WRITE_NL(); WRITE_STRING("</disabled_plugins>");
}

/*
 *-----------------------------------------------------------------------------
 * save configuration (public)
 *-----------------------------------------------------------------------------
 */

gboolean save_config_to_file(const gchar *utf8_path, ConfOptions *options, LayoutWindow *lw)
{
	SecureSaveInfo *ssi;
	gchar *rc_pathl;
	GString *outstr;
	gint indent = 0;
	GList *work;

	rc_pathl = path_from_utf8(utf8_path);
	ssi = secure_open(rc_pathl);
	g_free(rc_pathl);
	if (!ssi)
		{
		log_printf(_("error saving config file: %s\n"), utf8_path);
		return FALSE;
		}

	outstr = g_string_new("<!--\n");
	g_string_append(outstr, "######################################################################\n");
	g_string_append_printf(outstr, "# %30s config file	  version %-10s #\n", GQ_APPNAME, VERSION);
	g_string_append(outstr, "######################################################################\n");
	WRITE_SEPARATOR();

	WRITE_STRING("# Note: This file is autogenerated. Options can be changed here,\n");
	WRITE_STRING("#	   but user comments and formatting will be lost.\n");
	WRITE_SEPARATOR();
	WRITE_STRING("-->\n");
	WRITE_SEPARATOR();

	WRITE_STRING("<gq>\n");
	indent++;

	if (!lw)
		{
		WRITE_NL(); WRITE_STRING("<global\n");
		indent++;
		write_global_attributes(outstr, indent + 1);
		indent--;
		WRITE_STRING(">\n");

		indent++;

		write_color_profile(outstr, indent);

		WRITE_SEPARATOR();
		filter_write_list(outstr, indent);

		WRITE_SEPARATOR();
		write_marks_tooltips(outstr, indent);

		WRITE_SEPARATOR();
		write_disabled_plugins(outstr, indent);

		WRITE_SEPARATOR();
		write_class_filter(outstr, indent);

		WRITE_SEPARATOR();
		keyword_tree_write_config(outstr, indent);
		indent--;
		WRITE_NL(); WRITE_STRING("</global>\n");
		}
	WRITE_SEPARATOR();

	/* Layout Options */
	if (!lw)
		{
		/* If not save_window_positions, do not include a <layout> section */
		if (options->save_window_positions)
			{
			work = layout_window_list;
			while (work)
				{
				auto lw = static_cast<LayoutWindow *>(work->data);
				layout_write_config(lw, outstr, indent);
				work = work->next;
				}
			}
		}
	else
		{
		layout_write_config(lw, outstr, indent);
		}

	indent--;
	WRITE_NL(); WRITE_STRING("</gq>\n");
	WRITE_SEPARATOR();

	secure_fputs(ssi, outstr->str);
	g_string_free(outstr, TRUE);

	if (secure_close(ssi))
		{
		log_printf(_("error saving config file: %s\nerror: %s\n"), utf8_path,
			   secsave_strerror(secsave_errno));
		return FALSE;
		}

	return TRUE;
}

gboolean save_default_layout_options_to_file(const gchar *utf8_path, ConfOptions *, LayoutWindow *lw)
{
	SecureSaveInfo *ssi;
	gchar *rc_pathl;
	GString *outstr;
	gint indent = 0;

	rc_pathl = path_from_utf8(utf8_path);
	ssi = secure_open(rc_pathl);
	g_free(rc_pathl);
	if (!ssi)
		{
		log_printf(_("error saving default layout file: %s\n"), utf8_path);
		return FALSE;
		}

	outstr = g_string_new("<!--\n");
	g_string_append(outstr, "######################################################################\n");
	g_string_append_printf(outstr, "# %8s default layout file	  version %-10s #\n", GQ_APPNAME, VERSION);
	g_string_append(outstr, "######################################################################\n");
	WRITE_SEPARATOR();

	WRITE_STRING("# Note: This file is autogenerated. Options can be changed here,\n");
	WRITE_STRING("#	   but user comments and formatting will be lost.\n");
	WRITE_SEPARATOR();
	WRITE_STRING("-->\n");
	WRITE_SEPARATOR();

	WRITE_STRING("<gq>\n");
	indent++;

	layout_write_config(lw, outstr, indent);

	indent--;
	WRITE_NL(); WRITE_STRING("</gq>\n");
	WRITE_SEPARATOR();

	secure_fputs(ssi, outstr->str);
	g_string_free(outstr, TRUE);

	if (secure_close(ssi))
		{
		log_printf(_("error saving config file: %s\nerror: %s\n"), utf8_path,
			   secsave_strerror(secsave_errno));
		return FALSE;
		}

	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * loading attributes for elements (private)
 *-----------------------------------------------------------------------------
 */


static gboolean load_global_params(const gchar **attribute_names, const gchar **attribute_values)
{
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		/* General options */
		if (READ_BOOL(*options, show_icon_names)) continue;
		if (READ_BOOL(*options, show_star_rating)) continue;
		if (READ_BOOL(*options, show_predefined_keyword_tree)) continue;

		if (READ_BOOL(*options, tree_descend_subdirs)) continue;
		if (READ_BOOL(*options, view_dir_list_single_click_enter)) continue;
		if (READ_BOOL(*options, circular_selection_lists)) continue;
		if (READ_BOOL(*options, lazy_image_sync)) continue;
		if (READ_BOOL(*options, update_on_time_change)) continue;

		if (READ_UINT_CLAMP(*options, duplicates_similarity_threshold, 0, 100)) continue;
		if (READ_UINT_CLAMP(*options, duplicates_match, 0, DUPE_MATCH_ALL)) continue;
		if (READ_UINT_CLAMP(*options, duplicates_select_type, 0, DUPE_SELECT_GROUP2)) continue;
		if (READ_BOOL(*options, duplicates_thumbnails)) continue;
		if (READ_BOOL(*options, rot_invariant_sim)) continue;
		if (READ_BOOL(*options, sort_totals)) continue;

		if (READ_BOOL(*options, progressive_key_scrolling)) continue;
		if (READ_UINT_CLAMP(*options, keyboard_scroll_step, 1, 32)) continue;

		if (READ_BOOL(*options, mousewheel_scrolls)) continue;
		if (READ_BOOL(*options, image_lm_click_nav)) continue;
		if (READ_BOOL(*options, image_l_click_archive)) continue;
		if (READ_BOOL(*options, image_l_click_video)) continue;
		if (READ_CHAR(*options, image_l_click_video_editor)) continue;

		if (READ_INT(*options, open_recent_list_maxsize)) continue;
		if (READ_INT(*options, recent_folder_image_list_maxsize)) continue;
		if (READ_INT(*options, dnd_icon_size)) continue;
		if (READ_UINT_ENUM(*options, dnd_default_action)) continue;
		if (READ_BOOL(*options, place_dialogs_under_mouse)) continue;
		if (READ_INT(*options, clipboard_selection)) continue;
		if (READ_UINT_ENUM(*options, rectangle_draw_aspect_ratio)) continue;

		if (READ_BOOL(*options, save_window_positions)) continue;
		if (READ_BOOL(*options, use_saved_window_positions_for_new_windows)) continue;
		if (READ_BOOL(*options, save_window_workspace)) continue;
		if (READ_BOOL(*options, tools_restore_state)) continue;
		if (READ_BOOL(*options, save_dialog_window_positions)) continue;
		if (READ_BOOL(*options, hide_window_decorations)) continue;
		if (READ_BOOL(*options, show_window_ids)) continue;
		if (READ_BOOL(*options, expand_menu_toolbar)) continue;
		if (READ_BOOL(*options, hamburger_menu)) continue;

		if (READ_INT(*options, log_window_lines)) continue;
		if (READ_BOOL(*options, log_window.timer_data)) continue;
		if (READ_CHAR(*options, log_window.action)) continue;

		if (READ_BOOL(*options, appimage_notifications)) continue;
		if (READ_BOOL(*options, marks_save)) continue;
		if (READ_CHAR(*options, help_search_engine)) continue;

		if (READ_BOOL(*options, external_preview.enable)) continue;
		if (READ_CHAR(*options, external_preview.select)) continue;
		if (READ_CHAR(*options, external_preview.extract)) continue;

		if (READ_BOOL(*options, collections_duplicates)) continue;
		if (READ_BOOL(*options, collections_on_top)) continue;
		if (READ_BOOL(*options, hide_window_in_fullscreen)) continue;
		if (READ_BOOL(*options, hide_osd_in_fullscreen)) continue;

		if (READ_BOOL(*options, selectable_bars.menu_bar)) continue;
		if (READ_BOOL(*options, selectable_bars.status_bar)) continue;
		if (READ_BOOL(*options, selectable_bars.tool_bar)) continue;

		/* Properties dialog options */
		if (READ_CHAR(*options, properties.tabs_order)) continue;

		if (READ_BOOL(*options, with_rename)) continue;

		/* Image options */
		if (READ_UINT_ENUM_CLAMP(*options, image.zoom_mode, 0, ZOOM_RESET_NONE)) continue;
		if (READ_UINT_ENUM_CLAMP(*options, image.zoom_style, 0, ZOOM_ARITHMETIC)) continue;
		if (READ_BOOL(*options, image.zoom_2pass)) continue;
		if (READ_BOOL(*options, image.zoom_to_fit_allow_expand)) continue;
		if (READ_BOOL(*options, image.fit_window_to_image)) continue;
		if (READ_BOOL(*options, image.limit_window_size)) continue;
		if (READ_INT(*options, image.max_window_size)) continue;
		if (READ_BOOL(*options, image.limit_autofit_size)) continue;
		if (READ_INT(*options, image.max_autofit_size)) continue;
		if (READ_INT(*options, image.max_enlargement_size)) continue;
		if (READ_UINT_ENUM_CLAMP(*options, image.scroll_reset_method, 0, ScrollReset::COUNT - 1)) continue;
		if (READ_INT(*options, image.tile_cache_max)) continue;
		if (READ_INT(*options, image.image_cache_max)) continue;
		if (READ_UINT_CLAMP(*options, image.zoom_quality, GDK_INTERP_NEAREST, GDK_INTERP_BILINEAR)) continue;
		if (READ_INT(*options, image.zoom_increment)) continue;
		if (READ_BOOL(*options, image.enable_read_ahead)) continue;
		if (READ_BOOL(*options, image.exif_rotate_enable)) continue;
		if (READ_BOOL(*options, image.use_custom_border_color)) continue;
		if (READ_BOOL(*options, image.use_custom_border_color_in_fullscreen)) continue;
		if (READ_COLOR(*options, image.border_color)) continue;
		if (READ_COLOR(*options, image.alpha_color_1)) continue;
		if (READ_COLOR(*options, image.alpha_color_2)) continue;
		if (READ_INT(*options, image.tile_size)) continue;

		/* Thumbnails options */
		if (READ_INT_CLAMP(*options, thumbnails.max_width, 16, 512)) continue;
		if (READ_INT_CLAMP(*options, thumbnails.max_height, 16, 512)) continue;

		if (READ_BOOL(*options, thumbnails.enable_caching)) continue;
		if (READ_BOOL(*options, thumbnails.cache_into_dirs)) continue;
		if (READ_BOOL(*options, thumbnails.use_xvpics)) continue;
		if (READ_BOOL(*options, thumbnails.spec_standard)) continue;
		if (READ_UINT_CLAMP(*options, thumbnails.quality, GDK_INTERP_NEAREST, GDK_INTERP_BILINEAR)) continue;
		if (READ_BOOL(*options, thumbnails.use_exif)) continue;
		if (READ_BOOL(*options, thumbnails.use_color_management)) continue;
		if (READ_INT(*options, thumbnails.collection_preview)) continue;
		if (READ_BOOL(*options, thumbnails.use_ft_metadata)) continue;

		/* File sorting options */
		if (READ_BOOL(*options, file_sort.case_sensitive)) continue;

		/* File operations *options */
		if (READ_BOOL(*options, file_ops.enable_in_place_rename)) continue;
		if (READ_BOOL(*options, file_ops.confirm_delete)) continue;
		if (READ_BOOL(*options, file_ops.confirm_move_to_trash)) continue;
		if (READ_BOOL(*options, file_ops.enable_delete_key)) continue;
		if (READ_BOOL(*options, file_ops.use_system_trash)) continue;
		if (READ_BOOL(*options, file_ops.safe_delete_enable)) continue;
		if (READ_CHAR(*options, file_ops.safe_delete_path)) continue;
		if (READ_INT(*options, file_ops.safe_delete_folder_maxsize)) continue;
		if (READ_BOOL(*options, file_ops.no_trash)) continue;

		/* Fullscreen options */
		if (READ_INT(*options, fullscreen.screen)) continue;
		if (READ_BOOL(*options, fullscreen.clean_flip)) continue;
		if (READ_BOOL(*options, fullscreen.disable_saver)) continue;
		if (READ_BOOL(*options, fullscreen.above)) continue;

		/* Image overlay */
		if (READ_CHAR(*options, image_overlay.template_string)) continue;
		if (READ_INT(*options, image_overlay.x)) continue;
		if (READ_INT(*options, image_overlay.y)) continue;
		if (READ_USHORT(*options, image_overlay.text_red)) continue;
		if (READ_USHORT(*options, image_overlay.text_green)) continue;
		if (READ_USHORT(*options, image_overlay.text_blue)) continue;
		if (READ_USHORT(*options, image_overlay.text_alpha)) continue;
		if (READ_USHORT(*options, image_overlay.background_red)) continue;
		if (READ_USHORT(*options, image_overlay.background_green)) continue;
		if (READ_USHORT(*options, image_overlay.background_blue)) continue;
		if (READ_USHORT(*options, image_overlay.background_alpha)) continue;
		if (READ_CHAR(*options, image_overlay.font)) continue;

		/* Slideshow options */
		if (READ_INT_UNIT(*options, slideshow.delay, SLIDESHOW_SUBSECOND_PRECISION)) continue;
		if (READ_BOOL(*options, slideshow.random)) continue;
		if (READ_BOOL(*options, slideshow.repeat)) continue;

		/* Collection options */
		if (READ_BOOL(*options, collections.rectangular_selection)) continue;

		/* Filtering options */
		if (READ_BOOL(*options, file_filter.show_hidden_files)) continue;
		if (READ_BOOL(*options, file_filter.show_parent_directory)) continue;
		if (READ_BOOL(*options, file_filter.show_dot_directory)) continue;
		if (READ_BOOL(*options, file_filter.disable_file_extension_checks)) continue;
		if (READ_BOOL(*options, file_filter.disable)) continue;
		if (READ_CHAR(*options, sidecar.ext)) continue;

		/* Color Profiles */

		/* Shell command */
		if (READ_CHAR(*options, shell.path)) continue;
		if (READ_CHAR(*options, shell.options)) continue;

		/* Helpers */
		if (READ_CHAR(*options, helpers.html_browser.command_name)) continue;
		if (READ_CHAR(*options, helpers.html_browser.command_line)) continue;

		/* Metadata */
		if (READ_BOOL(*options, metadata.enable_metadata_dirs)) continue;
		if (READ_BOOL(*options, metadata.save_in_image_file)) continue;
		if (READ_BOOL(*options, metadata.save_legacy_IPTC)) continue;
		if (READ_BOOL(*options, metadata.warn_on_write_problems)) continue;
		if (READ_BOOL(*options, metadata.save_legacy_format)) continue;
		if (READ_BOOL(*options, metadata.sync_grouped_files)) continue;
		if (READ_BOOL(*options, metadata.confirm_write)) continue;
		if (READ_BOOL(*options, metadata.sidecar_extended_name)) continue;
		if (READ_BOOL(*options, metadata.confirm_after_timeout)) continue;
		if (READ_INT(*options, metadata.confirm_timeout)) continue;
		if (READ_BOOL(*options, metadata.confirm_on_image_change)) continue;
		if (READ_BOOL(*options, metadata.confirm_on_dir_change)) continue;
		if (READ_BOOL(*options, metadata.keywords_case_sensitive)) continue;
		if (READ_BOOL(*options, metadata.write_orientation)) continue;
		if (READ_BOOL(*options, metadata.check_spelling)) continue;

		if (READ_INT(*options, stereo.mode)) continue;
		if (READ_INT(*options, stereo.fsmode)) continue;
		if (READ_BOOL(*options, stereo.enable_fsmode)) continue;
		if (READ_INT(*options, stereo.fixed_w)) continue;
		if (READ_INT(*options, stereo.fixed_h)) continue;
		if (READ_INT(*options, stereo.fixed_x1)) continue;
		if (READ_INT(*options, stereo.fixed_y1)) continue;
		if (READ_INT(*options, stereo.fixed_x2)) continue;
		if (READ_INT(*options, stereo.fixed_y2)) continue;

		if (READ_BOOL(*options, read_metadata_in_idle)) continue;

		if (READ_UINT(*options, star_rating.star)) continue;
		if (READ_UINT(*options, star_rating.rejected)) continue;

		/* copy move rename */
		if (READ_INT(*options, cp_mv_rn.auto_start))  continue;
		if (READ_INT(*options, cp_mv_rn.auto_padding)) continue;
		if (READ_CHAR(*options, cp_mv_rn.auto_end)) continue;
		if (READ_INT(*options, cp_mv_rn.formatted_start)) continue;

		/* Printer text */
		if (READ_CHAR(*options, printer.template_string)) continue;
		if (READ_CHAR(*options, printer.image_font)) continue;
		if (READ_CHAR(*options, printer.page_font)) continue;
		if (READ_CHAR(*options, printer.page_text)) continue;
		if (READ_INT(*options, printer.image_text_position)) continue;
		if (READ_INT(*options, printer.page_text_position)) continue;
		if (READ_BOOL(*options, printer.show_image_text)) continue;
		if (READ_BOOL(*options, printer.show_page_text)) continue;

		/* Threads */
		if (READ_INT(*options, threads.duplicates)) continue;

		/* user-definable mouse buttons */
		if (READ_CHAR(*options, mouse_button_8)) continue;
		if (READ_CHAR(*options, mouse_button_9)) continue;

		/* GPU - see main.cc */
		if (READ_BOOL(*options, override_disable_gpu)) continue;

		/* Alternative similarity algorithm */
		if (READ_BOOL(*options, alternate_similarity_algorithm.enabled)) continue;
		if (READ_BOOL(*options, alternate_similarity_algorithm.grayscale)) continue;

		/* Dummy options */
		if (READ_DUMMY(*options, image.dither_quality, "deprecated since 2012-08-13")) continue;

		/* Unknown options */
		log_printf("unknown attribute %s = %s\n", option, value);
		}

	return TRUE;
}

static void options_load_color_profiles(GQParserData *, GMarkupParseContext *, const gchar *, const gchar **attribute_names, const gchar **attribute_values, gpointer, GError **)
{
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_BOOL(options->color_profile, enabled)) continue;
		if (READ_BOOL(options->color_profile, use_image)) continue;
		if (READ_INT(options->color_profile, input_type)) continue;
		if (READ_CHAR(options->color_profile, screen_file)) continue;
		if (READ_BOOL(options->color_profile, use_x11_screen_profile)) continue;
		if (READ_INT(options->color_profile, render_intent)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

}

static void options_load_profile(GQParserData *parser_data, GMarkupParseContext *, const gchar *, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	gint i = GPOINTER_TO_INT(data);
	if (i < 0 || i >= COLOR_PROFILE_INPUTS) return;
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("input_file", options->color_profile.input_file[i])) continue;
		if (READ_CHAR_FULL("input_name", options->color_profile.input_name[i])) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}
	i++;
	options_parse_func_set_data(parser_data, GINT_TO_POINTER(i));

}

static void options_load_marks_tooltips(GQParserData *parser_data, GMarkupParseContext *, const gchar *, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	gint i = GPOINTER_TO_INT(data);
	if (i < 0 || i >= FILEDATA_MARKS_SIZE) return;
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;
		if (READ_CHAR_FULL("text",  options->marks_tooltips[i])) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}
	i++;
	options_parse_func_set_data(parser_data, GINT_TO_POINTER(i));

}

static void options_load_disabled_plugins(GQParserData *parser_data, GMarkupParseContext *, const gchar *, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	gint i = GPOINTER_TO_INT(data);
	struct {
		gchar *path;
	} tmp;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;
		tmp.path = nullptr;
		if (READ_CHAR_FULL("path", tmp.path))
			{
			options->disabled_plugins = g_list_append(options->disabled_plugins, g_strdup(tmp.path));
			continue;
			}

		log_printf("unknown attribute %s = %s\n", option, value);
		}
	i++;
	options_parse_func_set_data(parser_data, GINT_TO_POINTER(i));
}

/*
 *-----------------------------------------------------------------------------
 * xml file structure (private)
 *-----------------------------------------------------------------------------
 */
struct GQParserData
{
	GList *parse_func_stack;
	gboolean startup; /* reading config for the first time - add commandline and defaults */
};

static const gchar *options_get_id(const gchar **attribute_names, const gchar **attribute_values)
{
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (strcmp(option, "id") == 0) return value;

		}
	return nullptr;
}


void options_parse_leaf(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **, const gchar **, gpointer, GError **)
{
	log_printf("unexpected: %s\n", element_name);
	options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
}

static void options_parse_color_profiles(GQParserData *parser_data, GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **error)
{
	if (g_ascii_strcasecmp(element_name, "profile") == 0)
		{
		options_load_profile(parser_data, context, element_name, attribute_names, attribute_values, data, error);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <profile>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_marks_tooltips(GQParserData *parser_data, GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **error)
{
	if (g_ascii_strcasecmp(element_name, "tooltip") == 0)
		{
		options_load_marks_tooltips(parser_data, context, element_name, attribute_names, attribute_values, data, error);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <profile>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void class_filter_load_filter_type(const gchar **attribute_names, const gchar **attribute_values)
{
	// This is called with all attributes for a given XML element.  So for
	// a sample input of:
	// <filter_type filter = "RAW Image" enabled = "true" />
	// attribute_names will be {"filter", "enabled"} and attribute_values
	// will be {"RAW Image", "true"}.
	// For a sample input of:
	// <filter_type enabled = "true" filter = "RAW Image" />
	// attribute_names will be {"enabled", "filter"} and attribute_values
	// will be {"true", "RAW Image"}.

	const gchar *enabled_name = nullptr;
	const gchar *enabled_value = nullptr;
	int format_class_index = -1;

	// In this loop, we iterate through matching attribute/value pairs in
	// tandem, looking for a "filter" value and an "enabled" value.
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		// If the name is "filter" and the value is in our
		// format_class_list, then stash the format class index.
		if (g_strcmp0("filter", option) == 0)
			{
			for (int i = 0; i < FILE_FORMAT_CLASSES; i++)
				{
				if (g_strcmp0(format_class_list[i], value) == 0)
					{
					format_class_index = i;
					break;
					}
				}
			continue;
			}

		if (g_strcmp0("enabled", option) == 0)
			{
			enabled_name = option;
			enabled_value = value;
			continue;
			}

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	if (enabled_name == nullptr || enabled_value == nullptr || format_class_index < 0)
		{
		log_printf("Failed to parse <filter_type> config element\n");
		return;
		}

	if (!read_bool_option(enabled_name, "enabled", enabled_value,
						  &(options->class_filter[format_class_index])))
		{
		log_printf("Failed to load <filter_type> config element with "
			   "class index %d\n", format_class_index);
		}
}

static void options_parse_class_filter(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer, GError **)
{
	if (g_ascii_strcasecmp(element_name, "filter_type") == 0)
		{
		class_filter_load_filter_type(attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <profile>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_disabled_plugins(GQParserData *parser_data, GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **error)
{
	if (g_ascii_strcasecmp(element_name, "plugin") == 0)
		{
		options_load_disabled_plugins(parser_data, context, element_name, attribute_names, attribute_values, data, error);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <profile>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_filter(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer, GError **)
{
	if (g_ascii_strcasecmp(element_name, "file_type") == 0)
		{
		filter_load_file_type(attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <filter>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_filter_end(GQParserData *parser_data, GMarkupParseContext *, const gchar *, gpointer, GError **)
{
	if (parser_data->startup) filter_add_defaults();
	filter_rebuild();
}

static void options_parse_keyword_end(GQParserData *, GMarkupParseContext *, const gchar *, gpointer data, GError **)
{
	auto iter_ptr = static_cast<GtkTreeIter *>(data);
	gtk_tree_iter_free(iter_ptr);
}


static void options_parse_keyword(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	auto iter_ptr = static_cast<GtkTreeIter *>(data);
	if (g_ascii_strcasecmp(element_name, "keyword") == 0)
		{
		GtkTreeIter *child = keyword_add_from_config(keyword_tree, iter_ptr, attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_keyword, options_parse_keyword_end, child);
		}
	else
		{
		log_printf("unexpected in <keyword>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}



static void options_parse_keyword_tree(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer, GError **)
{
	if (g_ascii_strcasecmp(element_name, "keyword") == 0)
		{
		GtkTreeIter *iter_ptr = keyword_add_from_config(keyword_tree, nullptr, attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_keyword, options_parse_keyword_end, iter_ptr);
		}
	else
		{
		log_printf("unexpected in <keyword_tree>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}


static void options_parse_global(GQParserData *parser_data, GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **error)
{
	if (g_ascii_strcasecmp(element_name, "color_profiles") == 0)
		{
		options_load_color_profiles(parser_data, context, element_name, attribute_names, attribute_values, data, error);
		options_parse_func_push(parser_data, options_parse_color_profiles, nullptr, GINT_TO_POINTER(0));
		}
	else if (g_ascii_strcasecmp(element_name, "filter") == 0)
		{
		options_parse_func_push(parser_data, options_parse_filter, options_parse_filter_end, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "marks_tooltips") == 0)
		{
		options_load_marks_tooltips(parser_data, context, element_name, attribute_names, attribute_values, data, error);
		options_parse_func_push(parser_data, options_parse_marks_tooltips, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "class_filter") == 0)
		{
		options_parse_func_push(parser_data, options_parse_class_filter, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "keyword_tree") == 0)
		{
		if (!keyword_tree) keyword_tree_new();
		options_parse_func_push(parser_data, options_parse_keyword_tree, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "disabled_plugins") == 0)
		{
		options_load_disabled_plugins(parser_data, context, element_name, attribute_names, attribute_values, data, error);
		options_parse_func_push(parser_data, options_parse_disabled_plugins, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <global>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_global_end(GQParserData *, GMarkupParseContext *, const gchar *, gpointer, GError **)
{
#if !HAVE_EXIV2
	/* some options do not work without exiv2 */
	options->metadata.save_in_image_file = FALSE;
	options->metadata.save_legacy_format = TRUE;
	options->metadata.write_orientation = FALSE;
	DEBUG_1("compiled without Exiv2 - disabling XMP write support");
#endif
}

static void options_parse_pane_exif(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	auto pane = static_cast<GtkWidget *>(data);
	if (g_ascii_strcasecmp(element_name, "entry") == 0)
		{
		bar_pane_exif_entry_add_from_config(pane, attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <pane_exif>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_pane_keywords(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	auto pane = static_cast<GtkWidget *>(data);

	if (g_ascii_strcasecmp(element_name, "expanded") == 0)
		{
		bar_pane_keywords_entry_add_from_config(pane, attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <pane_keywords>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_bar(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	auto bar = static_cast<GtkWidget *>(data);
	if (g_ascii_strcasecmp(element_name, "pane_comment") == 0)
		{
		GtkWidget *pane = bar_find_pane_by_id(bar, PANE_COMMENT, options_get_id(attribute_names, attribute_values));
		if (pane)
			{
			bar_pane_comment_update_from_config(pane, attribute_names, attribute_values);
			}
		else
			{
			pane = bar_pane_comment_new_from_config(attribute_names, attribute_values);
			bar_add(bar, pane);
			}
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
#if HAVE_LIBCHAMPLAIN && HAVE_LIBCHAMPLAIN_GTK
	else if (g_ascii_strcasecmp(element_name, "pane_gps") == 0)
		{
		/* Use this flag to determine if --disable-clutter has been issued */
		if (!options->disable_gpu)
			{
			GtkWidget *pane = bar_find_pane_by_id(bar, PANE_GPS, options_get_id(attribute_names, attribute_values));
			if (pane)
				{
				bar_pane_gps_update_from_config(pane, attribute_names, attribute_values);
				}
			else
				{
				pane = bar_pane_gps_new_from_config(attribute_names, attribute_values);
				bar_add(bar, pane);
				}
			options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
			}
		}
#endif
	else if (g_ascii_strcasecmp(element_name, "pane_exif") == 0)
		{
		GtkWidget *pane = bar_find_pane_by_id(bar, PANE_EXIF, options_get_id(attribute_names, attribute_values));
		if (pane)
			{
			bar_pane_exif_update_from_config(pane, attribute_names, attribute_values);
			}
		else
			{
			pane = bar_pane_exif_new_from_config(attribute_names, attribute_values);
			bar_add(bar, pane);
			}
		options_parse_func_push(parser_data, options_parse_pane_exif, nullptr, pane);
		}
	else if (g_ascii_strcasecmp(element_name, "pane_histogram") == 0)
		{
		GtkWidget *pane = bar_find_pane_by_id(bar, PANE_HISTOGRAM, options_get_id(attribute_names, attribute_values));
		if (pane)
			{
			bar_pane_histogram_update_from_config(pane, attribute_names, attribute_values);
			}
		else
			{
			pane = bar_pane_histogram_new_from_config(attribute_names, attribute_values);
			bar_add(bar, pane);
			}
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "pane_rating") == 0)
		{
		GtkWidget *pane = bar_find_pane_by_id(bar, PANE_RATING, options_get_id(attribute_names, attribute_values));
		if (pane)
			{
			bar_pane_rating_update_from_config(pane, attribute_names, attribute_values);
			}
		else
			{
			pane = bar_pane_rating_new_from_config(attribute_names, attribute_values);
			bar_add(bar, pane);
			}
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "pane_keywords") == 0)
		{
		GtkWidget *pane = bar_find_pane_by_id(bar, PANE_KEYWORDS, options_get_id(attribute_names, attribute_values));
		if (pane)
			{
			bar_pane_keywords_update_from_config(pane, attribute_names, attribute_values);
			}
		else
			{
			pane = bar_pane_keywords_new_from_config(attribute_names, attribute_values);
			bar_add(bar, pane);
			}
		options_parse_func_push(parser_data, options_parse_pane_keywords, nullptr, pane);
		}
	else if (g_ascii_strcasecmp(element_name, "clear") == 0)
		{
		bar_clear(bar);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <bar>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_toolbar(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	auto lw = static_cast<LayoutWindow *>(data);
	if (g_ascii_strcasecmp(element_name, "toolitem") == 0)
		{
		layout_toolbar_add_from_config(lw, TOOLBAR_MAIN, attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "clear") == 0)
		{
		layout_toolbar_clear(lw, TOOLBAR_MAIN);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <toolbar>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_statusbar(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	auto lw = static_cast<LayoutWindow *>(data);
	if (g_ascii_strcasecmp(element_name, "toolitem") == 0)
		{
		layout_toolbar_add_from_config(lw, TOOLBAR_STATUS, attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "clear") == 0)
		{
		layout_toolbar_clear(lw, TOOLBAR_STATUS);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <statusbar>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_dialogs(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer, GError **)
{
	if (g_ascii_strcasecmp(element_name, "window") == 0)
		{
		generic_dialog_windows_load_config(attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <dialogs>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_layout(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer data, GError **)
{
	auto lw = static_cast<LayoutWindow *>(data);
	if (g_ascii_strcasecmp(element_name, "bar") == 0)
		{
		if (!lw->bar)
			{
			GtkWidget *bar = bar_new_from_config(lw, attribute_names, attribute_values);
			layout_bar_set(lw, bar);
			}
		else
			{
			bar_update_from_config(lw->bar, attribute_names, attribute_values, lw, FALSE);
			}

		options_parse_func_push(parser_data, options_parse_bar, nullptr, lw->bar);
		}
	else if (g_ascii_strcasecmp(element_name, "bar_sort") == 0)
		{
		if (g_list_length(layout_window_list) == 1)
			{
			bar_sort_cold_start(lw, attribute_names, attribute_values);
			}
		else
			{
			GtkWidget *bar = bar_sort_new_from_config(lw, attribute_names, attribute_values);
			layout_bar_sort_set(lw, bar);
			gtk_widget_show(lw->bar_sort);
			}
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
	else if (g_ascii_strcasecmp(element_name, "toolbar") == 0)
		{
		options_parse_func_push(parser_data, options_parse_toolbar, nullptr, lw);
		}
	else if (g_ascii_strcasecmp(element_name, "statusbar") == 0)
		{
		options_parse_func_push(parser_data, options_parse_statusbar, nullptr, lw);
		}
	else if (g_ascii_strcasecmp(element_name, "dialogs") == 0)
		{
		options_parse_func_push(parser_data, options_parse_dialogs, nullptr, nullptr);
		}
	else
		{
		log_printf("unexpected in <layout>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}

static void options_parse_layout_end(GQParserData *, GMarkupParseContext *, const gchar *, gpointer data, GError **)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_util_sync(lw);
}

static void options_parse_toplevel(GQParserData *parser_data, GMarkupParseContext *, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer, GError **)
{
	if (g_ascii_strcasecmp(element_name, "gq") == 0)
		{
		/* optional top-level node */
		options_parse_func_push(parser_data, options_parse_toplevel, nullptr, nullptr);
		return;
		}
	if (g_ascii_strcasecmp(element_name, "global") == 0)
		{
		load_global_params(attribute_names, attribute_values);
		options_parse_func_push(parser_data, options_parse_global, options_parse_global_end, nullptr);
		return;
		}

	if (g_ascii_strcasecmp(element_name, "layout") == 0)
		{
		LayoutWindow *lw;
		lw = layout_find_by_layout_id(options_get_id(attribute_names, attribute_values));
		if (lw)
			{
			layout_update_from_config(lw, attribute_names, attribute_values);
			}
		else
			{
			lw = layout_new_from_config(attribute_names, attribute_values, parser_data->startup);
			}
		options_parse_func_push(parser_data, options_parse_layout, options_parse_layout_end, lw);
		}
	else
		{
		log_printf("unexpected in <toplevel>: <%s>\n", element_name);
		options_parse_func_push(parser_data, options_parse_leaf, nullptr, nullptr);
		}
}





/*
 *-----------------------------------------------------------------------------
 * parser
 *-----------------------------------------------------------------------------
 */


struct GQParserFuncData
{
	GQParserStartFunc start_func;
	GQParserEndFunc end_func;
	gpointer data;
};

void options_parse_func_push(GQParserData *parser_data, GQParserStartFunc start_func, GQParserEndFunc end_func, gpointer data)
{
	auto func_data = g_new0(GQParserFuncData, 1);
	func_data->start_func = start_func;
	func_data->end_func = end_func;
	func_data->data = data;

	parser_data->parse_func_stack = g_list_prepend(parser_data->parse_func_stack, func_data);
}

void options_parse_func_pop(GQParserData *parser_data)
{
	g_free(parser_data->parse_func_stack->data);
	parser_data->parse_func_stack = g_list_delete_link(parser_data->parse_func_stack, parser_data->parse_func_stack);
}

void options_parse_func_set_data(GQParserData *parser_data, gpointer data)
{
	auto func = static_cast<GQParserFuncData *>(parser_data->parse_func_stack->data);
	func->data = data;
}


static void start_element(GMarkupParseContext *context,
			  const gchar *element_name,
			  const gchar **attribute_names,
			  const gchar **attribute_values,
			  gpointer user_data,
			  GError **error)
{
	auto parser_data = static_cast<GQParserData *>(user_data);
	auto func = static_cast<GQParserFuncData *>(parser_data->parse_func_stack->data);
	DEBUG_2("start %s", element_name);

	if (func->start_func)
		func->start_func(parser_data, context, element_name, attribute_names, attribute_values, func->data, error);
}

static void end_element(GMarkupParseContext *context,
			  const gchar *element_name,
			  gpointer user_data,
			  GError **error)
{
	auto parser_data = static_cast<GQParserData *>(user_data);
	auto func = static_cast<GQParserFuncData *>(parser_data->parse_func_stack->data);
	DEBUG_2("end %s", element_name);

	if (func->end_func)
		func->end_func(parser_data, context, element_name, func->data, error);

	options_parse_func_pop(parser_data);
}

static GMarkupParser parser = {
	start_element,
	end_element,
	nullptr,
	nullptr,
	nullptr
};

/*
 *-----------------------------------------------------------------------------
 * load configuration (public)
 *-----------------------------------------------------------------------------
 */

gboolean load_config_from_buf(const gchar *buf, gsize size, gboolean startup)
{
	GMarkupParseContext *context;
	gboolean ret = TRUE;

	auto parser_data = g_new0(GQParserData, 1);

	parser_data->startup = startup;
	options_parse_func_push(parser_data, options_parse_toplevel, nullptr, nullptr);

	context = g_markup_parse_context_new(&parser, static_cast<GMarkupParseFlags>(0), parser_data, nullptr);

	if (g_markup_parse_context_parse(context, buf, size, nullptr) == FALSE)
		{
		ret = FALSE;
		DEBUG_1("Parse failed");
		}

	g_free(parser_data);

	g_markup_parse_context_free(context);
	return ret;
}

gboolean load_config_from_file(const gchar *utf8_path, gboolean startup)
{
	gsize size;
	gchar *buf;
	gboolean ret = TRUE;

	if (g_file_get_contents(utf8_path, &buf, &size, nullptr) == FALSE)
		{
		return FALSE;
		}
	ret = load_config_from_buf(buf, size, startup);
	g_free(buf);
	return ret;
}



/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
