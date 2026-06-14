#!/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later

import re
import sys
import traceback

from lib import geeqie_scripted_test as gst


def main(argv) -> int:
    test_runner = gst.GeeqieScriptedTest(argv)

    container_dir = test_runner.container_dir
    imgs_dir = test_runner.symlink_images_dir

    empty_re = re.compile(r"^$")
    some_class_re = re.compile(r"Class: \S")

    script = [
        (f"--file={imgs_dir}", None),
        ("--get-file-info",    some_class_re),
        ("--action=PanView",   None),
        ("--get-file-info",    some_class_re),
    ]

    test_runner.run(script)
    return 0


if __name__ == "__main__":
    try:
        exit(main(sys.argv))

    except gst.GeeqieTestError as e:
        traceback.print_exception(e)
        exit(e.exit_code)
