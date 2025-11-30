/*
 * Copyright (C) 2024 The Geeqie Team
 *
 * Author: Colin Clark
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

#include "command-line-handling.h"

#include <cmath>
#include <cstdlib>
#include <vector>

#include "cache-maint.h"
#include "cache.h"
#include "collect-io.h"
#include "collect.h"
#include "compat-deprecated.h"
#include "compat.h"
#include "dupe.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "glua.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "logwindow.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "rcfile.h"
#include "slideshow.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "utilops.h"

namespace
{

enum OUTPUT_TYPE {
	GUI, /**< Option requires the GUI */
	TEXT, /**< Option only outputs text to the command line */
	N_A /**< Not Applicable */
};

enum OPTION_TYPE {
	PRIMARY_REMOTE, /**< Option can be used in both primary and remote instances */
	REMOTE, /**< Option can be used only in remote instances */
	NA /**< Not Applicable */
};

struct CommandLineOptionEntry
{
	const gchar *option_name;
	void (*func)(GtkApplication *app, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *);
	OPTION_TYPE option_type;
	OUTPUT_TYPE display_type;
};

/* https://en.wikipedia.org/wiki/ANSI_escape_code */
#define BOLD_ON "\033[1m"
#define BOLD_OFF "\033[0m"

LayoutWindow *lw_id = nullptr; /* points to the window set by the --id option */

/* To enable file names containing newlines to be processed correctly,
 * the --print0 remote option sets returned data to be terminated with a null
 * character rather a newline
 */
gboolean print0 = FALSE;

/**
 * @brief Ensures file path is absolute.
 * @param[in] filename Filepath, absolute or relative to calling directory
 * @param[in] app_command_line
 * @returns absolute path
 *
 * If first character of input filepath is not the directory
 * separator, assume it as a relative path and prepend
 * the directory the remote command was initiated from
 *
 * Return value must be freed with g_free()
 */
gchar *set_cwd(gchar *filename, GApplicationCommandLine *app_command_line)
{
	gchar *temp;

	if (strncmp(filename, G_DIR_SEPARATOR_S, 1) != 0)
		{
		temp = g_build_filename(g_application_command_line_get_cwd(app_command_line), filename, NULL);
		}
	else
		{
		temp = g_strdup(filename);
		}

	return temp;
}

gboolean close_window_cb(gpointer)
{
	if (layout_valid(&lw_id)) layout_menu_close_cb(nullptr, lw_id);

	return G_SOURCE_REMOVE;
}

gchar *config_file_path(const gchar *param)
{
	gchar *path = nullptr;

	if (file_extension_match(param, ".xml"))
		{
		path = g_build_filename(get_window_layouts_dir(), param, NULL);
		}
	else if (file_extension_match(param, nullptr))
		{
		g_autofree gchar *full_name = g_strconcat(param, ".xml", NULL);
		path = g_build_filename(get_window_layouts_dir(), full_name, NULL);
		}

	if (!isfile(path))
		{
		g_free(path);
		path = nullptr;
		}

	return path;
}

gboolean is_config_file(const gchar *param)
{
	g_autofree gchar *name = config_file_path(param);

	return name != nullptr;
}

gboolean wait_cb(gpointer data)
{
	gint position = GPOINTER_TO_INT(data);
	gint x = position >> 16;
	gint y = position - (x << 16);

	gq_gtk_window_move(GTK_WINDOW(lw_id->window), x, y);

	return G_SOURCE_REMOVE;
}

void gq_action(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	gboolean remote_instance;

	remote_instance = g_application_command_line_get_is_remote(app_command_line);

	gchar *text = nullptr;
	g_variant_dict_lookup(command_line_options_dict, "action", "&s", &text);

	layout_valid(&lw_id);

	if (g_strstr_len(text, -1, ".desktop") != nullptr)
		{
		file_util_start_editor_from_filelist(text, layout_selection_list(lw_id), layout_get_path(lw_id), lw_id->window);
		}
	else
		{
		GtkAction *action;

		action = gq_gtk_action_group_get_action(lw_id->action_group, text);
		if (action)
			{
			gq_gtk_action_activate(action);
			}
		else
			{
			g_application_command_line_print(app_command_line, _("Action %s is unknown\n"), text);
			if (!remote_instance)
				{
				exit_program();
				}
			}
		}
}

void gq_action_list(GtkApplication *, GApplicationCommandLine *app_command_line,GVariantDict *, GList *)
{
	gint max_length = 0;

	std::vector<ActionItem> list = get_action_items();

	/* Get the length required for padding */
	for (const ActionItem &action_item : list)
		{
		const auto length = g_utf8_strlen(action_item.name, -1);
		max_length = std::max<gint>(length, max_length);
		}

	/* Pad the action names to the same column for readable output */
	g_autoptr(GString) out_string = g_string_new(nullptr);
	for (const ActionItem &action_item : list)
		{
		g_string_append_printf(out_string, "%-*s", max_length + 4, action_item.name);
		out_string = g_string_append(out_string, action_item.label);
		out_string = g_string_append(out_string, "\n");
		}

	g_application_command_line_print(app_command_line, "%s\n", out_string->str);
}

void gq_back(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	layout_image_prev(lw_id);
}

void gq_cache_metadata(GtkApplication *app, GApplicationCommandLine *, GVariantDict *, GList *)
{
	cache_maintain_home_remote(app, TRUE, FALSE, nullptr);
}

template<gboolean shared, gboolean recurse>
void gq_cache_render(GtkApplication *app, GApplicationCommandLine *, GVariantDict *command_line_options_dict, GList *)
{
	if (shared && !options->thumbnails.spec_standard) return;

	g_autoptr(GString) key = g_string_new("cache-render");
	if (shared) g_string_append(key, "-shared");
	if (recurse) g_string_append(key, "-recurse");

	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, key->str, "&s", &text);

	cache_manager_render_remote(app, text, recurse, shared, nullptr);
}

void gq_cache_shared(GtkApplication *, GApplicationCommandLine *, GVariantDict *command_line_options_dict, GList *)
{
	gchar* text;

	g_variant_dict_lookup(command_line_options_dict, "cache-shared", "&s", &text);

	if (!g_strcmp0(text, "clear"))
		{
		cache_manager_standard_process_remote(TRUE);
		}
	else if (!g_strcmp0(text, "clean"))
		{
		cache_manager_standard_process_remote(FALSE);
		}
}

