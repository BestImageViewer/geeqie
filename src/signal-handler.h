/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <string>
#include <vector>

#include <gtk/gtk.h>

/** Install fatal-signal diagnostics without invoking normal process cleanup. */
void setup_sig_handler();

/** Return the reserved crash report path, or nullptr if creation failed. */
const char *signal_handler_report_path();

/** Find unacknowledged reports owned by this user, excluding active instances. */
std::vector<std::string> crash_reports_pending(const char *directory);

/** Offer to open or save reports from an earlier crash, once per launch. */
void crash_report_notification_show(GtkWindow *parent, const char *directory = "/tmp");

#endif /* SIGNAL_HANDLER_H */
