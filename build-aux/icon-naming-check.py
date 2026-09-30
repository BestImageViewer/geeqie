#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Check centralized theme icon names and literal arguments to icon APIs."""

import pathlib
import re
import sys
import xml.etree.ElementTree as ET


# Zero-based position of the icon-name argument. Extend for new wrappers.
ICON_ARGUMENTS = {
    'gtk_image_new_from_icon_name': 0,
    'gtk_button_new_from_icon_name': 0,
    'g_themed_icon_new': 0,
    'g_themed_icon_new_with_default_fallbacks': 0,
    'gtk_image_set_from_icon_name': 1,
    'gtk_button_set_icon_name': 1,
    'gtk_menu_button_set_icon_name': 1,
    'gtk_entry_set_icon_from_icon_name': 2,
    'gtk_icon_theme_has_icon': 1,
    'gtk_icon_theme_lookup_icon': 1,
    'icon_theme_load_pixbuf_copy': 1,
    'pref_button_new': 1,
    'pref_toolbar_button': 1,
}

# The fallback-aware wrappers use the same argument positions as GTK.
ICON_WRAPPERS = {name: name.replace('gtk_', 'ui_', 1)
                 for name in ICON_ARGUMENTS
                 if name.startswith('gtk_') and 'icon_theme' not in name}
ICON_WRAPPERS.update({'g_themed_icon_new': 'ui_icon_new',
                      'g_themed_icon_new_with_default_fallbacks': 'ui_icon_new'})
ICON_ARGUMENTS.update({wrapper: ICON_ARGUMENTS[name]
                       for name, wrapper in ICON_WRAPPERS.items()})
ICON_ARGUMENTS['ui_icon_new'] = 0

TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    r'|[A-Za-z_]\w*|[^\s]', re.DOTALL)


def check(root):
    source = root / 'src'
    definitions = source / 'main-defines.h'
    constants = dict(re.findall(
        r'^#define\s+(GQ_ICON_\w+)\s+"([^"]+)"',
        definitions.read_text(encoding="utf-8"), re.MULTILINE))
    names = {name: constant for constant, name in constants.items()}
    errors = []
    used_names = set()
    for path in sorted(source.rglob('*')):
        if (path.suffix not in ('.c', '.cc', '.h', '.inc')
                or path == definitions
                or {'third-party', 'tests'} & set(path.relative_to(source).parts)):
            continue
        text = path.read_text(encoding="utf-8")
        tokens = [(match.group(), match.start()) for match in TOKEN.finditer(text)
                  if not match.group().startswith(('//', '/*'))]
        bad = set()
        for index, (token, offset) in enumerate(tokens):
            if token in constants:
                used_names.add(constants[token])
            # Hyphenated names are unambiguous enough to check in tables,
            # assignments and return statements, as well as API calls.
            if token.startswith('"') and token[1:-1] in names and '-' in token:
                bad.add(index)
            if token not in ICON_ARGUMENTS or index + 1 == len(tokens):
                continue
            if tokens[index + 1][0] != '(':
                continue
            if token in ICON_WRAPPERS and path != source / 'ui-misc.cc':
                line = text.count('\n', 0, offset) + 1
                errors.append(f'{path.relative_to(root)}:{line}: use '
                              f'{ICON_WRAPPERS[token]} to preserve bundled fallbacks')
            depth = 0
            argument = 0
            for pos in range(index + 2, len(tokens)):
                value = tokens[pos][0]
                if value == ')' and depth == 0:
                    break
                if value == ',' and depth == 0:
                    argument += 1
                elif value in ('(', '[', '{'):
                    depth += 1
                elif value in (')', ']', '}'):
                    depth -= 1
                elif argument == ICON_ARGUMENTS[token] and value.startswith('"'):
                    bad.add(pos)
        for index in sorted(bad):
            token, offset = tokens[index]
            line = text.count('\n', 0, offset) + 1
            replacement = names.get(token[1:-1], 'a GQ_ICON_* constant')
            errors.append(f'{path.relative_to(root)}:{line}: use {replacement} '
                          f'instead of {token}; define theme icons in src/main-defines.h')
    manifest = root / 'data/icons/icons.gresource.xml'
    resources = {entry.get('alias', entry.text): entry.text
                 for entry in ET.parse(manifest).iter('file')}
    for name in sorted(used_names):
        suffix = '' if name.endswith('-symbolic') or name == 'application-pdf' else '-symbolic'
        alias = f'scalable/actions/gq-fallback-{name}{suffix}.svg'
        if alias not in resources:
            errors.append(f'{manifest.relative_to(root)}: missing bundled fallback for {name}')
        elif not (manifest.parent / resources[alias]).is_file():
            errors.append(f'{manifest.relative_to(root)}: missing file {resources[alias]}')
    for error in errors:
        print(error)
    return bool(errors)


if __name__ == '__main__':
    root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent
    sys.exit(check(root))
