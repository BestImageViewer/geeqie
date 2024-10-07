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
import subprocess
import sys
import time
import traceback
from typing import Optional

MAX_GEEQIE_INIT_TIME_S = 10
MAX_REMOTE_CMD_TIME_S = 10
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


def main(argv) -> int:
    geeqie_exe = argv[1]
    test_image_path = "--file=" + argv[2]

    # All geeqie commands start with this.
    geeqie_cmd_prefix = ["xvfb-run", "--auto-servernum", geeqie_exe]

    # Start geeqie and have it open the provided test image path.
    # TODO(xsdg): Note that killing the `xvfb-run` script will not forward that
    # signal to the geeqie process.  See
    # <https://unix.stackexchange.com/questions/291804/howto-terminate-xvfb-run-properly>.
    # So we should modify or replace xvfb-run to allow us to kill an errant geeqie process.
    geeqie_proc = subprocess.Popen(args=[*geeqie_cmd_prefix, test_image_path])

    # try/finally to ensure we clean up geeqie_proc
    try:
        start_time = time.monotonic()
        # Wait up to MAX_GEEQIE_INIT_TIME_S for remote to initialize.
        command_fifo_path = pathlib.Path(os.environ["XDG_CONFIG_HOME"], "geeqie/layouts")

        while not command_fifo_path.exists():
            time.sleep(1)
            if time.monotonic() - start_time > MAX_GEEQIE_INIT_TIME_S:
                raise GeeqieTestError("init (before creating command fifo)", geeqie_proc)

        # We make sure Geeqie can stay alive for 2 seconds after initialization.
        time.sleep(2)
        if geeqie_proc.poll() is not None:
            raise GeeqieTestError("2-second post-init check", geeqie_proc)

        file_info_result = subprocess.run(
                args=[*geeqie_cmd_prefix, "--get-file-info"],
                capture_output=True, text=True, timeout=MAX_REMOTE_CMD_TIME_S)

        # Check if Geeqie crashed (which would cause xvfb-run to terminate)
        time.sleep(1)
        if geeqie_proc.poll() is not None:
            raise GeeqieTestError("remote command processing", geeqie_proc)

        # Request shutdown
        subprocess.run(args=[*geeqie_cmd_prefix, "--quit"],
                       timeout=MAX_REMOTE_CMD_TIME_S)

        # If there's a timeout, or the exit code isn't zero, flag it.
        try:
            if geeqie_proc.wait(MAX_GEEQIE_SHUTDOWN_TIME_S) != 0:
                raise GeeqieTestError("shutdown", geeqie_proc)
        except subprocess.TimeoutExpired:
            # Re-raise as shutdown error.
            raise GeeqieTestError("shutdown", geeqie_proc)

        # Check if file was recognized correctly or not
        if "Class: Unknown" in file_info_result.stdout:
            raise GeeqieTestError("file was not loaded correctly", geeqie_proc=None)

    except subprocess.TimeoutExpired as e:
        # This means one of the remote commands failed.  Re-raise as test error,
        # and include the original exception (to show which remote failed).
        raise GeeqieTestError("remote command timeout", geeqie_proc=None) from e

    finally:
        if geeqie_proc.poll() is None:
            # Stash the true error now, before we alter the state of geeqie_proc.
            exit_error = GeeqieTestError("shutdown", geeqie_proc)

            # geeqie is still running after we requested it to exit, so try SIGTERM.
            geeqie_proc.terminate()

            try:
                geeqie_proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                print("sending SIGKILL to geeqie")
                geeqie_proc.kill()

            raise exit_error

    return 0


if __name__ == "__main__":
    try:
        exit(main(sys.argv))

    except GeeqieTestError as e:
        traceback.print_exception(e)
        exit(e.exit_code)
