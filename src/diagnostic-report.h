/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DIAGNOSTIC_REPORT_H
#define DIAGNOSTIC_REPORT_H

#include <string>

#include <gtk/gtk.h>

struct LayoutWindow;

/** Classify an environment hint without returning arbitrary user-provided text. */
std::string diagnostic_report_desktop_hint(const char *value);

/** Build a report using only explicitly allowed technical fields. */
std::string diagnostic_report_create(const LayoutWindow *layout = nullptr);

/** Refresh the immutable crash snapshot from the main thread. */
void diagnostic_report_refresh();

/** Preview a diagnostic report and offer to save it locally. */
void diagnostic_report_dialog_show(GtkWindow *parent);

#endif
