#! /bin/sh
#**********************************************************************
# Copyright (C) 2024 - The Geeqie Team
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
## @brief Run clang-tidy on source files.
##

show_help()
{
	printf "Run clang-tidy on source files.

-a --all  Process all source files - default is changed files only
-f --fix  Fix errors where possible
-h --help Display this message
\n\n"
}

process_file()
{
	if [ -z "$file" ]
	then
		return
	fi

	i=$((i + 1))
	printf '%d/%d %s\n' "$i" "$total_files" "$file"
	clang-tidy ${fix+"--fix-errors"} --config-file ./.clang-tidy -p ./build "$file" 2> /dev/null

	secs_now=$(cut --delimiter='.' --fields=1 < /proc/uptime)

	elapsed_time=$((secs_now - secs_start))
	remaining_files=$((total_files - i))
	average_time=$((elapsed_time / i))
	estimated_time=$((average_time * remaining_files))

	printf 'Remaining: %dm:%ds\n' $((estimated_time % 3600 / 60)) $((estimated_time % 60))
}

if [ ! -d ".git" ] || [ ! -d "src" ] || [ ! -f "geeqie.1" ]
then
	printf '%s\n' "This is not a Geeqie project folder"
	exit 1
fi

# if variable fix is defined in this way, clang-tidy gives errors.
# fix=""
process_all=0

while :
do
	case $1 in
		-h | -\? | --help)
			show_help

			exit 0
			;;
		-a | --all)
			process_all=1
			;;
		-f | --fix)
			fix="--fix-errors"
			;;
		--) # End of all options.
			shift
			break
			;;
		?*)
			printf 'Unknown option %s\n' "$1" >&2

			exit 1
			;;
		*)
			break
			;;
	esac

	shift
done

if [ ! -d "build" ]
then
	meson setup build
fi
ninja -C build

i=0
secs_start=$(cut --delimiter='.' --fields=1 < /proc/uptime)

if [ "$process_all" -eq 1 ]
then
	total_files=$(find src -name "*.cc" | wc --lines)

	while read -r file
	do
		process_file
	done << EOF
$(find src -name "*.cc" | sort)
EOF
else
	total_files=$(git diff --name-only ./src/*.cc | wc --lines)

	while read -r file
	do
		process_file
	done << EOF
$(git diff --name-only ./src/*.cc | sort)
EOF
fi