void gq_cache_thumbs(GtkApplication *app, GApplicationCommandLine *, GVariantDict *command_line_options_dict, GList *)
{
	gchar* text;

	g_variant_dict_lookup(command_line_options_dict, "cache-thumbs", "&s", &text);

	if (!g_strcmp0(text, "clear"))
		{
		cache_maintain_home_remote(app, FALSE, TRUE, nullptr);
		}
	else if (!g_strcmp0(text, "clean"))
		{
		cache_maintain_home_remote(app, FALSE, FALSE, nullptr);
		}
}

void gq_close_window(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	g_idle_add(close_window_cb, nullptr);
}

void gq_config_load(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "config-load", "&s", &text);
	g_autofree gchar *filename = expand_tilde(text);

	if (!g_strstr_len(filename, -1, G_DIR_SEPARATOR_S))
		{
		if (is_config_file(filename))
			{
			g_autofree gchar *tmp = config_file_path(filename);
			std::swap(filename, tmp);
			}
		}

	if (isfile(filename))
		{
		load_config_from_file(filename, FALSE);
		}
	else
		{
		g_application_command_line_print(app_command_line, "remote sent filename that does not exist:\"%s\"\n", filename ? filename : "(null)");
		layout_set_path(nullptr, homedir());
		}
}

#ifdef DEBUG
/**
 * @brief Convert debug input string to integer and set level
 * @param GtkApplication 
 * @param GApplicationCommandLine 
 * @param command_line_options_dict 
 * @param GList 
 * 
 * The debug string should be between 0 and 4. If an illegal text string is
 * input, the conversion will default to integer 0.
 */
void gq_debug(GtkApplication *, GApplicationCommandLine *, GVariantDict *command_line_options_dict, GList *)
{
	g_autofree gchar *debug_level = nullptr;;
	g_variant_dict_lookup(command_line_options_dict, "debug", "s", &debug_level);
	set_debug_level((gint)g_ascii_strtoll(debug_level, nullptr, 10));
}
#endif

void gq_delay(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "delay", "&s", &text);

	gdouble t1;
	gdouble t2;
	gdouble t3;
	gdouble n;
	gint res;

	res = sscanf(text, "%lf:%lf:%lf", &t1, &t2, &t3);
	if (res == 3)
		{
		n = (t1 * 3600) + (t2 * 60) + t3;
		if (n < SLIDESHOW_MIN_SECONDS || n > SLIDESHOW_MAX_SECONDS ||
		        t1 >= 24 || t2 >= 60 || t3 >= 60)
			{
			g_application_command_line_print(app_command_line, _("Remote slideshow delay out of range (%.1f to %.1f)\n"),
			                                 SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS);
			return;
			}
		}
	else if (res == 2)
		{
		n = t1 * 60 + t2;
		if (n < SLIDESHOW_MIN_SECONDS || n > SLIDESHOW_MAX_SECONDS ||
		        t1 >= 60 || t2 >= 60)
			{
			g_application_command_line_print(app_command_line, _("Remote slideshow delay out of range (%.1f to %.1f)\n"),
			                                 SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS);
			return;
			}
		}
	else if (res == 1)
		{
		n = t1;
		if (n < SLIDESHOW_MIN_SECONDS || n > SLIDESHOW_MAX_SECONDS)
			{
			g_application_command_line_print(app_command_line,_("Remote slideshow delay out of range (%.1f to %.1f)\n"),
			                                 SLIDESHOW_MIN_SECONDS, SLIDESHOW_MAX_SECONDS);
			return;
			}
		}
	else
		{
		n = 0;
		}

	options->slideshow.delay = static_cast<gint>((n * 10.0) + 0.01);
}

void file_load_no_raise(const gchar *text, GApplicationCommandLine *app_command_line)
{
	g_autofree gchar *tilde_filename = nullptr;

	g_autofree gchar *tmp_file = download_web_file(text, TRUE, nullptr);
	if (!tmp_file)
		{
		tilde_filename = expand_tilde(text);
		}
	else
		{
		tilde_filename = g_steal_pointer(&tmp_file);
		}

	g_autofree gchar *filename = set_cwd(tilde_filename, app_command_line);

	if (isfile(filename))
		{
		if (file_extension_match(filename, GQ_COLLECTION_EXT))
			{
			collection_window_new(filename);
			}
		else
			{
			layout_set_path(lw_id, filename);
			}
		}
	else if (isdir(filename))
		{
		layout_set_path(lw_id, filename);
		}
	else
		{
		/* shoould not happen */
		g_application_command_line_print(app_command_line, "File " BOLD_ON "%s" BOLD_OFF " does not exist\n",  filename);
		}
}

template<gboolean recurse>
void gq_dupes(GtkApplication *app, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *path;
	g_variant_dict_lookup(command_line_options_dict, recurse ? "dupes-recurse" : "dupes", "&s", &path);

	g_autofree gchar *folder_path = expand_tilde(path);
	if (!isdir(folder_path))
		{
		g_autofree gchar *notification_message = g_strdup_printf("\"%s\"%s", folder_path, _(" is not a folder"));
		cache_maintenance_notification(app, notification_message, FALSE);
		g_application_command_line_print(app_command_line, "%s\n", notification_message);

		exit(EXIT_FAILURE);
		}

	dupe_window_add_folder(folder_path, recurse);
}

void gq_dupes_export(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	g_autoptr(GString) output_string = export_duplicates_data_command_line();

	g_application_command_line_print(app_command_line, "%s\n", output_string->str);
}

void gq_file(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{

	gchar *text = nullptr;
	g_variant_dict_lookup(command_line_options_dict, "file", "&s", &text);

	if (text)
		{
		file_load_no_raise(text, app_command_line);
		gtk_window_present(GTK_WINDOW(lw_id->window));
		}
}

void gq_File(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "File", "&s", &text);

	file_load_no_raise(text, app_command_line);
}

