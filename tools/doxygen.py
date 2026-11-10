#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Generate and cache Geeqie's Doxygen documentation.

"""Skip unchanged documentation builds; optionally omit the editor search index."""

import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


STATE_FILE = '.geeqie-doxygen-state.json'


def quoted(path):
    """Quote a path for Doxygen's configuration syntax."""
    return '"' + str(path).replace('\\', '\\\\').replace('"', '\\"') + '"'


def input_files(root, output):
    """Use version-controlled and nonignored files, excluding the output tree."""
    result = subprocess.run(['git', 'ls-files', '--cached', '--others',
                             '--exclude-standard', '-z'], cwd=root,
                            check=True, stdout=subprocess.PIPE)
    files = []
    for name in sorted(set(os.fsdecode(result.stdout).split('\0')) - {''}):
        path = root / name
        if path.is_file() and output not in path.resolve().parents and path.resolve() != output:
            files.append(path)
    return files


def fingerprint(root, output, environment, html_only):
    """Include contents, additions/deletions, settings, and generator dependencies."""
    files = input_files(root, output)
    digest = hashlib.sha256()
    digest.update(str(root).encode())
    digest.update(str(html_only).encode())
    config = (root / 'doc/doxygen.conf').read_text()
    for name in sorted(set(re.findall(r'\$\((\w+)\)', config))):
        digest.update(json.dumps([name, environment.get(name, '')]).encode())
    for path in files:
        digest.update(os.fsencode(path.relative_to(root)))
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    for name in ('doxygen', 'dot', 'java', 'mscgen', 'dia', 'latex', 'pdflatex',
                 'epstopdf', 'doxygen-bash.sed'):
        executable = shutil.which(name)
        digest.update(json.dumps([name, executable]).encode())
        if executable:
            stat = Path(executable).stat()
            digest.update(str((stat.st_size, stat.st_mtime_ns)).encode())
    version = subprocess.check_output(['doxygen', '--version'], env=environment)
    digest.update(version)
    plantuml = Path(environment['PLANTUML_JAR_PATH'])
    if plantuml.is_file():
        digest.update(hashlib.sha256(plantuml.read_bytes()).digest())
    return digest.hexdigest(), files


def current(output, signature, html_only):
    """A successful signature is usable only while all recorded outputs exist."""
    try:
        state = json.loads((output / STATE_FILE).read_text())
        required = ['html/index.html', 'Geeqie.tag']
        if not html_only:
            required.append('searchdata.xml')
        return (state['signature'] == signature and
                all((output / name).is_file() for name in required + state['outputs']))
    except (OSError, ValueError, KeyError, TypeError):
        return False


def run_doxygen(config, environment, warnings):
    """Keep each pass's diagnostics, including those from a failed check."""
    warnings.write_text('')
    with config.open('a') as stream:
        stream.write('WARN_LOGFILE = ' + quoted(warnings) + '\n')
    try:
        subprocess.run(['doxygen', str(config)], env=environment, check=True)
    finally:
        print('Doxygen warning log: ' + str(warnings), flush=True)


