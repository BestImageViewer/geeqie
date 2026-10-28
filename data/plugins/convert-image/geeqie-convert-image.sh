#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Create JPEG or PNG copies using ImageMagick and Zenity.
## Originals and existing destination files are never overwritten.
## Only the first frame/page is converted. Supported formats depend on
## ImageMagick's installed delegates and security policy.

if ! command -v zenity > /dev/null 2>&1
then
	printf '%s\n' 'Error: Convert image requires Zenity.' >&2
	exit 1
fi

if command -v magick > /dev/null 2>&1
then
	converter=magick
elif command -v convert > /dev/null 2>&1
then
	converter=convert
else
	zenity --error --no-markup --title='Convert image' --text='ImageMagick is required. Install ImageMagick and try again.'
	exit 1
fi

[ "$#" -gt 0 ] || exit 0

source_mode=$(zenity --list --radiolist --title='Conversion source' --text='Choose which member of each selected group to convert.' --column='Select' --column='Source' TRUE Selected FALSE JPEG FALSE RAW) || exit 0
case "$source_mode" in Selected|JPEG|RAW) ;; *) exit 1 ;; esac
max_size=$(zenity --entry --title='Output size' --text='Maximum width and height in pixels (0 keeps original size; smaller images are not enlarged).' --entry-text=0) || exit 0
case "$max_size" in ''|*[!0-9]*) exit 1 ;; *) ;; esac
[ "$max_size" -le 65535 ] || exit 1

format=$(zenity --list --radiolist --title='Convert image' --text='Choose an output format. Originals are preserved; existing output names are skipped.' --column='Select' --column='Format' TRUE JPEG FALSE PNG) || exit 0
case "$format" in
	JPEG) extension=jpg; coder=jpeg ;;
	PNG) extension=png; coder=png ;;
	*) exit 1 ;;
esac

destination=$(zenity --file-selection --directory --title='Choose a folder for converted copies') || exit 0
case "$destination" in
	/*) ;;
	*) destination="$PWD/$destination" ;;
esac
if [ ! -d "$destination" ] || [ ! -w "$destination" ]
then
	printf 'Error: Destination is not a writable folder: %s\n' "$destination" >&2
	exit 1
fi

if [ "$format" = JPEG ]
then
	quality=$(zenity --scale --title='JPEG quality' --text='Choose JPEG quality. Transparent areas become white.' --min-value=1 --max-value=100 --value=90 --step=1) || exit 0
	case "$quality" in
		''|*[!0-9]*) exit 1 ;;
		*) ;;
	esac
	[ "$quality" -ge 1 ] && [ "$quality" -le 100 ] || exit 1
fi

work_dir=$(mktemp -d "$destination/.geeqie-convert.XXXXXXXXXX") || exit 1
trap 'rm -rf "$work_dir"' 0
trap 'exit 1' HUP INT TERM

## @brief Copy the completed image after the caller exclusively creates its name.
write_image()
{
	trap 'rm -f "$output"; exit 1' HUP INT TERM
	if cat "$work_dir/output"
	then
		return 0
	fi
	rm -f "$output"
	return 1
}

## @brief Decode the first frame with the options for the selected output format.
convert_image()
{
	if [ "$format" = JPEG ]
	then
		set -- -background white -alpha remove -alpha off -quality "$quality"
	else
		set --
	fi
	if [ "$max_size" -gt 0 ]
	then
		set -- "$@" -resize "${max_size}x${max_size}>"
	fi
	"$converter" "$work_dir/input[0]" -auto-orient "$@" "$coder:$work_dir/output"
}

result=0
for source
do
	case "$source" in
		/*) ;;
		*) source="$PWD/$source" ;;
	esac
	if [ "$source_mode" != Selected ]
	then
		# The remote grouping protocol is line-delimited; refuse ambiguous paths.
		case "$source" in *'
'*) printf 'Error: Group lookup cannot handle a newline in a filename.\n' >&2; result=1; continue ;; *) ;; esac
		if ! geeqie --get-sidecars="$source" > "$work_dir/group"
		then
			printf 'Error: Cannot query group: %s\n' "$source" >&2
			result=1
			continue
		fi
		chosen=
		matches=0
		while IFS= read -r member || [ -n "$member" ]
		do
			ext=$(printf '%s' "${member##*.}" | tr '[:upper:]' '[:lower:]')
			case "$source_mode:$ext" in
				JPEG:jpg|JPEG:jpeg|JPEG:jpe|RAW:3fr|RAW:arw|RAW:cr2|RAW:cr3|RAW:crw|RAW:dcr|RAW:k25|RAW:dng|RAW:erf|RAW:fff|RAW:iiq|RAW:kdc|RAW:mef|RAW:mos|RAW:mrw|RAW:nef|RAW:nrw|RAW:orf|RAW:pef|RAW:raf|RAW:raw|RAW:rw2|RAW:rwl|RAW:sr2|RAW:srf|RAW:srw|RAW:x3f)
					[ -f "$member" ] || continue
					chosen=$member
					matches=$((matches + 1)) ;;
				*) ;;
			esac
		done < "$work_dir/group"
		if [ "$matches" -ne 1 ]
		then
			printf 'Error: Expected one %s member in group: %s (found %s)\n' "$source_mode" "$source" "$matches" >&2
			result=1
			continue
		fi
		source=$chosen
		if [ -f "$work_dir/processed" ] && grep -F -x -e "$source" "$work_dir/processed" >/dev/null
		then
			continue
		fi
		printf '%s\n' "$source" >> "$work_dir/processed"
	fi
	name=${source##*/}
	stem=${name%.*}
	[ -n "$stem" ] || stem=$name
	output="$destination/$stem.$extension"
	if [ -e "$output" ] || [ -L "$output" ]
	then
		printf 'Skipped existing destination: %s\n' "$output" >&2
		result=1
		continue
	fi
	# A fixed input name prevents ImageMagick interpreting filename syntax.
	if [ ! -f "$source" ] || ! cp "$source" "$work_dir/input"
	then
		printf 'Error: Cannot read source: %s\n' "$source" >&2
		result=1
		continue
	fi
	if convert_image
	then
		# Noclobber refuses conflicts created during conversion. The function
		# runs only after redirection succeeds, so cleanup cannot remove a
		# conflicting file. Copying also works on filesystems without hard links.
		if (set -C; write_image > "$output")
		then
			printf 'Created: %s\n' "$output"
		else
			printf 'Error: Cannot create destination: %s\n' "$output" >&2
			result=1
		fi
	else
		printf 'Error: Conversion failed: %s\n' "$source" >&2
		result=1
	fi
	rm -f "$work_dir/input" "$work_dir/output"
done

exit "$result"
