# SPDX-License-Identifier: GPL-2.0-or-later

install_data('geeqie-export-jpeg', install_dir : gq_bindir)

i18n.merge_file(
    input : 'org.geeqie.export-jpeg.desktop.in',
    output : 'org.geeqie.export-jpeg.desktop',
    type : 'desktop',
    po_dir : podir,
    install : true,
    install_dir : desktopdir)


test('Export jpeg plugin', find_program('python3'),
    args : [join_paths(meson.project_source_root(), 'build-aux', 'test-export-jpeg.py'),
            join_paths(meson.current_source_dir(), 'geeqie-export-jpeg')],
    suite : 'functional')
