#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Prepare full and minimal AppImages for uploading to the Latest release on GitHub.
##
## This script should be run after a new release of Geeqie has been made, and after
## new AppImages have been created in Continuous Build on GitHub.
##
## Download AppImages from Continuous Build, remove "latest" from the file name
## and replace the text with the version number.
## The Continuous Build update information is also removed, so release AppImages
## are not updated to the Continuous Build release by AppImageUpdate.
##
## The renamed files may then be uploaded to the Latest Release section on GitHub.
##
## @FIXME If the latest version is a patch version, the AppImage will only show
## the major/minor version plus git commit and not the patch version.
##

set -e

# readelf supports foreign architectures without rewriting the AppImage payload.
for tool in readelf dd wget
do
	if ! command -v "$tool" > /dev/null
	then
		printf '%s was not found\n' "$tool" >&2
		exit 1
	fi
done

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
tmp_file=$(mktemp  "$tmp_dir/geeqie.XXXXXXXXXX")

latest_tag=$(git tag | tail -1)
latest_version="${latest_tag#?}"

strip_appimage_update_information()
{
	appimage="$1"

	# AppImages append a filesystem after the ELF runtime. objcopy discards it.
	# Keep the section and its size, but clear its contents in place.
	sections=$(LC_ALL=C readelf --section-headers --wide "$appimage")
	update_section=$(printf '%s\n' "$sections" | awk '
		{
		for (i = 1; i <= NF; i++)
			if ($i == ".upd_info" && $(i + 1) == "PROGBITS")
				print $(i + 3), $(i + 4)
		}')
	if [ -z "$update_section" ]
	then
		printf 'No update information section found in %s\n' "$appimage" >&2
		exit 1
	fi
	update_offset=$((0x${update_section% *}))
	update_size=$((0x${update_section#* }))
	dd if=/dev/zero of="$appimage" bs=1 seek="$update_offset" count="$update_size" conv=notrunc status=none
}

cd "$tmp_dir" || exit

minimal=""
architecture="x86_64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"
strip_appimage_update_information "$new_name"

minimal="-minimal"
architecture="x86_64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"
strip_appimage_update_information "$new_name"

minimal=""
architecture="aarch64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"
strip_appimage_update_information "$new_name"

minimal="-minimal"
architecture="aarch64"
wget --no-verbose --show-progress --output-file="$tmp_file" "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"
new_name="Geeqie-$latest_version$minimal-$architecture.AppImage"
mv "Geeqie$minimal-latest-$architecture.AppImage" "$new_name"
strip_appimage_update_information "$new_name"

rm "$tmp_file"

printf '%s\n' "$tmp_dir"

gh release upload "$latest_tag" \
    "$tmp_dir"/Geeqie-"$latest_version"-x86_64.AppImage \
    "$tmp_dir"/Geeqie-"$latest_version"-minimal-x86_64.AppImage \
    "$tmp_dir"/Geeqie-"$latest_version"-aarch64.AppImage \
    "$tmp_dir"/Geeqie-"$latest_version"-minimal-aarch64.AppImage
