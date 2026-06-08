// SPDX-License-Identifier: GPL-2.0-or-later

#include "convert-configuration.h"

#include "accelerators.h"
#include "main-defines.h"
#include "ui-fileops.h"

namespace
{
void apply_special_renames(GString *name);
gchar *camel_to_kebab(const gchar *input);

gboolean is_app_action(const gchar *action_name)
{
	static const gchar *app_actions[] =
		{
		"About",
		"HelpChangeLog",
		"HelpContents",
		"HelpKbd",
		"HelpNotes",
		"HelpPdf",
		"HelpSearch",
		"HelpShortcuts",
		"NewCollection",
		"NewWindowDefault",
		"OpenCollection",
		"OpenFile",
		"OpenRecent",
		"OpenWith",
		"Preferences",
		"Quit"
		};

	for (const auto *name : app_actions)
		{
		if (g_strcmp0(action_name, name) == 0)
			{
			return TRUE;
			}
		}

	return FALSE;
}

gboolean convert_gtk_accel_lines_to_keyfile(const gchar *input, GKeyFile *keyfile, GError **error)
{
	g_auto(GStrv) lines = g_strsplit(input, "\n", -1);

	g_autoptr(GRegex) regex = g_regex_new(R"re(\(gtk_accel_path\s+"([^"]+)"\s+"([^"]+)"\))re", G_REGEX_OPTIMIZE, (GRegexMatchFlags)0, error);

	if (!regex)
		{
		return FALSE;
		}

	for (gint i = 0; lines[i]; i++)
		{
		gchar *line = g_strstrip(lines[i]);

		/* Skip comments and empty lines
		 */
		if (*line == '\0' || *line == ';')
			{
			continue;
			}

		g_autoptr(GMatchInfo) match = nullptr;

		if (g_regex_match(regex, line, (GRegexMatchFlags)0, &match))
			{
			g_autofree gchar *path  = g_match_info_fetch(match, 1);
			g_autofree gchar *accel = g_match_info_fetch(match, 2);

			/* Use final path component as group name
			 */
			g_autofree gchar *raw_name = g_path_get_basename(path);
			const gchar *scope = is_app_action(raw_name) ? "app." : "win.";

			g_autofree gchar *kebab = camel_to_kebab(raw_name);

			g_autoptr(GString) rewritten = g_string_new(kebab);
			apply_special_renames(rewritten);
			g_autoptr(GString) result = g_string_new("");
			g_string_append(result, scope);
			g_string_append(result, rewritten->str);
			if (!g_strstr_len(result->str, -1, "alt1") && !g_strstr_len(result->str, -1, "alt2"))
				{
				g_key_file_set_string(keyfile,  result->str, "accels", accel);
				}
			}
		}

	return TRUE;
}

gchar *camel_to_kebab(const gchar *input)
{
	GString *out = g_string_new(nullptr);

	for (const gchar *p = input; *p != '\0'; ++p)
		{
		if (g_ascii_isupper(*p))
			{
			if (p != input)
				{
				g_string_append_c(out, '-');
				}

			g_string_append_c(out, g_ascii_tolower(*p));
			}
		else
			{
			g_string_append_c(out, *p);
			}
		}

	return g_string_free(out, FALSE);
}

void apply_special_renames(GString *name)
{
	struct RenameRule
		{
		const gchar *from;
		const gchar *to;
		};

	static const RenameRule rules[] =
		{
		{ "rating0", "rating-0" },
		{ "rating1", "rating-1" },
		{ "rating2", "rating-2" },
		{ "rating3", "rating-3" },
		{ "rating4", "rating-4" },
		{ "rating5", "rating-5" },
		{ "rating-m1", "rating-rejected" },

		{ "crop-four-three", "crop::4-3" },
		{ "crop-one-one", "crop::1-1" },
		{ "crop-sixteen-nine", "crop::16-9" },
		{ "crop-three-two", "crop::3-2" },

		{ "osd1", "osd-1" },
		{ "osd2", "osd-2" },
		{ "osd3", "osd-3" },
		{ "osd4", "osd-4" },

		{ "color-profile0", "color-profile-0" },
		{ "color-profile1", "color-profile-1" },
		{ "color-profile2", "color-profile-2" },
		{ "color-profile3", "color-profile-3" },
		{ "color-profile4", "color-profile-4" },
		{ "color-profile5", "color-profile-5" },

		{ "zoom100", "zoom-1-1" },
		{ "zoom200", "zoom-2-1" },
		{ "zoom25", "zoom-1-4" },
		{ "zoom300", "zoom-3-1" },
		{ "zoom33", "zoom-1-3" },
		{ "zoom400", "zoom-4-1" },
		{ "zoom50", "zoom-1-2" },

		{ "connect-zoom100", "connect-zoom-1-1" },
		{ "connect-zoom200", "connect-zoom-2-1" },
		{ "connect-zoom25", "connect-zoom-1-4" },
		{ "connect-zoom300", "connect-zoom-3-1" },
		{ "connect-zoom33", "connect-zoom-1-3" },
		{ "connect-zoom400", "connect-zoom-4-1" },
		{ "connect-zoom50", "connect-zoom-1-2" },

		{ "rotate180", "rotate-180" },
		{ "rotate-ccw", "rotate-ccw-90" },
		{ "rotate-cw", "rotate-cw-90" },

		{ "histogram-chan", "histogram-channel" },

		{ "add-mark0", "add-mark-0" },
		{ "add-mark1", "add-mark-1" },
		{ "add-mark2", "add-mark-2" },
		{ "add-mark3", "add-mark-3" },
		{ "add-mark4", "add-mark-4" },
		{ "add-mark5", "add-mark-5" },
		{ "add-mark6", "add-mark-6" },
		{ "add-mark7", "add-mark-7" },
		{ "add-mark8", "add-mark-8" },
		{ "add-mark9", "add-mark-9" },

		{ "filter-mark0", "filter-mark-0" },
		{ "filter-mark1", "filter-mark-1" },
		{ "filter-mark2", "filter-mark-2" },
		{ "filter-mark3", "filter-mark-3" },
		{ "filter-mark4", "filter-mark-4" },
		{ "filter-mark5", "filter-mark-5" },
		{ "filter-mark6", "filter-mark-6" },
		{ "filter-mark7", "filter-mark-7" },
		{ "filter-mark8", "filter-mark-8" },
		{ "filter-mark9", "filter-mark-9" },

		{ "toggle-mark0", "toggle-mark-0" },
		{ "toggle-mark1", "toggle-mark-1" },
		{ "toggle-mark2", "toggle-mark-2" },
		{ "toggle-mark3", "toggle-mark-3" },
		{ "toggle-mark4", "toggle-mark-4" },
		{ "toggle-mark5", "toggle-mark-5" },
		{ "toggle-mark6", "toggle-mark-6" },
		{ "toggle-mark7", "toggle-mark-7" },
		{ "toggle-mark8", "toggle-mark-8" },
		{ "toggle-mark9", "toggle-mark-9" },

		{ "select-mark0", "select-mark-0" },
		{ "select-mark1", "select-mark-1" },
		{ "select-mark2", "select-mark-2" },
		{ "select-mark3", "select-mark-3" },
		{ "select-mark4", "select-mark-4" },
		{ "select-mark5", "select-mark-5" },
		{ "select-mark6", "select-mark-6" },
		{ "select-mark7", "select-mark-7" },
		{ "select-mark8", "select-mark-8" },
		{ "select-mark9", "select-mark-9" },

		{ "intersection-mark0", "intersect-mark-0" },
		{ "intersection-mark1", "intersect-mark-1" },
		{ "intersection-mark2", "intersect-mark-2" },
		{ "intersection-mark3", "intersect-mark-3" },
		{ "intersection-mark4", "intersect-mark-4" },
		{ "intersection-mark5", "intersect-mark-5" },
		{ "intersection-mark6", "intersect-mark-6" },
		{ "intersection-mark7", "intersect-mark-7" },
		{ "intersection-mark8", "intersect-mark-8" },
		{ "intersection-mark9", "intersect-mark-9" },

		{ "unselect-mark0", "deselect-mark-0" },
		{ "unselect-mark1", "deselect-mark-1" },
		{ "unselect-mark2", "deselect-mark-2" },
		{ "unselect-mark3", "deselect-mark-3" },
		{ "unselect-mark4", "deselect-mark-4" },
		{ "unselect-mark5", "deselect-mark-5" },
		{ "unselect-mark6", "deselect-mark-6" },
		{ "unselect-mark7", "deselect-mark-7" },
		{ "unselect-mark8", "deselect-mark-8" },
		{ "unselect-mark9", "deselect-mark-9" },


		{ "set-mark0", "set-mark-0" },
		{ "set-mark1", "set-mark-1" },
		{ "set-mark2", "set-mark-2" },
		{ "set-mark3", "set-mark-3" },
		{ "set-mark4", "set-mark-4" },
		{ "set-mark5", "set-mark-5" },
		{ "set-mark6", "set-mark-6" },
		{ "set-mark7", "set-mark-7" },
		{ "set-mark8", "set-mark-8" },
		{ "set-mark9", "set-mark-9" },

		{ "reset-mark0", "clear-mark-0" },
		{ "reset-mark1", "clear-mark-1" },
		{ "reset-mark2", "clear-mark-2" },
		{ "reset-mark3", "clear-mark-3" },
		{ "reset-mark4", "clear-mark-4" },
		{ "reset-mark5", "clear-mark-5" },
		{ "reset-mark6", "clear-mark-6" },
		{ "reset-mark7", "clear-mark-7" },
		{ "reset-mark8", "clear-mark-8" },
		{ "reset-mark9", "clear-mark-9" },
		};

	for (const auto &rule : rules)
		{
		if (g_strcmp0(name->str, rule.from) == 0)
			{
			g_string_assign(name, rule.to);
			return;
			}
		}
}

} //namespace

