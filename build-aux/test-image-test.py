#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Regression tests for image-test startup and process cleanup."""

import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    'image_test', pathlib.Path(__file__).with_name('image-test.py'))
IMAGE_TEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMAGE_TEST)


class ImageTestHarness(unittest.TestCase):
    def test_readiness_retries_until_viewer_answers(self):
        proc = mock.Mock()
        proc.poll.return_value = None
        remote = mock.Mock(side_effect=[
            subprocess.CompletedProcess([], 1, 'Not running'),
            subprocess.TimeoutExpired('probe', 2),
            subprocess.CompletedProcess([], 0, '/images/test.png\n')])
        with mock.patch.object(IMAGE_TEST.time, 'sleep'):
            IMAGE_TEST.wait_for_remote(proc, remote, '/images/test.png')
        self.assertEqual(remote.call_count, 3)

    def test_readiness_waits_for_viewer_registration_barrier(self):
        proc = mock.Mock()
        proc.poll.return_value = None
        path = mock.Mock()
        path.exists.side_effect = [False, False, True]
        remote = mock.Mock(return_value=subprocess.CompletedProcess([], 0, '/images/test.png'))
        with mock.patch.object(IMAGE_TEST.time, 'sleep'):
            IMAGE_TEST.wait_for_remote(proc, remote, '/images/test.png', path)
        remote.assert_called_once()

    def test_readiness_reports_deadline_and_early_exit(self):
        proc = mock.Mock()
        proc.poll.return_value = None
        with mock.patch.object(IMAGE_TEST.time, 'monotonic', side_effect=[0, 31]):
            with self.assertRaisesRegex(IMAGE_TEST.GeeqieTestError, 'remote readiness'):
                IMAGE_TEST.wait_for_remote(proc, mock.Mock(), '/images/test.png')
        proc.poll.return_value = 7
        remote = mock.Mock()
        with self.assertRaises(IMAGE_TEST.GeeqieTestError) as error:
            IMAGE_TEST.wait_for_remote(proc, remote, '/images/test.png')
        self.assertEqual(error.exception.exit_code, 7)
        remote.assert_not_called()

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Requires /proc')
    def test_cleanup_stops_children_of_exited_wrapper(self):
        code = ("import subprocess,sys; "
                "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)'], "
                "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); "
                "print(child.pid,flush=True)")
        result = IMAGE_TEST.run_command([sys.executable, '-c', code], 10)
        child = int(result.stdout.strip())
        status = pathlib.Path('/proc') / str(child) / 'stat'
        deadline = time.monotonic() + 3
        while status.exists():
            try:
                state = status.read_text().rsplit(')', 1)[1].split()[0]
            except FileNotFoundError:
                break
            if state == 'Z':
                break
            if time.monotonic() >= deadline:
                self.fail('Child survived process-group cleanup')
            time.sleep(0.01)

    def test_timed_out_command_is_reaped(self):
        processes = []
        popen = subprocess.Popen

        def start(*args, **kwargs):
            proc = popen(*args, **kwargs)
            processes.append(proc)
            return proc

        with mock.patch.object(IMAGE_TEST.subprocess, 'Popen', side_effect=start):
            with self.assertRaises(subprocess.TimeoutExpired):
                IMAGE_TEST.run_command([sys.executable, '-c', 'import time; time.sleep(60)'], 0.1)
        self.assertIsNotNone(processes[0].poll())

    def test_cleanup_preserves_original_startup_error(self):
        with tempfile.TemporaryDirectory() as directory:
            home = pathlib.Path(directory)
            image = home / 'test.png'
            image.touch()
            proc = mock.Mock()
            proc.poll.return_value = None
            original = IMAGE_TEST.GeeqieTestError('original startup failure', proc)
            with mock.patch.dict(IMAGE_TEST.os.environ, {'XDG_CONFIG_HOME': directory}), \
                 mock.patch.object(IMAGE_TEST.pathlib.Path, 'home', return_value=home), \
                 mock.patch.object(IMAGE_TEST.subprocess, 'Popen', return_value=proc), \
                 mock.patch.object(IMAGE_TEST, 'wait_for_remote', side_effect=original), \
                 mock.patch.object(IMAGE_TEST, 'stop_process_group') as cleanup:
                with self.assertRaises(IMAGE_TEST.GeeqieTestError) as error:
                    IMAGE_TEST.main(['image-test.py', 'geeqie', str(image)])
            self.assertIs(error.exception, original)
            cleanup.assert_called_once_with(proc)


if __name__ == '__main__':
    unittest.main()
