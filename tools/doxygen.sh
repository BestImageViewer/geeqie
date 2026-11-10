#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Generate Doxygen documentation, skipping unchanged successful builds.
##
## Usage: tools/doxygen.sh [--force] [--html-only | --check] [destination]
## The default destination is ../doxygen. Requires Python 3 and Doxygen.
## Use --help for details.

script_dir=$(CDPATH='' cd -P "$(dirname "$0")" && pwd) || exit 1
exec python3 "$script_dir/doxygen.py" "$@"