void gq_file_extensions(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	GList *extensions_list = pixbuf_gdk_known_extensions();

	for (GList *work = filter_get_list(); work; work = work->next)
		{
		auto *fe = static_cast<FilterEntry *>(work->data);

		if (!g_list_find_custom(extensions_list, fe->key, reinterpret_cast<GCompareFunc>(g_strcmp0)))
			{
			extensions_list = g_list_insert_sorted(extensions_list, g_strdup(fe->key), reinterpret_cast<GCompareFunc>(g_strcmp0));
			}
		}

	g_autoptr(GString) types_string = string_list_join(extensions_list, " ");

	g_list_free_full(extensions_list, g_free);

	g_application_command_line_print(app_command_line, "%s\n", types_string->str);
}

void gq_first(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	layout_image_first(lw_id);
}

void gq_fullscreen(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	layout_image_full_screen_toggle(lw_id);
}

void gq_geometry(GtkApplication *, GApplicationCommandLine *, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "geometry", "&s", &text);

	if (text[0] == '+')
		{
		g_auto(GStrv) geometry = g_strsplit_set(text, "+", 3);
		if (geometry[1] != nullptr && geometry[2] != nullptr )
			{
			gq_gtk_window_move(GTK_WINDOW(lw_id->window), atoi(geometry[1]), atoi(geometry[2]));
			}
		}
	else
		{
		g_auto(GStrv) geometry = g_strsplit_set(text, "+x", 4);
		if (geometry[0] != nullptr && geometry[1] != nullptr)
			{
			gtk_window_resize(GTK_WINDOW(lw_id->window), atoi(geometry[0]), atoi(geometry[1]));
			}
		if (geometry[2] != nullptr && geometry[3] != nullptr)
			{
			/* There is an occasional problem with a window_move immediately after a window_resize */
			g_idle_add(wait_cb, GINT_TO_POINTER((atoi(geometry[2]) << 16) + atoi(geometry[3])));
			}
		}
}

void gq_get_collection(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "get-collection", "&s", &text);

	if (is_collection(text))
		{
		g_autoptr(GString) contents = collection_contents(text, g_string_new(nullptr));
		g_application_command_line_print(app_command_line, "%s%c",  contents->str, print0 ? 0 : '\n');
		}
	else
		{
		g_application_command_line_print(app_command_line, "Collection " BOLD_ON "%s" BOLD_OFF " does not exist\n", text);
		}
}

void gq_get_collection_list(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "get-collection-list", "&s", &text);

	GList *collection_list = nullptr;
	GList *work;
	g_autoptr(GString) out_string = g_string_new(nullptr);

	collect_manager_list(&collection_list, nullptr, nullptr);

	work = collection_list;
	while (work)
		{
		auto collection_name = static_cast<const gchar *>(work->data);
		out_string = g_string_append(out_string, collection_name);
		out_string = g_string_append_c(out_string, print0 ? 0 : '\n');

		work = work->next;
		}

	g_application_command_line_print(app_command_line, "%s%c", out_string->str, print0 ? 0 : '\n');

	g_list_free_full(collection_list, g_free);
}

void gq_get_destination(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "get-destination", "&s", &text);

	g_autofree gchar *filename = expand_tilde(text);
	FileData *fd = file_data_new_group(filename);

	if (fd->change && fd->change->dest)
		{
		g_application_command_line_print(app_command_line, "%s",  fd->change->dest);
		}
}

void gq_get_file_info(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	if (!layout_valid(&lw_id)) return;

	const gchar *filename = image_get_path(lw_id->image);
	if (!filename) return;

	FileData *fd = file_data_new_group(filename);

	g_autoptr(GString) out_string = g_string_new(nullptr);

	FileFormatClass format_class = fd->pixbuf ? filter_file_get_class(filename) : FORMAT_CLASS_UNKNOWN;
	g_string_append_printf(out_string, _("Class: %s\n"), format_class_list[format_class]);

	if (fd->page_total > 1)
		{
		g_string_append_printf(out_string, _("Page no: %d/%d\n"), fd->page_num + 1, fd->page_total);
		}

	if (fd->exif)
		{
		g_autofree gchar *country_name = exif_get_data_as_text(fd->exif, "formatted.countryname");
		if (country_name)
			{
			g_string_append_printf(out_string, _("Country name: %s\n"), country_name);
			}

		g_autofree gchar *country_code = exif_get_data_as_text(fd->exif, "formatted.countrycode");
		if (country_code)
			{
			g_string_append_printf(out_string, _("Country code: %s\n"), country_code);
			}

		g_autofree gchar *timezone = exif_get_data_as_text(fd->exif, "formatted.timezone");
		if (timezone)
			{
			g_string_append_printf(out_string, _("Timezone: %s\n"), timezone);
			}

		g_autofree gchar *local_time = exif_get_data_as_text(fd->exif, "formatted.localtime");
		if (local_time)
			{
			g_string_append_printf(out_string, ("Local time: %s\n"), local_time);
			}
		}

	if (fd->marks > 0)
		{
		out_string = g_string_append(out_string, _("Marks:"));

		for (gint i = 0; i < FILEDATA_MARKS_SIZE; i++)
			{
			if ((fd->marks & (1 << i) ) != 0)
				{
				g_string_append_printf(out_string, (" %d"), i + 1);
				}
			}
		out_string = g_string_append(out_string, "\n");
		}

	g_autofree gchar *thumb_file = cache_find_location(CACHE_TYPE_THUMB, filename);
	if (thumb_file)
		{
		g_string_append_printf(out_string, _("Thumb: %s\n"), thumb_file);
		}

	g_application_command_line_print(app_command_line, "%s%c", out_string->str, print0 ? 0 : '\n');

	file_data_unref(fd);
}

