# SPDX-License-Identifier: GPL-2.0-or-later

install_data('geeqie-convert-image.sh', install_dir : gq_bindir, install_mode : 'rwxr-xr-x')

i18n.merge_file(
    input : 'org.geeqie.convert-image.desktop.in',
    output : 'org.geeqie.convert-image.desktop',
    type : 'desktop',
    po_dir : podir,
    install : true,
    install_dir : desktopdir)

test('Convert image plugin', find_program('python3'),
    args : [join_paths(meson.project_source_root(), 'build-aux', 'test-convert-image.py'),
            join_paths(meson.current_source_dir(), 'geeqie-convert-image.sh')],
    suite : 'functional')
