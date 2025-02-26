# This file is a part of Geeqie project (https://www.geeqie.org/).
# Copyright (C) 2008 - 2022 The Geeqie Team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

add_project_arguments('-DGETTEXT_PACKAGE="geeqie"', language:'c')

i18n.gettext(meson.project_name(), args : ['--keyword=N_', '--from-code=UTF-8'])

# Create the list of translators for the About - Credits dialog
po_sources = files([
  'ar.po',
  'be.po',
  'bg.po',
  'ca.po',
  'cs.po',
  'da.po',
  'de.po',
  'el.po',
  'en_GB.po',
  'eo.po',
  'es.po',
  'et.po',
  'eu.po',
  'fi.po',
  'fr.po',
  'hu.po',
  'id.po',
  'it.po',
  'ja.po',
  'ko.po',
  'nb.po',
  'nl.po',
  'pl.po',
  'pt_BR.po',
  'ro.po',
  'ru.po',
  'sk.po',
  'sl.po',
  'sr@latin.po',
  'sr.po',
  'sv.po',
  'th.po',
  'tlh.po',
  'tr.po',
  'uk.po',
  'vi.po',
  'zh_CN.po',
  'zh_TW.po'
])

translators_sh = join_paths(scriptsdir, 'translators.sh')

translator_resources = custom_target('translator_resources',
                input : 'locales.txt',
                output : 'translators',
                command : [translators_sh, meson.current_build_dir(), meson.current_source_dir(), '@INPUT@', po_sources])

project_sources += gnome.compile_resources('translators', 'translators.gresource.xml', dependencies : translator_resources)
