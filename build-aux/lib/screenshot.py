#!/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later

import pathlib
import subprocess
import time
from typing import Optional

MAX_SSHOT_CAPTURE_TIME_S = 10

class GeeqieScreenshot:
    def __init__(self, sshot_dir: Optional[pathlib.Path] = None):
        """Initializes GeeqieScreenshot

        May raise ImportError.
        """
        # We run this here to make it easier to use this as an optional, best-effort dependency
        import psutil

        if sshot_dir:
            self._sshot_dir = sshot_dir
        else:
            self._sshot_dir = pathlib.Path("/tmp/gq_test_screenshots/")

    def capture(self, base_filename: str, xvfb_run_process: subprocess.Popen) -> Optional[pathlib.Path]:
        import psutil

        self._sshot_dir.mkdir(parents=True, exist_ok=True)
        escaped_name = base_filename.replace('.', '_')
        sshot_name = f"{int(time.time())}_{escaped_name}.png"
        sshot_file = self._sshot_dir / sshot_name

        # We borrow the geeqie process's environment, to pick up the appropriate DISPLAY and
        # XAUTHORITY values from xvfb.
        geeqie_env = None

        xvfb_process = psutil.Process(xvfb_run_process.pid)
        for child in xvfb_process.children():
            if not child.name().endswith("geeqie"):
                continue

            # Found the geeqie process.  Now find the environment variables we care about.
            geeqie_env = child.environ()
            break

        if not geeqie_env:
            return None

        # Attempt to take the screenshot
        subprocess.run(args=["import", "-window", "root", str(sshot_file)], timeout=MAX_SSHOT_CAPTURE_TIME_S, env=geeqie_env)
        return sshot_file
