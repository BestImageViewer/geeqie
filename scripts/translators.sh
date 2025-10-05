#! /bin/sh
#**********************************************************************
# Copyright (C) 2023 - The Geeqie Team
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
## @brief Generate lists of people who have made commits to the repository.
##
## The lists will be displayed in the About - Credits dialog.
##
## $1 Meson current_build_dir \n
## $2 Meson current_source_dir \n
## $3 locales.txt \n
## $4...$n po source list - space separated list \n
##
## It is expected that the .po files have a list of translators in the form: \n
## \# Translators: \n
## \# translator1_name <translator1 email> \n
## \# translator2_name <translator2 email> \n
## \#

build_dir="$1"
shift
source_dir="$1"
shift
locales="$1"
shift

while [ -n "$1" ]
do
	base=$(basename "$1")
	full_file_path="$source_dir/$base"
	locale=${base%.po}

	awk -W posix 'BEGIN {LINT = "fatal"} {if ((NF > 0) && ($1 == "'"$locale"'")) {print $0}}' "$locales"
	awk -W posix 'BEGIN {LINT = "fatal"} $0 ~/Translators:/ {
		while (1) {
			getline $0
			if ($0 == "#")
				{
				exit
				}
			else
				{
				print substr($0, 3)
				}
			}
		print $0
	}' "$full_file_path"

shift
done > "$build_dir"/translators
printf "\n\0" >> "$build_dir"/translators
