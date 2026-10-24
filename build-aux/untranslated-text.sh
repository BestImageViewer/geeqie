#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Locate likely untranslated C++ strings.
## Non-UI strings require an inline // NOTRANSLATE: reason comment.
## The annotation suppresses its line; keep UI strings on separate lines.

exec python3 "$(dirname "$0")/untranslated-text.py" "$@"
