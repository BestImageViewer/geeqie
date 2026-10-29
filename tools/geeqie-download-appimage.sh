#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Download full and minimal AppImages from the Continuous build release on GitHub.
## Optionally extract the full size AppImage.
##
## Downloads are made to $HOME/bin.
## It is expected that $HOME/bin exists and that it is in $PATH.
##
## Downloads will not be made unless the server version is newer than the local file.
##

version="2025-08-14"
backups=3

show_help()
{
	printf "Download the latest Geeqie AppImages from the
Continuous Build release on GitHub.

-b --backups <n> Set number of backups (default is 3)
-d --desktop Install desktop icon and menu item
-e --extract Extract AppImage
-h --help Display this message
-m --minimal Download minimal version of AppImage
-r --revert <n> Revert to downloaded AppImage backup. For the latest version set <n> to 0
-v --version Display version of this file

The Continuous Build release is updated each
time the source code is updated.

The default action is to download an AppImage to
\$HOME/bin. A symbolic link will be set so that
\"geeqie\" points to the executable

No downloads will be made unless the file on the
server at GitHub is newer than the local file.

The full size AppImage is about 120MB and the
minimal AppImage is about 10MB. Therefore the full
size version will load much slower and will have
a slightly slower run speed.

However the minimal version has limited capabilities
compared to the full size version.

The minimal option (-m or --minimal) will download
the minimal version.

The extract option (-e or --extract) will extract
The contents of the AppImage into a sub-directory
of \$HOME/bin, and then set the symbolic link to the
extracted executable.

This will take up some disk space, but the
extracted executable will run as fast as a
packaged release.

If the extract option is selected, a symbolic link from
\$HOME/.local/share/bash-completion/completions/geeqie
to the extracted completion script will be set to enable command line completion.

Completion can also be used without extracting the AppImage by installing
the data/completions/geeqie file from the matching Geeqie source release in
the directory above. Enable bash-completion on the host and start a new
Bash shell. See Bash Completion in the manual's Command Line Options page.
\n\n"
}

show_version()
{
	printf "Version: %s\n" "$version"
}

spinner()
{
	message="$1"
	character_count=$((${#message} + 4))
	pid=$!
	delay=0.75
	spinstr='\|/-'

	while kill -0 "$pid" 2> /dev/null
	do
		temp=${spinstr#?}
		printf "$message [%c]" "$spinstr"
		spinstr=$temp${spinstr%"$temp"}
		sleep "$delay"
		printf "%$character_count""s" | tr " " "\b"
	done
}

architecture=$(uname -m)

extract=0
minimal=""
desktop=0
backups_option=0
revert=""
revert_option=0

while :
do
	case $1 in
		-h | -\? | --help)
			show_help

			exit 0
			;;
		-v | --version)
			show_version

			exit 0
			;;
		-d | --desktop)
			desktop=1
			;;
		-b | --backups)
			backups_option=1
			if [ -n "$2" ]
			then
				backups=$2
				shift
			else
				printf '"--backups" requires a non-empty option argument.\n' >&2

				exit 1
			fi
			;;
		-r | --revert)
			revert_option=1
			if [ -n "$2" ]
			then
				revert=$2
				shift
			else
				printf '"--revert" requires a non-empty option argument.\n' >&2

				exit 1
			fi
			;;
		-e | --extract)
			extract=1
			;;
		-m | --minimal)
			minimal="-minimal"
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

if [ ! -d "$HOME/bin" ]
then
	printf "\$HOME/bin does not exist.
It is required for this script to run.
Also, \$HOME/bin should be in \$PATH.\n"

	exit 1
fi

if [ "$backups_option" -eq 1 ] && {
	[ "$minimal" = "-minimal" ] || [ "$extract" -eq 1 ] || [ "$revert_option" -eq 1 ]
}
then
	printf "backups must be the only option\n"

	exit 1
fi

if [ "$desktop" -eq 1 ] && {
	[ "$minimal" = "-minimal" ] || [ "$extract" -eq 1 ]
}
then
	printf "desktop must be the only option\n"

	exit 1
fi

if [ "$backups_option" -eq 1 ]
then
	if ! [ "$backups" -gt 0 ] 2> /dev/null
	then
		printf "%s is not an integer\n" "$backups"

		exit 1
	else
		tmp_file=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
		cp "$0" "$tmp_file"
		sed --in-place "s/^backups=.*/backups=$backups/" "$tmp_file"
		chmod +x "$tmp_file"
		mv "$tmp_file" "$0"

		exit 0
	fi
fi

