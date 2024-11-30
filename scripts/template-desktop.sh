#!/bin/sh


## @file
## @brief Insert updated menu "Valid sections" list into the desktop template file.
##
## This needs to be run only when the menus have changed.
##

tmp_file=$(mktemp "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX")
path=$(dirname "$(realpath "$0")")
srcpath=$(dirname "$path")/src/ui/menu-classic.ui
templatepath=$(dirname "$path")/plugins/org.geeqie.template.desktop.in

awk -W posix -v src_path="$srcpath" 'BEGIN {
LINT = "fatal"
menu_flag = 0
template_flag = 0
i = 0
}

function get_menus()
{
	{while ((getline line < src_path) > 0 )
		{
		if (line == "<ui>")
			{
			menu_flag = 1
			}
		if (line == "<\047ui>")
			{
			menu_flag = 0
			}
		if (menu_flag >= 1)
			{
			gsub(/\047|"|\/|<|>/, "", line)
			split_count = split(line, lineArr, "=")
			if (split_count > 1 && index(lineArr[1], "menu action") > 0)
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
			if (split_count > 1 && index(lineArr[1], "placeholder name"))
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
			if (split_count > 1 && lineArr[2] == "PluginsMenu")
				{
				i = i - 1
				}
			}
		}
	}
}

/Valid sections/ {template_flag = 1; print; get_menus()}
/For other keys/ {template_flag = 0; print ""}
(template_flag == 0) {print}

END {close(src_path)}
'  "$templatepath" > "$tmp_file"

cat "$tmp_file"
printf '%s\n' "$PWD"

if diff --unified=0 "./plugins/org.geeqie.template.desktop.in" "$tmp_file" | zenity --title="Plugin template update" --text-info
then
	mv "$tmp_file" "$templatepath"
else
	rm "$tmp_file"
fi
