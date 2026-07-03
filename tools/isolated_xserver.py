#!/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later

import itertools
import optparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

DEBUG_MODE=False


def start_xephyr(xargs: list) -> list[subprocess.Popen, str]:
    """Starts a Xephyr instance with the specified arguments.

    Args:
        xargs: A list of arguments to include.  Key/value pairs should be separated with
          an "=" character (which will be split into two arguments internally).  Because
          Xephyr handles repeated arguments, arguments will _not_ be de-duplicated.

          A small set of default arguments will be used, but may be overridden by the
          specified arguments.

    Returns:
        List containing the subprocess.Popen handle for the Xephyr process, as well as the
        DISPLAY for the server.
    """

    # TODO: use pass_fds with the -displayfd argument to avoid blind sleep
    # Argument merging behavior:
    default_xephyr_args = [
            ["-ac"], ["-br"], ["-reset"], ["-terminate", "2"], ["-screen", "1920x1080"]]

    xephyr_args = [arg.split("=", 2) for arg in xargs]
    xephyr_arg_keys = set([arg[0] for arg in xephyr_args])

    # Prepend default args to xephyr_args only-if they haven't been overridden.
    accepted_defaults = [arg for arg in default_xephyr_args
                         if arg[0] not in xephyr_arg_keys]

    # Concatenate and flatten final args list.
    xephyr_args = itertools.chain.from_iterable(accepted_defaults + xephyr_args)

    xephyr_display = ":55"
    xephyr_cmd = ["Xephyr", *xephyr_args, xephyr_display]

    if DEBUG_MODE:
        print("Xephyr: " + repr(xephyr_cmd))

    xephyr_proc = subprocess.Popen(xephyr_cmd, stderr=subprocess.DEVNULL)
    time.sleep(1)

    return [xephyr_proc, xephyr_display]


def add_symlink(parent_dir: pathlib.Path, spec: str):
    """Creates a symlink in the specified location.

    Args:
        parent_dir:
            The directory under which symlinks will be created.  Paths outside this
            directory will not be modified (including through prior symlinks).
    """
    dest_str, target_str = spec.split("=", 2)
    dest_path = (parent_dir / dest_str).resolve()
    target_path = pathlib.Path(target_str)
    # Validate target path.
    target_path.resolve(strict=True)

    if not dest_path.is_relative_to(parent_dir):
        raise RuntimeError(f"Symlink dest spec {spec} resolved {dest_str} to {dest_path}, "
                           f"which is not a child of {parent_dir}")

    if DEBUG_MODE:
        print(f"Symlink dest spec {spec} resolved {dest_str} to {dest_path}, which is a "
              f"child of {parent_dir}")

    dest_path.parent.mkdir(parents=True, exist_ok=True)
    dest_path.symlink_to(target_path)


# Inspired by isolate-test.sh, but intended for interactive development and diagnostics
def main(args, passthrough_argv) -> int:
    # We need to clean this up by hand, because there's a race condition with dbus
    # infrastructure shutting down asynchronously, so it often takes two attempts.
    test_home = tempfile.TemporaryDirectory(prefix="gq_xephyr_")
    xephyr_proc = None
    try:
        env = {}

        env["HOME"] = pathlib.Path(test_home.name)
        # Change to the temp homedir.
        os.chdir(env["HOME"])

        env["XDG_CONFIG_HOME"] = env["HOME"] / ".config"
        env["XDG_CONFIG_HOME"].mkdir(parents=True)

        # Mode setting as required by the spec.
        # https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
        # (Mode only affects the leaf dir; not parents)
        env["XDG_RUNTIME_DIR"] = env["HOME"] / ".runtime"
        env["XDG_RUNTIME_DIR"].mkdir(mode=0o700, parents=True)

        # This file must exist to prevent a modal dialog in main.cc being triggered.
        gq_config = env["XDG_CONFIG_HOME"] / "geeqie"
        gq_config.mkdir()
        (gq_config / "accels.ini").touch()

        sandbox_bin = env["HOME"] / "bin"
        sandbox_bin.mkdir()
        env["PATH"] = f"{sandbox_bin}:/usr/bin:/bin"

        for symlink_spec in args.link_path:
            add_symlink(env["HOME"], symlink_spec)

        # If `bwrap` is installed, symlink it into the sandbox PATH. Used by glycin GdkPixuf.
        bwrap_path = shutil.which("bwrap")
        if bwrap_path:
            add_symlink(env["HOME"], f"bin/bwrap={bwrap_path}")

        xephyr_proc, xephyr_display = start_xephyr(args.xarg)
        env["DISPLAY"] = xephyr_display

        str_env = {k: str(v) for k, v in env.items()}
        print("Variables in isolated environment:", file=sys.stderr)
        subprocess.run(["dbus-run-session", "--", "env"], env=str_env, stdout=sys.stderr)
        print("", file=sys.stderr)

        # TODO: Use single dbus-run-session?
        # Optionally starts a window manager.
        # We don't clean it up manually; it should exit when the Xephyr server dies.
        if args.wm:
            wm_proc = subprocess.Popen(args.wm, env=str_env, stdout=sys.stderr)
            time.sleep(1)

        run_cmd = ["dbus-run-session", "--", *passthrough_argv]
        print(f"running: {repr(run_cmd)}", file=sys.stderr)
        subprocess.run(run_cmd, env=str_env)

    finally:
        if xephyr_proc and xephyr_proc.poll() is None:
            print("Terminating Xephyr…", file=sys.stderr)
            xephyr_proc.terminate()

        try:
            test_home.cleanup()
        except (ConnectionAbortedError, OSError):
            # Cleanup race condition with dbus infrastructure
            print("First cleanup attempt failed; sleeping and retrying…", file=sys.stderr)
            time.sleep(2)

            test_home.cleanup()

    return 0

if __name__ == "__main__":
    # Using optparse instead of argparse to correctly handle Xephyr args that start with
    # a dash.  See https://github.com/python/cpython/issues/53580
    parser = optparse.OptionParser()
    parser.add_option("--xarg", action="append", default=[],
                        help="Arg for Xephyr server; repeatable. Use key=value when needed.")
    parser.add_option("--debug", action="store_true", help="Enables harness debug logging")
    parser.add_option("--link_path", action="append", default=[],
                        help=("File or dir to symlink into the isolated homedir.  Use "
                              "dest=source, with dest relative to isolated homedir.  Parent "
                              "directories will be created."))
    parser.add_option("--wm",
                        help="A window-manager to start.")
    args, passthrough_argv = parser.parse_args()

    DEBUG_MODE = args.debug
    if DEBUG_MODE:
        print(f"sa: {repr(args)}\npa: {repr(passthrough_argv)}\n")

    exit(main(args, passthrough_argv))
