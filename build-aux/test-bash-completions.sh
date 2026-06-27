#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Validates that bash completions include all (and only) appropriate options and actions
##
## $1 Geeqie executable
## $2 Path to bash completions data file
##

geeqie_exe="$1"
completions_file="$2"

# Make sure input files exist.
if [ ! -x "$geeqie_exe" ] || [ ! -e "$completions_file" ]; then
    echo "Missing input files"
    exit 2
fi

exit_status=0


# Command line completion
## Check the sections: actions, options.
## The file_types section is not checked.
## Look for options not included and options erroneously included.

actions_cc=$(mktemp "${HOME}/geeqie_bash_completions_test.XXXXXXXXXX")
actions_help=$(mktemp "${HOME}/geeqie_bash_completions_test.XXXXXXXXXX")
actions_help_filtered=$(mktemp "${HOME}/geeqie_bash_completions_test.XXXXXXXXXX")
help_output=$(mktemp "${HOME}/geeqie_bash_completions_test.XXXXXXXXXX")
options_cc=$(mktemp "${HOME}/geeqie_bash_completions_test.XXXXXXXXXX")
options_help=$(mktemp "${HOME}/geeqie_bash_completions_test.XXXXXXXXXX")

options1=$(grep 'options=' "$completions_file")
# length(options=') = 9
options2=$(echo "$options1" | cut -c 9-)
# \x27 is "-" character
options3=$(echo "$options2" | sed "s/\x27//g")
options4=$(echo "$options3" | sed "s/ /\n/g")
echo "$options4" | sort > "$options_cc"

action_list1=$(grep 'actions=' "$completions_file")
action_list2=$(echo "$action_list1" | cut --delimiter='=' --fields=2)
action_list3=$(echo "$action_list2" | sed "s/\x27//g")
action_list4=$(echo "$action_list3" | sed 's/ /\n/g')
echo "$action_list4" | sort > "$actions_cc"

xvfb-run --auto-servernum "$geeqie_exe" --help > "$help_output"

awk -W posix -v options_help="$options_help" '
BEGIN {
LINT = "fatal"
valid_found = 0
}

/Application Options/ {valid_found = 1}
/--display=/ {valid_found = 0}
/--/ && valid_found {
	start = match($0, /--/)
	new=substr($0, start)
	{gsub(/ .*/, "", new)}
	{gsub(/=.*/, "=", new)}
	{gsub(/\[.*/, "", new)}
	print new >> options_help
	}

END {
close(options_help)
}
' "$help_output"

# https://backreference.org/2010/02/10/idiomatic-awk/ is a good reference
awk -W posix '
BEGIN {
LINT = "fatal"
exit_status = 0
}

NR == FNR{a[$0] = "";next} !($0 in a) {
	exit_status = 1
	print "Bash completions - Option missing: " $0
	}

END {
exit exit_status
}
' "$options_cc" "$options_help"

if [ $? = 1 ]
then
	exit_status=1
fi

awk -W posix '
BEGIN {
LINT = "fatal"
exit_status = 0
}

NR == FNR{a[$0] = "";next} !($0 in a) {
	# In a no-debug build, --debug and --grep are missing.
	# This error can be ignored
	if ($0 != "--debug=" && $0 != "--grep=") {
		exit_status = 1
		print "Bash completions - Option error: " $0
		}
	}

END {
exit exit_status
}
' "$options_help" "$options_cc"

if [ $? = 1 ]
then
	exit_status=1
fi

xvfb-run --auto-servernum "$geeqie_exe" --action-list --quit | cut --delimiter=' ' --fields=1 | sed '/^$/d' > "$actions_help"

## @FIXME Find a better way to ignore the junk
awk -W posix -v actions_help_filtered="$actions_help_filtered" '
BEGIN {
LINT = "fatal"
valid_found = 0
}

/about/ {
	valid_found = 1
	}

/^[A-Z].*?/ && valid_found {
	if ((index($0, "desktop") == 0) && (index($0, "glx") == 0) && (index($0, "Geeqie not running") == 0) && (index($0, "Gtk-Message") == 0)) {
		print $0 >> actions_help_filtered
		}
	}

END {
close(actions_help_filtered)
}
' "$actions_help"

awk -W posix '
BEGIN {
LINT = "fatal"
exit_status = 0
}

NR == FNR{a[$0] = "";next} !($0 in a) {
	print "Bash completions - Action missing: " $0
	exit_status = 1
	}

END {
exit exit_status
}
' "$actions_cc" "$actions_help_filtered"

if [ $? = 1 ]
then
	exit_status=1
fi

awk -W posix '
BEGIN {
LINT = "fatal"
exit_status = 0
}

NR == FNR{a[$0] = "";next} !($0 in a) {
	print "Bash completions - Action error: " $0
	exit_status = 1
	}

END {
exit exit_status
}
' "$actions_help_filtered" "$actions_cc"

if [ $? = 1 ]
then
	exit_status=1
fi

rm --force "$actions_cc"
rm --force "$actions_help"
rm --force "$actions_help_filtered"
rm --force "$help_output"
rm --force "$options_cc"
rm --force "$options_help"


exit "$exit_status"
