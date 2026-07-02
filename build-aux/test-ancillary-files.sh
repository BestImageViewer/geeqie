#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Perform validity checks on project ancillary files
##
## $1 Root of project sources or NULL for current
##
## Perform validity checks on project ancillary files:
## appstream
## desktop
## scripts
## ui
## xml
##

if [ -n "$1" ]
then
	cd "$1" || exit 1
fi

exit_status=0

# All script files must be POSIX
# downsize is a third-party file and is excluded
while read -r file
do
	result=$(file "$file" | grep "shell script")

	if [ -n "$result" ]
	then
		if [ "${result#*"POSIX"}" = "$result" ]
		then
			printf "ERROR; Executable script is not POSIX: %s\n" "$file"
			exit_status=1
		fi
	fi
done << EOF
$(find "./data/plugins" "./build-aux" "./packaging" "./snap/local" "./tools" -type f -not -name downsize -executable)
EOF

# Script files must have the file extension .sh  or
# be symlinked as so - for doxygen
while read -r file
do
	result=$(file "$file" | grep "POSIX shell script")

	if [ -n "$result" ]
	then
		base_with_sh=$(basename "$file")
		base_without_sh=$(basename "$file" .sh)

		if [ "$base_with_sh" = "$base_without_sh" ]
		then
			if [ ! -f "$file.sh" ]
			then
				printf "ERROR; Executable script does not have a .sh extension: %s\n" "$file"
				exit_status=1
			fi
		fi
	fi
done << EOF
$(find "./data/plugins" "./build-aux" "./packaging" "./tools" "./snap/local" -type f -executable)
EOF

# Check if all options are in the disabled checks
while read -r line
do
	if [ -n "$line" ]
	then
		res=$(grep "$line" "./tools/test-all.sh")
		if [ -z "$res" ]
		then
			printf "ERROR; Option no disabled check in ./build-aux/test-all.sh: %s\n" "$line"
			exit_status=1
		fi
	fi
done << EOF
$(awk -W posix 'BEGIN {LINT = "fatal"; FS="\047"} /option\(/ { if (substr($2, 1, 2) != "gq") { print $2 } }' meson_options.txt)
EOF

# Check if all options are in the disabled checks in a GitHub run
# Directory .github is not in the source tar
if [ -d ".github" ]
then
	while read -r line
	do
		if [ -n "$line" ]
		then
			res=$(grep "\-D$line=disabled" "./.github/workflows/check-build-actions.yml")
			if [ -z "$res" ]
			then
				printf "ERROR; Option no disabled check in .github/workflows/check-build-actions.yml: %s\n" "$line"
				exit_status=1
			fi
		fi
	done << EOF
$(awk -W posix 'BEGIN {LINT = "fatal"; FS="\047"} /option\(/ { if (substr($2, 1, 2) != "gq") { print $2 } }' meson_options.txt)
EOF
fi

# Markdown lint
# Runs as a GitHub Action
if [ -z "$GITHUB_WORKSPACE" ]
then
	if [ -z "$(command -v mdl)" ]
	then
		printf "ERROR: mdl is not installed"
		exit_status=1
	else
		while read -r line
		do
			if [ -n "$line" ]
			then
				if [ "${line#*": MD"}" != "$line" ]
				then
					printf "ERROR; Markdown lint error in: %s\n" "$line"
					exit_status=1
				fi
			fi
		done << EOF
$(find . -not -path "*/.*" -not -path "*/subprojects/*" -name "*.md" -exec mdl --no-verbose --config .mdlrc {} \;)
EOF
	fi
fi

# Shellcheck lint
# Runs as a GitHub Action
if [ -z "$GITHUB_WORKSPACE" ]
then
	if [ -z "$(command -v shellcheck)" ]
	then
		printf "ERROR: shellcheck is not installed"
		exit_status=1
	else
		while read -r line
		do
			if [ -n "$line" ]
			then
				shellcheck_error=$(shellcheck "$line" 2>&1)
				if [ -n "$shellcheck_error" ]
				then
					printf "ERROR; shellcheck error in: %s\n" "$shellcheck_error"
					exit_status=1
				fi
			fi
		done << EOF
$(find . -name "*.sh" -not -path "./subprojects/*")
EOF
	fi
fi

# gtk-builder ui lint - should not check the menu.ui files
if [ -z "$(command -v gtk4-builder-tool)" ]
then
	printf "ERROR: gtk4-builder-tool is not installed"
	exit_status=1
else
	while read -r line
	do
		if [ -n "$line" ]
		then
			if [ "${line#*"menu"}" = "$line" ]
			then
				if [ -z "$GITHUB_WORKSPACE" ]
				then
					if ! builder_error=$(gtk4-builder-tool validate "$line" 2>&1)
					then
						printf "ERROR; gtk4-builder-tool error in: %s\n" "$builder_error"
						exit_status=1
					fi
				else
					if ! builder_error=$(xvfb-run --auto-servernum gtk4-builder-tool validate "$line" 2>&1)
					then
						printf "ERROR; gtk4-builder-tool error in: %s\n" "$builder_error"
						exit_status=1
					fi
				fi
			fi
		fi
	done << EOF
$(find $! -name "*.ui")
EOF
fi

# Desktop files lint
if [ -z "$(command -v desktop-file-validate)" ]
then
	printf "ERROR: desktop-file-validate is not installed"
	exit_status=1
else
	while read -r line
	do
		if [ -n "$line" ]
		then
			desktop_file=$(basename "$line" ".in")
			ln --symbolic "$line" "./$desktop_file"
			result=$(desktop-file-validate "./$desktop_file")

			rm "./$desktop_file"
			if [ -n "$result" ]
			then
				printf "ERROR; desktop-file-validate error in: %s %s\n" "$line" "$result"
				exit_status=1
			fi
		fi
	done << EOF
$(find . -name "*.desktop.in")
EOF
fi
echo "pwd $PWD"
# AppStream lint
if [ -z "$(command -v appstreamcli)" ]
then
	printf "ERROR: appstreamcli is not installed"
	exit_status=1
else
	if ! result=$(appstreamcli validate ./data/org.geeqie.Geeqie.metainfo.xml.in --pedantic --explain --no-net)
	then
		exit_status=1
		status="Error"
	else
		line_count=$(echo "$result" | wc --lines)

		if [ "$line_count" -gt 1 ]
		then
			status="Warning"
		else
			status="Passed"
		fi
	fi

	printf "%s: appstreamcli in org.geeqie.Geeqie.metainfo.xml.in: \n%s\n" "$status" "$result"
fi

# xml files lint
if [ -z "$(command -v xmllint)" ]
then
	printf "ERROR: xmllint is not installed"
	exit_status=1
else
	while read -r line
	do
		if [ -n "$line" ]
		then
			if ! xmllint --quiet --nowarning "$line" > /dev/null
			then
				printf "ERROR: xmllint error in: %s\n" "$line"
				exit_status=1
			fi
		fi
	done << EOF
$(find ./doc/docbook -name "*.xml")
EOF
fi


exit "$exit_status"
