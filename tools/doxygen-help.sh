#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Facilitate the integration of the Doxygen html files
## into a code editor.
##
## doxygen-help.sh &lt;parameter to be searched for&gt;
##
## The environment variable DOCDIR must be set to point to the
## Doxygen html files location.
##
## The environment variable PROJECT must be set to the value used
## when creating the Doxygen files.
##
## Set a hot key in the code editor to call this script with
## the highlighted variable or function name as a parameter.
##
## The file $DOCDIR/searchdata.xml contains an index of all documented
## items and is scanned to locate the relevant html section.
##
## xdg-open is used to call the html browser to display the document.
##
##  **********************************************************************
##
## To generate the Doxygen html files run 'doxygen.sh'
##
## searchdata.xml is searched for a string of type; \n
##    &lt;field name="name"&gt;ExifWin&lt;/field&gt; \n
##    &lt;field name="url"&gt; \n
## or \n
##    &lt;field name="name"&gt;advanced_exif_new&lt;/field&gt; \n
##    &lt;field name="args"&gt; \n
##    &lt;field name="url"&gt;
##
## If this fails, search again for a string of type: \n
##    .*ADVANCED_EXIF_DATA_COLUMN_WIDTH.* \n
##    &lt;field name="url"&gt;
##

if [ -z "${DOCDIR}" ]
then
	printf '%s\n' "Environment variable DOCDIR not set"
	zenity --title="Geeqie" --warning --text="Environment variable DOCDIR not set"
elif [ -z "${PROJECT}" ]
then
	printf '%s\n' "Environment variable PROJECT not set"
	zenity --title="Geeqie" --warning --text="Environment variable PROJECT not set"
else
	url_found=$(awk -W posix -v search_param="$1" -v docdir="$DOCDIR" '
		BEGIN {
		LINT = "fatal"
		FS=">|<"
		}

		{
		if (match($0, "<field name=\"name\">"search_param"</field>"))
			{
			getline
			if (match($0, "url") > 0)
				{
				n=split($0, url_name, /[<>]/)
				print "file://"docdir"/html/"url_name[3]
				exit
				}
			else
				{
				getline
				if (match($0, "url") > 0)
					{
					n=split($0, url_name, /[<>]/)
					print "file://"docdir"/html/"url_name[3]
					exit
					}
				}
			}
		}
' "$DOCDIR/searchdata.xml")

	if [ -z "$url_found" ]
	then
		url_found=$(awk -W posix -v search_param="$1" -v docdir="$DOCDIR" '
			BEGIN {
			LINT = "fatal"
			FS=">|<"
			}

			{
			if (match($0, search_param) > 0)
				{
				getline
				if (match($0, "url") > 0)
					{
					n=split($0, url_name, /[<>]/)
					print "file://"docdir"/html/"url_name[3]
					exit
					}
				}
			}
' "$DOCDIR/searchdata.xml")

		if [ -z "$url_found" ]
		then
			exit 1
		else
			xdg-open "$url_found"
			exit 0
		fi
	else
		xdg-open "$url_found"
		exit 0
	fi
fi
