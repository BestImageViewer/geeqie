#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Capture bounded thread backtraces from an unresponsive Geeqie process.

"""Collect hang diagnostics without requiring a responsive application main loop."""

import argparse
import ctypes
import datetime
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def debugger_command(gdb, pid):
    """Disable debugger startup scripts, automatic loading, and value printing."""
    return [gdb, '--nx', '--nh', '--batch',
            '-iex', 'set auto-load off',
            '-iex', 'set debuginfod enabled off',
            '-ex', 'set pagination off',
            '-ex', 'set confirm off',
            '-ex', 'set print frame-arguments none',
            '-ex', 'set print entry-values no',
            '-ex', 'set backtrace limit 40',
            '--pid', str(pid),
            '-ex', 'info threads',
            '-ex', 'thread apply all bt 40',
            '-ex', 'detach']


def capture(command, report, timeout):
    """Interrupt a stalled debugger so it has an opportunity to detach."""
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=report, stderr=subprocess.STDOUT,
                               start_new_session=True)
    try:
        return process.wait(timeout=timeout) == 0
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def positive_pid(value):
    pid = int(value)
    if pid <= 1:
        raise argparse.ArgumentTypeError('PID must be greater than 1')
    return pid


def launch_geeqie(executable):
    """Permit only this helper and its debugger children to trace the new process."""
    if not sys.platform.startswith('linux'):
        raise OSError('Launch mode currently requires Linux; use PID mode on other systems')
    library = ctypes.CDLL(None, use_errno=True)
    library.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong,
                             ctypes.c_ulong, ctypes.c_ulong]
    library.prctl.restype = ctypes.c_int
    helper_pid = os.getpid()

    def allow_helper():
        # PR_SET_PTRACER: authorize this helper's descendants, not arbitrary debuggers.
        if library.prctl(0x59616d61, helper_pid, 0, 0, 0) != 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))

    # The helper is single-threaded. Set permission in the child before exec;
    # no system-wide ptrace setting or installed Geeqie binary is modified.
    return subprocess.Popen([executable], stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            preexec_fn=allow_helper)


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog=
        'Requires Python 3 and GDB. Attachment temporarily pauses Geeqie. '
        'Review the report for paths before sharing. Do not run as root.')
    parser.add_argument('pid', nargs='?', type=positive_pid, help='PID of the frozen Geeqie process')
    parser.add_argument('--launch', nargs='?', const='geeqie', metavar='EXECUTABLE',
                        help='start Geeqie for testing; press Enter after reproducing the freeze (Linux)')
    args = parser.parse_args()
    if (args.pid is None) == (args.launch is None):
        parser.error('specify a PID or --launch, for example --launch ./build/src/geeqie')
    if hasattr(os, 'geteuid') and os.geteuid() == 0:
        parser.error('run as the same ordinary user as Geeqie, not root')
    gdb = shutil.which('gdb')
    if not gdb:
        parser.error('GDB is not installed; install the gdb package first')
    if args.launch is not None:
        executable = shutil.which(args.launch)
        if not executable:
            parser.error('Geeqie executable not found: ' + args.launch)
        print('Close existing Geeqie windows first so the test opens a separate process.', flush=True)
        try:
            process = launch_geeqie(executable)
            print('Geeqie started. Reproduce the freeze in its window.', flush=True)
            input('Then return here and press Enter to collect the report: ')
        except (OSError, subprocess.SubprocessError, EOFError, KeyboardInterrupt) as error:
            print('\nLaunch cancelled or failed: ' + str(error), file=sys.stderr)
            return 1
        if process.poll() is not None:
            print('The test process has exited. Close existing Geeqie instances and try again.', file=sys.stderr)
            return 1
        args.pid = process.pid
    try:
        os.kill(args.pid, 0)
    except OSError as error:
        parser.error(str(error))

    fd, path = tempfile.mkstemp(prefix='geeqie-hang-', suffix='.txt', dir='/tmp')
    print('Writing hang report: ' + path, flush=True)
    success = True
    with os.fdopen(fd, 'w', encoding='utf-8') as report:
        report.write('Geeqie hang report, format 1\n')
        report.write('Generated (UTC): ' + datetime.datetime.now(datetime.timezone.utc).isoformat() + '\n')
        report.write('PID: ' + str(args.pid) + '\n')
        report.write('Three samples; up to 40 frames per thread. Arguments and locals omitted.\n')
        report.write('Executable, library, and source paths may be present. Review before sharing.\n')
        for sample in range(1, 4):
            report.write('\n[Sample ' + str(sample) + '] ' +
                         datetime.datetime.now(datetime.timezone.utc).isoformat() + '\n')
            report.flush()
            try:
                ok = capture(debugger_command(gdb, args.pid), report, 30)
            except (OSError, subprocess.TimeoutExpired) as error:
                report.write('\nDebugger failed or timed out: ' + str(error) + '\n')
                ok = False
            if not ok:
                success = False
                report.write('\nCapture incomplete. Check debugger messages above.\n')
                break
            if sample < 3:
                time.sleep(2)
    if not success:
        print('Capture failed or incomplete; see ' + path, file=sys.stderr)
        print('Attachment may be blocked by ptrace restrictions or sandboxing. '
              'Close Geeqie and retry with --launch instead of a PID. '
              'If the debugger timed out, check whether Geeqie resumed.', file=sys.stderr)
    print('Review the report before attaching it to an issue. Matching debug symbols improve traces.')
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
