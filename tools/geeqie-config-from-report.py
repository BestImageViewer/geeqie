#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Reproduce diagnostic settings in a private temporary Geeqie configuration.
## @details Run from a source checkout with Python 3. Only settings present in
## the supplied diagnostic report are written; omitted settings use Geeqie's
## defaults when the generated file is loaded. The result's path is copied to
## the desktop clipboard, never PRIMARY.

"""Create a Geeqie configuration solely from a diagnostic settings report.

Usage:
  tools/geeqie-config-from-report.py report.txt

Requires a Geeqie checkout for the diagnostic schema and serialization types.
Only reported, serializable settings are written. Geeqie supplies defaults for
omitted settings when loading the generated configuration.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

LIMIT = 8 * 1024 * 1024
WINDOW = re.compile(r'(?:Saved window|Window) ([1-9][0-9]*)')
VIEW_ALIASES = {'file_view': 'file_view_type', 'directory_view': 'dir_view_type'}
VIEW_VALUES = {'file_view_type': {'list': '0', 'icons': '1'},
               'dir_view_type': {'list': '0', 'tree': '1'}}


def read_limited(path):
    """Read an existing regular file without unbounded memory use."""
    if not path.is_file():
        raise ValueError('File does not exist or is not a regular file: ' + str(path))
    with path.open('rb') as stream:
        data = stream.read(LIMIT + 1)
    if len(data) > LIMIT:
        raise ValueError('File exceeds the 8 MiB limit: ' + str(path))
    return data


def read_report(path):
    text = read_limited(path).decode('utf-8-sig')
    lines = text.splitlines()
    standalone = bool(lines and lines[0] == 'Geeqie standalone diagnostic report, format 1')
    native = (len(lines) >= 2 and lines[0].startswith('Geeqie ') and
              lines[1] == 'Diagnostic settings report, format 1')
    if not (standalone or native):
        raise ValueError('Not a supported Geeqie diagnostic settings report (format 1)')
    preferences, windows = {}, {}
    target = None
    for line in lines:
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1]
            if section in ('Preferences', 'Saved settings'):
                target = preferences
            elif WINDOW.fullmatch(section):
                number = int(WINDOW.fullmatch(section).group(1))
                if number in windows:
                    raise ValueError('Duplicate window section: ' + section)
                if len(windows) >= 128:
                    raise ValueError('Report contains more than 128 windows')
                target = windows.setdefault(number, {})
            else:
                target = None
        elif target is not None and '=' in line:
            name, value = (part.strip() for part in line.split('=', 1))
            if name in ('source', 'status', 'saved_layouts', 'active_window'):
                continue
            name = VIEW_ALIASES.get(name, name) if target is not preferences else name
            if name in target:
                raise ValueError('Duplicate diagnostic setting: ' + name)
            target[name] = value
    return preferences, [windows[index] for index in sorted(windows)], standalone


def load_schema():
    """Reuse the drift checker's type/alias information, not a second allowlist."""
    root = Path(__file__).resolve().parents[1]
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('diagnostic_schema', root / 'build-aux/check-diagnostic-schema.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    fields = module.configuration_fields(root)
    manifest = json.loads((root / 'data/diagnostic-report-schema.json').read_text(encoding='utf-8'))
    schema = {'global': {}, 'layout': {}}
    for identity, binding in manifest['reported'].items():
        field = fields.get(identity)
        if not field or field['binding'] != binding:
            raise ValueError('Diagnostic schema has changed; run the schema consistency check')
        if field['kind'] not in ('BOOL', 'INT', 'UINT', 'INT_UNIT'):
            raise ValueError('Unsupported diagnostic setting type: ' + binding)
        scope, name = binding.split('.', 1)
        schema[scope][name] = (field['name'], field['kind'])
    precision = re.search(r'^#define SLIDESHOW_SUBSECOND_PRECISION ([0-9]+)$',
                          (root / 'src/slideshow.h').read_text(), re.M)
    if not precision or int(precision.group(1)) <= 0:
        raise ValueError('Cannot determine slideshow delay units from this checkout')
    return schema, int(precision.group(1))


def encode_value(name, value, kind, standalone, precision):
    if name in VIEW_VALUES:
        value = VIEW_VALUES[name].get(value, value)
    if kind == 'BOOL':
        values = {'true': 'true', 'false': 'false', '1': 'true', '0': 'false'}
        if value.lower() not in values:
            raise ValueError('Invalid boolean diagnostic setting: ' + name)
        return values[value.lower()]
    if not re.fullmatch(r'-?[0-9]{1,12}', value):
        raise ValueError('Invalid numeric diagnostic setting: ' + name)
    number = int(value)
    minimum, maximum = (0, 2**32 - 1) if kind == 'UINT' else (-2**31, 2**31 - 1)
    if not minimum <= number <= maximum:
        raise ValueError('Diagnostic setting is out of range: ' + name)
    if kind == 'INT_UNIT':
        if name != 'slideshow.delay' or number < 0:
            raise ValueError('Unsupported diagnostic unit conversion: ' + name)
        # Native reports contain internal ticks. Standalone reports copy the
        # saved XML value (whole seconds, if present), not internal ticks.
        return str(number) if standalone else str(number // precision) + '.' + str(number % precision)
    return str(number)


def prepare_settings(values, schema, standalone, precision, scope, skipped):
    result = {}
    for name, value in values.items():
        if name not in schema or (name in VIEW_VALUES and value == 'unknown'):
            skipped.append(scope + '.' + name)
            continue
        attribute, kind = schema[name]
        result[attribute] = encode_value(name, value, kind, standalone, precision)
    return result


def create_config(preferences, windows):
    """Construct a new document containing only validated report settings."""
    root = ET.Element('gq')
    ET.SubElement(root, 'global', preferences)
    for index, settings in enumerate(windows, 1):
        layout = ET.SubElement(root, 'layout', {'id': 'lw-report-' + str(index)})
        layout.attrib.update(settings)
    return root


def copy_path(path):
    """Use the Ctrl+V clipboard; clipboard helpers retain ownership after exit."""
    commands = []
    if os.environ.get('WAYLAND_DISPLAY') or os.environ.get('XDG_SESSION_TYPE') == 'wayland':
        commands.append(['wl-copy', '--type', 'text/plain;charset=utf-8'])
    if os.environ.get('DISPLAY'):
        commands += [['xclip', '-selection', 'clipboard', '-in'], ['xsel', '--clipboard', '--input']]
    if sys.platform == 'darwin':
        commands.append(['pbcopy'])
    for command in commands:
        executable = shutil.which(command[0])
        if not executable:
            continue
        try:
            result = subprocess.run([executable] + command[1:], input=str(path).encode('utf-8'),
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            if result.returncode == 0:
                return command[0]
        except (OSError, subprocess.TimeoutExpired):
            continue
    raise ValueError('Could not copy the path: use a desktop session with wl-copy (Wayland), '
                     'xclip/xsel (X11), or pbcopy (macOS). The generated file has been retained.')


def formatted_xml(root):
    """Indent elements and put each attribute on its own line, preserving values."""
    if hasattr(ET, 'indent'):
        ET.indent(root)
    text = ET.tostring(root, encoding='utf-8', xml_declaration=True).decode('utf-8')

    def wrap_tag(match):
        tag = match.group()
        if tag.startswith(('</', '<!', '<?')):
            return tag
        # ElementTree has already escaped quotes and angle brackets in values.
        # Match complete attributes, not spaces inside their quoted values.
        attributes = list(re.finditer(r' [^\s=]+="[^"]*"', tag))
        if not attributes:
            return tag
        line_start = text.rfind('\n', 0, match.start()) + 1
        indent = re.match(r'[ \t]*', text[line_start:match.start()]).group()
        lines = [tag[:attributes[0].start()]]
        lines += [indent + '  ' + attribute.group().lstrip() for attribute in attributes]
        lines.append(indent + tag[attributes[-1].end():].lstrip())
        return '\n'.join(lines)

    # Consume comments as whole tokens so examples of XML inside comments stay intact.
    return (re.sub(r'<!--.*?-->|<[^>]*>', wrap_tag, text, flags=re.S) + '\n').encode('utf-8')


def launch_config(destination, executable):
    """Start a separate interactive instance without reading the developer's profile."""
    executable = shutil.which(os.path.expanduser(executable))
    if not executable:
        raise ValueError('Geeqie executable not found; select one with --geeqie')
    executable = str(Path(executable).resolve())
    dbus = shutil.which('dbus-run-session')
    if not dbus:
        raise ValueError('--launch requires dbus-run-session')
    profile = destination.parent / 'profile'
    profile.mkdir(mode=0o700)
    environment = os.environ.copy()
    # Keep the desktop runtime directory: Wayland sockets live there. Only the
    # child's persistent configuration, cache, data, and state are redirected.
    authority = Path.home() / '.Xauthority'
    if 'XAUTHORITY' not in environment and authority.is_file():
        environment['XAUTHORITY'] = str(authority)
    environment['HOME'] = str(profile)
    for variable, directory in [('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache'),
                                ('XDG_DATA_HOME', 'data'), ('XDG_STATE_HOME', 'state')]:
        path = profile / directory
        path.mkdir(mode=0o700)
        environment[variable] = str(path)
    config = profile / 'config' / 'geeqie'
    config.mkdir(mode=0o700)
    shutil.copyfile(destination, config / 'geeqierc.xml')
    (config / 'geeqierc.xml').chmod(0o600)
    # Avoid the first-run shortcut migration prompt in this disposable profile.
    (config / 'accels.ini').touch(mode=0o600)
    environment['GQ_NEW_INSTANCE'] = 'y'
    environment.pop('GQ_CACHE_MAINTENANCE', None)
    log = destination.parent / 'geeqie-launch.log'
    fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        process = subprocess.Popen([dbus, '--', executable], env=environment, cwd=profile,
                                   stdin=subprocess.DEVNULL, stdout=stream, stderr=stream,
                                   start_new_session=True)
    print('Launched separate Geeqie session (PID ' + str(process.pid) + ').')
    print('Test profile: ' + str(profile))
    print('Launch log: ' + str(log))
    return process


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('report', type=Path, help='existing Geeqie diagnostic settings report')
    parser.add_argument('-l', '--launch', action='store_true', help='launch Geeqie in a private temporary profile')
    parser.add_argument('-g', '--geeqie', metavar='EXECUTABLE', help='executable for --launch (default: installed geeqie)')
    args = parser.parse_args()
    if args.geeqie and not args.launch:
        parser.error('--geeqie requires --launch')
    workspace = None
    try:
        preferences, windows, standalone = read_report(args.report.expanduser())
        schema, precision = load_schema()
        skipped = []
        preferences = prepare_settings(preferences, schema['global'], standalone, precision, 'global', skipped)
        windows = [prepare_settings(window, schema['layout'], standalone, precision, 'window ' + str(index), skipped)
                   for index, window in enumerate(windows, 1)]
        count = len(preferences) + sum(len(window) for window in windows)
        if not count:
            raise ValueError('Report contains no supported configuration settings')
        root = create_config(preferences, windows)
        workspace = Path(tempfile.mkdtemp(prefix='geeqie-config-from-report-', dir='/tmp'))
        destination = workspace / 'geeqierc.xml'
        fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(formatted_xml(root))
        print('Generated configuration: ' + str(destination), flush=True)
        print('Wrote ' + str(count) + ' reported settings. Omitted settings use Geeqie defaults when loaded.')
        if skipped:
            print('Not representable or unknown to this checkout: ' + ', '.join(skipped), file=sys.stderr)
        clipboard_status = 0
        try:
            print('Path copied to the standard clipboard using ' + copy_path(destination) + '.')
        except ValueError as error:
            print('Warning: ' + str(error), file=sys.stderr)
            clipboard_status = 1
        if args.launch:
            launch_config(destination, args.geeqie or 'geeqie')
        return clipboard_status
    except (OSError, ValueError, UnicodeError, ET.ParseError, ImportError) as error:
        print('Error: ' + str(error), file=sys.stderr)
        if workspace:
            print('Temporary files: ' + str(workspace), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
