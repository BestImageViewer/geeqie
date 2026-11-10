#! /bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

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
## \# translator1_name &lt;translator1 email&gt; \n
## \# translator2_name &lt;translator2 email&gt; \n
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
