#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Check preview selection and batch cancellation without camera-specific files."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

PLUGIN = str(Path(sys.argv.pop(1)).resolve())


class ExportJpeg(unittest.TestCase):
    def test_previews_and_batch_cancel(self):
        with tempfile.TemporaryDirectory(prefix='geeqie-export-test-') as directory:
            root = Path(directory)
            tools = root / 'bin'
            tools.mkdir()
            # Control optional tool detection independently of the host system.
            for name in ('sed', 'grep', 'wc', 'cut', 'head', 'tail', 'tr',
                         'mktemp', 'basename', 'rm', 'cat', 'sort'):
                tool = shutil.which(name)
                self.assertIsNotNone(tool)
                (tools / name).symlink_to(tool)

            def script(name, body):
                path = tools / name
                path.write_text('#!/bin/sh\n' + body)
                path.chmod(0o755)

            script('exiv2', 'exit 0\n')
            script('exiftool', '''case "$1" in
-b) printf 'preview bytes'; exit 0 ;;
esac
case "$*" in
*'-MIMEType -'*) cat >/dev/null; printf '4x3\\n%s\\n' "$TEST_MIME" ;;
*) printf 'Orientation: 6\\n'
   [ "$TEST_EMPTY" = 1 ] || printf 'PreviewImage: (Binary data 13 bytes, use -b option to extract)\\n' ;;
esac
''')
            script('zenity', '''case "$*" in
*'--list'*)
    cat > "$TEST_ROWS"
    printf 'chooser\\n' >> "$TEST_LOG"
    [ "$TEST_CANCEL" = 1 ] && exit 1
    printf '1:PreviewImage:%s\\n' "$TEST_MIME" ;;
*'jpgicc is not installed'*) printf 'warning\\n' >> "$TEST_LOG" ;;
*) printf 'info\\n' >> "$TEST_LOG" ;;
esac
''')
            script('mogrify', '''[ "$1" = -rotate ] && [ "$2" = 90 ] || exit 1
printf 'rotate\\n' >> "$TEST_LOG"
''')
            script('geeqie', '''case "$1" in
--view=*) printf 'view\\n' >> "$TEST_LOG" ;;
*) printf 'Perceptual\\n' ;;
esac
''')
            log = root / 'log'
            rows = root / 'rows'
            environment = dict(os.environ, PATH=str(tools), TMPDIR=str(root),
                               TEST_LOG=str(log), TEST_ROWS=str(rows),
                               TEST_MIME='image/tiff', TEST_EMPTY='0', TEST_CANCEL='0')

            def run(**settings):
                log.write_text('')
                return subprocess.run(['/bin/sh', PLUGIN, 'first.raw', 'second.raw'],
                                      env=dict(environment, **settings),
                                      capture_output=True, text=True, check=False)

            self.assertEqual(run().returncode, 0)
            self.assertEqual(log.read_text().splitlines(),
                             ['chooser', 'warning', 'rotate', 'view',
                              'chooser', 'rotate', 'view'])
            self.assertEqual(rows.read_text().splitlines(),
                             ['TRUE', 'Preview 1: image/tiff,\t 4x3 pixels,\t 13 bytes',
                              '1:PreviewImage:image/tiff', ''])
            self.assertEqual(run(TEST_CANCEL='1').returncode, 0)
            self.assertEqual(log.read_text().splitlines(), ['chooser'])
            self.assertEqual(run(TEST_EMPTY='1').returncode, 0)
            self.assertEqual(log.read_text().splitlines(), ['info', 'info'])
            self.assertEqual(run(TEST_MIME='application/octet-stream').returncode, 0)
            self.assertEqual(log.read_text().splitlines(), ['info', 'info'])


if __name__ == '__main__':
    unittest.main()