template<bool recurse>
void gq_get_filelist(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	g_autoptr(GString) key = g_string_new("get-filelist");
	if (recurse) g_string_append(key, "-recurse");

	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, key->str, "&s", &text);

	FileData *dir_fd;
	if (strcmp(text, "") == 0)
		{
		if (!layout_valid(&lw_id)) return;

		dir_fd = file_data_new_dir(lw_id->dir_fd->path);
		}
	else
		{
		g_autofree gchar *tilde_filename = expand_tilde(text);
		if (!isdir(tilde_filename)) return;

		dir_fd = file_data_new_dir(tilde_filename);
		}

	g_autoptr(FileDataList) list = nullptr;
	if (recurse)
		{
		list = filelist_recursive(dir_fd);
		}
	else
		{
		filelist_read(dir_fd, &list, nullptr);
		}

	g_autoptr(GString) out_string = g_string_new(nullptr);
	for (GList *work = list; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		g_string_append(out_string, fd->path);

		FileFormatClass format_class = filter_file_get_class(fd->path);
		switch (format_class)
			{
			case FORMAT_CLASS_IMAGE:
				out_string = g_string_append(out_string, "    Class: Image");
				break;
			case FORMAT_CLASS_RAWIMAGE:
				out_string = g_string_append(out_string, "    Class: RAW image");
				break;
			case FORMAT_CLASS_META:
				out_string = g_string_append(out_string, "    Class: Metadata");
				break;
			case FORMAT_CLASS_VIDEO:
				out_string = g_string_append(out_string, "    Class: Video");
				break;
			case FORMAT_CLASS_COLLECTION:
				out_string = g_string_append(out_string, "    Class: Collection");
				break;
			case FORMAT_CLASS_DOCUMENT:
				out_string = g_string_append(out_string, "    Class: Document");
				break;
			case FORMAT_CLASS_ARCHIVE:
				out_string = g_string_append(out_string, "    Class: Archive");
				break;
			case FORMAT_CLASS_UNKNOWN:
				out_string = g_string_append(out_string, "    Class: Unknown");
				break;
			default:
				out_string = g_string_append(out_string, "    Class: Unknown");
				break;
			}

		out_string = g_string_append(out_string, "\n");
		}

	g_application_command_line_print(app_command_line, "%s\n",  out_string->str);

	file_data_unref(dir_fd);
}

void gq_get_rectangle(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	if (!options->draw_rectangle) return;
	if (!layout_valid(&lw_id)) return;

	auto *pr = PIXBUF_RENDERER(lw_id->image->pr);
	if (!pr) return;

	const auto [x1, y1, x2, y2] = image_get_rectangle();

	g_autofree gchar *rectangle_info = g_strdup_printf(_("%dx%d+%d+%d"),
	                                                   std::abs(x1 - x2),
	                                                   std::abs(y1 - y2),
	                                                   std::min(x1, x2),
	                                                   std::min(y1, y2));

	g_application_command_line_print(app_command_line, "%s\n", rectangle_info);
}

void gq_get_render_intent(GtkApplication *, GApplicationCommandLine *app_command_line,GVariantDict *, GList *)
{
	g_autofree gchar *render_intent = nullptr;

	switch (options->color_profile.render_intent)
		{
		case 0:
			render_intent = g_strdup("Perceptual");
			break;
		case 1:
			render_intent = g_strdup("Relative Colorimetric");
			break;
		case 2:
			render_intent = g_strdup("Saturation");
			break;
		case 3:
			render_intent = g_strdup("Absolute Colorimetric");
			break;
		default:
			render_intent = g_strdup("none");
			break;
		}

	g_application_command_line_print(app_command_line, "%s\n",  render_intent);
}

void gq_get_selection(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	if (!layout_valid(&lw_id)) return;

	g_autoptr(FileDataList) selected = layout_selection_list(lw_id);  // Keep copy to free.
	g_autoptr(GString) out_string = g_string_new(nullptr);

	GList *work = selected;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		g_assert(fd->magick == FD_MAGICK);

		g_string_append_printf(out_string, "%s    %s\n",
		                       fd->path,
		                       format_class_list[filter_file_get_class(fd->path)]);

		work = work->next;
		}

	g_application_command_line_print(app_command_line, "%s\n",  out_string->str);
}


void gq_get_sidecars(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "get-sidecars", "&s", &text);
	g_autofree gchar *filename = expand_tilde(text);

	if (isfile(filename))
		{
		FileData *fd = file_data_new_group(filename);

		GList *work;
		if (fd->parent)
			{
			fd = fd->parent;
			}

		g_application_command_line_print(app_command_line, "%s\n", fd->path);

		work = fd->sidecar_files;

		while (work)
			{
			fd = static_cast<FileData *>(work->data);
			work = work->next;
			g_application_command_line_print(app_command_line, "%s\n", fd->path);
			}
		}
}

#ifdef DEBUG
void gq_grep(GtkApplication *, GApplicationCommandLine *, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "grep", "&s", &text);
	set_regexp(text);
}
#endif

void gq_id(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "id", "&s", &text);

	lw_id = layout_find_by_layout_id(text);
	if (!lw_id)
		{
		g_application_command_line_print(app_command_line, "Layout window ID " BOLD_ON "%s" BOLD_OFF " does not exist\n",text);
		}
}

void gq_last(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	layout_image_last(lw_id);
}

void gq_log_file(GtkApplication *, GApplicationCommandLine *, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "log-file", "&s", &text);

	command_line->log_file = path_from_utf8(text);
}

void gq_lua(GtkApplication *, GApplicationCommandLine *app_command_line,
            [[maybe_unused]] GVariantDict *command_line_options_dict, GList *)
{
#if HAVE_LUA
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "lua", "&s", &text);

	g_auto(GStrv) lua_command = g_strsplit(text, ",", 2);
	if (lua_command[0] && lua_command[1])
		{
		FileData *fd = file_data_new_group(lua_command[0]);
		g_autofree gchar *result = g_strdup(lua_callvalue(fd, lua_command[1], nullptr));
		if (result)
			{
			g_application_command_line_print(app_command_line, "%s\n", result);
			}
		else
			{
			g_application_command_line_print(app_command_line, _("lua error: no data\n"));
			}
		}
	else
		{
		g_application_command_line_print(app_command_line, _("lua error: no data\n"));
		}
#else
	g_application_command_line_print(app_command_line, _("Lua is not available\n"));
#endif
}

void gq_new_window(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	LayoutWindow *lw = get_current_layout();
	if (!lw) return;

	lw_id = layout_new_from_default();

	layout_set_path(lw_id, g_application_command_line_get_cwd(app_command_line));
}

void gq_next(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	layout_image_next(lw_id);
}

