## Process this file with automake to produce Makefile.in.

helpdir = @htmldir@

EXTRA_DIST = docbook2html.sh docbook

html/GuideIndex.html: docbook/GuideIndex.xml
	rm -rf html; mkdir html; cp $(srcdir)/icons/* html/
	if [ -x "$(GNOME_DOC_TOOL)" ]; then \
		"$(GNOME_DOC_TOOL)" html -o html $(srcdir)/docbook/GuideIndex.xml ; \
	else \
		echo "gnome-doc-tool not found, html is not built" ; \
	fi

html: html/GuideIndex.html

install-data-hook: html
	if [ -x "$(GNOME_DOC_TOOL)" ]; then \
		$(MKDIR_P) "$(DESTDIR)$(helpdir)" || exit 1; \
		cd $(srcdir)/html; for f in * ; do $(INSTALL_DATA) "$$f" "$(DESTDIR)$(helpdir)/$$f" ; done; \
		ln -s -f GuideIndex.html "$(DESTDIR)$(helpdir)/index.html" ; \
	fi
	
uninstall-hook:
	rm "$(DESTDIR)$(helpdir)/"*

clean-local:
	rm -rf html

