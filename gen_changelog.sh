#!/bin/sh

## @file
## @brief Update ChangeLog file  
## - it keeps "pre-svn" history and inserts git log at top,  
## - it uses C locale for date format.  
## - It has to be run where ChangeLog.gqview is.  
##
## ChangeLog.html is also created
##

builddir="$2"

cd "$1"

[ ! -e "ChangeLog.gqview" ] && exit 1
[ ! -x "$(command -v git)" ] && exit 1
[ ! -d ".git" ] && exit 1

LC_ALL=C git log --no-merges --no-notes --encoding=UTF-8 --no-follow --use-mailmap 1b58572cf58e9d2d4a0305108395dab5c66d3a09..HEAD > "$builddir/ChangeLog.$$.new" && \
cat ChangeLog.gqview >> "$builddir/ChangeLog.$$.new" && \
mv -f "$builddir/ChangeLog.$$.new" "$builddir/ChangeLog"


echo "<textarea rows='6614' cols='100'>" >"$builddir/ChangeLog.$$.old.html" && \
tail -6613 "$builddir/ChangeLog" >> "$builddir/ChangeLog.$$.old.html" && \
echo "</textarea>" >>"$builddir/ChangeLog.$$.old.html" && \
echo "<html>" > "$builddir/ChangeLog.$$.new.html" && \
echo "<body>" >> "$builddir/ChangeLog.$$.new.html" && \
echo "<ul>" >> "$builddir/ChangeLog.$$.new.html" && \
LC_ALL=C git log --no-merges --no-notes --encoding=UTF-8 --date=format:'%Y-%m-%d' --no-follow --use-mailmap --pretty=format:"<li><a href=\"http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git;a=commit;h=%H\">view commit </a></li><p>Author: %aN<br>Date: %ad<br><textarea rows=4 cols=100>%s %n%n%b</textarea><br><br></p>" 1b58572cf58e9d2d4a0305108395dab5c66d3a09..HEAD >> "$builddir/ChangeLog.$$.new.html" && \
echo "" >> "$builddir/ChangeLog.$$.new.html" && \
cat "$builddir/ChangeLog.$$.old.html" >> "$builddir/ChangeLog.$$.new.html" && \
echo "</ul>" >> "$builddir/ChangeLog.$$.new.html" && \
echo "</body>" >> "$builddir/ChangeLog.$$.new.html" && \
echo "</html>" >> "$builddir/ChangeLog.$$.new.html"

rm "$builddir/ChangeLog.$$.old.html"
mv -f "$builddir/ChangeLog.$$.new.html" "$builddir/ChangeLog.html"

# Meson: distribute in tarballs. The first variable is more reliable, but requires Meson 0.58.
# Fallback to the older one if necessary
for distdir in "$MESON_PROJECT_DIST_ROOT" "$MESON_DIST_ROOT"; do
    if [ -n "$distdir" ]; then
        cp -f "$builddir/ChangeLog.html" "$distdir/ChangeLog.html"
        cp -f "$builddir/ChangeLog" "$distdir/ChangeLog"
        break
    fi
done

exit 0
