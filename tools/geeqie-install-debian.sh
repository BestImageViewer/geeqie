#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Download, compile, and install Geeqie on Debian-based systems.
##
## If run from a folder that already contains the Geeqie sources, the source
## code will be updated from the repository.
## Dialogs allow the user to install additional features.
##

version="2026-09-23"
description='
Geeqie is an image viewer.
This script will download, compile, and install Geeqie on Debian-based systems.
If run from a folder that already contains the Geeqie sources, the source
code will be updated from the repository.
Dialogs allow the user to install additional features.

Command line options are:
-v --version The version of this file
-h --help Output this text
-c --commit=ID Checkout and compile commit ID
-t --tag=TAG Checkout and compile TAG (e.g. v1.4 or v1.3)
-b --back=N Checkout commit -N (e.g. "-b 1" for last-but-one commit)
-l --list List required dependencies
'

# Required before starting the graphical installer
prerequisite_array="zenity
sudo"

# Essential for compiling
essential_array="tar
git
build-essential
gettext
pkg-config
libglib2.0-dev
libgtk-4-dev
meson
ninja-build"

# Optional libraries
optional_array="yelp-tools (optional HTML-help generation)
yelp-tools
help2man (developer documentation tools)
help2man
doclifter (developer documentation tools)
doclifter
LCMS (for color management)
liblcms2-dev
exiv2 (for exif handling)
libexiv2-dev
lua (for lua commands)
liblua5.3-dev
libffmpegthumbnailer (for mpeg thumbnails)
libffmpegthumbnailer-dev
libtiff (for tiff support)
libtiff-dev
libjpeg (for jpeg support)
libjpeg-dev
librsvg2 (for viewing .svg images)
librsvg2-common
libwmf (for viewing .wmf images)
libwmf-0.2-7-gtk
exiftran (for image rotation)
exiftran
imagemagick (for image rotation)
imagemagick
exiv2 command line (for jpeg export)
exiv2
jpgicc (for jpeg export color correction)
liblcms2-utils
pandoc (for generating README help file)
pandoc
gphoto2 (for tethered photography and camera download plugins)
gphoto2
libimage-exiftool-perl (for jpeg extraction plugin)
libimage-exiftool-perl
libheif (for HEIF support)
libheif-dev
libwebp (for WebP images)
libwebp-dev
libdjvulibre (for DjVu images)
libdjvulibre-dev
libopenexr (for exr images)
libopenexr-dev
libimath (for exr images)
libimath-dev
libopenjp2 (for JP2 images)
libopenjp2-7-dev
libraw (for CR3 images)
libraw-dev
libarchive (for compressed files e.g. zip, including timezone)
libarchive-dev
libspelling (for spelling checks)
libspelling-1-dev
libshumate >= 1.5.0 (for GPS maps)
libshumate-dev
libpoppler (for pdf file preview)
libpoppler-glib-dev
libjxl (for viewing .jxl images)
libjxl-dev
libcfitsio (for .fits images)
libcfitsio-dev"

####################################################################
# Get System Info
# Derived from: https://github.com/coto/server-easy-install (GPL)
####################################################################
lowercase()
{
	printf '%b\n' "$1" | sed "y/ABCDEFGHIJKLMNOPQRSTUVWXYZ/abcdefghijklmnopqrstuvwxyz/"
}

