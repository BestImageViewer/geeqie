// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef ACCELERATORS_H
#define ACCELERATORS_H

void accel_map_load_merged();
GKeyFile *get_keyfile_merged();
GKeyFile *get_keyfile_search();

gchar *accels_ini_filename();
void remove_modified_shortcut(const gchar *action_name);
void update_modified_shortcut(const gchar *action_name, const gchar *shortcuts);
bool clear_modified_shortcuts();
void get_actions_and_accelerators(GKeyFile *key_file, GPtrArray *array);

#endif

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
