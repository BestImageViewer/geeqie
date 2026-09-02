// SPDX-License-Identifier: GPL-2.0-or-later

#include "accelerators.h"

#include <glib.h>
#include <gtk/gtk.h>

#include "convert-configuration.h"
#include "layout-util.h"
#include "main-defines.h"
#include "ui-fileops.h"

namespace
{

GKeyFile *accels_merged = nullptr;

void key_file_copy_group(GKeyFile *merged, GKeyFile *user, const char *group)
{
	constexpr auto plugin_action_prefix = "win.main-win-plugin-run::";
	/* Plugin targets are discovered at runtime and have no built-in group. */
	if (g_key_file_has_group(merged, group) || g_str_has_prefix(group, plugin_action_prefix))
		{
		size_t n_keys = 0;
		g_autoptr(GError) error = nullptr;
		g_auto(GStrv) keys = g_key_file_get_keys(user, group, &n_keys, &error);

		if (!keys) return;

		for (size_t i = 0; i < n_keys; i++)
			{
			g_autofree char *user_value = g_key_file_get_value(user, group, keys[i], &error);

			if (user_value)
				{
				g_key_file_set_value(merged, group, keys[i], user_value);
				}
			}
		}
}

void key_file_merge(GKeyFile *merged, GKeyFile *user)
{
	if (!merged || !user) return;

	size_t n_groups = 0;
	g_auto(GStrv) groups = g_key_file_get_groups(user, &n_groups);

	for (size_t i = 0; i < n_groups; i++)
		{
		key_file_copy_group(merged, user, groups[i]);
		}
}

GKeyFile *key_file_new_merged(GKeyFile *defaults, GKeyFile *user)
{
	GKeyFile *merged = g_key_file_new();
	size_t len;
	gchar *data = g_key_file_to_data(defaults, &len, nullptr);
	g_key_file_load_from_data(merged, data, len, G_KEY_FILE_NONE, nullptr);
	g_free(data);

	if (user)
		{
		key_file_merge(merged, user);
		}

	return merged;
}

} //namespace


char *accels_ini_filename()
{
	return g_build_filename(get_rc_dir(), "accels.ini", nullptr);
}

void accel_map_load_merged()
{
	g_autoptr(GKeyFile) defaults = g_key_file_new();
	g_autoptr(GKeyFile) user = g_key_file_new();
	g_autofree char *path = accels_ini_filename();

	g_autoptr(GError) error = nullptr;
	g_autoptr(GBytes) bytes = g_resources_lookup_data(GQ_RESOURCE_PATH_DATA "/accels.ini", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);

	if (!bytes)
		{
		log_printf("Failed to load accels.ini resource: %s\n", error ? error->message : "unknown error");
		}
	else
		{
		size_t length = 0;
		const char *data = static_cast<const char *>(g_bytes_get_data(bytes, &length));

		if (!g_key_file_load_from_data(defaults, data, length, G_KEY_FILE_NONE, &error))
			{
			log_printf("Failed to load built-in accelerators: %s\n", error->message);
			}
		}

	if (isfile(path))
		{
		g_autoptr(GError) error = nullptr;

		if (!g_key_file_load_from_file(user, path, G_KEY_FILE_NONE, &error))
			{
			log_printf("Failed to load user accelerators file %s: %s\n", path, error->message);
			}
		}

	g_clear_pointer(&accels_merged, g_key_file_unref);
	accels_merged = key_file_new_merged(defaults, user);
}

GKeyFile *get_keyfile_merged()
{
	return accels_merged;
}

bool accelerator_string_is_valid(const char *shortcuts)
{
	if (!shortcuts || !*shortcuts) return true;

	g_auto(GStrv) accelerator_list = g_strsplit(shortcuts, ";", -1);
	for (gchar **accelerator = accelerator_list; *accelerator; accelerator++)
		{
		g_strstrip(*accelerator);
		if (!**accelerator) continue;

		guint key = 0;
		GdkModifierType modifiers = GDK_NO_MODIFIER_MASK;
		gtk_accelerator_parse(*accelerator, &key, &modifiers);
		if (!key) return false;
		}

	return true;
}

bool remove_modified_shortcut(const char *action_name)
{
	if (!action_name || !*action_name)
		{
		log_printf("remove_modified_shortcut: invalid action_name\n");

		return false;
		}

	g_autoptr(GKeyFile) user = g_key_file_new();
	g_autofree char *path = accels_ini_filename();
	g_autoptr(GError) error = nullptr;

	if (isfile(path))
		{
		if (!g_key_file_load_from_file(user, path, G_KEY_FILE_NONE, &error))
			{
			log_printf("Failed to load user accelerators file %s: %s\n", path, error->message);
			g_clear_error(&error);

			return false;
			}
		}
	else
		{
		return true;
		}

	g_key_file_remove_group(user, action_name, nullptr);

	if (!g_key_file_save_to_file(user, path, &error))
		{
		log_printf("Error saving accelerator file %s: %s\n", path, error->message);
		g_clear_error(&error);
		return false;
		}

	return true;
}