systemProfile()
{
	OS="$(lowercase "$(uname)")"
	KERNEL=$(uname -r)
	MACH=$(uname -m)

	if [ "${OS}" = "windowsnt" ]
	then
		OS=windows
	elif [ "${OS}" = "darwin" ]
	then
		OS=mac
	else
		OS=$(uname)
		if [ "${OS}" = "SunOS" ]
		then
			OS=Solaris
			ARCH=$(uname -p)
			OSSTR="${OS} ${REV}(${ARCH} $(uname -v))"
		elif [ "${OS}" = "AIX" ]
		then
			# shellcheck disable=SC2034
			OSSTR="${OS} $(oslevel) ($(oslevel -r))"
		elif [ "${OS}" = "Linux" ]
		then
			if [ -f /etc/redhat-release ]
			then
				DistroBasedOn='RedHat'
				DIST=$(sed s/\ release.*// /etc/redhat-release)
				PSUEDONAME=$(sed s/.*\(// /etc/redhat-release | sed s/\)//)
				REV=$(sed s/.*release\ // /etc/redhat-release | sed s/\ .*//)
			elif [ -f /etc/SuSE-release ]
			then
				DistroBasedOn='SuSe'
				PSUEDONAME=$(tr "\n" ' ' < /etc/SuSE-release | sed s/VERSION.*//)
				REV=$(tr "\n" ' ' < /etc/SuSE-release | sed s/.*=\ //)
			elif [ -f /etc/mandrake-release ]
			then
				DistroBasedOn='Mandrake'
				PSUEDONAME=$(sed s/.*\(// /etc/mandrake-release | sed s/\)//)
				REV=$(cat | sed s/.*release\ // /etc/mandrake-release | sed s/\ .*//)
			elif [ -f /etc/debian_version ]
			then
				DistroBasedOn='Debian'
				if [ -f /etc/lsb-release ]
				then
					DIST=$(grep '^DISTRIB_ID' /etc/lsb-release | awk -F= '{ print $2 }')
					PSUEDONAME=$(grep '^DISTRIB_CODENAME' /etc/lsb-release | awk -F= '{ print $2 }')
					REV=$(grep '^DISTRIB_RELEASE' /etc/lsb-release | awk -F= '{ print $2 }')
				fi
			fi
			if [ -f /etc/UnitedLinux-release ]
			then
				DIST="${DIST}[$(tr "\n" ' ' < /etc/UnitedLinux-release | sed s/VERSION.*//)]"
			fi
			OS=$(lowercase "$OS")
			DistroBasedOn=$(lowercase "$DistroBasedOn")
			readonly OS
			readonly DIST
			readonly DistroBasedOn
			readonly PSUEDONAME
			readonly REV
			readonly KERNEL
			readonly MACH
		fi
	fi
}

install_essential()
{
	for file in $essential_array
	do
		if package_query "$file"
		then
			package_install "$file" || exit_install
		fi
	done
}

install_options()
{
	if [ -n "$options" ]
	then
		OLDIFS=$IFS
		IFS='|'
		# shellcheck disable=SC2086
		set $options
		while [ $# -gt 0 ]
		do
			package_install "$1" || exit_install
			if [ "$1" = "libshumate-dev" ]
			then
				gps_map=enabled
			fi
			shift
		done
		IFS=$OLDIFS
	fi
}

uninstall_previous()
{
	if [ -f build/build.ninja ] && [ -f build/meson-logs/install-log.txt ]
	then
		# shellcheck disable=SC2024
		sudo --askpass ninja -C build uninstall >> "$install_log" 2>&1 || exit_install
	fi
}

install_staged()
{
	# Keep privileged writes off the source filesystem (which may be NFS).
	install_stage=$(mktemp -d /tmp/geeqie-install.XXXXXXXXXX) || exit_install
	meson compile -C build >> "$install_log" 2>&1 || exit_install
	meson install -C build --no-rebuild --destdir "$install_stage/files" >> "$install_log" 2>&1 || exit_install
	tar -C "$install_stage/files" -cpf "$install_stage/files.tar" . >> "$install_log" 2>&1 || exit_install
	# Preserve installed file modes, but leave existing system directories alone.
	# shellcheck disable=SC2024
	sudo --askpass tar --no-same-owner --no-overwrite-dir -xpf "$install_stage/files.tar" -C / >> "$install_log" 2>&1 || exit_install

	# Meson records DESTDIR paths; uninstall needs the actual system paths.
	awk -v prefix="$install_stage/files" '
		index($0, prefix "/") == 1 { $0 = substr($0, length(prefix) + 1) }
		{ print }
	' build/meson-logs/install-log.txt > "$install_stage/install-log.txt" || exit_install
	cat "$install_stage/install-log.txt" > build/meson-logs/install-log.txt || exit_install
	rm -rf "$install_stage"
	install_stage=
}

stop_progress()
{
	if [ -n "$progress_pid" ]
	then
		exec 3>&-
		kill "$progress_pid" 2> /dev/null
		wait "$progress_pid" 2> /dev/null
		progress_pid=
	fi
	if [ -n "$zen_pipe" ]
	then
		rm -f "$zen_pipe"
		rmdir "$progress_dir"
		zen_pipe=
	fi
}

uninstall()
{
	current_dir="$(basename "$PWD")"
	if [ "$current_dir" = "geeqie" ]
	then

		uninstall_previous

		if ! zenity --title="Uninstall Geeqie" --text="WARNING.\nThis will delete folder:\n\n$PWD\n\nand all sub-folders!" --question --ok-label="Cancel" --cancel-label="OK" 2> /dev/null
		then
			cd ..
			sudo --askpass rm -rf geeqie
		fi
	else
		zenity --title="Uninstall Geeqie" --text="This is not a geeqie installation folder!\n\n$PWD" --warning 2> /dev/null
	fi

	exit_install
}

package_query()
{
	if [ "$DistroBasedOn" = "debian" ]
	then

		# shellcheck disable=SC2086
		res=$(dpkg-query --show --showformat='${Status}' "$1" 2>> $install_log)
		if [ "${res}" = "install ok installed" ]
		then
			status=1
		else
			status=0
		fi
	fi
	return "$status"
}

package_install()
{
	if [ "$DistroBasedOn" = "debian" ]
	then
		# shellcheck disable=SC2024
		sudo --askpass apt-get --assume-yes install "$@" >> "$install_log" 2>&1
	fi
}

# Keep these minimum versions in sync with meson.build.
check_versions()
{
	meson_version=$(meson --version)
	if ! dpkg --compare-versions "$meson_version" ge 1.3.2
	then
		printf '%s\n' "Meson >= 1.3.2 is required (found $meson_version)." >> "$install_log"
		exit_install
	fi

	check_library gtk4 4.18
	check_library glib-2.0 2.66
	check_library pango 1.46
	if [ "$gps_map" = "enabled" ]
	then
		check_library shumate-1.0 1.5.0
	fi
}

check_library()
{
	if ! pkg-config --atleast-version="$2" "$1"
	then
		printf '%s\n' "$1 >= $2 is required. Install a sufficiently recent development package." >> "$install_log"
		exit_install
	fi
}

exit_install()
{
	rm "$install_pass_script" > /dev/null 2>&1

	stop_progress
	if [ -n "$install_stage" ]
	then
		rm -rf "$install_stage"
	fi
	printf '%s\n' "Geeqie installation did not complete. Log file: $install_log" >&2
	zenity --title="$title" --text="Geeqie installation did not complete\nLog file: $install_log" --info 2> /dev/null

	exit 1
}

# Entry point

IFS='
'

# Parse the command line
OPTS=$(getopt -o vhc:t:b:ld: --long version,help,commit:,tag:,back:,list,debug: -- "$@")
eval set -- "$OPTS"

while true
do
	case "$1" in
		-v | --version)
			printf '%b\n' "$version"
			exit
			;;
		-h | --help)
			printf '%b\n' "$description"
			exit
			;;
		-c | --commit)
			COMMIT="$2"
			shift
			shift
			;;
		-t | --tag)
			TAG="$2"
			shift
			shift
			;;
		-b | --back)
			BACK="$2"
			shift
			shift
			;;
		-l | --list)
			LIST=yes
			shift
			;;
		*)
			break
			;;
	esac
done

if [ -n "$LIST" ]
then
	printf '%b\n' "Installer prerequisites:" "$prerequisite_array" ""
	printf '%b\n' "Minimum versions: Meson 1.3.2, GTK 4.18, GLib 2.66, Pango 1.46; GPS: libshumate 1.5.0" ""
	printf '%b\n' "Essential build dependencies:"
	for file in $essential_array
	do
		printf '%b\n' "$file"
	done

	printf '\n'
	printf '%b\n' "Optional libraries:"
	for file in $optional_array
	do
		printf '%b\n' "$file"
	done

	exit
fi

# Check prerequisites before using any graphical dialogs or sudo askpass.
for prerequisite in $prerequisite_array
do
	if ! command -v "$prerequisite" > /dev/null 2>&1
	then
		printf '%s\n' "Missing installer prerequisite: $prerequisite" "Install zenity and sudo first (as root: apt-get install zenity sudo)." >&2
		exit 1
	fi
done

# If uninstall has been run, maybe the current directory no longer exists
if [ ! -d "$PWD" ]
then
	zenity --error --title="Install Geeqie and dependencies" --text="Folder $PWD does not exist!" 2> /dev/null

	exit
fi

# Check system type
systemProfile
if [ "$DistroBasedOn" != "debian" ]
then
	zenity --error --title="Install Geeqie and dependencies" --text="Unknown operating system:\n
Operating System: $OS
Distribution: $DIST
Psuedoname: $PSUEDONAME
Revision: $REV
DistroBasedOn: $DistroBasedOn
Kernel: $KERNEL
Machine: $MACH" 2> /dev/null

	exit
fi

# If a Geeqie folder already exists here, warn the user
if [ -d "geeqie" ]
then
	zenity --info --title="Install Geeqie and dependencies" --text="This script is for use on Ubuntu and other\nDebian-based installations.\nIt will download, compile, and install Geeqie source\ncode and its dependencies.\n\nA sub-folder named \"geeqie\" will be created in the\nfolder this script is run from, and the source code\nwill be downloaded to that sub-folder.\n\nA sub-folder of that name already exists.\nPlease try another folder." 2> /dev/null

	exit
fi

# If it looks like a Geeqie download folder, assume an update
if [ -d ".git" ] && [ -d "src" ]
then
	mode="update"
else
	# If it looks like something else is already installed here, warn the user
	if [ -d ".git" ] || [ -d "src" ]
	then
		zenity --info --title="Install Geeqie and dependencies" --text="This script is for use on Ubuntu and other\nDebian-based installations.\nIt will download, compile, and install Geeqie source\ncode and its dependencies.\n\nIt looks like you are running this script from a folder which already has software installed.\n\nPlease try another folder." 2> /dev/null

		exit
	else
		mode="install"
	fi
fi

if [ "$mode" = "install" ]
then
	message="This script is for use on Ubuntu and other\nDebian-based installations.\nIt will download, compile, and install Geeqie source\ncode and its dependencies.\n\nA sub-folder named \"geeqie\" will be created in the\nfolder this script is run from, and the source code\nwill be downloaded to that sub-folder.\n\nIn subsequent dialogs you may choose which\noptional features to install."

	title="Install Geeqie and dependencies"
	install_option=TRUE
else
	message="This script is for use on Ubuntu and other\nDebian-based installations.\nIt will update the Geeqie source code and its\ndependencies, and will compile and install Geeqie.\n\nIn subsequent dialogs you may choose which\noptional features to install."

	title="Update Geeqie and re-install"
	install_option=FALSE
fi

# Ask whether to install or uninstall
if ! install_action=$(zenity --title="$title" --text="$message" --list --radiolist --column "" --column "" TRUE "Install" FALSE "Uninstall" --cancel-label="Cancel" --ok-label="OK" --hide-header 2> /dev/null)
then
	exit
fi

# Environment variable SUDO_ASKPASS cannot be "zenity --password",
# so create a temporary script containing the command
install_pass_script=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
printf '%b\n' "#!/bin/sh
exec zenity --password --title=\"$title\" 2>/dev/null" > "$install_pass_script"
chmod +x "$install_pass_script"
export SUDO_ASKPASS="$install_pass_script"

# Put the install log in tmp, to avoid writing to PWD during a new install.
install_log=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")

if [ "$install_action" = "Uninstall" ]
then
	uninstall
fi

sleep 100 | zenity --title="$title" --text="Checking for installed files" --progress --pulsate 2> /dev/null &
zen_pid=$!

# Get the standard options that are not yet installed
i=0
for file in $optional_array
do
	if [ $((i % 2)) -eq 0 ]
	then
		package_title="$file"
	else
		if package_query "$file"
		then
			if [ -z "$option_string" ]
			then
				option_string="${install_option:+${install_option}}\n${package_title}\n${file}"
			else
				option_string="${option_string:+${option_string}}\n$install_option\n${package_title}\n${file}"
			fi
		fi
	fi
	i=$((i + 1))
done

kill "$zen_pid" 2> /dev/null

# Ask the user which options to install
if [ -n "$option_string" ]
then
	if ! options=$(printf '%b\n' "$option_string" | zenity --title="$title" --list --checklist --text 'Select which library files to install:' --column='Select' --column='Library files' --column='Library' --hide-column=3 --print-column=3 2> /dev/null)
	then
		exit_install
	fi
fi

# Start of Zenity progress section
progress_dir=$(mktemp -d "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX") || exit_install
zen_pipe="$progress_dir/progress"
mkfifo "$zen_pipe" || exit_install
trap 'exit_install' HUP INT TERM PIPE
zenity --progress --title="$title" --text="Installing options…" --auto-close --no-cancel --percentage=0 < "$zen_pipe" 2> /dev/null &
progress_pid=$!
# Keep the pipe open until completion; no tail process or auto-kill is needed.
exec 3> "$zen_pipe"

printf '%b\n' "2" >&3
printf '%b\n' "#Installing essential libraries…" >&3

install_essential

printf '%b\n' "4" >&3
printf '%b\n' "#Installing options…" >&3

gps_map=auto
install_options
# Installed GPS dependencies are omitted from the selection dialog.
if ! package_query libshumate-dev
then
	gps_map=enabled
fi
check_versions

printf '%b\n' "6" >&3
printf '%b\n' "#Installing extra loaders…" >&3

printf '%b\n' "10" >&3
printf '%b\n' "#Getting new sources from server…" >&3

if [ "$mode" = "install" ]
then
	if ! git clone http://git.geeqie.org/git/geeqie.git >> "$install_log" 2>&1
	then
		git_error=$(tail -n5 "$install_log" 2>&1)
		zenity --title="$title" --error --text="Git error:\n\n$git_error" 2> /dev/null
		exit_install
	fi
else
	if ! git checkout master >> "$install_log" 2>&1
	then
		git_error="$(tail -n25 "$install_log" 2>&1)"
		zenity --title="$title" --error --text="Git error:\n\n$git_error" 2> /dev/null
		exit_install
	fi
	if ! git pull >> "$install_log" 2>&1
	then
		git_error=$(tail -n5 "$install_log" 2>&1)
		zenity --title="$title" --error --text="Git error:\n\n$git_error" 2> /dev/null
		exit_install
	fi
fi

printf '%b\n' "20" >&3
printf '%b\n' "#Cleaning installed version…" >&3

if [ "$mode" = "install" ]
then
	cd geeqie || exit 1
else
	uninstall_previous
fi

printf '%b\n' "30" >&3
printf '%b\n' "#Checkout required version…" >&3

if [ -n "$BACK" ]
then
	if ! git checkout master~"$BACK" >> "$install_log" 2>&1
	then
		git_error=$(tail -n5 "$install_log" 2>&1)
		zenity --title="$title" --error --text="Git error:\n\n$git_error" 2> /dev/null
		exit_install
	fi
elif [ -n "$COMMIT" ]
then

	if ! git checkout "$COMMIT" >> "$install_log" 2>&1
	then
		git_error=$(tail -n5 "$install_log" 2>&1)
		zenity --title="$title" --error --text="Git error:\n\n$git_error" 2> /dev/null
		exit_install
	fi
elif [ -n "$TAG" ]
then
	if ! git checkout "$TAG" >> "$install_log" 2>&1
	then
		git_error=$(tail -n5 "$install_log" 2>&1)
		zenity --title="$title" --error --text="Git error:\n\n$git_error" 2> /dev/null
		exit_install
	fi
fi

printf '%b\n' "40" >&3
printf '%b\n' "#Creating configuration files…" >&3

if [ -f build/meson-private/coredata.dat ]
then
	meson setup --reconfigure build -Dgps_map="$gps_map" >> "$install_log" 2>&1 || exit_install
else
	meson setup build -Dgps_map="$gps_map" >> "$install_log" 2>&1 || exit_install
fi
meson configure --no-pager build >> "$install_log" 2>&1 || exit_install
printf '%b\n' "90 " >&3
printf '%b\n' "#Installing Geeqie…" >&3
install_staged

rm "$install_pass_script"
mv -f "$install_log" "./build/install.log"

printf '%b\n' "100 " >&3
stop_progress
trap - HUP INT TERM PIPE

(for i in $(seq 0 4 100)
do
	printf '%b\n' "$i"
	sleep 0.1
done) | zenity --progress --title="$title" --text="Geeqie installation complete…\n" --auto-close --percentage=0 2> /dev/null

exit
