#!/bin/sh

mkdir html
#xsltproc --xinclude -o index.html /usr/share/xml/docbook/stylesheet/nwalsh/current/html/docbook.xsl docbook/GuideIndex.xml

# this requires gnome-doc-utils package
xsltproc --xinclude -o html/GuideIndex.html /usr/share/xml/gnome/xslt/docbook/html/db2html.xsl docbook/GuideIndex.xml
