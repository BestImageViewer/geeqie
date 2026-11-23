#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for schema drift detection, using synthetic source only."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location('diagnostic_schema', Path(__file__).with_name('check-diagnostic-schema.py'))
SCHEMA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCHEMA)


class DiagnosticSchema(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='geeqie-schema-test-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / 'src').mkdir()
        (self.root / 'tools').mkdir()
        self.writer = self.root / 'src/rcfile.cc'
        self.writer.write_text('''static void write_global_attributes(const ConfOptions *options, RcString &rc)
{
    WRITE_BOOL(*options, enabled);
    WRITE_CHAR(*options, private_path);
    WRITE_INT_FULL("thumbnails.max_width", options->thumbnails.size.width);
}
''')
        self.report = self.root / 'src/diagnostic-report.cc'
        self.report.write_text('''void report()
{
    out << options->enabled;
    out << options->thumbnails.size.width;
}
''')
        self.standalone = self.root / 'tools/geeqie-diagnostic-report.py'
        self.standalone.write_text('''PREFERENCES = ('enabled', 'thumbnails.size.width')
LAYOUT_FIELDS = ()
PREFERENCE_ALIASES = {'thumbnails.size.width': 'thumbnails.max_width'}
''')
        self.manifest = {
            'format': 1,
            'reported': {
                'src/rcfile.cc::write_global_attributes::enabled': 'global.enabled',
                'src/rcfile.cc::write_global_attributes::thumbnails.max_width': 'global.thumbnails.size.width',
            },
            'excluded': {'src/rcfile.cc::write_global_attributes::private_path': 'privacy'},
            'exclusion_reasons': {'privacy': 'May contain personal paths.'},
            'runtime_only': {},
        }

    def errors(self):
        return SCHEMA.check(self.root, self.manifest)[0]

    def test_matching_schema_and_alias(self):
        self.assertEqual(self.errors(), [])

    def test_new_field_requires_classification(self):
        self.writer.write_text(self.writer.read_text().replace('    WRITE_BOOL', '    WRITE_BOOL(*options, new_option);\n    WRITE_BOOL', 1))
        self.assertTrue(any('Unclassified configuration field:' in error and 'new_option' in error for error in self.errors()))

    def test_removed_reported_field_is_detected(self):
        self.writer.write_text(self.writer.read_text().replace('    WRITE_BOOL(*options, enabled);\n', ''))
        self.assertTrue(any('Removed configuration field' in error and 'enabled' in error for error in self.errors()))

    def test_removed_excluded_field_is_detected(self):
        self.writer.write_text(self.writer.read_text().replace('    WRITE_CHAR(*options, private_path);\n', ''))
        self.assertTrue(any('Removed configuration field' in error and 'private_path' in error for error in self.errors()))

    def test_missing_report_output_is_detected(self):
        self.report.write_text(self.report.read_text().replace('    out << options->enabled;\n', ''))
        self.assertTrue(any('no longer present in diagnostics' in error for error in self.errors()))

    def test_unclassified_report_output_is_detected(self):
        self.report.write_text(self.report.read_text().replace('    out <<', '    out << options->unexpected;\n    out <<', 1))
        self.assertTrue(any('lacks a reported schema entry' in error for error in self.errors()))

    def test_exclusion_requires_reason(self):
        self.manifest['exclusion_reasons']['privacy'] = ''
        self.assertTrue(any('Missing exclusion reason' in error for error in self.errors()))

    def test_standalone_alias_drift_is_detected(self):
        self.standalone.write_text(self.standalone.read_text().replace("'thumbnails.max_width'", "'thumbnails.width'"))
        self.assertTrue(any('attribute name differs' in error for error in self.errors()))

    def test_commented_fields_are_ignored(self):
        self.writer.write_text(self.writer.read_text().replace('    WRITE_BOOL', '    /* WRITE_BOOL(*options, ignored); */\n    WRITE_BOOL', 1))
        self.assertEqual(self.errors(), [])

    def test_nested_writer_new_field_is_detected(self):
        (self.root / 'src/bar.cc').write_text('''void pane_write_config(RcString &rc)
{
    WRITE_BOOL_FULL("new_pane_option", pane->enabled);
}
''')
        self.assertTrue(any('Unclassified configuration field: src/bar.cc::pane_write_config::new_pane_option' in error for error in self.errors()))

    def test_unknown_macro_fails_closed(self):
        self.writer.write_text(self.writer.read_text().replace('WRITE_BOOL', 'WRITE_NEW_TYPE', 1))
        with self.assertRaisesRegex(ValueError, 'Unsupported configuration writer macro'):
            self.errors()

    def test_literal_attribute_cannot_bypass_classification(self):
        self.writer.write_text(self.writer.read_text().replace('    WRITE_BOOL', r'    WRITE_STRING("new_key=\"true\"");' + '\n    WRITE_BOOL', 1))
        with self.assertRaisesRegex(ValueError, 'Literal configuration attribute bypasses'):
            self.errors()

    def test_serialized_fields_cannot_be_hidden_as_runtime_only(self):
        self.manifest['runtime_only']['global.enabled'] = 'Runtime state.'
        self.assertTrue(any('actually serialized' in error for error in self.errors()))


if __name__ == '__main__':
    unittest.main()
