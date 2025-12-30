#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Run all tests
##
## Run test with all options disabled,
## and then with -Ddevel=enabled and other
## options as auto

if [ ! -d "src" ]
then
	printf '%s\n' "This is not a Geeqie project folder"
	exit 1
fi

XDG_CONFIG_HOME=$(mktemp -d "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
XDG_CACHE_HOME=$(mktemp -d "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
XDG_DATA_HOME=$(mktemp -d "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
export XDG_CONFIG_HOME
export XDG_CACHE_HOME
export XDG_DATA_HOME

rm --recursive --force build
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")

# Check with all options disabled except for developer
meson setup \
-Darchive=disabled \
-Dcms=disabled \
-Ddoxygen=disabled \
-Ddjvu=disabled \
-Dexecinfo=disabled \
-Dexiv2=disabled \
-Dexr=disabled \
-Dfits=disabled \
-Dgit=disabled \
-Dgps-map=disabled \
-Dgtk4=disabled \
-Dheif=disabled \
-Dhelp_pdf=disabled \
-Dj2k=disabled \
-Djpeg=disabled \
-Djpegxl=disabled \
-Dlibraw=disabled \
-Dlua=disabled \
-Dnpy=disabled \
-Dpandoc=disabled \
-Dpdf=disabled \
-Dspell=disabled \
-Dtiff=disabled \
-Dunit_tests=disabled \
-Dvideothumbnailer=disabled \
-Dwebp=disabled \
-Dyelp-build=disabled \
--buildtype=debug \
build

if ninja test -C build
then
	options_disabled="PASS"
else
	options_disabled="FAIL"
fi

cp ./build/meson-logs/meson-log.txt "$tmpdir/testlog-options-disabled.txt"
cat ./build/meson-logs/testlog.txt >> "$tmpdir/testlog-options-disabled.txt"

rm --recursive --force build
meson setup --buildtype=debug -Dunit_tests=enabled build
if ninja test -C build
then
	options_enabled="PASS"
else
	options_enabled="FAIL"
fi

cp ./build/meson-logs/meson-log.txt "$tmpdir/testlog-options-enabled.txt"
cat ./build/meson-logs/testlog.txt >> "$tmpdir/testlog-options-enabled.txt"

rm -r "$XDG_CONFIG_HOME"
rm -r "$XDG_CACHE_HOME"
rm -r "$XDG_DATA_HOME"

printf "\n"
if [ "$options_disabled" = "PASS" ]
then
	printf "%s \033[1;32m PASS \033[0m\n" "$tmpdir/testlog-options-disabled.txt"
else
	printf "%s \033[1;31m FAIL \033[0m\n" "$tmpdir/testlog-options-disabled.txt"
fi

if [ "$options_enabled" = "PASS" ]
then
	printf "%s \033[1;32m PASS \033[0m\n" "$tmpdir/testlog-options-enabled.txt"
else
	printf "%s \033[1;31m FAIL \033[0m\n" "$tmpdir/testlog-options-enabled.txt"
fi

