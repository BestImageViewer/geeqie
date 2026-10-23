#!/bin/python3
#**********************************************************************
# Copyright (C) 2023 - The Geeqie Team
#
# Author: Colin Clark
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#**********************************************************************

import os
import pathlib
import signal
import subprocess
import sys
import time
import traceback
from lib import screenshot
from typing import Optional

# Change this to True to enable screenshots.
# Requires "psutil" import, plus `import` command from ImageMagick/GraphicsMagick.
SHOULD_TAKE_SCREENSHOT = False

MAX_GEEQIE_INIT_TIME_S = 30
MAX_REMOTE_CMD_TIME_S = 10
MAX_IMAGE_LOAD_TIME_S = 30
MAX_GEEQIE_SHUTDOWN_TIME_S = 10

class GeeqieTestError(Exception):
    """A custom exception type that forwards the geeqie exit code.

    Attributes:
        exit_code: An int with the exit code to return when image-test.py exits.
    """

    def __init__(self, description: str, geeqie_proc: Optional[subprocess.Popen]):
        """GeeqieTestError constructor.

        Args:
            description: A description of the failure state.
            geeqie_proc: The Popen wrapper for the geeqie process.  The exception
                message will be generic if this is not provided.
        """
        self._description = description
        if geeqie_proc is not None:
            self._geeqie_proc_provided = True
            self._proc_exit_code = geeqie_proc.poll()
        else:
            self._geeqie_proc_provided = False
            self._proc_exit_code = 1

        if self._proc_exit_code:
            self.exit_code = self._proc_exit_code
        else:
            self.exit_code = 1

    def __str__(self) -> str:
        if self._geeqie_proc_provided:
            if self._proc_exit_code is None:
                # geeqie process is still running.
                verb = "stalled"
            else:
                # geeqie process exited unexpectedly.
                verb = "crashed"

            return f"geeqie {verb} during: {self._description}"

        # Generic error.  This will automatically be preceded by the Exception
        # class name, "GeeqieTestError", so no extra decoration is needed.
        return self._description


def stop_process_group(proc: subprocess.Popen) -> None:
    """Stop the wrapper and its children, including children of an exited wrapper."""
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        pass
    finally:
        # xvfb-run can exit before Geeqie does. Always clean up remaining children.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            print("Process group did not exit after SIGKILL", file=sys.stderr)


def run_command(args, timeout: float) -> subprocess.CompletedProcess:
    """Run a remote client in its own process group and clean up on timeout."""
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, start_new_session=True)
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
        return subprocess.CompletedProcess(args, proc.returncode, stdout, stderr)
    finally:
        stop_process_group(proc)
        proc.stdout.close()
        proc.stderr.close()


def wait_for_remote(proc, run_remote, filename: str, startup_path=None) -> None:
    """Wait for the running viewer to answer, rather than for a startup directory."""
    deadline = time.monotonic() + MAX_GEEQIE_INIT_TIME_S
    while True:
        if proc.poll() is not None:
            raise GeeqieTestError("initialization", proc)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise GeeqieTestError("initialization (waiting for remote readiness)", proc)
        # startup_common runs after the viewer has registered on D-Bus. A client
        # launched earlier could become the primary application itself.
        if startup_path is not None and not startup_path.exists():
            time.sleep(min(0.2, remaining))
            continue
        try:
            result = run_remote("--tell", timeout=min(2, remaining))
            if result.returncode == 0 and result.stdout.strip() == filename:
                return
        except subprocess.TimeoutExpired:
            pass
        time.sleep(min(0.2, max(0, deadline - time.monotonic())))


