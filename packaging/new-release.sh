#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Create a new release
##
## new-release.sh [option]...\n
## Where:\n
## -v \<a.b\> is a major.minor version number\n
## -s \<c\> is the start hash number for a new major.minor release - if omitted, HEAD is used\n
## -p \<d\> is the patch version if a major.minor.patch release is being created\n
## -r Push the release to the repo. If omitted a test run is made.\n
## -h Print Help
##
## Will create a new release off the master branch, or will create a new
## patch version off an existing major.minor release branch.
##
## It is expected that the first line of NEWS is in the form "Geeqie \<a.b[.d]\>
##
## 1. Ensure that the main repo. is up to date
## 2. cd to a working directory and create or update NEWS and org.geeqie.metainfo.in for the new release data.
## 4. Run this script
##
## This script will:
## 1. Clone geeqie to a unique directory in /tmp and cd to it
## 2. Copy NEWS and org.geeqie.Geeqie.metainfo.xml.in from the working
## directory to the new clone dir
## 3. Create the new release
## 4. Rename the unique dir name to the form geeqie-<n.m>
## 5. Create the source tar

version=
start=
patch=
push=
while getopts "v:s:p:hr" option
do
	case $option in
		h)
			printf '%s\n%s\n%s\n%s\n%s\n' "-v <a.b> release major.minor e.g 1.9" "-s <c> start hash e.g. 728172681 (optional)" "-p <d> release patch version e.g. 2 for 1.6.2" "-r push to repo." "-h help"
			exit 0
			;;
		v) version="$OPTARG" ;;
		s) start="$OPTARG" ;;
		p) patch="$OPTARG" ;;
		r) push=true ;;
		*) exit 1 ;;
	esac
done

orig_dir=$PWD

if [ ! -f NEWS ]
then
	printf '%s\n' "File NEWS does not exist"
	exit 1
fi

if [ ! -f ./data/org.geeqie.Geeqie.metainfo.xml.in ]
then
	printf '%s\n' "File org.geeqie.Geeqie.metainfo.xml.in does not exist"
	exit 1
fi

if ! zenity --title="NEW RELEASE" --question --text "Have the following files been updated?\n\n$orig_dir/NEWS\n$orig_dir/org.geeqie.Geeqie.metainfo.xml.in\n\nContinue?"
then
	exit 1
fi

if [ "$push" = true ]
then
	if ! zenity --title="NEW RELEASE" --question --text "Do you have write access to the repo.?\nDo you really want to push?\n\nContinue?"
	then
		exit 1
	fi
fi

tmp_dir=${TMPDIR:-/tmp}
working_dir=$(mktemp --directory "$tmp_dir/geeqie.XXXXXXXXXX")

git clone git://git.geeqie.org/geeqie.git "$working_dir"

cp "$orig_dir/NEWS" "$working_dir"
cp "$orig_dir/data/org.geeqie.Geeqie.metainfo.xml.in" "$working_dir/data/"

cd "$working_dir" || exit 1

if [ -n "$start" ] && [ -n "$patch" ]
then
	printf '%s\n' "Cannot have start-hash and patch number together"
	exit 1
fi

if [ "$(printf '%s\n' "$version" | awk -W posix -F "." 'BEGIN {LINT = "fatal"} {print NF-1}')" -ne 1 ]
then
	printf '%s\n' "Version major.minor $version is not valid"
	exit 1
fi

if [ -n "$start" ]
then
	if ! git branch master --contains "$start" > /dev/null 2>&1
	then
		printf '%s\n' "Start hash is not in master branch"
		exit 1
	fi
fi

if [ -n "$patch" ]
then
	if ! git rev-parse stable/"$version" > /dev/null 2>&1
	then
		printf '%s\n' "Version $version does not exist"
		exit 1
	fi

	if ! [ "$patch" -ge 0 ] 2> /dev/null
	then
		printf '%s\n' "Patch $patch is not an integer"
		exit 1
	fi
else
	if git rev-parse stable/"$version" > /dev/null 2>&1
	then
		printf '%s\n' "Version $version already exists"
		exit 1
	fi
fi

if [ -z "$patch" ]
then
	revision="$version"
else
	revision="$version.$patch"
fi

if [ -z "$patch" ]
then
	if [ -z "$start" ]
	then
		git checkout -b stable/"$version"
	else
		git checkout -b stable/"$version" "$start"
	fi

	if [ "$push" = true ]
	then
		git push git@geeqie.org:geeqie stable/"$version"
	fi
else
	git checkout stable/"$version"
fi

# Regenerate to get the new version number in the man page
rm --recursive --force build
meson setup build
ninja -C build

if ! ./build-aux/generate-man-page.sh
then
	printf '%s\n' "generate-man-page.sh failed"
	exit 1
fi

git add NEWS
git add data/org.geeqie.Geeqie.metainfo.xml.in
git add geeqie.1
git add doc/docbook/CommandLineOptions.xml
git commit --message="Preparing for release v$revision"

if [ "$push" = true ]
then
	git push git@geeqie.org:geeqie
fi

git tag --sign "v$revision" --message="Release v$revision"

if [ "$push" = true ]
then
	git push git@geeqie.org:geeqie "v$revision"
fi

rm --recursive --force build

cd "$tmp_dir" || exit 1

rm --recursive --force "geeqie-$revision.tar.xz"
rm --recursive --force "geeqie-$revision.tar.xz.asc"
rm --recursive --force "geeqie-$revision"

mv "$working_dir" "geeqie-$revision"

tar --create --xz --file="$tmp_dir/geeqie-$revision.tar.xz" --exclude="AppImage*" --exclude=".git*" "geeqie-$revision"

gpg --armor --detach-sign --output "$tmp_dir/geeqie-$revision.tar.xz.asc" "$tmp_dir/geeqie-$revision.tar.xz"

cd "geeqie-$revision" || exit 1

git checkout master

git checkout stable/"$version" NEWS
git checkout stable/"$version" geeqie.1
git checkout stable/"$version" doc/docbook/CommandLineOptions.xml
git checkout stable/"$version" data/org.geeqie.Geeqie.metainfo.xml.in

git add NEWS
git add data/org.geeqie.Geeqie.metainfo.xml.in
git add geeqie.1
git add doc/docbook/CommandLineOptions.xml
git commit --message="Release v$revision files"

if [ "$push" = true ]
then
	git push git@geeqie.org:geeqie
fi

zenity --info --window-icon="info" --text="Upload files:\n\n$tmp_dir/geeqie-$revision.tar.xz\n$tmp_dir/geeqie-$revision.tar.xz.asc\n\nto https://github.com/BestImageViewer/geeqie/releases"