/**
 * @brief Update geeqierc.xml
 * @param 
 * @returns
 * 
 * This should be run once after 
 * If this is the first run with the revised action names code,
 * convert geeqierc.xml toolbars sections to the new format e.g.:
 *
 * 'SetMark0' is changed to 'set-mark-0'
 */
void convert_configuration_file()
{
	g_autofree gchar *rc_path = g_build_filename(get_rc_dir(), RC_FILE_NAME, nullptr);

	g_autofree gchar *contents = nullptr;
	gsize length = 0;
	GError *error = nullptr;

	if (!g_file_get_contents(rc_path, &contents, &length, &error))
		{
		log_printf("Failed to read file: %s", error->message);
		g_clear_error(&error);
		return;
		}

	g_autoptr(GRegex) regex = g_regex_new(
		R"((<toolitem\b[^>]*\baction\s*=\s*")([A-Za-z][A-Za-z0-9-]*)("))",
		G_REGEX_MULTILINE,
		static_cast<GRegexMatchFlags>(0),
		&error);

	if (!regex)
		{
		log_printf("Failed to compile regex: %s", error->message);
		g_clear_error(&error);
		return;
		}

	g_autoptr(GMatchInfo) match_info = nullptr;
	g_regex_match(regex, contents, static_cast<GRegexMatchFlags>(0), &match_info);

	g_autoptr(GString) result = g_string_new("");
	gint last_end = 0;

	while (g_match_info_matches(match_info))
		{
		gint start_pos = 0;
		gint end_pos = 0;
		g_match_info_fetch_pos(match_info, 0, &start_pos, &end_pos);

		g_string_append_len(result, contents + last_end, start_pos - last_end);

		g_autofree gchar *prefix = g_match_info_fetch(match_info, 1);
		g_autofree gchar *action = g_match_info_fetch(match_info, 2);
		g_autofree gchar *suffix = g_match_info_fetch(match_info, 3);

		const gchar *scope = is_app_action(action) ? "app." : "win.";

		g_autofree gchar *kebab = camel_to_kebab(action);
		g_autoptr(GString) rewritten = g_string_new(kebab);
		apply_special_renames(rewritten);

		g_string_append(result, prefix);
		g_string_append(result, scope);
		g_string_append(result, rewritten->str);
		g_string_append(result, suffix);

		last_end = end_pos;

		if (!g_match_info_next(match_info, &error))
			{
			if (error)
				{
				log_printf("Regex iteration failed: %s", error->message);
				g_clear_error(&error);
				return;
				}
			break;
			}
		}

	g_string_append(result, contents + last_end);

	g_autoptr(GFile) file = g_file_new_for_path(rc_path);
	g_autoptr(GFileOutputStream) out = g_file_replace(file,
							  nullptr,
							  TRUE,
							  G_FILE_CREATE_NONE,
							  nullptr,
							  &error);
	if (!out)
		{
		log_printf("Failed to open file for replacement: %s", error->message);
		g_clear_error(&error);
		return;
		}

	if (!g_output_stream_write_all(G_OUTPUT_STREAM(out),
				       result->str,
				       result->len,
				       nullptr,
				       nullptr,
				       &error))
		{
		log_printf("Failed to write file: %s", error->message);
		g_clear_error(&error);
		return;
		}

	if (!g_output_stream_close(G_OUTPUT_STREAM(out), nullptr, &error))
		{
		log_printf("Failed to close file: %s", error->message);
		g_clear_error(&error);
		return;
		}
}

