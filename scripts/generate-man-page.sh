#!/bin/sh

## @file
## @brief Create the man page from the output of the "geeqie--help" command
## and also create the xml file for Command Line Options
##
## This needs to be run only when the command line options change
##
## The programs help2man and doclifter are required - both are available as .deb packages.
##

if ! command -v help2man > /dev/null
then
	printf '%s\n' "help2man not installed"
	exit 1
fi

if ! command -v doclifter > /dev/null
then
	printf '%s\n' "doclifter not installed"
	exit 1
fi

if  [ ! -f build/src/geeqie  ]
then
	printf '%s\n' "geeqie executable has not been built"
	exit 1
fi

options_file=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
year=$(date +"%Y")

printf '%s\n' "[NAME]
Geeqie - GTK based multiformat image viewer

[DESCRIPTION]
Geeqie is an interactive GTK based image viewer that supports multiple image formats,
zooming, panning, thumbnails and sorting images into collections.

Generated for version:

[BUGS]
Please send bug reports and feedback to https://github.com/BestImageViewer/geeqie/issues

[COPYRIGHT]
Copyright (C) 1999-2004 by John Ellis. Copyright (C) 2004-$year by The Geeqie Team. Use this software  at  your
own  risk! This  software released under the GNU General Public License. Please read the COPYING file for more
information." > "$options_file"

help2man --no-info --include="$options_file" build/src/geeqie > geeqie.1

doclifter geeqie.1
mv geeqie.1.xml doc/docbook/CommandLineOptions.xml

rm "$options_file"
