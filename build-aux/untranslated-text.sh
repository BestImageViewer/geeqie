#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

#**********************************************************************

## @file
## @brief Locate strings not marked for translation
##
## The check is not comprehensive - the exclusions make this
## of limited value.
##
## @FIXME Strings starting with a space, or a lower-case alpha
## or if there is more than one string per line, are not
## checked for.
##
## The regex search is for a character sequence: \n
## double quotes \n
## upper-case alpha character \n
## alpha character or space \n
## printable character \n
## previous character type repeated one or more times \n
## double quotes
##
## The above sequence preceded by "_(" will not be a hit.
##
## $1 file to process
##

TARGET_FILENAME="$1"

# Lines containing these strings will not be checked for untranslated text.
# Only double-quotes should be escaped.
omit_text_array="
 msg
#define
#include
&lt
@brief
@param
BOLD_ON
COPYRIGHT
ColorSpace
DEBUG
Damien
ERR
EXIF
Error
Exif.
FIXME
ImageLoaderType
LUA_
MonoSpace
N_(
NikonTag
Pause
PixbufRenderer
PluginsMenu
READ_
Separator
URL
WRITE_
Wrap
XOFF
config_file_error
runtime_error
\"Desktop\"
\"File\"
\"Geeqie\"
\"Geeqie AppImage\"
\"Layout\"
\"OK\"
\"Xmp.
.html
/*
//
{\"
_attribute
action_group
courier
exif_get_item
filter_add_if_missing
font_name
g_ascii_strcasecmp
g_build_filename
g_critical
g_key_file_get_
g_message
g_object
g_signal
g_str_has_suffix
g_strstr_len
g_themed_icon_new
g_warning
getenv
gtk_action_group_get_action
gtk_container_child_get
gtk_widget_add_accelerator
layout_actions
layout_toolbar_add
luaL_
lua_
memcmp
mouse_button_
options->
osd_template_insert
pango_attr
path_to_utf8
primaries_name
print_term
printf
return g_strdup
runcmd
setenv
signal_
signals_
strcmp
strncmp
trc_name
website-label
write_char_option

##cache.cc
\"SimilarityGrid[32 x 32]=\"

##cellrendericon.cc
\"Background color as a GdkRGBA\",
\"Background color\",
\"Background set\",
\"Draw focus indicator\",
\"Fixed height\",
\"Fixed width\",
\"Focus\",
\"Foreground color as a GdkRGBA\",
\"Foreground color\",
\"Foreground set\",
\"GQvCellRendererIcon\",
\"Height of icon excluding text\",
\"Marks bit array\",
\"Marks\",
\"Number of marks\",
\"Pixbuf Object\",
\"Show marks\",
\"Show text\",
\"Text to render\",
\"Text\",
\"The pixbuf to render\",
\"Toggled mark\",
\"Whether the marks are displayed\",
\"Whether the text is displayed\",
\"Whether this tag affects the background color\",
\"Whether this tag affects the foreground color\",
\"Width of cell\",

##main.cc
SIGABRT,
SIGBUS,
SIGFPE,
SIGILL,
SIGIOT,
SIGSEGV,

##pixbuf-renderer.cc
\"Delay image update\",
\"Display cache size MiB\",
\"Expand image in autozoom.\",
\"Fit window to image size\",
\"Image actively loading\",
\"Image rendering complete\",
\"Limit size of image when autofitting\",
\"Limit size of parent window\",
\"New image scroll reset\",
\"Number of tiles to retain in memory at any one time.\",
\"Size increase limit of image when autofitting\",
\"Size limit of image when autofitting\",
\"Size limit of parent window\",
\"Tile cache count\",
\"Zoom maximum\",
\"Zoom minimum\",
\"Zoom quality\",

##preferences.cc
Valencia

##print.cc
static constexpr gchar image_text_font_title[] = \"Image text font\";
static constexpr gchar page_text_font_title[] = \"Page text font\";

##remote.cc
render_intent = g_strdup(\"Absolute Colorimetric\");
render_intent = g_strdup(\"Absolute Colorimetric\");
render_intent = g_strdup(\"Perceptual\");
render_intent = g_strdup(\"Relative Colorimetric\");
render_intent = g_strdup(\"Saturation\");
"

exclude_files_array="
exif.cc
format-canon.cc
format-fuji.cc
format-nikon.cc
format-olympus.cc
"

# A POSIX-compliant function that returns 0 if the substring is present, or 1
# otherwise.  See https://stackoverflow.com/a/229585
string_contains_substring()
{
	haystack="$1"
	needle="$2"

	case "$haystack" in
		*"$needle"*)
			return 0
			;;
		*)
			return 1
			;;
	esac
}

# Small self-test
if (string_contains_substring "alpha" "bet") || \
	(string_contains_substring "bet" "alphabet") || \
	(string_contains_substring "b*t" "bet")
then
	echo "Negative substring self-test failed."
	exit 1
fi
if ! (string_contains_substring "alphabet" "bet") || \
	! (string_contains_substring "(alpha)(bet)" "bet)")
then
	echo "Positive substring self-test failed."
	exit 1
fi

filename_printed="no"

omit="FILE_OK"
while read -r omit_file
do
	if [ -n "$omit_file" ]
	then
		if string_contains_substring "$TARGET_FILENAME" "$omit_file"
		then
			omit="omit"
		fi
	fi
done << EOF
$exclude_files_array
EOF

if [ "$omit" = "FILE_OK" ]
then
	while read -r infile_line
	do
		if [ -n "$infile_line" ]
		then
			omit="LINE_NOT_OK"
			while read -r omit_text
			do
				if [ -n "$omit_text" ]
				then
					if string_contains_substring "$infile_line" "$omit_text"
					then
						omit="omit"
					fi
				fi
			done << EOF
$omit_text_array
EOF
			if [ "$omit" = "LINE_NOT_OK" ]
			then
				if [ "$filename_printed" = "no" ]
				then
					printf "\nfile: %s\n" "$TARGET_FILENAME"
					filename_printed="yes"
				fi

				no_tabs=$(echo "$infile_line" | tr -s '\t')
				printf "line: %s\n" "$no_tabs"
			fi
		fi
	done << EOF
$(cat --number "$TARGET_FILENAME" | grep --perl-regexp '(?<!_\()"[[:upper:]]([[:lower:]]|[[:space:]])[[:print:]]+"')
EOF
fi

if [ "$filename_printed" = "yes" ]
then
	exit 1
else
	exit 0
fi
