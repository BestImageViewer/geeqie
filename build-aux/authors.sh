#! /bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

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
	git log --pretty=format:"%aN <%aE>" | sed 's/<>//' | sort | uniq -c | sort -k 1,1nr -k 2 | sed 's/^[[:space:]]*[0-9][0-9]* //' > "$1"/authors
	printf "\n\0" >> "$1"/authors
else
	printf "List of authors not available\n\0" > "$1"/authors
fi
