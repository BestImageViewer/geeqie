#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Check for stray debug statements
##
## $1 Source file to check
##
## Look for temporary debug tools and removed legacy DEBUG_1 through DEBUG_4/DEBUG_N calls.
##
## Intentional usages can include "disable debug check" in a comment on the same line.
##

res=$(grep --line-number 'DEBUG_0\|DEBUG_BT\|DEBUG_FD\|DEBUG_RU\|DEBUG_[1-4][[:space:]]*(\|DEBUG_N[[:space:]]*(' "$1" \
      | grep -v 'disable debug check')

if [ -z "$res" ]
then
	exit 0
else
	printf "%s\n" "$res"
	exit 1
fi