void gq_pixel_info(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	if (!layout_valid(&lw_id)) return;

	auto *pr = PIXBUF_RENDERER(lw_id->image->pr);
	if (!pr) return;

	gint width;
	gint height;
	pixbuf_renderer_get_image_size(pr, &width, &height);
	if (width < 1 || height < 1) return;

	GdkPoint pixel;
	pixbuf_renderer_get_mouse_position(pr, pixel);
	if (pixel.x < 0 || pixel.y < 0) return;

	gint r_mouse;
	gint g_mouse;
	gint b_mouse;
	gint a_mouse;
	pixbuf_renderer_get_pixel_colors(pr, pixel, &r_mouse, &g_mouse, &b_mouse, &a_mouse);

	g_autofree gchar *pixel_info = nullptr;
	if (gdk_pixbuf_get_has_alpha(pr->pixbuf))
		{
		pixel_info = g_strdup_printf(_("[%d,%d]: RGBA(%3d,%3d,%3d,%3d)"),
		                             pixel.x, pixel.y,
		                             r_mouse, g_mouse, b_mouse, a_mouse);
		}
	else
		{
		pixel_info = g_strdup_printf(_("[%d,%d]: RGB(%3d,%3d,%3d)"),
		                             pixel.x, pixel.y,
		                             r_mouse, g_mouse, b_mouse);
		}

	g_application_command_line_print(app_command_line, "%s\n", pixel_info);
}

void gq_print0(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	print0 = TRUE;

}

gboolean gq_quit_idle_cb(gpointer)
{
	exit_program();

	return G_SOURCE_REMOVE;
}

void gq_quit(GtkApplication *app, GApplicationCommandLine *, GVariantDict *, GList *)
{
	/* Schedule exit when idle. If done directly this error is generated:
	 * GDBus.Error:org.freedesktop.DBus.Error.NoReply: Message recipient disconnected from message bus without replying
	 * This gives the application enough time to finish any pending D-Bus communication.
	 */
	g_idle_add(gq_quit_idle_cb, app);
}

void gq_raise(GtkApplication *, GApplicationCommandLine *,GVariantDict *, GList *)
{
	gtk_window_present(GTK_WINDOW(lw_id->window));
}

void gq_selection_add(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "selection-add", "&s", &text);

	FileData *fd_to_select = nullptr;
	if (strcmp(text, "") == 0)
		{
		// No file specified, use current fd.
		fd_to_select = layout_image_get_fd(lw_id);
		}
	else
		{
		// Search through the current file list for a file matching the specified path.
		// "Match" is either a basename match or a file path match.
		g_autofree gchar *path = expand_tilde(text);
		g_autofree gchar *filename = g_path_get_basename(path);
		g_autofree gchar *slash_plus_filename = g_strdup_printf("%s%s", G_DIR_SEPARATOR_S, filename);

		g_autoptr(FileDataList) file_list = layout_list(lw_id);
		for (GList *work = file_list; work && !fd_to_select; work = work->next)
			{
			auto fd = static_cast<FileData *>(work->data);
			if (!strcmp(path, fd->path) || g_str_has_suffix(fd->path, slash_plus_filename))
				{
				fd_to_select = file_data_ref(fd);
				continue;  // will exit loop.
				}

			for (GList *sidecar = fd->sidecar_files; sidecar && !fd_to_select; sidecar = sidecar->next)
				{
				auto side_fd = static_cast<FileData *>(sidecar->data);
				if (!strcmp(path, side_fd->path)
				        || g_str_has_suffix(side_fd->path, slash_plus_filename))
					{
					fd_to_select = file_data_ref(side_fd);
					continue;  // will exit both nested loops.
					}
				}
			}

		if (!fd_to_select)
			{
			g_application_command_line_print(app_command_line, "File " BOLD_ON "%s" BOLD_OFF " is not in the current folder " BOLD_ON "%s" BOLD_OFF "%c",
			                                 filename, g_application_command_line_get_cwd(app_command_line), print0 ? 0 : '\n');
			}
		}

	if (fd_to_select)
		{
		g_autoptr(FileDataList) to_select = g_list_append(nullptr, fd_to_select);
		// Using the "_list" variant doesn't clear the existing selection.
		layout_select_list(lw_id, to_select);
		}
}

void gq_selection_clear(GtkApplication *, GApplicationCommandLine *,GVariantDict *, GList *)
{
	layout_select_none(lw_id);  // Checks lw_id validity internally.
}

void gq_selection_remove(GtkApplication *, GApplicationCommandLine *app_command_line,GVariantDict *command_line_options_dict, GList *)
{
	gchar *text = nullptr;
	g_variant_dict_lookup(command_line_options_dict, "selection-remove", "&s", &text);

	GList *selected = layout_selection_list(lw_id);  // Keep copy to free.
	if (!selected)
		{
		g_application_command_line_print(app_command_line, _("remote sent --selection-remove with empty selection.\n"));

		return;
		}

	FileData *fd_to_deselect = nullptr;
	g_autofree gchar *path = nullptr;
	g_autofree gchar *filename = nullptr;
	g_autofree gchar *slash_plus_filename = nullptr;

	if (!text || strcmp(text, "") == 0)
		{
		// No file specified, use current fd.
		fd_to_deselect = layout_image_get_fd(lw_id);
		if (!fd_to_deselect)
			{
			file_data_list_free(selected);
			g_application_command_line_print(app_command_line, _("remote sent \"--selection-remove:\" with no current image\n"));
			return;
			}
		}
	else
		{
		// Search through the selection list for a file matching the specified path.
		// "Match" is either a basename match or a file path match.
		path = expand_tilde(text);
		filename = g_path_get_basename(path);
		slash_plus_filename = g_strdup_printf("%s%s", G_DIR_SEPARATOR_S, filename);
		}

	GList *prior_link = nullptr;  // Stash base for link removal to avoid a second traversal.
	GList *link_to_remove = nullptr;
	for (GList *work = selected; work; prior_link = work, work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);
		if (fd_to_deselect)
			{
			if (fd == fd_to_deselect)
				{
				link_to_remove = work;
				break;
				}
			}
		else
			{
			// path, filename, and slash_plus_filename should be defined.

			if (!strcmp(path, fd->path) || g_str_has_suffix(fd->path, slash_plus_filename))
				{
				link_to_remove = work;
				break;
				}
			}
		}

	if (!link_to_remove)
		{
		if (fd_to_deselect)
			{
			g_application_command_line_print(app_command_line, _("remote sent \"--selection-remove=\" but current image is not selected\n"));
			}
		else
			{
			g_application_command_line_print(app_command_line, "File " BOLD_ON "%s" BOLD_OFF " is not selected\n", filename);
			}
		}
	else
		{
		if (link_to_remove == selected)
			{
			// Remove first link.
			selected = g_list_remove_link(selected, link_to_remove);
			}
		else
			{
			// Remove a subsequent link.
			prior_link = g_list_remove_link(prior_link, link_to_remove);
			}

		file_data_list_free(link_to_remove);
		link_to_remove = nullptr;

		// Re-select all but the deselected item.
		layout_select_none(lw_id);
		layout_select_list(lw_id, selected);
		}

	file_data_list_free(selected);
	file_data_unref(fd_to_deselect);
}

