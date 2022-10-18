#!/bin/sh
# This file is a part of Geeqie project (https://www.geeqie.org/).
# Copyright (C) 2008 - 2022 The Geeqie Team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software: foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# Allow standalone replacement for xxd
command -v xxdi.pl >/dev/null 2>&1 && XXD="xxdi.pl" || XXD="xxd -i"

# To generate the required code, xxd has to run in the same folder as the source
build_dir="$PWD"

cd "$(dirname "$1")" || return 1

${XXD} "$(basename "$1")" "$build_dir/$2"
