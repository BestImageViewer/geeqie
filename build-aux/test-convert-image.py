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
--entry) [ "$CONVERT_TEST_CANCEL" = size ] && exit 1; printf '%s\\n' "${CONVERT_TEST_SIZE:-0}" ;;
--list) case "$*" in
*'Conversion source'*) [ "$CONVERT_TEST_CANCEL" = source ] && exit 1; printf '%s\\n' "${CONVERT_TEST_SOURCE:-Selected}"; exit 0 ;;
esac
printf '%s\\n' "$CONVERT_TEST_FORMAT" ;;
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

            geeqie = tools / 'geeqie'
            geeqie.write_text('#!/bin/sh\ncat "$CONVERT_TEST_GROUP"\n')
            geeqie.chmod(0o755)
            for stage in ('source', 'size'):
                environment['CONVERT_TEST_CANCEL'] = stage
                self.assertEqual(run(root / 'unused.png').returncode, 0)
                self.assertEqual(list(destination.iterdir()), [])
            environment.pop('CONVERT_TEST_CANCEL')

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

            resized = root / 'large.png'
            subprocess.run([CONVERTER, '-size', '12x8', 'xc:red', str(resized)],
                           check=True)
            environment['CONVERT_TEST_SIZE'] = '6'
            self.assertEqual(run(resized).returncode, 0)
            dimensions = subprocess.check_output(
                [CONVERTER, str(destination / 'large.png'), '-format',
                 '%wx%h', 'info:'], text=True)
            self.assertEqual(dimensions, '6x4')

            jpeg = root / 'grouped.jpg'
            raw = root / 'grouped.nef'
            shutil.copyfile(source, jpeg)
            shutil.copyfile(resized, raw)
            group = root / 'group'
            group.write_text(str(raw) + '\n' + str(jpeg) + '\n')
            environment.update(CONVERT_TEST_SOURCE='JPEG',
                               CONVERT_TEST_GROUP=str(group))
            self.assertEqual(run(raw, jpeg).returncode, 0)
            dimensions = subprocess.check_output(
                [CONVERTER, str(destination / 'grouped.png'), '-format',
                 '%wx%h', 'info:'], text=True)
            self.assertEqual(dimensions, '3x2')
            (destination / 'grouped.png').unlink()
            environment['CONVERT_TEST_SOURCE'] = 'RAW'
            self.assertEqual(run(jpeg).returncode, 0)
            dimensions = subprocess.check_output(
                [CONVERTER, str(destination / 'grouped.png'), '-format',
                 '%wx%h', 'info:'], text=True)
            self.assertEqual(dimensions, '6x4')
            group.write_text(str(jpeg) + '\n')
            self.assertEqual(run(jpeg).returncode, 1)
            group.write_text(str(raw) + '\n' + str(raw) + '\n')
            self.assertEqual(run(jpeg).returncode, 1)



if __name__ == '__main__':
    unittest.main()
