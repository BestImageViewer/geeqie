#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Test hang report privacy options, failure handling, and debugger cleanup.

import contextlib
import importlib.util
import io
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    'hang_report', Path(__file__).resolve().parents[1] / 'tools/geeqie-hang-report.py')
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


class HangReport(unittest.TestCase):
    def test_debugger_options(self):
        command = REPORT.debugger_command('/usr/bin/gdb', 1234)
        self.assertIn('--nx', command)
        self.assertIn('set auto-load off', command)
        self.assertIn('set debuginfod enabled off', command)
        self.assertIn('set print frame-arguments none', command)
        self.assertIn('set print entry-values no', command)
        self.assertIn('thread apply all bt 40', command)
        self.assertEqual(command[-2:], ['-ex', 'detach'])
        self.assertNotIn('full', command)

    def test_timeout_interrupts_debugger(self):
        process = mock.Mock()
        process.wait.side_effect = [subprocess.TimeoutExpired('gdb', 30), 0]
        process.poll.return_value = None
        with mock.patch.object(REPORT.subprocess, 'Popen', return_value=process):
            with self.assertRaises(subprocess.TimeoutExpired):
                REPORT.capture(['gdb'], io.StringIO(), 30)
        process.send_signal.assert_called_once_with(signal.SIGINT)
        process.terminate.assert_not_called()

    def test_stalled_debugger_is_reaped(self):
        process = mock.Mock()
        process.wait.side_effect = [subprocess.TimeoutExpired('gdb', 30),
                                   subprocess.TimeoutExpired('gdb', 5),
                                   subprocess.TimeoutExpired('gdb', 5), 0]
        process.poll.return_value = None
        with mock.patch.object(REPORT.subprocess, 'Popen', return_value=process):
            with self.assertRaises(subprocess.TimeoutExpired):
                REPORT.capture(['gdb'], io.StringIO(), 30)
        process.terminate.assert_called_once()
        process.kill.assert_called_once()

    def run_report(self, results):
        with tempfile.TemporaryDirectory(prefix='geeqie-hang-test-') as directory:
            path = Path(directory) / 'report.txt'
            original_mkstemp = tempfile.mkstemp

            def create_report(**kwargs):
                self.assertEqual(kwargs['dir'], '/tmp')
                fd, name = original_mkstemp(dir=directory)
                Path(name).rename(path)
                return fd, str(path)

            with contextlib.ExitStack() as stack:
                stack.enter_context(mock.patch.object(sys, 'argv', ['report', '1234']))
                stack.enter_context(mock.patch.object(REPORT.os, 'geteuid', return_value=1000))
                stack.enter_context(mock.patch.object(REPORT.os, 'kill'))
                stack.enter_context(mock.patch.object(REPORT.shutil, 'which', return_value='/usr/bin/gdb'))
                stack.enter_context(mock.patch.object(REPORT.tempfile, 'mkstemp', side_effect=create_report))
                capture = stack.enter_context(mock.patch.object(REPORT, 'capture', side_effect=results))
                sleep = stack.enter_context(mock.patch.object(REPORT.time, 'sleep'))
                stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
                result = REPORT.main()
            return result, path.read_text(), path.stat().st_mode & 0o777, capture.call_count, sleep.call_count

    def test_three_samples_private_file(self):
        result, text, mode, captures, sleeps = self.run_report([True] * 3)
        self.assertEqual((result, mode, captures, sleeps), (0, 0o600, 3, 2))
        self.assertEqual(text.count('[Sample '), 3)

    def test_failed_attach_stops(self):
        result, text, _, captures, sleeps = self.run_report([False])
        self.assertEqual((result, captures, sleeps), (1, 1, 0))
        self.assertIn('Capture incomplete', text)

    def test_timeout_preserves_partial_report(self):
        result, text, _, captures, _ = self.run_report([subprocess.TimeoutExpired('gdb', 30)])
        self.assertEqual((result, captures), (1, 1))
        self.assertIn('timed out', text)

    def test_launch_authorizes_only_helper(self):
        library = mock.Mock()
        library.prctl.return_value = 0
        with mock.patch.object(REPORT.sys, 'platform', 'linux'), \
                mock.patch.object(REPORT.ctypes, 'CDLL', return_value=library), \
                mock.patch.object(REPORT.subprocess, 'Popen') as popen:
            REPORT.launch_geeqie('/path with spaces/geeqie')
            args, kwargs = popen.call_args
            self.assertEqual(args[0], ['/path with spaces/geeqie'])
            kwargs['preexec_fn']()
        library.prctl.assert_called_once_with(0x59616d61, REPORT.os.getpid(), 0, 0, 0)

    def test_launch_permission_failure(self):
        library = mock.Mock()
        library.prctl.return_value = -1
        with mock.patch.object(REPORT.sys, 'platform', 'linux'), \
                mock.patch.object(REPORT.ctypes, 'CDLL', return_value=library), \
                mock.patch.object(REPORT.subprocess, 'Popen') as popen:
            REPORT.launch_geeqie('geeqie')
            with self.assertRaises(OSError):
                popen.call_args.kwargs['preexec_fn']()


if __name__ == '__main__':
    unittest.main()
