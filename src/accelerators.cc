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
	if (g_key_file_has_group(merged, group))
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

	accels_merged = key_file_new_merged(defaults, user);
}

GKeyFile *get_keyfile_merged()
{
	return accels_merged;
}

void remove_modified_shortcut(const char *action_name)
{
	if (!action_name || !*action_name)
		{
		log_printf("remove_modified_shortcut: invalid action_name\n");

		return;
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

			return;
			}
		}
	else
		{
		return;
		}

	g_key_file_remove_group(user, action_name, nullptr);

	if (!g_key_file_save_to_file(user, path, &error))
		{
		log_printf("Error saving accelerator file %s: %s\n", path, error->message);
		g_clear_error(&error);
		}
}

void update_modified_shortcut(const char *action_name, const char *shortcuts)
{
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
		}
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
 * used to create keyboard map image
 */
void get_actions_and_accelerators(GKeyFile *key_file, GPtrArray *array)
{
	if (!key_file || !array)
		{
		return;
		}

	size_t n_actions = 0;
	g_auto(GStrv) actions = g_key_file_get_groups(key_file, &n_actions);

	for (size_t i = 0; i < n_actions; i++)
		{
		const char *action = actions[i];
		if (!action || !*action)
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
			auto mods = static_cast<GdkModifierType>(0);

			gtk_accelerator_parse(accels[j], &key, &mods);
			if (key == 0)
				{
				continue;
				}

			g_autofree char *normalized = gtk_accelerator_name(key, mods);
			if (!normalized)
				{
				continue;
				}

			char *escaped = g_markup_escape_text(normalized, -1);
			if (!escaped)
				{
				continue;
				}

			/* Do not display the "app." or "win." prefix */
			const char *display_action = action;

			if (g_str_has_prefix(action, "app.") || g_str_has_prefix(action, "win."))
				{
				display_action = action + 4;
				}

			g_auto(GStrv) parts = g_strsplit(display_action, "-win-", -1);

			g_autofree gchar *split_action = g_strjoinv(" ", parts);

			g_ptr_array_add(array, g_steal_pointer(&split_action));
			g_ptr_array_add(array, escaped);
			}
		}

	g_ptr_array_add(array, nullptr);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
