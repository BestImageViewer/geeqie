// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef ACCELERATORS_H
#define ACCELERATORS_H

#include <glib.h>

void accel_map_load_merged();
GKeyFile *get_keyfile_merged();
GKeyFile *get_keyfile_search();

gchar *accels_ini_filename();
bool accelerator_string_is_valid(const gchar *shortcuts);
bool remove_modified_shortcut(const gchar *action_name);
bool update_modified_shortcut(const gchar *action_name, const gchar *shortcuts);
bool clear_modified_shortcuts();
/**
 * @brief Restore v2.9 defaults with saved overrides from the sibling accels file.
 * @param path Shortcut file to write; an existing file is backed up with a ~ suffix.
 * @param error Receives a resource, file, or conversion error on failure.
 * @return True if the shortcut file was replaced successfully.
 */
bool restore_v2_shortcuts(const gchar *path, GError **error);
void get_actions_and_accelerators(GKeyFile *key_file, GPtrArray *array, const gchar *window_prefix = nullptr);

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
