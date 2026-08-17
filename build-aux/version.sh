#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Generate the Geeqie version number
##
## This script is called from meson.build
##
## If the current branch is "master" a revison number is generated of the form:  
## <n.m>+git<date of last commit>-<last commit hash>  
## where <n.m> is the most recent tag.  
## e.g. 1.7+git20220117-732b6935  
##
## If not on "master" or no .git directory, a revision number extracted
## from the first line of the NEWS file is generated.  
## This situation will occur when compiling from a source .tar.
## or for a release.  
## The first line of NEWS must be of the form:  
## Geeqie <n.m[.p]>
##

if [ -d .git ] && [ -x "$(command -v git)" ]
then
	branch=$(git rev-parse --abbrev-ref HEAD)

	if [ "$branch" = "master" ]
	then
		IFS='.'
		# shellcheck disable=SC2046
		set -- $(git tag --list v[1-9]* | tail -n 1 | tr -d 'v')

		major_version=$1
		minor_version=$2
#		patch_version=$3  # not used on master branch

		printf '%s%s%s%s%s%s%s' "$major_version" "." "$minor_version" "+git" "$(git log --max-count=1 --date=format:"%Y%m%d" --format="%cd")" "-" "$(git rev-parse --quiet --verify --short HEAD)"
	else
		version=$(head -1 NEWS)
		# shellcheck disable=SC2086
		set -- $version
		printf '%s' "$2"
	fi
else
	version=$(head -1 NEWS)
	# shellcheck disable=SC2086
	set -- $version
	printf '%s' "$2"
fi
