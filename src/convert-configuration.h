// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef CONVERT_CONFIGURATION_H
#define CONVERT_CONFIGURATION_H

#include <glib.h>

/**
 * @brief Merge active GTK3 shortcut overrides over existing defaults.
 * @param keyfile Defaults to update, retaining unchanged primary/alternate bindings.
 * @param input Contents of the old accels file.
 * @param error Receives a parse error; keyfile is unchanged on failure.
 * @return True if conversion succeeded.
 */
bool merge_legacy_shortcuts(GKeyFile *keyfile, const gchar *input, GError **error);

void convert_accel_map();
void convert_configuration_file();

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