void gq_show_log_window(GtkApplication *, GApplicationCommandLine *,GVariantDict *, GList *)
{
	log_window_new(lw_id);
}

void gq_slideshow(GtkApplication *, GApplicationCommandLine *,GVariantDict *, GList *)
{
	layout_image_slideshow_toggle(lw_id);
}

void gq_slideshow_recurse(GtkApplication *, GApplicationCommandLine *,GVariantDict *command_line_options_dict, GList *)
{
	GList *list;
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "slideshow-recurse", "&s", &text);

	g_autofree gchar *tilde_filename = expand_tilde(text);
	FileData *dir_fd = file_data_new_dir(tilde_filename);

	layout_valid(&lw_id);
	list = filelist_recursive_full(dir_fd, lw_id->options.file_view_list_sort);
	file_data_unref(dir_fd);
	if (!list) return;

	layout_image_slideshow_stop(lw_id);
	layout_image_slideshow_start_from_list(lw_id, list);
}

void gq_tell(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *, GList *)
{
	g_autofree gchar *out_string = nullptr;

	layout_valid(&lw_id);

	const gchar *filename = image_get_path(lw_id->image);
	if (filename)
		{
		if (lw_id->image->collection && lw_id->image->collection->name)
			{
			g_autofree gchar *collection_name = remove_extension_from_path(lw_id->image->collection->name);
			out_string = g_strconcat(filename, "    Collection: ", collection_name, NULL);
			}
		else
			{
			out_string = g_strconcat(filename, NULL);
			}
		}
	else
		{
		out_string = g_strconcat(lw_id->dir_fd->path, G_DIR_SEPARATOR_S, NULL);
		}

	g_application_command_line_print(app_command_line, "%s%c", out_string, print0 ? 0 : '\n');
}

void gq_tools(GtkApplication *, GApplicationCommandLine *, GVariantDict *, GList *)
{
	gboolean popped;
	gboolean hidden;

	layout_tools_float_get(lw_id, &popped, &hidden) && hidden;
	layout_tools_float_set(lw_id, popped, !hidden);
}

void gq_version(GtkApplication *, GApplicationCommandLine *app_command_line,GVariantDict *, GList *)
{
	g_application_command_line_print(app_command_line, "%s %s GTK%d\n", GQ_APPNAME, VERSION, gtk_major_version);
}

void gq_get_window_list(GtkApplication *, GApplicationCommandLine *app_command_line,GVariantDict *, GList *)
{
	g_autofree gchar *window_list = layout_get_window_list();

	g_application_command_line_print(app_command_line, "%s\n", window_list);
}

void gq_view(GtkApplication *, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	const gchar *text;
	g_variant_dict_lookup(command_line_options_dict, "view", "&s", &text);

	g_autofree gchar *tilde_filename = expand_tilde(text);

	g_autofree gchar *filename = set_cwd(tilde_filename, app_command_line);

	view_window_new(file_data_new_group(filename));
}

/**
 * @brief Parse all non-option command line parameters
 * @param app_command_line
 * @returns List of files on command line (*gchar)
 *
 */
GList *directories_collections_files(GtkApplication *app, GApplicationCommandLine *app_command_line)
{
	GList *file_list = nullptr;
	const gchar *current_arg;
	gboolean remote_instance;
	gchar **argv=nullptr;
	gchar *download_web_tmp_file;
	gint argc;

	remote_instance = g_application_command_line_get_is_remote(app_command_line);

	argv = g_application_command_line_get_arguments(app_command_line, &argc);

	for (gint i = 1; i < argc; i++)
		{
		current_arg = argv[i];
		g_autofree gchar *real_path = g_canonicalize_filename(current_arg, nullptr);

		if (isdir(real_path))
			{
			layout_set_path(lw_id, real_path);
			}
		else if (is_collection(current_arg))
			{
			const gchar *path = collection_path(current_arg);
			collection_window_new(path);
			}
		else if (isfile(real_path))
			{
			file_list = g_list_prepend(file_list, g_strdup(real_path));
			}
		else
			{
			download_web_tmp_file = download_web_file(current_arg, FALSE, nullptr);

			if (download_web_tmp_file)
				{
				file_list = g_list_prepend(file_list, download_web_tmp_file);
				}
			else
				{
				g_application_command_line_print(app_command_line, "%s%s%s%s%s", "Unknown parameter: ", BOLD_ON, current_arg, BOLD_OFF, "\n");
				if (!remote_instance)
					{
					g_application_quit(G_APPLICATION(app));
					exit(EXIT_FAILURE);
					}
				}
			}
		}

	return g_list_reverse(file_list);
}

/**
 * @brief Process command line file or dir parameters
 * @param file_list (*gchar)
 *
 */
