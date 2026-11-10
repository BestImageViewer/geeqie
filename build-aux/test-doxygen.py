#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Test Doxygen build caching and failure recovery without generating a full manual.

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


class DoxygenBuild(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='geeqie-doxygen-test-')
        self.addCleanup(self.temporary.cleanup)
        base = Path(self.temporary.name)
        self.root = base / 'checkout'
        self.output = base / 'output with spaces'
        self.bin = base / 'bin'
        self.log = base / 'calls'
        for path in (self.root / 'src', self.root / 'doc/images', self.root / 'tools', self.bin):
            path.mkdir(parents=True)
        for name in ('doxygen.sh', 'doxygen.py'):
            shutil.copyfile(SOURCE / 'tools' / name, self.root / 'tools' / name)
        (self.root / 'doc/doxygen.conf').write_text('FILE_PATTERNS = *.cc *.md *.dox\nINLINE_SOURCES = $(INLINE_SOURCES)\n')
        (self.root / 'doc/image-style.css').write_text('/* Test stylesheet */\n')
        (self.root / '.gitignore').write_text('build*/\n')
        (self.root / 'src/sample.cc').write_text('/** Original comment. */\n')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        fake = self.bin / 'doxygen'
        fake.write_text('''#!/usr/bin/env python3
import os
from pathlib import Path
import re
import sys
if sys.argv[1] == '--version':
    print(os.environ.get('FAKE_DOXYGEN_VERSION', 'test-1'))
    sys.exit(0)
config = Path(sys.argv[1]).read_text()
index = 'SERVER_BASED_SEARCH = YES' in config
checking = 'WARN_AS_ERROR = FAIL_ON_WARNINGS' in config
warning_file = re.search(r'^WARN_LOGFILE = "(.*)"$', config, re.M).group(1)
Path(warning_file).write_text('malformed test comment\\n' if os.environ.get('FAKE_DOXYGEN_WARN') else '')
with open(os.environ['FAKE_DOXYGEN_LOG'], 'a') as stream:
    stream.write('check\\n' if checking else 'index\\n' if index else 'html\\n')
if checking:
    sys.exit(1 if os.environ.get('FAKE_DOXYGEN_WARN') else 0)
if os.environ.get('FAKE_DOXYGEN_FAIL') == ('index' if index else 'html'):
    sys.exit(1)
output = Path(os.environ['DOCDIR'])
(output / 'html').mkdir(exist_ok=True)
(output / 'html/index.html').write_text('index' if index else 'browser')
(output / 'html/symbol.html').write_text('symbol')
(output / 'Geeqie.tag').write_text('tag')
if index:
    (output / 'searchdata.xml').write_text('search index')
''')
        fake.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        FAKE_DOXYGEN_LOG=str(self.log))

    def run_build(self, *arguments, success=True):
        result = subprocess.run(['sh', 'tools/doxygen.sh', *arguments, str(self.output)],
                                cwd=self.root, env=self.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        warning_log = self.output / 'doxygen-check-warnings.log'
        warnings = warning_log.read_text() if warning_log.exists() else ''
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr + warnings)
        return result

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_unchanged_force_and_deleted_output(self):
        self.run_build()
        self.assertEqual(self.calls(), ['index', 'html'])
        self.assertEqual((self.output / 'searchdata.xml').read_text(), 'search index')
        self.assertEqual((self.output / 'html/index.html').read_text(), 'browser')
        self.assertIn('up to date', self.run_build().stdout)
        self.assertEqual(len(self.calls()), 2)
        self.run_build('--force')
        self.assertEqual(len(self.calls()), 4)
        (self.output / 'html/symbol.html').unlink()
        self.run_build()
        self.assertEqual(len(self.calls()), 6)

    def test_changed_added_deleted_and_ignored_inputs(self):
        self.run_build()
        sample = self.root / 'src/sample.cc'
        old = sample.stat()
        sample.write_text('/** Modified comment. */\n')
        os.utime(sample, ns=(old.st_atime_ns, old.st_mtime_ns))
        self.run_build()
        self.assertEqual(len(self.calls()), 4)
        added = self.root / 'doc/new.dox'
        added.write_text('/** @page new New page */\n')
        self.run_build()
        self.assertEqual(len(self.calls()), 6)
        added.unlink()
        self.run_build()
        self.assertEqual(len(self.calls()), 8)
        (self.root / 'build-coverage').mkdir()
        (self.root / 'build-coverage/generated.cc').write_text('generated')
        self.run_build()
        self.assertEqual(len(self.calls()), 8)

    def test_modes_settings_and_tool_version(self):
        self.run_build('--html-only')
        self.assertEqual(self.calls(), ['html'])
        self.assertFalse((self.output / 'searchdata.xml').exists())
        self.run_build('--html-only')
        self.assertEqual(len(self.calls()), 1)
        self.run_build()
        self.assertEqual(len(self.calls()), 3)
        self.env['INLINE_SOURCES'] = 'NO'
        self.run_build()
        self.assertEqual(len(self.calls()), 5)
        self.env['FAKE_DOXYGEN_VERSION'] = 'test-2'
        self.run_build()
        self.assertEqual(len(self.calls()), 7)

    def test_failure_invalidates_marker_and_can_be_retried(self):
        self.run_build()
        self.env['FAKE_DOXYGEN_FAIL'] = 'html'
        self.run_build('--force', success=False)
        self.assertFalse((self.output / '.geeqie-doxygen-state.json').exists())
        self.assertFalse((self.output / '.geeqie-doxygen-lock').exists())
        self.assertFalse(list(self.output.glob('.doxygen-*')))
        del self.env['FAKE_DOXYGEN_FAIL']
        self.run_build()
        self.assertEqual(self.calls()[-2:], ['index', 'html'])
        self.run_build()
        self.assertEqual(len(self.calls()), 6)

    def test_warning_logs_and_check_do_not_replace_manual(self):
        self.run_build()
        for name in ('doxygen-index-warnings.log', 'doxygen-warnings.log'):
            self.assertTrue((self.output / name).is_file())
        marker = (self.output / '.geeqie-doxygen-state.json').read_bytes()
        html = (self.output / 'html/index.html').stat().st_mtime_ns
        self.run_build('--check')
        self.run_build('--check')
        self.assertEqual(self.calls()[-2:], ['check', 'check'])
        self.env['FAKE_DOXYGEN_WARN'] = '1'
        self.run_build('--check', success=False)
        self.assertIn('malformed test comment', (self.output / 'doxygen-check-warnings.log').read_text())
        self.assertEqual((self.output / '.geeqie-doxygen-state.json').read_bytes(), marker)
        self.assertEqual((self.output / 'html/index.html').stat().st_mtime_ns, html)
        self.assertFalse((self.output / '.geeqie-doxygen-lock').exists())

    @unittest.skipUnless(shutil.which('doxygen'), 'Requires Doxygen')
    def test_real_check_accepts_undocumented_code_and_rejects_bad_comments(self):
        real = shutil.which('doxygen')
        fake = self.bin / 'doxygen'
        fake.unlink()
        fake.symlink_to(real)
        source = self.root / 'src/sample.cc'
        source.write_text('void sample(int actual) {}\n')
        self.run_build('--check')
        for comment in ('/** @not_a_doxygen_command Invalid. */',
                        '/** @param missing Not a parameter. */',
                        '/** @ref no_such_documented_symbol */'):
            with self.subTest(comment=comment):
                source.write_text(comment + '\nvoid sample(int actual) {}\n')
                self.run_build('--check', success=False)
                self.assertTrue((self.output / 'doxygen-check-warnings.log').read_text().strip())


    def test_output_inside_checkout_does_not_invalidate_itself(self):
        self.output = self.root / 'documentation output'
        self.run_build()
        self.run_build()
        self.assertEqual(len(self.calls()), 2)


if __name__ == '__main__':
    unittest.main()
