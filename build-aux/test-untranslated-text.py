#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Regression tests for untranslated-text annotations and diagnostics."""

import pathlib
import subprocess
import tempfile
import unittest


CHECKER = pathlib.Path(__file__).with_name('untranslated-text.sh')


class UntranslatedText(unittest.TestCase):
    def check_source(self, source, filename='sample.cc'):
        with tempfile.TemporaryDirectory(prefix='geeqie-translation-') as directory:
            path = pathlib.Path(directory) / filename
            path.write_text(source, encoding='utf-8')
            return subprocess.run(['sh', str(CHECKER), str(path)],
                                  capture_output=True, text=True, check=False)

    def test_protocol_method_requires_an_exclusion(self):
        result = self.check_source('call("Get");\n')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Get', result.stdout)

    def test_inline_marker_suppresses_protocol_text(self):
        result = self.check_source('call("Get"); // NOTRANSLATE: D-Bus method.\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_marker_does_not_suppress_another_line(self):
        result = self.check_source(
            'call("Get"); // NOTRANSLATE: D-Bus method.\n'
            'label("Untranslated label");\n')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Untranslated label', result.stdout)
        self.assertNotIn('call(', result.stdout)

    def test_translated_ui_text_is_accepted(self):
        result = self.check_source('label(_("Translated label"));\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unrelated_comment_does_not_suppress_a_string(self):
        result = self.check_source('call("Protocol value"); // Legacy annotation.\n')
        self.assertEqual(result.returncode, 1)
        self.assertIn('Protocol value', result.stdout)

    def test_marker_in_a_string_is_not_an_annotation(self):
        result = self.check_source('label("Untranslated label // NOTRANSLATE: not a comment");\n')
        self.assertEqual(result.returncode, 1)

    def test_comments_and_includes_are_not_ui_strings(self):
        result = self.check_source('/* "Example text" */\n// "Example text"\n#include "Example.h"\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_concatenated_translated_strings(self):
        result = self.check_source('label(_("First part"\n "Second part"));\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_plural_messages_are_translated(self):
        result = self.check_source('label(ngettext("One item", "Multiple items", count));\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_escaped_quotes_do_not_hide_the_next_string(self):
        result = self.check_source('call("lowercase \\"quoted\\""); label("Untranslated label");\n')
        self.assertEqual(result.returncode, 1)

    def test_block_marker_preserves_macro_continuations(self):
        result = self.check_source('#define VALUE "Protocol value" /* NOTRANSLATE: Protocol token. */ \\\n "continuation"\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_raw_string_requires_an_actual_comment(self):
        result = self.check_source('call(R"tag(Untranslated text // NOTRANSLATE: inside string)tag");\n')
        self.assertEqual(result.returncode, 1)

    def test_annotation_requires_a_reason(self):
        result = self.check_source('call("Protocol value"); // NOTRANSLATE:\n')
        self.assertEqual(result.returncode, 1)

    def test_formerly_excluded_filename_is_checked(self):
        result = self.check_source('label("Untranslated label");\n', 'convert-configuration.cc')
        self.assertEqual(result.returncode, 1)

    def test_formerly_excluded_function_is_checked(self):
        result = self.check_source('return g_strdup("Untranslated label");\n')
        self.assertEqual(result.returncode, 1)

    def test_compact_comment_marker_is_accepted(self):
        result = self.check_source('call("Get"); //NOTRANSLATE: D-Bus method.\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_block_annotation_requires_a_reason(self):
        result = self.check_source('call("Protocol value"); /* NOTRANSLATE: */\n')
        self.assertEqual(result.returncode, 1)

    def test_translated_argument_does_not_hide_another_argument(self):
        result = self.check_source('call(_("Translated label"), "Untranslated label");\n')
        self.assertEqual(result.returncode, 1)


if __name__ == '__main__':
    unittest.main()
