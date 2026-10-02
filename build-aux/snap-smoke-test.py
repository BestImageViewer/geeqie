#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Check image loading and clean shutdown through the packaged app launcher."""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zlib


def png_chunk(kind, data):
    return (struct.pack("!I", len(data)) + kind + data
            + struct.pack("!I", zlib.crc32(kind + data)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", default="build/snap-smoke-test.log")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.command:
        parser.error("provide the app command, for example: snap run geeqie.x11")

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    environment.pop("GQ_NEW_INSTANCE", None)

    # The home interface permits nonhidden files in the user's home directory.
    with tempfile.TemporaryDirectory(prefix="geeqie-snap-smoke-", dir=Path.home()) as directory:
        image = Path(directory) / "smoke.png"
        header = struct.pack("!IIBBBBB", 16, 16, 8, 2, 0, 0, 0)
        pixels = (b"\x00" + b"\xff\x00\x00" * 16) * 16
        image.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
                          + png_chunk(b"IDAT", zlib.compress(pixels))
                          + png_chunk(b"IEND", b""))

        with log_path.open("w") as log:
            process = subprocess.Popen(args.command + [f"--file={image}"],
                                       env=environment, stdout=log, stderr=subprocess.STDOUT)

            def remote(option):
                result = subprocess.run(args.command + [option], env=environment,
                                        capture_output=True, text=True, timeout=10)
                log.write(result.stdout + result.stderr)
                log.flush()
                return result

            try:
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f"App exited before loading the image: {process.returncode}")
                    # A remote command started too early can become the primary app.
                    owner = subprocess.run(
                        ["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus",
                         "--object-path", "/org/freedesktop/DBus", "--method",
                         "org.freedesktop.DBus.NameHasOwner", "org.geeqie.Geeqie"],
                        env=environment, capture_output=True, text=True, timeout=10, check=True)
                    if "true" not in owner.stdout:
                        time.sleep(1)
                        continue
                    # Materialize the pixbuf before querying its image class.
                    remote("--get-render-intent")
                    result = remote("--get-file-info")
                    if (result.returncode == 0 and "Class: " in result.stdout
                            and "Class: Unknown" not in result.stdout):
                        break
                    time.sleep(1)
                else:
                    raise RuntimeError("App did not decode the test PNG within 60 seconds")

                if remote("--quit").returncode != 0:
                    raise RuntimeError("App rejected the shutdown request")
                if process.wait(timeout=15) != 0:
                    raise RuntimeError(f"App failed during shutdown: {process.returncode}")
                print("Packaged image loading and clean shutdown passed")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == "__main__":
    main()
