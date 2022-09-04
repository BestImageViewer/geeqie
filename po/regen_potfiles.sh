#!/bin/sh

## @file
## @brief Generate a patch to update POTFILES
##
## Use like this: ./regen_potfiles.sh | patch -p0
##

# TODO(xsdg): Re-write this in a simpler way and test that it works: (cd ..; find ... | sort > $TMP)

TMP=POTFILES.$$
((find ../src/ -type f \( -name '*.c' -o -name '*.cc' \) ; find ../ -type f -name '*.desktop.in' ; find ../ -type f -name '*.appdata.xml.in') | while read f; do
	(echo $f | sed 's#^../##')
done) | sort > $TMP
diff -u POTFILES $TMP
rm -f $TMP
