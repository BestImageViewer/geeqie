#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Verify symbols and opt-in crash backtracing in a packaged debug AppImage.
## @details Run on Linux under an isolated display and D-Bus session. Extracts
## the AppImage without FUSE, launches its AppRun with a private profile, and
## sends SIGABRT only to that child. Core dumps are disabled. Logs are retained.

"""Check the final debug AppImage, including its deployed runtime libraries."""

import argparse
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import sys
import time


def verify_symbols(binary):
    """Check the packaged executable rather than its pre-deployment counterpart."""
    sections = subprocess.check_output(['readelf', '--section-headers', '--wide', str(binary)],
                                       text=True, timeout=30)
    for section in ('.debug_info', '.debug_line'):
        if not re.search(r'\s' + re.escape(section) + r'\s', sections):
            raise ValueError('Packaged executable is missing ' + section)
    symbols = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(binary)],
                                      text=True, timeout=30)
    if 'backtrace_symbols_fd' not in symbols:
        raise ValueError('Packaged executable has no developer backtrace support')


def ready_report(pid):
    """Identify this child's report and wait until its SIGABRT handler is installed."""
    try:
        status = Path('/proc', str(pid), 'status').read_text()
        match = re.search(r'^SigCgt:\s*([0-9a-fA-F]+)$', status, re.M)
        if not match or not int(match[1], 16) & (1 << (signal.SIGABRT - 1)):
            return None
        for descriptor in Path('/proc', str(pid), 'fd').iterdir():
            target = os.readlink(descriptor)
            if re.fullmatch(r'/tmp/geeqie-crash-[A-Za-z0-9]+\.txt', target):
                return Path(target)
    except (FileNotFoundError, ProcessLookupError):
        pass
    return None


def check_backtrace(appdir, output):
    """Exercise the packaged launch wrapper, using only a disposable profile."""
    profile = output / 'profile'
    profile.mkdir(mode=0o700)
    environment = os.environ.copy()
    for variable, directory in [('HOME', 'home'), ('XDG_CONFIG_HOME', 'config'),
                                ('XDG_CACHE_HOME', 'cache'), ('XDG_DATA_HOME', 'data'),
                                ('XDG_STATE_HOME', 'state')]:
        path = profile / directory
        path.mkdir(mode=0o700)
        environment[variable] = str(path)
    config = profile / 'config' / 'geeqie'
    config.mkdir(mode=0o700)
    (config / 'accels.ini').touch(mode=0o600)
    environment.update(GQ_NEW_INSTANCE='y', GQ_CRASH_BACKTRACE='1')
    environment.pop('GQ_CACHE_MAINTENANCE', None)
    # Apply to this checker and its children, never to an existing Geeqie process.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    report = None
    with (output / 'launch.log').open('wb') as log:
        process = subprocess.Popen([str(appdir / 'AppRun')], env=environment, cwd=profile,
                                   stdin=subprocess.DEVNULL, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 30
            while process.poll() is None and time.monotonic() < deadline:
                report = ready_report(process.pid)
                if report:
                    break
                time.sleep(0.1)
            if not report:
                raise ValueError('Packaged Geeqie did not initialize its crash handler; see launch.log')
            process.send_signal(signal.SIGABRT)
            if process.wait(timeout=30) != -signal.SIGABRT:
                raise ValueError('Packaged Geeqie did not terminate with SIGABRT')
            destination = output / 'crash-report.txt'
            shutil.copyfile(report, destination)
            text = destination.read_text(errors='replace')
            marker = 'Best-effort developer backtrace (not signal-safe):\n'
            if marker not in text or len(re.findall(r'\[0x[0-9a-fA-F]+\]', text.split(marker, 1)[-1])) < 2:
                raise ValueError('Packaged Geeqie did not produce a usable backtrace; see crash-report.txt')
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if report and report.exists():
                # Keep failures as well as successful reports in the CI artifacts.
                shutil.copyfile(report, output / 'crash-report.txt')
                report.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appimage', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True, help='new directory for extraction and logs')
    args = parser.parse_args()
    try:
        appimage = args.appimage.resolve(strict=True)
        output = args.output_dir.resolve()
        output.mkdir(mode=0o700, parents=True, exist_ok=False)
        with (output / 'extract.log').open('wb') as log:
            subprocess.run([str(appimage), '--appimage-extract'], cwd=output,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        appdir = output / 'squashfs-root'
        verify_symbols(appdir / 'usr' / 'bin' / 'geeqie')
        check_backtrace(appdir, output)
        print('Packaged debug symbols and crash backtrace verified.')
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print('Debug AppImage check failed: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
