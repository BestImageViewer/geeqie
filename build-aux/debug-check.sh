#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Check for stray debug statements
##
## $1 Source file to check
##
## Look for DEBUG_0, DEBUG_BT, DEBUG_FD
##
## Intentional usages can include "disable debug check" in a comment on the same line.
##

res=$(grep --line-number 'DEBUG_0\|DEBUG_BT\|DEBUG_FD' "$1" \
      | grep -v 'disable debug check')

if [ -z "$res" ]
then
	exit 0
else
	printf "%s\n" "$res"
	exit 1
fi
