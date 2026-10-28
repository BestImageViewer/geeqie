/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

/** Install fatal-signal diagnostics without invoking normal process cleanup. */
void setup_sig_handler();

#endif /* SIGNAL_HANDLER_H */
