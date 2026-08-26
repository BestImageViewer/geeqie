# SPDX-License-Identifier: GPL-2.0-or-later

add_project_arguments('-DGETTEXT_PACKAGE="geeqie"', language:'c')

generate_potfiles_sh = find_program(
  join_paths(meson.project_source_root(), 'tools', 'generate-potfiles.sh'),
  required : true)

run_command(generate_potfiles_sh, check : true)

i18n.gettext(meson.project_name(), args : ['--keyword=N_', '--from-code=UTF-8'])

update_translation_sh = find_program(
  join_paths(meson.project_source_root(), 'tools', 'update-translation.sh'),
  required : true)

run_target(
  'update-translations',
  command : [update_translation_sh, '--all'])

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

translators_sh = join_paths(buildauxdir, 'translators.sh')

translator_resources = custom_target('translator_resources',
                input : 'locales.txt',
                output : 'translators',
                command : [
                  translators_sh,
                  meson.current_build_dir(),
                  meson.current_source_dir(),
                  '@INPUT@',
                ] + po_sources)

project_sources += gnome.compile_resources('translators', 'translators.gresource.xml', dependencies : translator_resources)
