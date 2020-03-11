#!/bin/sh

# Script to update ChangeLog file,
# it keeps "pre-svn" history and inserts git log at top,
# it uses C locale for date format.
# It has to be run where ChangeLog.gqview is.
#
# ChangeLog.html is also created

[ ! -e "ChangeLog.gqview" ] && exit 1
[ ! -x "$(command -v git)" ] && exit 0

LC_ALL=C git log --no-merges --no-notes --encoding=UTF-8 --no-follow --use-mailmap 1b58572cf58e9d2d4a0305108395dab5c66d3a09..HEAD > ChangeLog.$$.new && \
cat ChangeLog.gqview >> ChangeLog.$$.new && \
mv -f ChangeLog.$$.new ChangeLog


echo "<textarea rows='6614' cols='100'>" >ChangeLog.$$.old.html && \
tail -6613 ChangeLog >> ChangeLog.$$.old.html && \
echo "</textarea>" >>ChangeLog.$$.old.html && \
echo "<html>" > ChangeLog.$$.new.html && \
echo "<body>" >> ChangeLog.$$.new.html && \
echo "<ul>" >> ChangeLog.$$.new.html && \
LC_ALL=C git log --no-merges --no-notes --encoding=UTF-8 --date=format:'%Y-%m-%d' --no-follow --use-mailmap --pretty=format:"<li><a href=\"http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git;a=commit;h=%H\">view commit </a></li><p>Author: %aN<br>Date: %ad<br><textarea rows=4 cols=100>%s %n%n%b</textarea><br><br></p>" 1b58572cf58e9d2d4a0305108395dab5c66d3a09..HEAD >> ChangeLog.$$.new.html && \
echo "" >> ChangeLog.$$.new.html && \
cat ChangeLog.$$.old.html >> ChangeLog.$$.new.html && \
echo "</ul>" >> ChangeLog.$$.new.html && \
echo "</body>" >> ChangeLog.$$.new.html && \
echo "</html>" >> ChangeLog.$$.new.html
[ -e ChangeLog.html ] && mv -f ChangeLog.html ChangeLog.html.bak
mv -f ChangeLog.$$.new.html ChangeLog.html

rm -f ChangeLog.$$.old.html

exit 0