void process_files(GList *file_list)
{
	if (file_list)
		{
		GList *work;
		gboolean multiple_dirs = FALSE;
		work = file_list;

		g_autofree gchar *basepath = g_path_get_dirname(static_cast<const gchar *>(work->data));

		/* If command line arguments contain multiple dirs, a new
		 * collection will be created to hold them
		 */
		while (work)
			{
			g_autofree gchar *newpath = g_path_get_dirname(static_cast<const gchar *>(work->data));
			if (g_strcmp0(newpath, basepath) != 0)
				{
				multiple_dirs = TRUE;
				break;
				}
			work = work->next;
			}

		if (multiple_dirs)
			{
			CollectWindow *cw;

			cw = collection_window_new(nullptr);
			CollectionData *cd = nullptr;
			cd = cw->cd;
			g_free(cd->path);
			cd->path = nullptr;

			work = file_list;
			while (work)
				{
				collection_add(cd, file_data_new_no_grouping((gchar *)(work->data)), FALSE);
				work = work->next;
				}
			}
		else
			{
			GList *work;
			GList *selected;
			FileData *fd;
			layout_valid(&lw_id);

			selected = nullptr;
			work = file_list;
			layout_set_path(lw_id, static_cast<const gchar *>(work->data));
			while (work)
				{
				fd = file_data_new_simple(static_cast<gchar *>(work->data));
				selected = g_list_append(selected, fd);
				layout_list_sync_fd(lw_id, fd);
				file_data_unref(fd);
				work = work->next;
				}
			layout_select_list(lw_id, selected);
			}
		}
}

/* print0 and id are first so that they can affect other command line entries */
CommandLineOptionEntry command_line_options[] =
{
	{ "print0",                      gq_print0,                      PRIMARY_REMOTE, GUI  },
	{ "id",                          gq_id,                          REMOTE        , N_A  },
	{ "action",                      gq_action,                      PRIMARY_REMOTE, GUI  },
	{ "action-list",                 gq_action_list,                 PRIMARY_REMOTE, TEXT },
	{ "back",                        gq_back,                        PRIMARY_REMOTE, GUI  },
	{ "cache-metadata",              gq_cache_metadata,              PRIMARY_REMOTE, GUI  },
	{ "cache-render",                gq_cache_render<FALSE, FALSE>,  PRIMARY_REMOTE, GUI  },
	{ "cache-render-recurse",        gq_cache_render<FALSE, TRUE>,   PRIMARY_REMOTE, GUI  },
	{ "cache-render-shared",         gq_cache_render<TRUE, FALSE>,   PRIMARY_REMOTE, GUI  },
	{ "cache-render-shared-recurse", gq_cache_render<TRUE, TRUE>,    PRIMARY_REMOTE, GUI  },
	{ "cache-shared",                gq_cache_shared,                PRIMARY_REMOTE, GUI  },
	{ "cache-thumbs",                gq_cache_thumbs,                PRIMARY_REMOTE, GUI  },
	{ "close-window",                gq_close_window,                PRIMARY_REMOTE, GUI  },
	{ "config-load",                 gq_config_load,                 PRIMARY_REMOTE, GUI  },
#ifdef DEBUG
	{ "debug",                       gq_debug,                       PRIMARY_REMOTE, GUI  },
#endif
	{ "delay",                       gq_delay,                       PRIMARY_REMOTE, GUI  },
	{ "file",                        gq_file,                        PRIMARY_REMOTE, GUI  },
	{ "dupes",                       gq_dupes<FALSE>,                PRIMARY_REMOTE, GUI  },
	{ "dupes-export",                gq_dupes_export,                PRIMARY_REMOTE, TEXT },
	{ "dupes-recurse",               gq_dupes<TRUE>,                 PRIMARY_REMOTE, GUI  },
	{ "File",                        gq_File,                        PRIMARY_REMOTE, GUI  },
	{ "file-extensions",             gq_file_extensions,             PRIMARY_REMOTE, TEXT },
	{ "first",                       gq_first,                       PRIMARY_REMOTE, GUI  },
	{ "fullscreen",                  gq_fullscreen,                  PRIMARY_REMOTE, GUI  },
	{ "geometry",                    gq_geometry,                    PRIMARY_REMOTE, GUI  },
	{ "get-collection",              gq_get_collection,              PRIMARY_REMOTE, TEXT },
	{ "get-collection-list",         gq_get_collection_list,         PRIMARY_REMOTE, TEXT },
	{ "get-destination",             gq_get_destination,             PRIMARY_REMOTE, GUI  },
	{ "get-file-info",               gq_get_file_info,               REMOTE        , N_A  },
	{ "get-filelist",                gq_get_filelist<false>,         PRIMARY_REMOTE, GUI  },
	{ "get-filelist-recurse",        gq_get_filelist<true>,          PRIMARY_REMOTE, GUI  },
	{ "get-rectangle",               gq_get_rectangle,               REMOTE        , N_A  },
	{ "get-render-intent",           gq_get_render_intent,           REMOTE        , N_A  },
	{ "get-selection",               gq_get_selection,               REMOTE        , N_A  },
	{ "get-sidecars",                gq_get_sidecars,                REMOTE        , N_A  },
	{ "get-window-list",             gq_get_window_list,             REMOTE        , N_A  },
#ifdef DEBUG
	{ "grep",                        gq_grep,                        PRIMARY_REMOTE, GUI  },
#endif
	{ "last",                        gq_last,                        PRIMARY_REMOTE, GUI  },
	{ "log-file",                    gq_log_file,                    PRIMARY_REMOTE, GUI  },
	{ "lua",                         gq_lua,                         REMOTE        , N_A  },
	{ "new-window",                  gq_new_window,                  PRIMARY_REMOTE, GUI  },
	{ "next",                        gq_next,                        PRIMARY_REMOTE, GUI  },
	{ "pixel-info",                  gq_pixel_info,                  REMOTE        , N_A  },
	{ "quit",                        gq_quit,                        PRIMARY_REMOTE, GUI  },
	{ "raise",                       gq_raise,                       PRIMARY_REMOTE, GUI  },
	{ "selection-add",               gq_selection_add,               REMOTE        , N_A  },
	{ "selection-clear",             gq_selection_clear,             REMOTE        , N_A  },
	{ "selection-remove",            gq_selection_remove,            REMOTE        , N_A  },
	{ "show-log-window",             gq_show_log_window,             PRIMARY_REMOTE, GUI  },
	{ "slideshow-recurse",           gq_slideshow_recurse,           PRIMARY_REMOTE, GUI  },
	{ "slideshow",                   gq_slideshow,                   PRIMARY_REMOTE, GUI  },
	{ "tell",                        gq_tell,                        REMOTE        , N_A  },
	{ "tools",                       gq_tools,                       PRIMARY_REMOTE, GUI  },
	{ "version",                     gq_version,                     PRIMARY_REMOTE, TEXT },
	{ "view",                        gq_view,                        PRIMARY_REMOTE, GUI  },
	{ nullptr,                       nullptr,                        NA,             N_A }
};

