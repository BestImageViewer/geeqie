#!/bin/sh

#**********************************************************************
# Copyright (C) 2022 - The Geeqie Team
#
# Author: Colin Clark
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#**********************************************************************

## @file
## @brief Create the Lua API html document, which is part of
## the Geeqie Help file.
##
## It is run during the generation of the help files.
##
## The generated Lua html files are placed in doc/html/lua-api
##
## The doxygen.conf file is modified to extract only those comments
## that are useful as part of an API description.
##

if ! command -v doxygen > /dev/null
then
	printf '%s\n' "doxygen not installed"
	exit 1
fi

export PROJECT="Geeqie"
VERSION=$(git tag --list v[1-9]* | tail -1)
export VERSION
export SRCDIR="$1"
export DOCDIR="$2"
export INLINE_SOURCES=NO
export STRIP_CODE_COMMENTS=YES

TMPFILE=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX") || exit 1

# Modify the Geeqie doxygen.conf file to produce
# only the data needed for the lua API document
awk '
BEGIN {
	FILE_PATTERNS_found = "FALSE"
}
{
	if (FILE_PATTERNS_found == "TRUE")
		{
		if ($0 ~ /\\/)
			{
			next
			}
		else
			{
			FILE_PATTERNS_found = "FALSE"
			}
		}
	if ($1 == SHOW_INCLUDE_FILES)
		{
		{print "SHOW_INCLUDE_FILES = NO"}
		}
	else if ($1 == "FILE_PATTERNS")
		{
		print "FILE_PATTERNS = lua.cc"
		FILE_PATTERNS_found = "TRUE"
		next
		}
	else if ($1 == "EXCLUDE_SYMBOLS")
		{
		print "EXCLUDE_SYMBOLS = L \\"
		print "lua_callvalue \\"
		print "lua_check_exif \\"
		print "lua_check_image \\"
		print "lua_init \\"
		print "_XOPEN_SOURCE \\"
		print "LUA_register_global \\"
		print "LUA_register_meta"
		}
	else if ($1 == "SOURCE_BROWSER")
		{
		print "SOURCE_BROWSER = NO"
		}
	else if ($1 == "HAVE_DOT")
		{
		{print "HAVE_DOT = NO"}
		}
	else
		{
		{print}
		}
}
' "$SRCDIR"/doxygen.conf > "$TMPFILE"

doxygen "$TMPFILE"

rm "$TMPFILE"
