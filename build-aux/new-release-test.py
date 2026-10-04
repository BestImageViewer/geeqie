#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Exercise release orchestration with real Git/archives and mocked build/signing tools."""

import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

SCRIPT = Path(sys.argv.pop(1)).resolve() / 'packaging/new-release.sh'
GIT = shutil.which('git')

# Never connect to a release repository or use the developer's signing keys.
TOOL = '''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import subprocess
import sys

name = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['RELEASE_LOG'], 'a') as log:
    log.write(json.dumps([name, args, str(Path.cwd())]) + '\\n')
if name == 'git':
    if args[0] == 'clone':
        args[1] = os.environ['RELEASE_FIXTURE']
    elif args[0] == 'push':
        sys.exit(0)
    elif args[0] == 'verify-tag':
        sys.exit(0)
    elif args[0] == 'tag' and '--sign' in args:
        args.remove('--sign')
    sys.exit(subprocess.call([os.environ['RELEASE_GIT'], *args]))
if name == 'xvfb-run':
    sys.exit(subprocess.call(args[1:]))
if name == 'gpg':
    if os.environ.get('FAIL_SIGN'):
        sys.exit(1)
    if '--output' in args:
        Path(args[args.index('--output') + 1]).write_text('test signature')
if name == 'meson':
    if args[0] == 'setup':
        Path('build').mkdir()
    elif os.environ.get('FAIL_TESTS') and ('archive-test' in str(Path.cwd())) == (os.environ['FAIL_TESTS'] == 'archive'):
        sys.exit(1)
if name == 'ninja' and 'update-translations' in args:
    Path('po/test.po').write_text('Updated translation\\n')
'''


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='geeqie-release-test.')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / 'repo'
        self.repo.mkdir()
        self.env = dict(os.environ, GIT_CONFIG_NOSYSTEM='1',
                        GIT_CONFIG_GLOBAL=os.devnull, GIT_AUTHOR_NAME='Release Test',
                        GIT_AUTHOR_EMAIL='release@example.invalid',
                        GIT_COMMITTER_NAME='Release Test',
                        GIT_COMMITTER_EMAIL='release@example.invalid')
        self.git('init', '-b', 'master')
        files = {
            'NEWS': 'Geeqie 3.2\nOld release\n',
            'data/org.geeqie.Geeqie.metainfo.xml.in':
                '<component>\n  <releases>\n    <release version="3.2" date="2026-09-20" />\n  </releases>\n</component>\n',
            'data/man/geeqie.1': 'Old man page\n',
            'doc/docbook/CommandLineOptions.xml': '<old/>\n',
            'po/test.po': 'Old translation\n',
            'build-aux/generate-man-page.sh':
                '#!/bin/sh\nset -eu\nprintf "New man page\\n" > data/man/geeqie.1\nprintf "<new/>\\n" > doc/docbook/CommandLineOptions.xml\n',
            'build-aux/version.sh': '#!/bin/sh\nsed -n "1s/^Geeqie //p" NEWS\n',
        }
        for filename, text in files.items():
            path = self.repo / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            if filename.endswith('.sh'):
                path.chmod(0o755)
        self.git('add', '.')
        self.git('commit', '-m', 'Initial release')
        self.git('tag', 'v3.2')
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        for name in ('git', 'gpg', 'meson', 'ninja', 'help2man', 'doclifter',
                     'msgfmt', 'msgmerge', 'xgettext', 'itstool', 'xvfb-run', 'Xvfb'):
            path = self.bin / name
            path.write_text(TOOL)
            path.chmod(0o755)
        self.output = self.root / 'output'
        self.output.mkdir()
        self.log = self.root / 'commands.jsonl'
        self.env.update(PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        TMPDIR=str(self.output), RELEASE_FIXTURE=str(self.repo),
                        RELEASE_GIT=GIT, RELEASE_LOG=str(self.log))
        (self.repo / 'NEWS').write_text('Geeqie 3.3\nReviewed release notes\n')

    def git(self, *args, cwd=None):
        return subprocess.check_output([GIT, *args], cwd=cwd or self.repo,
                                       env=self.env, stderr=subprocess.STDOUT, text=True)

    def run_release(self, *args, success=True):
        result = subprocess.run(['sh', str(SCRIPT), *args], cwd=self.repo,
                                env=self.env, capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def commands(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_prepare_and_repeat_without_publishing(self):
        original_metadata = (self.repo / 'data/org.geeqie.Geeqie.metainfo.xml.in').read_bytes()
        for _ in range(2):
            self.run_release()
        archives = list(self.output.glob('*/geeqie-3.3.tar.xz'))
        self.assertEqual(len(archives), 2)
        for archive in archives:
            self.assertTrue(Path(str(archive) + '.asc').exists())
            with tarfile.open(archive) as source:
                names = source.getnames()
                self.assertFalse(any('/.git' in name or '/build/' in name for name in names))
                meta = ET.fromstring(source.extractfile('geeqie-3.3/data/org.geeqie.Geeqie.metainfo.xml.in').read())
                entry = meta.find('releases')[0]
                self.assertEqual(entry.get('version'), '3.3')
                self.assertEqual(entry.get('date'), datetime.date.today().isoformat())
                self.assertEqual(source.extractfile('geeqie-3.3/po/test.po').read(), b'Updated translation\n')
            repo = archive.parent / 'geeqie-3.3'
            self.assertEqual(self.git('show', 'master:po/test.po', cwd=repo), 'Updated translation\n')
        self.assertEqual((self.repo / 'data/org.geeqie.Geeqie.metainfo.xml.in').read_bytes(), original_metadata)
        self.assertFalse(any(name == 'git' and args[0] == 'push' for name, args, _ in self.commands()))
        self.assertEqual(sum(name == 'meson' and args[0] == 'test' for name, args, _ in self.commands()), 4)
        self.assertEqual(sum(name == 'xvfb-run' for name, _, _ in self.commands()), 4)

    def test_patch_and_explicit_version(self):
        self.git('branch', 'stable/3.2')
        (self.repo / 'NEWS').write_text('Geeqie 3.2.1\nPatch notes\n')
        self.run_release('-v', '3.2', '-p', '1')
        self.assertEqual(len(list(self.output.glob('*/geeqie-3.2.1.tar.xz'))), 1)

    def test_start_commit_excludes_later_source_changes(self):
        start = self.git('rev-parse', 'HEAD').strip()
        (self.repo / 'later-source.txt').write_text('Source added after chosen commit\n')
        self.git('add', 'later-source.txt')
        self.git('commit', '-m', 'Add later source change')
        self.run_release('-s', start)
        archive = next(self.output.glob('*/geeqie-3.3.tar.xz'))
        with tarfile.open(archive) as source:
            self.assertNotIn('geeqie-3.3/later-source.txt', source.getnames())
        repo = archive.parent / 'geeqie-3.3'
        self.assertEqual(self.git('rev-parse', 'v3.3^', cwd=repo).strip(), start)
        self.assertEqual(self.git('show', 'master:later-source.txt', cwd=repo),
                         'Source added after chosen commit\n')

    def test_invalid_inputs_fail_before_clone(self):
        for args in (('-v', '9.9'), ('-p', '1'), ('unexpected',)):
            self.run_release(*args, success=False)
        (self.repo / 'NEWS').write_text('Geeqie ../../bad\n')
        self.run_release(success=False)
        self.assertFalse(self.commands())

    def test_existing_tag_is_rejected(self):
        (self.repo / 'NEWS').write_text('Geeqie 3.2\n')
        result = self.run_release(success=False)
        self.assertIn('Tag v3.2 already exists', result.stderr)

    def test_existing_release_metadata_is_preserved(self):
        metadata = self.repo / 'data/org.geeqie.Geeqie.metainfo.xml.in'
        metadata.write_text(metadata.read_text().replace(
            '<releases>', '<releases>\n    <release version="3.3" date="2026-10-01" />'))
        self.run_release()
        archive = next(self.output.glob('*/geeqie-3.3.tar.xz'))
        with tarfile.open(archive) as source:
            meta = ET.fromstring(source.extractfile('geeqie-3.3/data/org.geeqie.Geeqie.metainfo.xml.in').read())
            entries = [entry for entry in meta.find('releases') if entry.get('version') == '3.3']
            self.assertEqual(len(entries), 1)
            self.assertEqual(entries[0].get('date'), '2026-10-01')

    def test_already_prepared_files_can_be_released(self):
        metadata = self.repo / 'data/org.geeqie.Geeqie.metainfo.xml.in'
        metadata.write_text(metadata.read_text().replace(
            '<releases>', '<releases>\n    <release version="3.3" date="2026-10-01" />'))
        (self.repo / 'po/test.po').write_text('Updated translation\n')
        (self.repo / 'data/man/geeqie.1').write_text('New man page\n')
        (self.repo / 'doc/docbook/CommandLineOptions.xml').write_text('<new/>\n')
        self.git('add', '.')
        self.git('commit', '-m', 'Update release files manually')
        self.run_release()
        self.assertEqual(len(list(self.output.glob('*/geeqie-3.3.tar.xz'))), 1)

    def test_patch_does_not_overwrite_newer_master_translations(self):
        self.git('branch', 'stable/3.2')
        (self.repo / 'po/test.po').write_text('Newer master translation\n')
        self.git('add', 'po/test.po')
        self.git('commit', '-m', 'Improve translation on master')
        (self.repo / 'NEWS').write_text('Geeqie 3.2.1\nPatch notes\n')
        result = self.run_release('-r', success=False)
        self.assertIn('Translation merge needs review', result.stderr)
        self.assertFalse(any(name == 'git' and args[0] == 'push' for name, args, _ in self.commands()))

    def test_test_failures_prevent_publication(self):
        for phase in ('checkout', 'archive'):
            self.env['FAIL_TESTS'] = phase
            self.run_release('-r', success=False)
        self.assertFalse(any(name == 'git' and args[0] == 'push' for name, args, _ in self.commands()))

    def test_signing_failure_prevents_clone(self):
        self.env['FAIL_SIGN'] = '1'
        self.run_release(success=False)
        self.assertFalse(any(name == 'git' and args[0] == 'clone' for name, args, _ in self.commands()))

    def test_repository_signing_key_is_used_for_archive(self):
        self.git('config', 'user.signingkey', 'release-test-key')
        self.run_release()
        signatures = [args for name, args, _ in self.commands()
                      if name == 'gpg' and '--detach-sign' in args]
        self.assertEqual(len(signatures), 2)
        self.assertTrue(all(args[:2] == ['--local-user', 'release-test-key'] for args in signatures))

    def test_publish_is_atomic_and_after_verification(self):
        self.run_release('-r')
        commands = self.commands()
        pushes = [(i, args) for i, (name, args, _) in enumerate(commands) if name == 'git' and args[0] == 'push']
        self.assertEqual(len(pushes), 1)
        index, args = pushes[0]
        self.assertIn('--atomic', args)
        self.assertEqual(args[-3:], ['stable/3.3', 'v3.3', 'master'])
        last_test = max(i for i, (name, args, _) in enumerate(commands) if name == 'meson' and args[0] == 'test')
        self.assertGreater(index, last_test)


if __name__ == '__main__':
    unittest.main()