def main(argv) -> int:
    geeqie_exe = argv[1]
    test_image_path = pathlib.Path(argv[2])

    if not test_image_path.exists():
        raise RuntimeError(f"Test image path {test_image_path} does not exist")

    # To avoid geeqie's behavior being potentially altered by other files in the
    # same parent directory as test_image_path, we symlink test_image_path to
    # an other-wise-empty container directory and access it from there.
    container_dir = pathlib.Path.home() / "container_dir"
    container_dir.mkdir()
    symlink_image_file = container_dir / test_image_path.name
    symlink_image_file.symlink_to(test_image_path)

    if not symlink_image_file.exists():
        raise RuntimeError("symlinking image file failed")

    gq_sshot = screenshot.GeeqieScreenshot() if SHOULD_TAKE_SCREENSHOT else None

    # All geeqie commands start with this.
    geeqie_cmd_prefix = ["xvfb-run", "--auto-servernum", geeqie_exe]

    def run_remote_command(command: str, timeout=MAX_REMOTE_CMD_TIME_S) -> subprocess.CompletedProcess:
        return run_command([*geeqie_cmd_prefix, command], timeout)

    def wait_for_file_info() -> subprocess.CompletedProcess:
        """Wait until Geeqie reports that the current image has been loaded."""
        deadline = time.monotonic() + MAX_IMAGE_LOAD_TIME_S

        while True:
            # After the GTK4 migration, fd->pixbuf can still be null at this
            # point. This extra command forces the pixbuf to be created.
            run_remote_command("--get-render-intent")
            file_info_result = run_remote_command("--get-file-info")

            if ("Class: " in file_info_result.stdout
                    and "Class: Unknown" not in file_info_result.stdout):
                return file_info_result

            if time.monotonic() >= deadline:
                return file_info_result

            time.sleep(1)

    # A dedicated process group lets cleanup stop xvfb-run, Xvfb and Geeqie.
    geeqie_proc = subprocess.Popen(
            args=[*geeqie_cmd_prefix, f"--file={symlink_image_file}"],
            start_new_session=True)

    try:
        startup_path = pathlib.Path(os.environ["XDG_CONFIG_HOME"], "geeqie/layouts")
        wait_for_remote(geeqie_proc, run_remote_command, str(symlink_image_file), startup_path)

        # We make sure Geeqie can stay alive for 2 seconds after initialization.
        time.sleep(2)
        if geeqie_proc.poll() is not None:
            raise GeeqieTestError("2-second post-init check", geeqie_proc)

        file_info_result = wait_for_file_info()

        # Check if Geeqie crashed (which would cause xvfb-run to terminate)
        time.sleep(1)
        if geeqie_proc.poll() is not None:
            raise GeeqieTestError("remote command processing", geeqie_proc)

        if gq_sshot:
            ss_result = gq_sshot.capture(symlink_image_file.name, geeqie_proc)
            if ss_result:
                print(f"\nCAPTURED SCREENSHOT: {ss_result}\n")
            else:
                print("FAILED TO CAPTURE SCREENSHOT")

        # Request shutdown
        run_remote_command("--quit")

        # If there's a timeout, or the exit code isn't zero, flag it.
        try:
            if geeqie_proc.wait(MAX_GEEQIE_SHUTDOWN_TIME_S) != 0:
                raise GeeqieTestError("shutdown", geeqie_proc)
        except subprocess.TimeoutExpired:
            # Re-raise as shutdown error.
            raise GeeqieTestError("shutdown", geeqie_proc)

        # Check if file was recognized correctly or not
        if ("Class: Unknown" in file_info_result.stdout
                or "Class: " not in file_info_result.stdout):
            raise GeeqieTestError("file was not loaded correctly", geeqie_proc=None)

    except subprocess.TimeoutExpired as e:
        # This means one of the remote commands failed.  Re-raise as test error,
        # and include the original exception (to show which remote failed).
        raise GeeqieTestError("remote command timeout", geeqie_proc=None) from e

    finally:
        # Do not replace a startup/load error with a secondary shutdown error.
        stop_process_group(geeqie_proc)

    return 0


if __name__ == "__main__":
    try:
        exit_code = main(sys.argv)
        if exit_code not in [0, 1]:
            # This is so that Geeqie crashes in expected-failure cases will
            # still trigger a non-passing result.  This is following the
            # protocol that Meson describes here:
            # https://mesonbuild.com/Unit-tests.html#skipped-tests-and-hard-errors
            #
            # We can replace this with expected_exitcode once we bump our meson
            # dependency to 1.11.0 .
            exit_code = 99

        exit(exit_code)

    except GeeqieTestError as e:
        traceback.print_exception(e)
        if e.exit_code not in [0, 1]:
            # See comment above.
            exit(99)
        exit(e.exit_code)
