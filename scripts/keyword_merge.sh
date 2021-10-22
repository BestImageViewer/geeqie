#!/bin/bash
#
# This is a simple script to merge the keyword tree of one Geeqie
# configuration file into another.
# The keyword trees are simply concatenated. When Geeqie loads
# the resulting configuration file, any duplicates are discarded.
#
# There is no error checking.

merge_file()
{
flag=0
while read -r line_merge
do
	if [[ flag -eq 0 ]]
	then
		if [[ $line_merge =~ "<keyword_tree>" ]]
		then
			flag=1
		fi
	else
		if [[ $line_merge =~ "</keyword_tree>" ]]
		then
			flag=0
		else
			echo "$line_merge" >> "$2"
		fi
	fi
done < "$1"
}

np=$#

zenity --info --text="This script will merge the keywords from one Geeqie\nconfiguration file into another.\n\n\The command format is:\nmerge.sh {config. file to merge into} {config. file to merge from}\n\nIf you do not supply parameters, you are prompted.\n\nYou are given the option to backup the main config. file before it is overwritten with the merged data.\n\nEnsure that Geeqie is not running." --title="Geeqie merge keywords" --width=400 --height=200
if [[ $? -eq 1 ]]
then
	exit
fi


if [[ $np -ge 3 ]]
then
	zenity --error --text "Too many parameters"
	exit
elif [[ $np -eq 0 ]]
then
	config_main=$(zenity --file-selection --file-filter="geeqierc.xml" --file-filter="*.xml" --file-filter="*" --title="Select main configuration file")
	if [[ $? -eq 1 ]]
	then
		exit
	fi
	config_merge=$(zenity --file-selection --file-filter="geeqierc.xml" --file-filter="*.xml" --file-filter="*"-- title="Select configuration file to merge from")
	if [[ $? -eq 1 ]]
	then
		exit
	fi
elif [[ $np -eq 1 ]]
then
	config_merge=$(zenity --file-selection --file-filter="geeqierc.xml" --file-filter="*.xml" --file-filter="*" --title="Select configuration file to merge from")
	if [[ $? -eq 1 ]]
	then
		exit
	fi
fi

tmp_file=$(mktemp)

while read -r line_main
do
	if [[ $line_main =~ "</keyword_tree>" ]]
	then
		merge_file "$config_merge" "$tmp_file"
	fi
	echo "$line_main" >> "$tmp_file"
done < "$config_main"

zenity --question --text="Backup configuration file before overwriting?"
if [[ $? -eq 0 ]]
then
	cp "$config_main" "$config_main.$(date +%Y%m%d%H%M%S)"
fi

mv "$tmp_file" "$config_main"
