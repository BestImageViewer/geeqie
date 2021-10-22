#!/bin/sh

if [ "${PWD##*/}" != "web" ] ; then
  echo $0 must be called from directory \"web\"
  exit 1
fi

# later we can include user manual
# ln -sf ../doc

chmod -R a+rX .
rsync --archive \
   --verbose \
   --partial \
   --progress \
   --copy-links \
   --keep-dirlinks \
   --delete \
   --exclude "*.bak" \
   --exclude .xvpics \
   --exclude .thumbnails \
   --exclude .wml \
   --exclude "*.wml" \
   --exclude "*~" \
   --exclude .gitignore \
   --exclude Makefile \
   --exclude .wmlrc \
   --exclude .wmkrc \
   --exclude sync \
   --exclude "*.sh" \
   --exclude ".*.swp" \
   ./ tschil:/srv/www/geeqie.org/