/*
 * Cache Maintenance
 */

void gq_cm_quit(GtkApplication *app, GApplicationCommandLine *, GVariantDict *, GList *)
{
	g_application_withdraw_notification(G_APPLICATION(app), "cache_maintenance");

	g_application_quit(G_APPLICATION(app));
}

void gq_cm_dir(GtkApplication *app, GApplicationCommandLine *app_command_line, GVariantDict *command_line_options_dict, GList *)
{
	gboolean remote_instance;
	gint diff_count;
	gsize i = 0;
	gsize size;

	remote_instance = g_application_command_line_get_is_remote(app_command_line);

	if (remote_instance)
		{
		g_application_command_line_print(app_command_line, _("Cache Maintenance is already running\n"));
		return;
		}

	const gchar *path;
	g_variant_dict_lookup(command_line_options_dict, "cache-maintenance", "&s", &path);

	g_autofree gchar *folder_path = expand_tilde(path);
	if (!isdir(folder_path))
		{
		g_autofree gchar *notification_message = g_strdup_printf("\"%s\"%s", folder_path, _(" is not a folder"));
		cache_maintenance_notification(app, notification_message, FALSE);
		g_application_command_line_print(app_command_line, "%s\n", notification_message);

		exit(EXIT_FAILURE);
		}

	g_autofree gchar *rc_path = g_build_filename(get_rc_dir(), RC_FILE_NAME, nullptr);
	if (!isfile(rc_path))
		{
		g_autofree gchar *notification_message = g_strconcat(_("Configuration file path "), rc_path, _(" is not a file"), nullptr);
		cache_maintenance_notification(app, notification_message, FALSE);
		g_application_command_line_print(app_command_line, "%s\n", notification_message);

		exit(EXIT_FAILURE);
		}

	g_autofree gchar *buf_config_file = nullptr;
	if (!g_file_get_contents(rc_path, &buf_config_file, &size, nullptr))
		{
		g_autofree gchar *notification_message = g_strconcat(_("Cannot load "), rc_path, nullptr);
		cache_maintenance_notification(app, notification_message, FALSE);
		g_application_command_line_print(app_command_line, "%s\n", notification_message);

		exit(EXIT_FAILURE);
		}

	/* Load only the <global> section */
	while (i < size)
		{
		diff_count = strncmp("</global>", &buf_config_file[i], 9);
		if (diff_count == 0)
			{
			break;
			}
		i++;
		}
	load_config_from_buf(buf_config_file, i + 9, FALSE);

	if (!options->thumbnails.enable_caching)
		{
		const gchar *notification_message = _("Caching not enabled");
		cache_maintenance_notification(app, notification_message, FALSE);
		g_application_command_line_print(app_command_line, "%s\n", notification_message);

		exit(EXIT_FAILURE);
		}

	cache_maintenance(app, folder_path);
}

CommandLineOptionEntry command_line_options_cache_maintenance[] =
{
	{ "cache-maintenance", gq_cm_dir,  REMOTE, N_A },
	{ "quit",              gq_cm_quit, REMOTE, N_A },
	{ nullptr,             nullptr,    NA,     N_A }
};

} // namespace

gint process_command_line(GtkApplication *app, GApplicationCommandLine *app_command_line, gpointer )
{
	GVariantDict* command_line_options_dict;
	gint i;
	GList *file_list = nullptr;

	/* These values are used for the rest of this command line */
	/* Make lw_id point to current window
	 */
	layout_valid(&lw_id);
	print0 = FALSE;

	command_line_options_dict = g_application_command_line_get_options_dict(app_command_line);

	/* Parse other command line arguments, which should be files, URLs, directories
	 * or collections.
	 * Create the file list before the option processing in case an
	 * option needs to modify the file list.
	 */
	file_list = directories_collections_files(app, app_command_line);

	/* Execute the command line options */
	i = 0;
	while (command_line_options[i].func != nullptr)
		{
		if (g_variant_dict_contains(command_line_options_dict, command_line_options[i].option_name))
			{
			/* Exit if option is a remote only and the instance is primary */
			if (command_line_options[i].option_type == REMOTE)
				{
				if (! g_application_command_line_get_is_remote(app_command_line))
					{
					g_application_command_line_print(app_command_line, "%s%s%s%s%s", _("Geeqie is not running: --"), BOLD_ON, command_line_options[i].option_name, BOLD_OFF, _(" is a Remote command\n"));

					g_application_quit(G_APPLICATION(app));
					exit(EXIT_FAILURE);
					}
				}

			/* Instance is either primary or remote */
			command_line_options[i].func(app, app_command_line, command_line_options_dict, file_list);

			/* If the instance is a primary and the option only outputs text,
			 * e.g. --version, kill the application after the text is output
			 */
			if (! g_application_command_line_get_is_remote(app_command_line))
				{
				if (command_line_options[i].display_type == TEXT)
					{
					g_application_quit(G_APPLICATION(app));
					exit(EXIT_SUCCESS);
					}
				}
			}

		i++;
		}

	process_files(file_list);

	g_list_free_full(file_list, g_free);

	return TRUE;
}

gint process_command_line_cache_maintenance(GtkApplication *app, GApplicationCommandLine *app_command_line, gpointer)
{
	gboolean option_found = FALSE;
	gint i;
	GVariantDict* command_line_options_dict;

	command_line_options_dict = g_application_command_line_get_options_dict(app_command_line);

	/* Execute the command line options */
	i = 0;
	while (command_line_options_cache_maintenance[i].func != nullptr)
		{
		if (g_variant_dict_contains(command_line_options_dict, command_line_options_cache_maintenance[i].option_name))
			{
			command_line_options_cache_maintenance[i].func(app, app_command_line, command_line_options_dict, nullptr);

			option_found = TRUE;
			}

		i++;
		}

	if (!option_found)
		{
		g_application_command_line_print(app_command_line, "%s", _("No option specified\n"));
		cache_maintenance_notification(app, _("No option specified"), FALSE);

		g_application_quit(G_APPLICATION(app));
		}

	return 0;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
