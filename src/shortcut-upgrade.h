// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SHORTCUT_UPGRADE_H
#define SHORTCUT_UPGRADE_H

#include <gtk/gtk.h>

using ShortcutUpgradeCallback = void (*)(bool accepted, gpointer data);

/** @brief Whether an old accels file exists without a corresponding accels.ini. */
bool shortcut_upgrade_needed(const gchar *path);

/**
 * @brief Ask which shortcuts to use, without selecting either option initially.
 * @param parent Parent window, which may still be hidden during startup.
 * @param path Shortcut configuration file to create after an explicit choice.
 * @param description Information about the GTK4 upgrade.
 * @param callback Called after successful persistence or cancellation.
 * @param data Callback data, which must remain valid until the window closes.
 * @return The new modal window; the caller presents it when ready.
 */
GtkWidget *shortcut_upgrade_dialog_new(GtkWindow *parent, const gchar *path, const gchar *description,
                                      ShortcutUpgradeCallback callback, gpointer data);

#endif