if [ "$desktop" -eq 1 ]
then
	if [ -f "$HOME/Desktop/org.geeqie.Geeqie.desktop" ]
	then
		printf "Desktop file already exists\n"

		exit 0
	fi

	file_count=$(find "$HOME/bin/" -name "Geeqie*latest*\.AppImage" -print | wc -l)
	if [ "$file_count" -eq 0 ]
	then
		printf "No AppImages have been downloaded\n"

		exit 1
	fi

	tmp_dir=$(mktemp --directory "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
	cd "$tmp_dir" || exit 1

	app=$(find "$HOME/bin/" -name "Geeqie*latest*\.AppImage" -print | sort --reverse | head -1)
	$app --appimage-extract "usr/share/applications/org.geeqie.Geeqie.desktop" > /dev/null
	$app --appimage-extract "usr/share/pixmaps/geeqie.png" > /dev/null
	xdg-desktop-icon install --novendor "squashfs-root/usr/share/applications/org.geeqie.Geeqie.desktop"
	xdg-icon-resource install --novendor --size 48 "squashfs-root/usr/share/pixmaps/geeqie.png"
	xdg-desktop-menu install --novendor "squashfs-root/usr/share/applications/org.geeqie.Geeqie.desktop"
	rm --recursive --force "$tmp_dir"

	exit 0
fi

# The cd needs to be here because the --backups option needs PWD
cd "$HOME/bin/" || exit 1

if [ "$revert_option" -eq 1 ]
then
	if ! [ "$revert" -ge 0 ] 2> /dev/null
	then
		printf "%s is not an integer\n" "$revert"

		exit 1
	else
		if [ "$revert" -eq 0 ]
		then
			revert=""
		else
			revert=".$revert"
		fi
	fi

	if ! [ -f "$HOME/bin/Geeqie$minimal-latest-$architecture.AppImage$revert" ]
	then
		printf "Backup $HOME/bin/Geeqie%s-latest-$architecture.AppImage%s does not exist\n" "$minimal" "$revert"

		exit 1
	fi

	if [ "$extract" -eq 1 ]
	then
		rm --recursive --force "Geeqie$minimal-latest-$architecture-AppImage"
		mkdir "Geeqie$minimal-latest-$architecture-AppImage"
		cd "Geeqie$minimal-latest-$architecture-AppImage" || exit 1

		(../Geeqie-latest-x86_64.AppImage --appimage-extract > /dev/null) & spinner "Extracting Geeqie AppImage…"

		printf "\nExtraction complete\n"

		cd ..
		ln --symbolic --force "./Geeqie$minimal-latest-$architecture-AppImage/squashfs-root/AppRun" geeqie
	else
		ln --symbolic --force "$HOME/bin/Geeqie$minimal-latest-$architecture.AppImage$revert" geeqie
	fi

	exit 0
fi

log_file=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")

wget --no-verbose --show-progress --backups="$backups" --output-file="$log_file" --timestamping "https://github.com/BestImageViewer/geeqie/releases/download/continuous/Geeqie$minimal-latest-$architecture.AppImage"

download_size=$(stat --printf "%s" "$log_file")
rm "$log_file"

# If a new file was downloaded, check if extraction is required
if [ "$download_size" -gt 0 ]
then
	chmod +x "Geeqie$minimal-latest-$architecture.AppImage"

	if [ "$extract" -eq 1 ]
	then
		rm --recursive --force "Geeqie$minimal-latest-$architecture-AppImage"
		mkdir "Geeqie$minimal-latest-$architecture-AppImage"
		cd "Geeqie$minimal-latest-$architecture-AppImage" || exit 1

		(../Geeqie-latest-x86_64.AppImage --appimage-extract > /dev/null) & spinner "Extracting Geeqie AppImage…"

		printf "\nExtraction complete\n"

		if [ ! -f "$HOME/.local/share/bash-completion/completions/geeqie" ]
		then
			mkdir --parents "$HOME/.local/share/bash-completion/completions/"
			ln --symbolic "$HOME/bin/Geeqie-latest-x86_64-AppImage/squashfs-root/usr/share/bash-completion/completions/geeqie" "$HOME/.local/share/bash-completion/completions/geeqie"
		fi

		cd ..
		ln --symbolic --force "./Geeqie$minimal-latest-$architecture-AppImage/squashfs-root/AppRun" geeqie

		# Fix problems in linuxdeploy
		# Calling ./squashfs-root/AppRun from a symlink fails.
		# shellcheck disable=SC2016
		sed -i '/^this_dir/ c\this_dir=$(dirname "$(readlink -f "$BASH_SOURCE")")' "./Geeqie$minimal-latest-$architecture-AppImage/squashfs-root/AppRun"

		# shellcheck disable=SC2016
		sed -i '/^exec/ c\exec $(readlink -f $this_dir/AppRun.wrapped) "$@"' "./Geeqie$minimal-latest-$architecture-AppImage/squashfs-root/AppRun"
	else
		ln --symbolic --force "Geeqie$minimal-latest-$architecture.AppImage" geeqie
	fi
fi
