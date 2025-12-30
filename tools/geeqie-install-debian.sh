#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Download, compile, and install Geeqie on Debian-based systems.
##
## If run from a folder that already contains the Geeqie sources, the source
## code will be updated from the repository.
## Dialogs allow the user to install additional features.
##

version="2024-11-11"
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

# Essential for compiling
essential_array="git
build-essential
libglib2.0-0
libtool
meson
ninja-build
yelp-tools
help2man
doclifter"

# Optional for GTK3
optional_array="LCMS (for color management)
liblcms2-dev
exiv2 (for exif handling)
libgexiv2-dev
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
libwmf0.2-7-gtk
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
libomp (required by libraw)
libomp-dev
libarchive (for compressed files e.g. zip, including timezone)
libarchive-dev
libgspell (for spelling checks)
libgspell-1-dev
libchamplain gtk (for GPS maps)
libchamplain-gtk-0.12-dev
libchamplain (for GPS maps)
libchamplain-0.12-dev
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
			package_install "$file"
		fi
	done

	if [ "$1" = "GTK3" ]
	then
		if package_query "libgtk-3-dev"
		then
			package_install libgtk-3-dev
		fi
	else
		if package_query "libgtk2.0-dev"
		then
			package_install libgtk2.0-dev
		fi
	fi
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
			package_install "$1"
			shift
		done
		IFS=$OLDIFS
	fi
}

uninstall()
{
	current_dir="$(basename "$PWD")"
	if [ "$current_dir" = "geeqie" ]
	then

		sudo --askpass  ninja -C build uninstall

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

exit_install()
{
	rm "$install_pass_script" > /dev/null 2>&1

	if [ -p "$zen_pipe" ]
	then
		printf '%b\n' "100" > "$zen_pipe"
		printf '%b\n' "#End" > "$zen_pipe"
	fi

	zenity --title="$title" --text="Geeqie is not installed\nLog file: $install_log" --info 2> /dev/null

	rm "$zen_pipe" > /dev/null 2>&1

	exit 1
}

# Entry point

IFS='
'

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
			LIST="$2"
			shift
			shift
			;;
		*)
			break
			;;
	esac
done

if [ -n "$LIST" ]
then
	printf '%b\n' "Essential libraries:"
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

# Use GTK3 as default
gtk3_installed=TRUE

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

# Ask whether to install GTK3 or uninstall

if ! gtk_version=$(zenity --title="$title" --text="$message" --list --radiolist --column "" --column "" "$gtk3_installed" "Install" FALSE "Uninstall" --cancel-label="Cancel" --ok-label="OK" --hide-header 2> /dev/null)
then
	exit
fi

# Environment variable SUDO_ASKPASS cannot be "zenity --password",
# so create a temporary script containing the command
install_pass_script=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
printf '%b\n' "#!/bin/sh
if zenity --password --title=\"$title\" 2>/dev/null
then
	exit 1
fi" > "$install_pass_script"
chmod +x "$install_pass_script"
export SUDO_ASKPASS="$install_pass_script"

if [ "$gtk_version" = "Uninstall" ]
then
	uninstall
fi

# Put the install log in tmp, to avoid writing to PWD during a new install
rm install.log 2> /dev/null
install_log=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")

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
zen_pipe=$(mktemp -u "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
mkfifo "$zen_pipe"
(tail -f "$zen_pipe" 2> /dev/null) | zenity --progress --title="$title" --text="Installing options..." --auto-close --auto-kill --percentage=0 2> /dev/null &

printf '%b\n' "2" > "$zen_pipe"
printf '%b\n' "#Installing essential libraries..." > "$zen_pipe"

install_essential "$gtk_version"

printf '%b\n' "4" > "$zen_pipe"
printf '%b\n' "#Installing options..." > "$zen_pipe"

install_options

printf '%b\n' "6" > "$zen_pipe"
printf '%b\n' "#Installing extra loaders..." > "$zen_pipe"

printf '%b\n' "10" > "$zen_pipe"
printf '%b\n' "#Getting new sources from server..." > "$zen_pipe"

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

printf '%b\n' "20" > "$zen_pipe"
printf '%b\n' "#Cleaning installed version..." > "$zen_pipe"

if [ "$mode" = "install" ]
then
	cd geeqie || exit 1
else
	sudo --askpass  ninja -C build uninstall
fi

printf '%b\n' "30" > "$zen_pipe"
printf '%b\n' "#Checkout required version..." > "$zen_pipe"

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

printf '%b\n' "40" > "$zen_pipe"
printf '%b\n' "#Creating configuration files..." > "$zen_pipe"

if [ -z "${gtk_version%%GTK3*}" ]
then
	meson setup build
	printf '%b\n' "90 " > "$zen_pipe"
	printf '%b\n' "#Installing Geeqie..." > "$zen_pipe"
	ninja -C build install
else
	meson setup build
	meson configure --no-pager build
	printf '%b\n' "90 " > "$zen_pipe"
	printf '%b\n' "#Installing Geeqie..." > "$zen_pipe"
	sudo --askpass meson install -C build
fi

rm "$install_pass_script"
mv -f "$install_log" "./build/install.log"

printf '%b\n' "100 " > "$zen_pipe"
rm "$zen_pipe"

(for i in $(seq 0 4 100)
do
	printf '%b\n' "$i"
	sleep 0.1
done) | zenity --progress --title="$title" --text="Geeqie installation complete...\n" --auto-close --percentage=0 2> /dev/null

exit
