#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Find likely untranslated C++ strings, honoring local NOTRANSLATE comments.

This remains a heuristic: only strings starting with an uppercase letter
followed by lowercase text or whitespace are candidates. Comments, character
literals and include directives are syntax, rather than exclusion-list entries.
"""

import pathlib
import re
import sys


TOKENS = re.compile(
    r'(?P<line>//[^\n]*)|(?P<block>/\*.*?\*/)|'
    r'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})'
    r'\(.*?\)(?P=delimiter)")|'
    r'(?P<string>(?:u8|u|U|L)?"(?:\\.|[^"\\])*")|'
    r"(?P<char>(?:u8|u|U|L)?'(?:\\.|[^'\\])*')",
    re.DOTALL)
TRANSLATED = re.compile(r'(?<!\w)(?:_|N_|ngettext)\s*\(\s*$')
MARKER = re.compile(r'\bNOTRANSLATE:\s*\S')
CANDIDATE = re.compile(r'[A-Z](?:[a-z]|\s).+', re.DOTALL)


def untranslated_lines(source):
    """Return one-based line numbers requiring translation or an annotation."""
    tokens = list(TOKENS.finditer(source))
    code = list(source)
    suppressed = set()
    for token in tokens:
        if token.lastgroup not in ('line', 'block'):
            continue
        first_line = source.count('\n', 0, token.start()) + 1
        for offset, line in enumerate(token.group().splitlines()):
            if token.lastgroup == 'block' and line.endswith('*/'):
                line = line[:-2]
            if MARKER.search(line):
                suppressed.add(first_line + offset)
        for index in range(token.start(), token.end()):
            if code[index] != '\n':
                code[index] = ' '
    code = ''.join(code)
    previous_end = 0
    previous_translated = False
    plural_argument = False
    findings = set()
    for token in tokens:
        if token.lastgroup not in ('string', 'raw'):
            continue
        prefix = code[previous_end:token.start()]
        translated = (bool(TRANSLATED.search(prefix)) or
                      (previous_translated and not prefix.strip()) or
                      (plural_argument and prefix.strip() == ','))
        plural_argument = bool(re.search(r'\bngettext\s*\(\s*$', prefix)) or (plural_argument and not prefix.strip())
        previous_end = token.end()
        previous_translated = translated
        if translated:
            continue
        first_line = source.count('\n', 0, token.start()) + 1
        last_line = source.count('\n', 0, token.end()) + 1
        if any(line in suppressed for line in range(first_line, last_line + 1)):
            continue
        line_start = source.rfind('\n', 0, token.start()) + 1
        if re.match(r'\s*#\s*include\b', source[line_start:token.start()]):
            continue
        text = token.group()
        if token.lastgroup == 'raw':
            content = text[text.index('(') + 1:text.rindex(')')]
        else:
            content = text[text.index('"') + 1:-1]
        if CANDIDATE.match(content):
            findings.add(first_line)
    return sorted(findings)


def main():
    path = pathlib.Path(sys.argv[1])
    try:
        source = path.read_text(encoding='utf-8')
    except OSError as error:
        print(error, file=sys.stderr)
        return 2
    findings = untranslated_lines(source)
    if findings:
        print(f'\nfile: {path}')
        lines = source.splitlines()
        for number in findings:
            print(f'line: {number}\t{lines[number - 1]}')
    return int(bool(findings))


if __name__ == '__main__':
    sys.exit(main())