bool update_modified_shortcut(const char *action_name, const char *shortcuts)
{
	if (!action_name || !*action_name || !accelerator_string_is_valid(shortcuts)) return false;

	g_autoptr(GKeyFile) user = g_key_file_new();
	g_autofree char *path = accels_ini_filename();
	g_autoptr(GError) error = nullptr;

	if (isfile(path))
		{
		if (!g_key_file_load_from_file(user, path, G_KEY_FILE_NONE, &error))
			{
			log_printf("Failed to load user accelerators file %s: %s\n", path, error->message);
			g_clear_error(&error);
			}
		}

	g_key_file_set_string(user, action_name, "accels", shortcuts ? shortcuts : "");

	if (!g_key_file_save_to_file(user, path, &error))
		{
		log_printf("Error saving accelerator file %s: %s\n", path, error->message);
		g_clear_error(&error);
		return false;
		}

	return true;
}

bool clear_modified_shortcuts()
{
	g_autofree char *path = accels_ini_filename();
	g_autoptr(GFile) file = g_file_new_for_path(path);
	g_autoptr(GError) error = nullptr;

	/* If file exists, move it to filename~ */
	if (g_file_query_exists(file, nullptr))
		{
		g_autofree char *backup_name = g_strconcat(path, "~", nullptr);
		g_autoptr(GFile) backup = g_file_new_for_path(backup_name);

		if (!g_file_move(file, backup, G_FILE_COPY_OVERWRITE, nullptr, nullptr, nullptr, &error))
			{
			log_printf("Failed to back up accelerator file %s to %s: %s\n", path, backup_name, error->message);
			return FALSE;
			}
		}

	/* Create new empty file */
	g_autoptr(GFileOutputStream) out = g_file_create(file, G_FILE_CREATE_NONE, nullptr, &error);

	if (!out)
		{
		log_printf("Failed to create empty accelerator file %s: %s\n", path, error->message);

		return FALSE;
		}

	if (!g_output_stream_close(G_OUTPUT_STREAM(out), nullptr, &error))
		{
		log_printf("Failed to close accelerator file %s: %s\n", path, error->message);

		return FALSE;
		}

	return TRUE;
}

/*
 * Appends pairs of:
 *   action-name-without-prefix, escaped-normalized-accel
 *
 * to the supplied GPtrArray.
 *
 * The array must be empty on entry and should usually be created with:
 *   g_ptr_array_new_with_free_func(g_free)
 *
 * Result layout:
 *   [ action1, accel1, action2, accel2, ..., nullptr ]
 *
 * Duplicate accelerators prefer app actions, then main window actions, then
 * the first remaining action in key file order.
 *
 * used to create keyboard map image
 */
void get_actions_and_accelerators(GKeyFile *key_file, GPtrArray *array, const gchar *window_prefix)
{
	if (!key_file || !array)
		{
		return;
		}

	size_t n_actions = 0;
	g_auto(GStrv) actions = g_key_file_get_groups(key_file, &n_actions);
	g_autoptr(GHashTable) used_accels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);

	for (guint priority = 0; priority < 3; priority++)
		{
		for (size_t i = 0; i < n_actions; i++)
			{
			const char *action = actions[i];
			if (!action || !*action)
				{
				continue;
				}

			if (window_prefix && !g_str_has_prefix(action, "app.") && !g_str_has_prefix(action, window_prefix))
				{
				continue;
				}

			guint action_priority = 2;
			if (g_str_has_prefix(action, "app."))
				{
				action_priority = 0;
				}
			else if (g_str_has_prefix(action, "win.main-win-"))
				{
				action_priority = 1;
				}

			if (action_priority != priority)
				{
				continue;
				}

			size_t n_accels = 0;
			g_auto(GStrv) accels = g_key_file_get_string_list(key_file, action, "accels", &n_accels, nullptr);

			if (!accels || n_accels == 0)
				{
				continue;
				}

			for (size_t j = 0; j < n_accels; j++)
				{
				if (!accels[j] || !*accels[j])
					{
					continue;
					}

				unsigned int key = 0;
				GdkModifierType mods = GDK_NO_MODIFIER_MASK;

				gtk_accelerator_parse(accels[j], &key, &mods);
				if (key == 0)
					{
					continue;
					}

				g_autofree char *normalized = gtk_accelerator_name(key, mods);
				if (!normalized || g_hash_table_contains(used_accels, normalized))
					{
					continue;
					}

				char *escaped = g_markup_escape_text(normalized, -1);
				if (!escaped)
					{
					continue;
					}

				g_hash_table_add(used_accels, g_strdup(normalized));

				/* Do not display the application or window-type prefix */
				const char *display_action = action;

				if (g_str_has_prefix(action, "app.") || g_str_has_prefix(action, "win."))
					{
					display_action = action + 4;
					}

				const gchar *window_separator = g_strstr_len(display_action, -1, "-win-");
				if (window_separator)
					{
					display_action = window_separator + sizeof("-win-") - 1;
					}

				g_ptr_array_add(array, g_strdup(display_action));
				g_ptr_array_add(array, escaped);
				}
			}
		}

	g_ptr_array_add(array, nullptr);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
