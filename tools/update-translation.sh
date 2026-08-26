#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Update a language translation file
## $1 Language file to update, or --all to update all language files
##
## Generate LINGUAS file from existing .po files
## Create a new temporary message.pot file
## Merge updates into the required language file
##

script_dir=$(CDPATH='' cd "$(dirname "$0")" && pwd) || exit 1
cd "$script_dir/.." || exit 1

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

if [ "$1" != "--all" ] && [ ! -f "./$1" ]
then
	printf '%s\n' "'$1' is not a file in the current directory."
	printf '%s\n' "Call by: ./tools/update-translation.sh xx.po"
	printf '%s\n' "      or: ./tools/update-translation.sh --all"
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
POT_UI=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_UI_SORTED=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_SOURCES=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")
POT_SOURCES_SORTED=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXX")

find ../ -type f -name '*.metainfo.xml.in' > "$POT_APPSTREAM"
find ../ -type f -name '*.desktop.in' > "$POT_DESKTOPS"
find ../data -type f -name '*.ui' > "$POT_UI"
find ../src -type f -name '*.cc' > "$POT_SOURCES"
sort "$POT_APPSTREAM" > "$POT_APPSTREAM_SORTED"
sort "$POT_DESKTOPS" > "$POT_DESKTOPS_SORTED"
sort "$POT_UI" > "$POT_UI_SORTED"
sort "$POT_SOURCES" > "$POT_SOURCES_SORTED"

xargs itstool  --output="$POT_FILE" \
           < "$POT_APPSTREAM_SORTED"

xargs xgettext --language=Desktop \
         --from-code=UTF-8 \
         --keyword=_ \
         --join-existing \
         --output="$POT_FILE" \
          < "$POT_DESKTOPS_SORTED"

xargs xgettext --language=Glade \
         --from-code=UTF-8 \
         --join-existing \
         --output="$POT_FILE" \
          < "$POT_UI_SORTED"

xargs xgettext --language=C++ \
         --from-code=UTF-8 \
         --keyword=_ \
         --keyword=N_ \
         --join-existing \
         --output="$POT_FILE" \
          < "$POT_SOURCES_SORTED"

if [ "$1" = "--all" ]
then
	merge_status=0
	for po_file in *.po
	do
		if ! msgmerge --update "$po_file" "$POT_FILE"
		then
			merge_status=1
			break
		fi
	done
else
	merge_status=0
	msgmerge --update "$1" "$POT_FILE" || merge_status=1
fi

rm "$POT_FILE"
rm "$POT_APPSTREAM"
rm "$POT_APPSTREAM_SORTED"
rm "$POT_DESKTOPS"
rm "$POT_DESKTOPS_SORTED"
rm "$POT_UI"
rm "$POT_UI_SORTED"
rm "$POT_SOURCES"
rm "$POT_SOURCES_SORTED"

exit "$merge_status"
