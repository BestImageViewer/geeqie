#! /bin/bash

# Insert updated menu "Valid sections" list into the desktop template file.
# This needs to be run only when the menus have changed.

tmp_file=$(mktemp)
path=$(dirname "$(realpath "$0")")
srcpath=$(dirname "$path")/src/layout_util.c
templatepath=$(dirname "$path")/plugins/template.desktop.in

awk -v src_path="$srcpath" 'BEGIN {
menu_flag = 0
template_flag = 0
i = 0
}

function get_menus()
{
	{while ((getline line < src_path) > 0 )
		{
		if (line == "\"<ui>\"")
			{
			menu_flag = 1
			}
		if (line == "\"<\057ui>\";")
			{
			menu_flag = 0
			}
		if (menu_flag >= 1)
			{
			gsub(/\047|"|\/|<|>/, "", line)
			split(line, lineArr, "=")

			if (index(lineArr[1], "menu action") > 0)
				{
				i = i + 1
				menu[i] = lineArr[2]
				if ( i == 3)
					{
					print "#    " menu[1] "/" menu[2] "/"  lineArr[2]
					}
				if ( i == 2)
					{
					print "#    " menu[1] "/"  lineArr[2]
					}
				if (i == 1)
					{
					print "#    " lineArr[2]
					}
				}
			if (index(lineArr[1], "placeholder name"))
				{
				if ( i == 2)
					{
					print "#    " menu[1] "/" menu[2] "/" lineArr[2]
					}
				else
					{
					print "#    "   menu[1] "/" lineArr[2]
					}
				}
			gsub(" ", "", line)
			if (line == "menu")
				{
				i = i - 1
				}
			}
		}
	}
}

/Valid sections/ {template_flag = 1; print; get_menus()}
/This is a filter/ {template_flag = 0; print ""}
(template_flag == 0) {print}
'  "$templatepath" > "$tmp_file"

zenity --text-info < "$tmp_file"
if [ "$?" == 0 ]
then
	mv "$tmp_file" "$templatepath"
else
	rm "$tmp_file"
fi
