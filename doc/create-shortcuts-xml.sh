#!/bin/sh

#**********************************************************************
# Copyright (C) 2021 - The Geeqie Team
#
# Author: Colin Clark
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#**********************************************************************

## @file
## @brief Generate Help .xml files describing window shortcut keys.
##
## The .xml files are included within ./doc/docbook/GuideReferenceKeyboardShortcuts.xml
##
## For separate windows, source code files are searched for the string "hard_coded_window_keys"  
## which is an array containing the shortcut key and the menu label.
##
## For the main window the source file ./src/layout-util.cc is searched for
## lines which contain shortcut definitions.
##
## This needs to be run only when the sortcut keys have been changed
##

duplicates_xml="<link linkend=\"GuideImageSearchFindingDuplicates\">Duplicates window</link>"
search_xml="<link linkend=\"GuideImageSearchSearch\">Search window</link>"
pan_view_xml="<link linkend=\"GuideOtherWindowsPanView\">Pan view window</link>"
collections_xml="<link linkend=\"GuideCollections\">Collections window</link>"
image_xml="<link linkend=\"GuideOtherWindowsImageWindow\">Image view window</link>"
main_window_xml="<link linkend=\"GuideMainWindow\">Main window</link>"

pre_1_xml="<table frame=\"all\">\n
<title>\n
"

pre_2_xml="
keyboard shortcuts\n
</title>\n
<tgroup cols=\"2\" rowsep=\"1\" colsep=\"1\">\n
<thead>\n
<row>\n
<entry>Shortcut</entry>\n
<entry>Action</entry>\n
</row>\n
</thead>\n
<tbody>\n
"

post_xml="</tbody></tgroup></table>"

post_main_window_xml="
<row>
<entry>
<keycap>1</keycap>...<keycap>6</keycap>
</entry>
<entry>Toggle mark 1 ... 6</entry>
</row>
<row>
<entry>
<code>Ctrl +<keycap>1</keycap></code>...<code>Ctrl +<keycap>6</keycap></code>
</entry>
<entry>Select mark 1 ... 6</entry>
</row>

</tbody></tgroup></table>"

# shellcheck disable=SC2016
awk_window='BEGIN {
	{FS=","}
	while ($0 !~ /^hard_coded_window_keys/) {getline}
}
$0~/\{0, 0/ {exit}
{
gsub(/{0/, "", $1);
gsub(/{GDK_CONTROL_MASK \+ GDK_SHIFT_MASK/, "Ctrl + Shift +", $1);
gsub(/{GDK_CONTROL_MASK/, "Ctrl +", $1);
gsub(/{GDK_SHIFT_MASK/, "Shift +", $1);
gsub(/{GDK_MOD1_MASK/, "Alt +", $1);
gsub(/ GDK_KEY_/, "", $2);
gsub(/\047/, "", $2);
gsub(/N_\(/, "", $3);
gsub(/\)\}/, "", $3);
gsub(/"/, "", $3);
}
{print "<row> <entry> <code>", $1, "<keycap>", $2, "</keycap> </code> </entry> <entry>", $3, "</entry> </row>"}
'

# This assumes that lines beginning with /^  { "/ are the only ones in layout-util.cc containing key shortcuts
# shellcheck disable=SC2016
awk_main_window='BEGIN {
	{FS=","}
}
$0 ~ /^  { "/ {
	if ($4 !~ /NULL/) {
		{
		gsub(/^[[:space:]]+|[[:space:]]+$/,"",$4);
		gsub(/^[[:space:]]+|[[:space:]]+$/,"",$5);
		gsub(/{0/, "", $4);
		gsub(/<control>/, "Ctrl + ", $4);
		gsub(/<alt>/, "Alt + ", $4);
		gsub(/<shift>/, "Shift + ", $4);
		gsub(/"/,"", $4);
		gsub(/slash/,"/", $4);
		gsub(/bracketleft/,"[", $4);
		gsub(/bracketright/,"]", $4);
		gsub(/"/,"", $5);
		gsub(/N_\(/, "", $5);
		gsub(/\)\}/, "", $5);
		gsub(/"/, "", $5);
		gsub(/\.\.\./, "", $5);
		gsub(/)/, "", $5);
		}
		{print "<row> <entry> <code>", "<keycap>", $4, "</keycap> </code> </entry> <entry>", $5, "</entry> </row>"}
	}
}
'

keys_xml=$(awk "$awk_window" ./src/dupe.cc )
printf '%b\n' "$pre_1_xml $duplicates_xml $pre_2_xml $keys_xml $post_xml" > ./doc/docbook/GuideReferenceDuplicatesShortcuts.xml

keys_xml=$(awk "$awk_window" ./src/search.cc )
printf '%b\n' "$pre_1_xml $search_xml $pre_2_xml $keys_xml $post_xml" > ./doc/docbook/GuideReferenceSearchShortcuts.xml

keys_xml=$(awk "$awk_window" ./src/pan-view/pan-view.cc )
printf '%b\n' "$pre_1_xml $pan_view_xml $pre_2_xml $keys_xml $post_xml" > ./doc/docbook/GuideReferencePanViewShortcuts.xml

keys_xml=$(awk "$awk_window" ./src/collect-table.cc)
printf '%b\n' "$pre_1_xml $collections_xml $pre_2_xml $keys_xml $post_xml" > ./doc/docbook/GuideReferenceCollectionsShortcuts.xml

keys_xml=$(awk "$awk_window" ./src/img-view.cc)
printf '%b\n' "$pre_1_xml $image_xml $pre_2_xml $keys_xml $post_xml" > ./doc/docbook/GuideReferenceImageViewShortcuts.xml

keys_xml=$(awk "$awk_main_window" ./src/layout-util.cc)
printf '%b\n' "$pre_1_xml $main_window_xml $pre_2_xml $keys_xml $post_main_window_xml" > ./doc/docbook/GuideReferenceMainWindowShortcuts.xml
