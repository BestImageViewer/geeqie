#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Exercise the symlink plugin with controlled remote query responses."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class SymlinkPlugin(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="geeqie-symlink-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source folder"
        self.source.mkdir()
        self.first = self.source / " image with spaces.jpg "
        self.second = self.source / " image with spaces.jpg.xmp "
        self.first.write_text("image")
        self.second.write_text("metadata")
        self.destination = self.source / ".."
        self.binary = self.root / "bin"
        self.binary.mkdir()
        remote = self.binary / "geeqie"
        remote.write_text("#!" + sys.executable + "\n" + r'''
import json, os, sys
from pathlib import Path
settings=json.loads(Path(os.environ['GEEQIE_TEST_REMOTE']).read_text())
argument=sys.argv[1]
if argument.startswith('--get-sidecars='):
    sys.stdout.write(settings.get('group_output', '\n'.join(settings['files'])+'\n'))
    sys.exit(settings.get('group_status', 0))
path=argument[len('--get-destination='):]
print(settings['destinations'].get(path, ''), end='')
sys.exit(settings.get('destination_status', 0))
''')
        remote.chmod(0o700)
        self.settings = {"files": [str(self.first), str(self.second)],
                         "destinations": {str(path): str(self.destination / path.name)
                                          for path in (self.first, self.second)}}

    def run_plugin(self):
        settings_file = self.root / "remote.json"
        settings_file.write_text(json.dumps(self.settings))
        environment = dict(os.environ, PATH=str(self.binary) + os.pathsep + os.environ["PATH"],
                           GEEQIE_TEST_REMOTE=str(settings_file), TMPDIR=str(self.root))
        result = subprocess.run(["sh", PLUGIN, str(self.first)], env=environment,
                                capture_output=True, text=True)
        self.assertEqual(list(self.root.glob("geeqie-symlink.*")), [])
        return result

    def test_parent_destination_and_grouped_filenames_with_spaces(self):
        result = self.run_plugin()
        self.assertEqual(result.returncode, 0, result.stderr)
        for path in (self.first, self.second):
            link = self.destination / path.name
            self.assertTrue(link.is_symlink())
            self.assertEqual(os.readlink(link), str(path))

    def test_empty_group_query_is_reported(self):
        self.settings["group_output"] = ""
        result = self.run_plugin()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No grouped files", result.stderr)

    def test_failed_group_query_does_not_create_links(self):
        self.settings["group_status"] = 1
        result = self.run_plugin()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot read grouped files", result.stderr)
        self.assertFalse((self.destination / self.first.name).exists())

    def test_missing_or_failed_destination_is_reported(self):
        for failed in (False, True):
            with self.subTest(failed=failed):
                self.settings["destinations"] = {}
                self.settings["destination_status"] = int(failed)
                result = self.run_plugin()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("destination", result.stderr)

    def test_existing_file_directory_and_dangling_link_are_preserved(self):
        target = self.destination / self.first.name
        for kind in ("file", "directory", "symlink"):
            with self.subTest(kind=kind):
                if kind == "file":
                    target.write_text("existing")
                elif kind == "directory":
                    target.mkdir()
                else:
                    target.symlink_to("missing-target")
                result = self.run_plugin()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("already exists", result.stderr)
                if kind == "file":
                    self.assertEqual(target.read_text(), "existing")
                elif kind == "directory":
                    self.assertEqual(list(target.iterdir()), [])
                else:
                    self.assertEqual(os.readlink(target), "missing-target")
                target.rmdir() if kind == "directory" else target.unlink()

    def test_link_failure_is_not_masked_by_later_group_member(self):
        self.settings["destinations"][str(self.first)] = str(self.root / "absent" / "image.jpg")
        result = self.run_plugin()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot create symlink", result.stderr)
        self.assertFalse((self.destination / self.second.name).exists())

    def test_missing_later_destination_reports_partial_failure(self):
        del self.settings["destinations"][str(self.second)]
        result = self.run_plugin()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No symlink destination", result.stderr)
        self.assertTrue((self.destination / self.first.name).is_symlink())
        self.assertFalse((self.destination / self.second.name).exists())


if __name__ == "__main__":
    PLUGIN = str(Path(sys.argv.pop(1)).resolve())
    unittest.main()
