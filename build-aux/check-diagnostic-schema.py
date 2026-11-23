#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check serialized configuration fields against the diagnostic allowlist.

Reads source and an explicit classification manifest, never user configuration
or values. Includes nested configuration writers and optional build features.
"""

import argparse
import ast
import json
from pathlib import Path
import re
import sys

TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
FUNCTION = re.compile(r'^[\w:*<> &~]+\s+(\w+(?:::\w+)*)\s*\([^;{}]*\)(?:\s*const)?\s*\n\{', re.M)
ATTRIBUTE = re.compile(r'\bWRITE_(BOOL|INT|UINT|INT_UNIT|CHAR|COLOR)(_FULL)?\s*\(')
DIRECT = re.compile(r'\brc\.write_(?:bool|int|uint|int_unit|char|color)_option\s*\(')
KNOWN_MACROS = {'WRITE_' + kind + suffix for kind in ('BOOL', 'INT', 'UINT', 'INT_UNIT', 'CHAR', 'COLOR')
                for suffix in ('', '_FULL')} | {'WRITE_NL', 'WRITE_SEPARATOR', 'WRITE_STRING', 'WRITE_FORMAT_STRING'}


def masked(source, comments_only=False):
    def replace(match):
        token = match.group()
        if comments_only and not token.startswith(('//', '/*')):
            return token
        return ''.join('\n' if char == '\n' else ' ' for char in token)
    return TOKENS.sub(replace, source)


def closing(source, begin, left='(', right=')'):
    depth = 1
    for index in range(begin + 1, len(source)):
        if source[index] == left:
            depth += 1
        elif source[index] == right:
            depth -= 1
            if depth == 0:
                return index
    raise ValueError('Unbalanced source delimiters')


def arguments(source, mask, begin, end):
    result = []
    start = begin + 1
    depth = 0
    for index in range(start, end):
        if mask[index] in '([{':
            depth += 1
        elif mask[index] in ')]}':
            depth -= 1
        elif mask[index] == ',' and depth == 0:
            result.append(source[start:index].strip())
            start = index + 1
    result.append(source[start:end].strip())
    return result


def literal(value):
    if not re.fullmatch(r'"(?:\\.|[^"\\])*"', value):
        raise ValueError('Configuration attribute names must be string literals: ' + value)
    return ast.literal_eval(value)


def configuration_fields(root):
    fields = {}
    for path in sorted((root / 'src').rglob('*.cc')):
        original = path.read_text(encoding='utf-8')
        source = masked(original, comments_only=True)
        mask = masked(original)
        for macro in re.findall(r'\b(WRITE_[A-Z_]+)\s*\(', mask):
            if macro not in KNOWN_MACROS:
                raise ValueError('Unsupported configuration writer macro: ' + macro)
        for match in re.finditer(r'\bWRITE_(?:STRING|FORMAT_STRING)\s*\(', source):
            begin = match.end() - 1
            args = arguments(source, mask, begin, closing(mask, begin))
            # Attribute literals would bypass the normal schema macros.
            text = ''.join(ast.literal_eval(token.group()) for token in
                           re.finditer(r'"(?:\\.|[^"\\])*"', args[0]))
            if re.search(r'\b[\w.:-]+\s*=\s*["\']', text):
                raise ValueError('Literal configuration attribute bypasses WRITE_* option macros in ' + str(path.relative_to(root)))
        for kind in re.findall(r'\brc\.write_(\w+)_option\s*\(', mask):
            if kind not in ('bool', 'int', 'uint', 'int_unit', 'char', 'color'):
                raise ValueError('Unsupported configuration writer method: write_' + kind + '_option')
        functions = []
        for function in FUNCTION.finditer(mask):
            begin = function.end() - 1
            functions.append((begin, closing(mask, begin, '{', '}'), function.group(1)))
        for match in list(ATTRIBUTE.finditer(source)) + list(DIRECT.finditer(source)):
            begin = match.end() - 1
            end = closing(mask, begin)
            args = arguments(source, mask, begin, end)
            owners = [item for item in functions if item[0] < begin < item[1]]
            if not owners:
                raise ValueError('Unrecognized writer function in ' + str(path.relative_to(root)))
            owner = min(owners, key=lambda item: item[1] - item[0])[2]
            direct = match.re is DIRECT
            full = direct or bool(match.group(2))
            name = literal(args[0]) if full else args[1]
            identity = str(path.relative_to(root)) + '::' + owner + '::' + name
            binding = None
            if path.name == 'rcfile.cc' and owner == 'write_global_attributes':
                if full:
                    value = re.fullmatch(r'options->([\w.]+)', args[1])
                    binding = 'global.' + value.group(1) if value else None
                else:
                    binding = 'global.' + args[1]
            elif path.name == 'layout.cc' and owner == 'layout_write_attributes':
                if full:
                    value = re.fullmatch(r'lop\.([\w.]+)', args[1])
                    binding = 'layout.' + value.group(1) if value else None
                else:
                    binding = 'layout.' + args[1]
            kind = 'CHAR' if direct else match.group(1)
            item = {'binding': binding, 'kind': kind, 'name': name}
            if identity in fields and fields[identity] != item:
                raise ValueError('Conflicting serialization for ' + identity)
            fields[identity] = item
    return fields


def report_bindings(root):
    source = masked((root / 'src/diagnostic-report.cc').read_text(), comments_only=True)
    bindings = set()
    for field in re.findall(r'\blayout->options\.([\w.]+)', source):
        bindings.add('layout.' + field)
    for field in re.findall(r'(?<![\w])options->([\w.]+)', source):
        bindings.add('global.' + field)
    return bindings


def standalone_bindings(root):
    tree = ast.parse((root / 'tools/geeqie-diagnostic-report.py').read_text())
    assignments = {}
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in ('PREFERENCES', 'LAYOUT_FIELDS', 'PREFERENCE_ALIASES'):
                    assignments[target.id] = ast.literal_eval(node.value)
    return assignments


def check(root, manifest):
    if manifest.get('format') != 1:
        raise ValueError('Unsupported diagnostic schema manifest format')
    fields = configuration_fields(root)
    reported = manifest['reported']
    excluded = manifest['excluded']
    reasons = manifest['exclusion_reasons']
    errors = []
    classified = set(reported) | set(excluded)
    for name in sorted(set(fields) - classified):
        errors.append('Unclassified configuration field: ' + name)
    for name in sorted(classified - set(fields)):
        errors.append('Removed configuration field still classified: ' + name)
    for name in sorted(set(reported) & set(excluded)):
        errors.append('Field is both reported and excluded: ' + name)
    for name, reason in excluded.items():
        if reason not in reasons or not reasons[reason].strip():
            errors.append('Missing exclusion reason: ' + name)
    for name, binding in reported.items():
        if name in fields and fields[name]['binding'] != binding:
            errors.append('Reported field serialization/binding changed: ' + name)
    declared = set(reported.values())
    actual = report_bindings(root)
    runtime_only = manifest.get('runtime_only', {})
    for binding, reason in runtime_only.items():
        if not isinstance(reason, str) or not reason.strip():
            errors.append('Missing runtime-only classification reason: ' + binding)
        if binding in {item['binding'] for item in fields.values()}:
            errors.append('Runtime-only field is actually serialized: ' + binding)
    for binding in sorted(set(runtime_only) & declared):
        errors.append('Field is both runtime-only and serialized: ' + binding)
    for binding in sorted(set(runtime_only) - actual):
        errors.append('Stale runtime-only diagnostic binding: ' + binding)
    actual -= set(runtime_only)
    for binding in sorted(actual - declared):
        errors.append('Diagnostic configuration field lacks a reported schema entry: ' + binding)
    for binding in sorted(declared - actual):
        errors.append('Classified field is no longer present in diagnostics: ' + binding)
    standalone = standalone_bindings(root)
    aliases = standalone.get('PREFERENCE_ALIASES', {})
    for scope, label in (('global', 'PREFERENCES'), ('layout', 'LAYOUT_FIELDS')):
        expected = {binding.split('.', 1)[1] for binding in declared if binding.startswith(scope + '.')}
        # The standalone script converts the two view enums separately.
        if scope == 'layout':
            expected -= {'file_view_type', 'dir_view_type'}
        actual_fields = set(standalone.get(label, ()))
        for field in sorted(expected - actual_fields):
            errors.append('Standalone diagnostic field missing: ' + scope + '.' + field)
        for field in sorted(actual_fields - expected):
            errors.append('Standalone diagnostic field is stale/unclassified: ' + scope + '.' + field)
        for name, binding in reported.items():
            if binding.startswith(scope + '.') and name in fields:
                field = binding.split('.', 1)[1]
                if field in actual_fields and aliases.get(field, field) != fields[name]['name']:
                    errors.append('Standalone configuration attribute name differs: ' + binding + ' (use ' + fields[name]['name'] + ')')
    for alias in sorted(set(aliases) - set(standalone.get('PREFERENCES', ()))):
        errors.append('Stale standalone attribute alias: ' + alias)
    return errors, len(fields)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--list-excluded', action='store_true', help='List deliberately excluded field names and reasons')
    args = parser.parse_args()
    try:
        manifest = json.loads((args.root / 'data/diagnostic-report-schema.json').read_text())
        errors, count = check(args.root, manifest)
    except (OSError, ValueError, KeyError, TypeError, SyntaxError) as error:
        print('Diagnostic schema check failed: ' + str(error), file=sys.stderr)
        return 1
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        print('Update data/diagnostic-report-schema.json and the report allowlists; classify new fields explicitly.', file=sys.stderr)
        return 1
    if args.list_excluded:
        for name, reason in sorted(manifest['excluded'].items()):
            print(name + ': ' + manifest['exclusion_reasons'][reason])
    print('Diagnostic schema consistent: %d configuration fields; %d reported, %d explicitly excluded.' %
          (count, len(manifest['reported']), len(manifest['excluded'])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
