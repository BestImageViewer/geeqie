#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

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
mkdir -p "$DOCDIR" || exit 1
export INLINE_SOURCES=NO
export STRIP_CODE_COMMENTS=YES

TMPFILE=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX") || exit 1
trap 'rm -f "$TMPFILE"' EXIT

# Modify the Geeqie doxygen.conf file to produce
# only the data needed for the lua API document
awk -W posix '
BEGIN {
	LINT = "fatal"
	file_patterns_found = "FALSE"
	}
{
	if (file_patterns_found == "TRUE")
		{
		if ($0 ~ /\\/)
			{
			next
			}
		else
			{
			file_patterns_found = "FALSE"
			next
			}
		}
	if (NF > 0 && $1 == "SHOW_INCLUDE_FILES")
		{
		{print "SHOW_INCLUDE_FILES = NO"}
		}
	else if (NF > 0 && $1 == "FILE_PATTERNS")
		{
		print "FILE_PATTERNS = lua.cc"
		file_patterns_found = "TRUE"
		next
		}
	else if (NF > 0 && $1 == "EXCLUDE_SYMBOLS")
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
	else if (NF > 0 && $1 == "SOURCE_BROWSER")
		{
		print "SOURCE_BROWSER = NO"
		}
	else if (NF > 0 && $1 == "HAVE_DOT")
		{
		{print "HAVE_DOT = NO"}
		}
	else
		{
		{print}
		}
}
' "$SRCDIR"/doc/doxygen.conf > "$TMPFILE"

doxygen "$TMPFILE"
exit $?
