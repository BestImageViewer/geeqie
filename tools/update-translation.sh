#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Update a language translation file
## $1 Language file to update
##
## Generate LINGUAS file from existing .po files
## Create a new temporary message.pot file
## Merge updates into the required language file
##

if [ ! -d ".git" ] || [ ! -d "src" ]
then
	printf '%s\n' "This is not a Geeqie project folder"
	exit 1
fi

cd "./po" || exit 1

if ! command -v xgettext >/dev/null 2>&1
then
    echo "Error: xgettext is not installed."
    exit 1
fi

if ! command -v itstool >/dev/null 2>&1
then
    echo "Error: itstool is not installed."
    exit 1
fi

if [ ! -f "./$1" ]; then
    echo "'$1' is not a file in the current directory."
    echo "Call by: ./tools/update-translation.sh xx.po"
    exit 1
fi

# The LINGUAS file is required by Meson - maybe only for .desktop files
: > LINGUAS  # Truncate or create the file

for po_file in $(find . -name "*.po" 2>/dev/null | cut -c3- | sort)
do
    [ -f "$po_file" ] || continue
    lang="${po_file%.po}"
    echo "$lang" >> LINGUAS
done

# It is not necessary to maintain a messages.pot file
POT_FILE=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")

POT_APPSTREAM=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_APPSTREAM_SORTED=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_DESKTOPS=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_DESKTOPS_SORTED=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_SOURCES=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_SOURCES_SORTED=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")

find ../ -type f -name '*.metainfo.xml.in' > "$POT_APPSTREAM"
find ../ -type f -name '*.desktop.in' > "$POT_DESKTOPS"
find ../src -type f -name '*.cc' > "$POT_SOURCES"
sort "$POT_APPSTREAM" > "$POT_APPSTREAM_SORTED"
sort "$POT_DESKTOPS" > "$POT_DESKTOPS_SORTED"
sort "$POT_SOURCES" > "$POT_SOURCES_SORTED"

xargs itstool  --output="$POT_FILE" \
           < "$POT_APPSTREAM_SORTED"

xargs xgettext --language=Desktop \
         --from-code=UTF-8 \
         --keyword=_ \
         --join-existing \
         --output="$POT_FILE" \
          < "$POT_DESKTOPS_SORTED"

xargs xgettext --language=C++ \
         --from-code=UTF-8 \
         --keyword=_ \
         --keyword=N_ \
         --join-existing \
         --output="$POT_FILE" \
          < "$POT_SOURCES_SORTED"

msgmerge --update "$1" "$POT_FILE"

rm "$POT_FILE"
rm "$POT_APPSTREAM"
rm "$POT_APPSTREAM_SORTED"
rm "$POT_DESKTOPS"
rm "$POT_DESKTOPS_SORTED"
rm "$POT_SOURCES"
rm "$POT_SOURCES_SORTED"
