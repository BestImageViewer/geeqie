#!/bin/sh
#
# check-glib-types.sh
#
# Usage:
#   check-glib-types.sh <src-root> <build-root> <file>...
#
# Issues warnings when GLib fundamental typedefs are used instead
# of standard C/C++ types.
#
# See https://docs.gtk.org/glib/types.html
#

src_root="$1"
build_root="$2"
shift 2

if [ -z "$src_root" ] || [ -z "$build_root" ]
then
	echo "Usage: $0 <src-root> <build-root> <file>..." >&2
	exit 2
fi

tmpfile="$(mktemp "$build_root/glib-types-warnings.XXXXXX")"
trap 'rm -f "$tmpfile"' EXIT

for file in "$@"
do
# If file is tracked by git, skip it when clean.
# If it is not tracked, check the whole file.
if git -C "$src_root" ls-files --error-unmatch -- "$file" >/dev/null 2>&1
then
	if git -C "$src_root" diff --quiet -- "$file" &&
	   git -C "$src_root" diff --cached --quiet -- "$file"
	then
		continue
	fi

	check_whole_file=false
else
	check_whole_file=true
fi

if [ "$check_whole_file" = true ]
then
	awk -v file="$file" '
	BEGIN {
		type_map["gchar"]    = "char"
		type_map["guchar"]   = "unsigned char"
		type_map["gshort"]   = "short"
		type_map["gushort"]  = "unsigned short"
		type_map["gint"]     = "int"
		type_map["guint"]    = "unsigned int"
		type_map["glong"]    = "long"
		type_map["gulong"]   = "unsigned long"
		type_map["gint8"]    = "int8_t"
		type_map["guint8"]   = "uint8_t"
		type_map["gint16"]   = "int16_t"
		type_map["guint16"]  = "uint16_t"
		type_map["gint32"]   = "int32_t"
		type_map["guint32"]  = "uint32_t"
		type_map["gint64"]   = "int64_t"
		type_map["guint64"]  = "uint64_t"
		type_map["gfloat"]   = "float"
		type_map["gdouble"]  = "double"
		type_map["gboolean"] = "bool"
		type_map["gsize"]    = "size_t"
		type_map["gssize"]   = "ssize_t"
	}

	{
		orig = $0
		line = orig

		sub(/\/\/.*/, "", line)

		for (glib_type in type_map) {
			pattern = "(^|[^A-Za-z0-9_])" glib_type "([ \t*]+)[A-Za-z_][A-Za-z0-9_]*"

			if (line ~ pattern) {
				printf("%s:%d: warning: use '\''%s'\'' instead of '\''%s'\''\n",
				       file,
				       NR,
				       type_map[glib_type],
				       glib_type)
				print "  " orig
			}
		}
	}
	' "$src_root/$file" >> "$tmpfile"

	continue
fi


	git -C "$src_root" diff -U0 -- "$file" | awk -v file="$file" '
	BEGIN {
		type_map["gchar"]    = "char"
		type_map["guchar"]   = "unsigned char"
		type_map["gshort"]   = "short"
		type_map["gushort"]  = "unsigned short"
		type_map["gint"]     = "int"
		type_map["guint"]    = "unsigned int"
		type_map["glong"]    = "long"
		type_map["gulong"]   = "unsigned long"
		type_map["gint8"]    = "int8_t"
		type_map["guint8"]   = "uint8_t"
		type_map["gint16"]   = "int16_t"
		type_map["guint16"]  = "uint16_t"
		type_map["gint32"]   = "int32_t"
		type_map["guint32"]  = "uint32_t"
		type_map["gint64"]   = "int64_t"
		type_map["guint64"]  = "uint64_t"
		type_map["gfloat"]   = "float"
		type_map["gdouble"]  = "double"
		type_map["gboolean"] = "bool"
		type_map["gsize"]    = "size_t"
		type_map["gssize"]   = "ssize_t"
	}

	/^@@/ {
		if (match($0, /\+([0-9]+)(,([0-9]+))?/, m)) {
			lineno = m[1]
		}
		next
	}

	/^\+/ && !/^\+\+\+/ {
		orig = substr($0, 2)
		line = orig

		sub(/\/\/.*/, "", line)

		for (glib_type in type_map) {
			pattern = "(^|[^A-Za-z0-9_])" glib_type "([ \t*]+)[A-Za-z_][A-Za-z0-9_]*"

			if (line ~ pattern) {
				printf("%s:%d: warning: use '\''%s'\'' instead of '\''%s'\''\n",
				       file,
				       lineno,
				       type_map[glib_type],
				       glib_type)
				print "  " orig
			}
		}

		lineno++
	}
	' >> "$tmpfile"

	# Also check staged changes, if any
	git -C "$src_root" diff --cached -U0 -- "$file" | awk -v file="$file" '
	BEGIN {
		type_map["gchar"]    = "char"
		type_map["guchar"]   = "unsigned char"
		type_map["gshort"]   = "short"
		type_map["gushort"]  = "unsigned short"
		type_map["gint"]     = "int"
		type_map["guint"]    = "unsigned int"
		type_map["glong"]    = "long"
		type_map["gulong"]   = "unsigned long"
		type_map["gint8"]    = "int8_t"
		type_map["guint8"]   = "uint8_t"
		type_map["gint16"]   = "int16_t"
		type_map["guint16"]  = "uint16_t"
		type_map["gint32"]   = "int32_t"
		type_map["guint32"]  = "uint32_t"
		type_map["gint64"]   = "int64_t"
		type_map["guint64"]  = "uint64_t"
		type_map["gfloat"]   = "float"
		type_map["gdouble"]  = "double"
		type_map["gboolean"] = "bool"
		type_map["gsize"]    = "size_t"
		type_map["gssize"]   = "ssize_t"
	}

	/^@@/ {
		if (match($0, /\+([0-9]+)(,([0-9]+))?/, m)) {
			lineno = m[1]
		}
		next
	}

	/^\+/ && !/^\+\+\+/ {
		orig = substr($0, 2)
		line = orig

		sub(/\/\/.*/, "", line)

		for (glib_type in type_map) {
			pattern = "(^|[^A-Za-z0-9_])" glib_type "([ \t*]+)[A-Za-z_][A-Za-z0-9_]*"

			if (line ~ pattern) {
				printf("%s:%d: warning: use '\''%s'\'' instead of '\''%s'\''\n",
				       file,
				       lineno,
				       type_map[glib_type],
				       glib_type)
				print "  " orig
			}
		}

		lineno++
	}
	' >> "$tmpfile"
done

if [ -s "$tmpfile" ]
then
	warnings=$(grep -c 'warning:' "$tmpfile")

	echo
	echo "================================"
	echo "GLib type warnings"
	echo "================================"
	cat "$tmpfile"
	echo "================================"
	echo "Total GLib type warnings: $warnings"
	echo "================================"

	touch "$build_root/glib-types-warnings.flag"

	exit 1
fi

exit 0
