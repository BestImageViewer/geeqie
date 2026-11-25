#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Test report-only Geeqie configuration generation and clipboard selection.

import contextlib
import importlib.util
import io
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    'config_from_report', Path(__file__).resolve().parents[1] / 'tools/geeqie-config-from-report.py')
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)

NATIVE = 'Geeqie 3.4\nDiagnostic settings report, format 1\n'
SAVED = 'Geeqie standalone diagnostic report, format 1\n'


class ConfigFromReport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema, cls.precision = TOOL.load_schema()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='geeqie-config-report-test-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.report = self.directory / 'report.txt'

    def parsed(self, text):
        self.report.write_text(text)
        preferences, windows, standalone = TOOL.read_report(self.report)
        skipped = []
        preferences = TOOL.prepare_settings(preferences, self.schema['global'], standalone,
                                            self.precision, 'global', skipped)
        windows = [TOOL.prepare_settings(values, self.schema['layout'], standalone,
                                        self.precision, 'window', skipped) for values in windows]
        return preferences, windows, skipped

    def test_live_report_aliases_units_and_booleans(self):
        prefs, windows, skipped = self.parsed(NATIVE + '''[Preferences]
image.enable_read_ahead=0
thumbnails.size.width=192
slideshow.delay=55
[Window 1]
file_view=icons
directory_view=tree
show_marks=1
fullscreen=1
[Recent action requests]
0.021 win.main-win-quit
''')
        root = TOOL.create_config(prefs, windows)
        self.assertEqual(root.tag, 'gq')
        self.assertEqual(root.find('global').attrib,
                         {'image.enable_read_ahead': 'false', 'thumbnails.max_width': '192', 'slideshow.delay': '5.5'})
        self.assertEqual(root.find('layout').attrib,
                         {'id': 'lw-report-1', 'file_view_type': '1', 'dir_view_type': '1', 'show_marks': 'true'})
        self.assertEqual(skipped, ['window.fullscreen'])

    def test_standalone_report_preserves_saved_units(self):
        prefs, windows, _ = self.parsed(SAVED + '''[Saved settings]
source=saved configuration; unsaved runtime changes are unavailable
image.enable_read_ahead=false
slideshow.delay=5
[Saved window 1]
file_view_type=list
dir_view_type=tree
saved_layouts=1
''')
        self.assertEqual(prefs['slideshow.delay'], '5')
        self.assertEqual(windows[0]['file_view_type'], '0')
        self.assertEqual(windows[0]['dir_view_type'], '1')

    def test_missing_and_unrelated_reports_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'does not exist'):
            TOOL.read_report(self.report)
        self.report.write_text('Other program\n[Preferences]\nimage.read_ahead_images=8\n')
        with self.assertRaisesRegex(ValueError, 'Not a supported Geeqie'):
            TOOL.read_report(self.report)

    def test_unsupported_format_is_rejected(self):
        self.report.write_text(NATIVE.replace('format 1', 'format 2'))
        with self.assertRaises(ValueError):
            TOOL.read_report(self.report)

    def test_duplicate_settings_and_aliases_are_rejected(self):
        for text in ('[Preferences]\nimage.read_ahead_images=4\nimage.read_ahead_images=8\n',
                     '[Window 1]\nfile_view=icons\nfile_view_type=list\n'):
            self.report.write_text(NATIVE + text)
            with self.assertRaisesRegex(ValueError, 'Duplicate diagnostic setting'):
                TOOL.read_report(self.report)

    def test_duplicate_window_sections_are_rejected(self):
        self.report.write_text(NATIVE + '[Window 1]\nshow_marks=1\n[Window 1]\nshow_marks=0\n')
        with self.assertRaisesRegex(ValueError, 'Duplicate window section'):
            TOOL.read_report(self.report)

    def test_private_and_unknown_report_fields_are_not_written(self):
        prefs, windows, skipped = self.parsed(NATIVE + '''[Preferences]
image.read_ahead_images=8
date_time_format=/home/private-name
unknown_parameter=123
[Window 1]
split_mode=2
''')
        root = TOOL.create_config(prefs, windows)
        self.assertEqual(root.find('global').attrib, {'image.read_ahead_images': '8'})
        self.assertEqual(root.find('layout').attrib, {'id': 'lw-report-1'})
        self.assertNotIn(b'private-name', TOOL.formatted_xml(root))
        self.assertEqual(len(skipped), 3)

    def test_invalid_values_are_rejected(self):
        for name, value in (('image.enable_read_ahead', 'yes'),
                            ('image.read_ahead_images', '<invalid>'),
                            ('image.read_ahead_images', '999999999999'),
                            ('slideshow.delay', '-5')):
            with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                self.parsed(NATIVE + '[Preferences]\n' + name + '=' + value + '\n')

    def test_windows_are_generated_independently_in_report_order(self):
        prefs, windows, _ = self.parsed(NATIVE + '[Window 2]\nshow_marks=1\n[Window 1]\nfile_view=icons\n')
        root = TOOL.create_config(prefs, windows)
        layouts = root.findall('layout')
        self.assertEqual([node.attrib for node in layouts],
                         [{'id': 'lw-report-1', 'file_view_type': '1'},
                          {'id': 'lw-report-2', 'show_marks': 'true'}])
        self.assertEqual(root.find('global').attrib, {})

    def test_global_only_report_does_not_invent_window_settings(self):
        prefs, windows, _ = self.parsed(NATIVE + '[Preferences]\nimage.read_ahead_images=8\n')
        root = TOOL.create_config(prefs, windows)
        self.assertEqual(root.find('global').attrib, {'image.read_ahead_images': '8'})
        self.assertEqual(root.findall('layout'), [])

    def test_formatting_preserves_attribute_values_and_comments(self):
        root = ET.Element('gq')
        values = {'spaces': 'one two three', 'escaped': '<tag> & "quoted"',
                  'whitespace': 'one\ntwo\tthree', 'unicode': 'café'}
        ET.SubElement(root, 'global', values)
        comment = 'Leave <example attribute="one two"> unchanged'
        root.append(ET.Comment(comment))
        output = TOOL.formatted_xml(root)
        parsed = ET.fromstring(output)
        self.assertEqual(parsed.find('global').attrib, values)
        for name in values:
            self.assertIn('\n    ' + name + '=', output.decode('utf-8'))
        self.assertIn('<!--' + comment + '-->', output.decode('utf-8'))
        self.assertTrue(output.endswith(b'\n'))

    def run_main(self, text=None, clipboard_failure=False, launch_requested=False):
        self.report.write_text(text if text is not None else
                               NATIVE + '[Preferences]\nimage.read_ahead_images=8\n')
        original_report = self.report.read_bytes()
        original_mkdtemp = tempfile.mkdtemp

        def workspace(**kwargs):
            self.assertEqual(kwargs['dir'], '/tmp')
            return original_mkdtemp(prefix=kwargs['prefix'], dir=self.directory)

        output = io.StringIO()
        with contextlib.ExitStack() as stack:
            arguments = ['tool', str(self.report)]
            if launch_requested:
                arguments += ['--launch', '--geeqie', './build/src/geeqie']
            stack.enter_context(mock.patch.object(sys, 'argv', arguments))
            stack.enter_context(mock.patch.object(TOOL, 'load_schema', return_value=(self.schema, self.precision)))
            stack.enter_context(mock.patch.object(TOOL.tempfile, 'mkdtemp', side_effect=workspace))
            clipboard = stack.enter_context(mock.patch.object(TOOL, 'copy_path', return_value='xclip',
                side_effect=ValueError('clipboard unavailable') if clipboard_failure else None))
            launch = stack.enter_context(mock.patch.object(TOOL.subprocess, 'run'))
            launcher = stack.enter_context(mock.patch.object(TOOL, 'launch_config'))
            stack.enter_context(contextlib.redirect_stdout(output))
            stack.enter_context(contextlib.redirect_stderr(output))
            status = TOOL.main()
        launch.assert_not_called()
        if launch_requested:
            launcher.assert_called_once_with(clipboard.call_args.args[0], './build/src/geeqie')
        else:
            launcher.assert_not_called()
        self.assertEqual(self.report.read_bytes(), original_report)
        return status, clipboard, output.getvalue()

    def test_report_only_generation_and_private_output(self):
        status, clipboard, output = self.run_main()
        self.assertEqual(status, 0, output)
        destination = clipboard.call_args.args[0]
        self.assertEqual(destination.name, 'geeqierc.xml')
        self.assertEqual(stat.S_IMODE(destination.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(destination.parent.stat().st_mode), 0o700)
        root = ET.parse(destination).getroot()
        self.assertEqual(root.find('global').attrib, {'image.read_ahead_images': '8'})
        self.assertEqual([node.tag for node in root], ['global'])
        self.assertIn('\n    image.read_ahead_images="8"', destination.read_text())
        self.assertEqual(list(destination.parent.iterdir()), [destination])

    def test_report_without_serializable_settings_is_rejected(self):
        status, clipboard, output = self.run_main(NATIVE + '[Window 1]\nfullscreen=1\n')
        self.assertEqual(status, 1)
        clipboard.assert_not_called()
        self.assertIn('no supported configuration settings', output)
        self.assertEqual(list(self.directory.iterdir()), [self.report])

    def test_invalid_report_does_not_create_output(self):
        status, clipboard, output = self.run_main('Not a Geeqie report\n')
        self.assertEqual(status, 1)
        clipboard.assert_not_called()
        self.assertIn('Not a supported Geeqie', output)
        self.assertEqual(list(self.directory.iterdir()), [self.report])

    def test_clipboard_failure_retains_generated_file(self):
        status, clipboard, output = self.run_main(clipboard_failure=True)
        self.assertEqual(status, 1)
        self.assertTrue(clipboard.call_args.args[0].is_file())
        self.assertIn('Generated configuration:', output)

    def test_launch_with_selected_executable(self):
        status, _, output = self.run_main(launch_requested=True)
        self.assertEqual(status, 0, output)

    def test_clipboard_failure_does_not_prevent_launch(self):
        status, _, output = self.run_main(clipboard_failure=True, launch_requested=True)
        self.assertEqual(status, 1)
        self.assertIn('clipboard unavailable', output)

    def test_wayland_uses_standard_clipboard(self):
        destination = self.directory / 'geeqierc.xml'
        with mock.patch.dict(os.environ, {'WAYLAND_DISPLAY': 'wayland-test'}, clear=True), \
                mock.patch.object(TOOL.shutil, 'which', side_effect=lambda name: '/usr/bin/' + name), \
                mock.patch.object(TOOL.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run:
            self.assertEqual(TOOL.copy_path(destination), 'wl-copy')
        self.assertEqual(run.call_args.args[0], ['/usr/bin/wl-copy', '--type', 'text/plain;charset=utf-8'])
        self.assertEqual(run.call_args.kwargs['input'], str(destination).encode())

    def test_launch_isolates_settings_and_preserves_display(self):
        destination = self.directory / 'geeqierc.xml'
        destination.write_text('<gq><global /></gq>')
        environment = {'HOME': '/developer', 'XDG_CONFIG_HOME': '/developer/config',
                       'XDG_RUNTIME_DIR': '/run/user/1000', 'WAYLAND_DISPLAY': 'wayland-0',
                       'DISPLAY': ':1', 'GQ_CACHE_MAINTENANCE': 'yes'}
        with mock.patch.dict(os.environ, environment, clear=True), \
                mock.patch.object(TOOL.shutil, 'which', side_effect=lambda name: '/usr/bin/' + name), \
                mock.patch.object(TOOL.subprocess, 'Popen') as launch, \
                contextlib.redirect_stdout(io.StringIO()):
            TOOL.launch_config(destination, 'geeqie')
            self.assertEqual(dict(os.environ), environment)
        self.assertEqual(launch.call_args.args[0], ['/usr/bin/dbus-run-session', '--', '/usr/bin/geeqie'])
        child = launch.call_args.kwargs['env']
        profile = self.directory / 'profile'
        self.assertEqual(child['HOME'], str(profile))
        self.assertEqual(child['XDG_CONFIG_HOME'], str(profile / 'config'))
        self.assertEqual(child['XDG_RUNTIME_DIR'], environment['XDG_RUNTIME_DIR'])
        self.assertEqual(child['WAYLAND_DISPLAY'], 'wayland-0')
        self.assertEqual(child['DISPLAY'], ':1')
        self.assertEqual(child['GQ_NEW_INSTANCE'], 'y')
        self.assertNotIn('GQ_CACHE_MAINTENANCE', child)
        copied = profile / 'config/geeqie/geeqierc.xml'
        self.assertEqual(copied.read_bytes(), destination.read_bytes())
        self.assertEqual(stat.S_IMODE(copied.stat().st_mode), 0o600)
        self.assertTrue((copied.parent / 'accels.ini').is_file())
        self.assertTrue(launch.call_args.kwargs['start_new_session'])
        self.assertEqual(launch.call_args.kwargs['cwd'], profile)
        self.assertEqual(stat.S_IMODE((self.directory / 'geeqie-launch.log').stat().st_mode), 0o600)

    def test_missing_launch_executable_does_not_create_profile(self):
        with mock.patch.object(TOOL.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(ValueError, 'executable not found'):
                TOOL.launch_config(self.directory / 'geeqierc.xml', 'missing')
        self.assertFalse((self.directory / 'profile').exists())

    def test_x11_uses_clipboard_not_primary(self):
        destination = self.directory / 'geeqierc.xml'
        with mock.patch.dict(os.environ, {'DISPLAY': ':99'}, clear=True), \
                mock.patch.object(TOOL.shutil, 'which', side_effect=lambda name: '/usr/bin/' + name), \
                mock.patch.object(TOOL.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run:
            self.assertEqual(TOOL.copy_path(destination), 'xclip')
        self.assertEqual(run.call_args.args[0], ['/usr/bin/xclip', '-selection', 'clipboard', '-in'])

    def test_failed_clipboard_backend_falls_back(self):
        destination = self.directory / 'geeqierc.xml'
        with mock.patch.dict(os.environ, {'DISPLAY': ':99', 'WAYLAND_DISPLAY': 'wayland-test'}, clear=True), \
                mock.patch.object(TOOL.shutil, 'which', side_effect=lambda name: '/usr/bin/' + name), \
                mock.patch.object(TOOL.subprocess, 'run', side_effect=[mock.Mock(returncode=1), mock.Mock(returncode=0)]):
            self.assertEqual(TOOL.copy_path(destination), 'xclip')


if __name__ == '__main__':
    unittest.main()