/**
 * @brief Convert old accelerators to new
 * @param  
 * 
 * The original .config/geeqie/accels file contains
 * keyboard shortcuts set by the user. They must be preserved
 * by converting them to the new style and stored in .config/geeqie/accels.ini
 */
void convert_accel_map()
{
	g_autoptr(GKeyFile) keyfile = nullptr;
	g_autofree gchar *accels_ini_path = accels_ini_filename();

	/* If this is the first run with the revised style accelerator list,
	 * convert from this style:
	 *
	 * (gtk_accel_path "<Actions>/MenuActions/FirstPage" "<Alt>d")
	 *
	 * to GKeyFile style
	 *
	 * [first_page}
	 * accel=<Alt>d
	 */

	g_autofree gchar *contents = nullptr;
	gsize length = 0;

	g_autofree gchar *accels_map_path = g_build_filename(get_rc_dir(), "accels", nullptr);
	if (!g_file_get_contents(accels_map_path, &contents, &length, nullptr))
		{
		log_printf("Error loading accelerator file");

		return;
		}

	keyfile = g_key_file_new();

	if (!convert_gtk_accel_lines_to_keyfile(contents, keyfile, nullptr))
		{
		log_printf("Error converting accelerator file");

		return;
		}

	if (!g_key_file_save_to_file(keyfile, accels_ini_path, nullptr))
		{
		log_printf("Error saving accelerator file");
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
