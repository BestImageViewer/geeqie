/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef ACTION_HISTORY_H
#define ACTION_HISTORY_H

#include <string>

/** Record a compiled action-table identifier on the main thread, never user text. */
void action_history_record(const char *identifier);

/** Return the latest 50 requests, with elapsed timestamps and no parameters. */
std::string action_history_report();

/** Stop recording when a fatal signal arrives. Signal-safe and lock-free. */
void action_history_freeze();

/** Write the bounded history without allocation or locks, including in a signal handler. */
void action_history_write(int fd);

#endif
