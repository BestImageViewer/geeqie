#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Exercise image conversion with real ImageMagick and scripted dialog answers."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


PLUGIN = sys.argv.pop(1)
CONVERTER = shutil.which('magick') or shutil.which('convert')


@unittest.skipUnless(CONVERTER, 'Requires ImageMagick')
class ConvertImage(unittest.TestCase):
    def test_conversion_and_conflicts(self):
        with tempfile.TemporaryDirectory(prefix='geeqie-convert-test-') as directory:
            root = Path(directory)
            tools = root / 'bin'
            tools.mkdir()
            destination = root / 'output'
            destination.mkdir()
            zenity = tools / 'zenity'
            zenity.write_text('''#!/bin/sh
case "$1" in
--list) printf '%s\\n' "$CONVERT_TEST_FORMAT" ;;
--file-selection) printf '%s\\n' "$CONVERT_TEST_DESTINATION" ;;
--scale) [ "$CONVERT_TEST_FORMAT" = JPEG ] || exit 1; printf '90\\n' ;;
*) exit 1 ;;
esac
''')
            zenity.chmod(0o755)
            environment = dict(os.environ,
                               PATH=str(tools) + os.pathsep + os.environ['PATH'],
                               CONVERT_TEST_FORMAT='JPEG',
                               CONVERT_TEST_DESTINATION=str(destination))

            def run(*sources):
                return subprocess.run(['sh', PLUGIN, *map(str, sources)],
                                      env=environment, capture_output=True, text=True,
                                      check=False)

            source = root / 'odd [name] ;$.png'
            subprocess.run([CONVERTER, '-size', '3x2', 'xc:none', str(source)],
                           check=True)
            original = source.read_bytes()
            self.assertEqual(run(source).returncode, 0)
            output = destination / 'odd [name] ;$.jpg'
            saved = output.read_bytes()
            pixels = subprocess.check_output(
                [CONVERTER, str(output), '-colorspace', 'sRGB', '-format',
                 '%wx%h %[fx:r] %[fx:g] %[fx:b]', 'info:'], text=True)
            self.assertEqual(pixels, '3x2 1 1 1')
            self.assertEqual(run(source).returncode, 1)
            self.assertEqual(output.read_bytes(), saved)
            self.assertEqual(source.read_bytes(), original)

            broken = root / 'broken.png'
            broken.write_text('Invalid image')
            second = root / 'second.png'
            shutil.copyfile(source, second)
            self.assertEqual(run(broken, second).returncode, 1)
            self.assertFalse((destination / 'broken.jpg').exists())
            self.assertTrue((destination / 'second.jpg').is_file())

            blocked = destination / 'blocked.jpg'
            blocked.mkdir()
            third = root / 'blocked.png'
            shutil.copyfile(source, third)
            self.assertEqual(run(third).returncode, 1)
            self.assertEqual(list(blocked.iterdir()), [])
            blocked.rmdir()
            blocked.symlink_to(root / 'nonexistent')
            self.assertEqual(run(third).returncode, 1)
            self.assertTrue(blocked.is_symlink())
            self.assertFalse((root / 'nonexistent').exists())

            environment['CONVERT_TEST_FORMAT'] = 'PNG'
            self.assertEqual(run(source).returncode, 0)
            png = destination / 'odd [name] ;$.png'
            pixels = subprocess.check_output(
                [CONVERTER, str(png), '-format', '%m %wx%h %[fx:a]', 'info:'],
                text=True)
            self.assertEqual(pixels, 'PNG 3x2 0')
            saved_png = png.read_bytes()
            self.assertEqual(run(source).returncode, 1)
            self.assertEqual(png.read_bytes(), saved_png)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(list(destination.glob('.geeqie-convert.*')), [])


if __name__ == '__main__':
    unittest.main()