def generate(root, output, files, environment, html_only, check=False):
    """Retain two-pass generation for editor indexing and local browser search."""
    config = root / 'doc/doxygen.conf'
    lines = config.read_text().splitlines()
    patterns = []
    collecting = False
    for line in lines:
        if line.startswith('FILE_PATTERNS'):
            collecting = True
            line = line.split('=', 1)[1]
        if collecting:
            patterns.extend(line.replace('\\', '').split())
            if not line.rstrip().endswith('\\'):
                break
    inputs = [path for path in files if any(fnmatch.fnmatch(path.name, pattern) for pattern in patterns)]
    if check and not shutil.which('doxygen-bash.sed'):
        inputs = [path for path in inputs if path.suffix not in ('.sh', '.pl', '.awk')]
        print('Script comment checks skipped: install doxygen-bash.sed to include shell/Perl/AWK documentation.')
    with tempfile.TemporaryDirectory(prefix='.doxygen-', dir=output) as temporary:
        work = Path(temporary)
        common = ('@INCLUDE = ' + quoted(config) + '\nINPUT = ' +
                  ' \\\n'.join(quoted(path) for path in inputs) + '\n' +
                  'IMAGE_PATH = ' + quoted(root / 'doc/images') + '\n' +
                  'HTML_EXTRA_STYLESHEET = ' + quoted(root / 'doc/image-style.css') + '\n')
        formats = ('LATEX', 'RTF', 'MAN', 'XML', 'DOCBOOK', 'AUTOGEN_DEF',
                   'SQLITE3', 'PERLMOD', 'HTMLHELP', 'CHI', 'DOCSET', 'QHP', 'ECLIPSEHELP')
        if check:
            check_config = work / 'check.conf'
            check_config.write_text(common +
                                    'OUTPUT_DIRECTORY = ' + quoted(work / 'check-output') + '\n' +
                                    'GENERATE_HTML = NO\nGENERATE_TAGFILE =\nHAVE_DOT = NO\n'
                                    'CALL_GRAPH = NO\nCALLER_GRAPH = NO\nSOURCE_BROWSER = NO\n'
                                    'EXTRACT_ALL = YES\nWARNINGS = YES\nWARN_IF_DOC_ERROR = YES\n'
                                    'WARN_IF_UNDOCUMENTED = NO\nWARN_NO_PARAMDOC = NO\n'
                                    'WARN_IF_UNDOC_ENUM_VAL = NO\nWARN_IF_INCOMPLETE_DOC = NO\n'
                                    'WARN_AS_ERROR = FAIL_ON_WARNINGS\n' +
                                    ''.join('GENERATE_' + name + ' = NO\n' for name in formats) +
                                    'GENERATE_XML = YES\nXML_PROGRAMLISTING = NO\n')
            run_doxygen(check_config, environment, output / 'doxygen-check-warnings.log')
            return
        if not html_only:
            index_config = work / 'index.conf'
            index_config.write_text(common + 'SERVER_BASED_SEARCH = YES\nEXTERNAL_SEARCH = YES\n'
                                    'HAVE_DOT = NO\nCALL_GRAPH = NO\nCALLER_GRAPH = NO\n'
                                    'SOURCE_BROWSER = NO\nINLINE_SOURCES = NO\nGENERATE_TAGFILE =\n' +
                                    ''.join('GENERATE_' + name + ' = NO\n' for name in formats))
            run_doxygen(index_config, environment, output / 'doxygen-index-warnings.log')
            shutil.move(output / 'searchdata.xml', work / 'searchdata.xml')
        html_config = work / 'html.conf'
        html_config.write_text(common + 'SERVER_BASED_SEARCH = NO\nEXTERNAL_SEARCH = NO\n')
        run_doxygen(html_config, environment, output / 'doxygen-warnings.log')
        if not html_only:
            shutil.move(work / 'searchdata.xml', output / 'searchdata.xml')


def main():
    parser = argparse.ArgumentParser(prog="tools/doxygen.sh", description=__doc__)
    parser.add_argument('--force', action='store_true', help='rebuild even when inputs are unchanged')
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--html-only', action='store_true', help='skip generating/updating searchdata.xml for doxygen-help.sh')
    modes.add_argument('--check', action='store_true', help='check malformed documentation; do not require comments on every API')
    parser.add_argument('destination', nargs='?', default='../doxygen', help='output directory (default: ../doxygen)')
    args = parser.parse_args()
    root = Path.cwd()
    if not (root / '.git').exists() or not (root / 'src').is_dir():
        parser.error('run this command from the Geeqie checkout root')
    output = Path(args.destination).resolve()
    if output == root or output in root.parents:
        parser.error('the output directory must not contain the source checkout')
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update(DOCDIR=str(output), SRCDIR=str(root), PROJECT='Geeqie', GENERATE_HTML='YES')
    tags = subprocess.check_output(['git', 'tag', '--list', 'v[1-9]*'], text=True).splitlines()
    environment['VERSION'] = tags[-1] if tags else ''
    environment.setdefault('PLANTUML_JAR_PATH', str(Path.home() / 'bin/plantuml.jar'))
    environment.setdefault('INLINE_SOURCES', 'YES')
    environment.setdefault('STRIP_CODE_COMMENTS', 'NO')
    lock = output / '.geeqie-doxygen-lock'
    try:
        lock.mkdir()
    except FileExistsError:
        parser.error('another documentation build may be running: ' + str(lock))
    try:
        if args.check:
            generate(root, output, input_files(root, output), environment, False, check=True)
            print('Doxygen documentation check passed.')
            return
        signature, files = fingerprint(root, output, environment, args.html_only)
        if not args.force and current(output, signature, args.html_only):
            print('Doxygen documentation is up to date: ' + str(output))
            return
        (output / STATE_FILE).unlink(missing_ok=True)
        generate(root, output, files, environment, args.html_only)
        required = [output / 'html/index.html', output / 'Geeqie.tag']
        if not args.html_only:
            required.append(output / 'searchdata.xml')
        if not all(path.is_file() for path in required):
            raise RuntimeError('Doxygen did not produce all required output files')
        after, _ = fingerprint(root, output, environment, args.html_only)
        if signature != after:
            print('Inputs changed during generation; run again to refresh documentation.')
            return
        generated = [str(path.relative_to(output)) for path in output.rglob('*') if path.is_file()]
        (output / STATE_FILE).write_text(json.dumps({'signature': signature, 'outputs': generated}) + '\n')
    finally:
        lock.rmdir()


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print('Doxygen generation failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
