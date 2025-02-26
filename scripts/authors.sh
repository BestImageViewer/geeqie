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
## $2 running_from_git - "true" or "false" \n
##

if [ "$2" = "true" ]
then
	git log --pretty=format:"%aN <%aE>" | sed 's/<>//' | sort | uniq --count | sort --general-numeric-sort --reverse --stable --key 1,1 | cut  --characters 1-8 --complement > "$1"/authors
	printf "\n\0" >> "$1"/authors
else
	printf "List of authors not available\n\0" > "$1"/authors
fi
