#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Generate the gettext source-file list used by Meson

script_dir=$(CDPATH='' cd "$(dirname "$0")" && pwd) || exit 1
project_dir=$(CDPATH='' cd "$script_dir/.." && pwd) || exit 1
potfiles_tmp=$(mktemp "${TMPDIR:-/tmp}/geeqie-potfiles.XXXXXX") || exit 1

trap 'rm -f "$potfiles_tmp"' EXIT HUP INT TERM
cd "$project_dir" || exit 1

find src -type f -name '*.cc' > "$potfiles_tmp"
find data -type f \( -name '*.desktop.in' -o -name '*.metainfo.xml.in' -o -name '*.ui' \) >> "$potfiles_tmp"

LC_ALL=C sort -u "$potfiles_tmp" > po/POTFILES
