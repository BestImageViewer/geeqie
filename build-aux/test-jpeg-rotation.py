#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Check JPEG rotation preserves coefficients and refuses imperfect transforms."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, **kwargs).stdout


def main():
    image_command = shutil.which("magick") or shutil.which("convert")
    if not image_command or any(not shutil.which(tool) for tool in ("exiftran", "exiv2", "jpegtran")):
        print("Requires ImageMagick, exiftran, exiv2, and jpegtran")
        return 77

    plugin = str(Path(sys.argv[1]).resolve())
    inverse = {1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 8, 7: 7, 8: 6}
    with tempfile.TemporaryDirectory(prefix="geeqie-rotation-") as directory:
        binary_directory = Path(directory) / "bin"
        binary_directory.mkdir()
        geeqie = binary_directory / "geeqie"
        geeqie.write_text('#!/bin/sh\nprintf "%s\\n" "${1#--get-sidecars=}"\n')
        geeqie.chmod(0o700)
        environment = dict(os.environ, HOME=directory,
                           PATH=str(binary_directory) + os.pathsep + os.environ["PATH"])
        for dimensions in ("32x48", "33x35"):
            original = Path(directory) / "original.jpg"
            run(image_command, "-size", dimensions, "plasma:fractal", str(original))
            coefficients = run("jpegtran", "-copy", "none", "-optimize", str(original))
            for mode in ("explicit", "automatic", "grouped"):
                for orientation in range(1, 9):
                    image = Path(directory) / "image with spaces.jpg"
                    shutil.copyfile(original, image)
                    if mode != "explicit":
                        run("exiv2", "-M", f"set Exif.Image.Orientation Short {orientation}", str(image))
                    before = image.read_bytes()
                    arguments = {"explicit": ["-r", str(orientation)],
                                 "automatic": [], "grouped": ["-g"]}[mode]
                    result = subprocess.run(["sh", plugin, *arguments, str(image)],
                                            env=environment, capture_output=True)
                    # Transposition alone works even with partial edge blocks.
                    perfect = dimensions == "32x48" or orientation in (1, 5)
                    assert (result.returncode == 0) == perfect, (dimensions, mode, orientation, result.stderr)
                    if not perfect:
                        assert image.read_bytes() == before, "Refused rotation modified the source"
                        continue
                    run("sh", plugin, "-r", str(inverse[orientation]), str(image), env=environment)
                    restored = run("jpegtran", "-copy", "none", "-optimize", str(image))
                    assert restored == coefficients, "Rotation changed JPEG coefficients"

        # A cached orientation must be checked before writing it into the JPEG.
        shutil.copyfile(original, image)
        run("exiv2", "-M", "set Xmp.tiff.Orientation 6", str(image))
        run("exiv2", "-eX", str(image))
        sidecar = Path(directory + "/.local/share/geeqie/metadata/" + str(image) + ".gq.xmp")
        sidecar.parent.mkdir(parents=True)
        shutil.copyfile(image.with_suffix(".xmp"), sidecar)
        run("exiv2", "-M", "set Exif.Image.Orientation Short 1",
            "-M", "set Xmp.tiff.Orientation 1", str(image))
        before = image.read_bytes()
        sidecar_before = sidecar.read_bytes()
        result = subprocess.run(["sh", plugin, "-g", str(image)],
                                env=environment, capture_output=True)
        assert result.returncode != 0, "Imperfect cached orientation was accepted"
        assert image.read_bytes() == before, "Refused rotation modified source metadata"
        assert sidecar.read_bytes() == sidecar_before, "Refused rotation modified cached metadata"
    print("JPEG rotation checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
