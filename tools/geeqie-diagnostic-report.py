#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Collect privacy-filtered system information and saved Geeqie settings.

Requires Python 3. No additional Python packages, root access, or network access.
Examples:
  python3 geeqie-diagnostic-report.py
  python3 geeqie-diagnostic-report.py --config /path/to/geeqierc.xml
  python3 geeqie-diagnostic-report.py --geeqie /path/to/Geeqie.AppImage

Inspect the generated text before attaching it to an issue. Runtime state and
unsaved preferences cannot be read from another Geeqie process.
"""

import argparse
import ctypes
import ctypes.util
import datetime
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

PREFERENCES = (
    'hamburger_menu',
    'expand_menu_toolbar',
    'color_scheme',
    'tree_descend_subdirs',
    'view_dir_list_single_click_enter',
    'circular_selection_lists',
    'auto_next_folder',
    'lazy_image_sync',
    'update_on_time_change',
    'read_metadata_in_idle',
    'hide_window_in_fullscreen',
    'hide_osd_in_fullscreen',
    'sidebars_with_tools',
    'duplicates_match',
    'duplicates_similarity_threshold',
    'duplicates_thumbnails',
    'rot_invariant_sim',
    'dnd_default_action',
    'file_sort.case_sensitive',
    'file_ops.enable_in_place_rename',
    'file_ops.confirm_delete',
    'file_ops.confirm_move_to_trash',
    'file_ops.confirm_delete_dir',
    'file_ops.confirm_move_dir_to_trash',
    'file_ops.enable_delete_key',
    'file_ops.safe_delete_enable',
    'file_ops.use_system_trash',
    'file_ops.no_trash',
    'image.exif_rotate_enable',
    'image.scroll_reset_method',
    'image.fit_window_to_image',
    'image.limit_window_size',
    'image.limit_autofit_size',
    'image.max_autofit_size',
    'image.max_enlargement_size',
    'image.tile_cache_max',
    'image.image_cache_max',
    'image.jpeg_decode_memory_limit',
    'image.enable_read_ahead',
    'image.read_ahead_images',
    'image.read_ahead_until_full',
    'image.zoom_mode',
    'image.zoom_2pass',
    'image.zoom_to_fit_allow_expand',
    'image.allow_overscroll',
    'image.nearest_above_200',
    'image.zoom_quality',
    'image.zoom_increment',
    'image.zoom_style',
    'image.tile_size',
    'thumbnails.size.width',
    'thumbnails.size.height',
    'thumbnails.memory_cache_size',
    'thumbnails.enable_caching',
    'thumbnails.cache_into_dirs',
    'thumbnails.spec_standard',
    'thumbnails.quality',
    'thumbnails.use_exif',
    'thumbnails.use_color_management',
    'thumbnails.use_ft_metadata',
    'file_filter.show_hidden_files',
    'file_filter.dot_prefix_hidden_files',
    'file_filter.show_parent_directory',
    'file_filter.show_dot_directory',
    'file_filter.disable_file_extension_checks',
    'file_filter.disable',
    'slideshow.delay',
    'slideshow.random',
    'slideshow.repeat',
    'slideshow.transition',
    'slideshow.transition_duration',
    'fullscreen.screen',
    'fullscreen.clean_flip',
    'fullscreen.disable_saver',
    'metadata.save_in_image_file',
    'metadata.save_legacy_IPTC',
    'metadata.sync_grouped_files',
    'metadata.confirm_write',
    'metadata.write_orientation',
    'metadata.sidecar_extended_name',
    'stereo.mode',
    'stereo.fsmode',
    'stereo.enable_fsmode',
)
LAYOUT_FIELDS = (
    'show_thumbnails',
    'show_marks',
    'show_file_filter',
    'show_directory_date',
    'ignore_alpha',
    'animate',
    'tools_float',
    'tools_hidden',
    'bars_state.info',
    'bars_state.sort',
    'bars_state.hidden',
    'file_view_list_sort.method',
    'file_view_list_sort.ascending',
    'dir_view_list_sort.method',
    'dir_view_list_sort.ascending',
)
PREFERENCE_ALIASES = {
    "thumbnails.size.width": "thumbnails.max_width",
    "thumbnails.size.height": "thumbnails.max_height",
}

DISTRIBUTIONS = set("ubuntu debian fedora arch alpine opensuse-leap opensuse-tumbleweed void gentoo nixos linuxmint rhel rocky almalinux raspbian pop elementary".split())
DESKTOPS = {name.lower(): name for name in ("GNOME", "KDE", "XFCE", "X-Cinnamon", "Cinnamon", "MATE", "LXQt", "LXDE", "i3", "sway", "Unity", "Budgie", "COSMIC")}
VERSION = re.compile(r"[0-9]+(?:[.][0-9A-Za-z]+)*(?:[-+][0-9A-Za-z.]+)*")


def command(args):
    """Capture output locally; never reproduce arbitrary command output."""
    try:
        result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, errors="replace", timeout=5, check=False)
        return result.stdout[:65536] if result.returncode == 0 else ""
    except (OSError, subprocess.TimeoutExpired):
        return ""


def system_information():
    system = platform.system()
    lines = ["[System]", "os=" + (system if system in ("Linux", "Darwin", "FreeBSD", "NetBSD", "OpenBSD", "Windows") else "other")]
    machine = platform.machine()
    lines.append("architecture=" + (machine if machine in ("x86_64", "aarch64", "arm64", "i386", "i686", "armv7l", "ppc64le", "riscv64", "AMD64") else "other"))
    kernel = re.match(r"[0-9]+(?:[.][0-9]+)*", platform.release())
    lines.append("kernel_version=" + (kernel.group() if kernel else "unavailable"))
    if system == "Darwin":
        version = platform.mac_ver()[0]
        if VERSION.fullmatch(version):
            lines.append("macos_version=" + version)
    release = {}
    for path in (Path("/etc/os-release"), Path("/usr/lib/os-release")):
        try:
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                key, sep, value = line.partition("=")
                if sep and key in ("ID", "VERSION_ID"):
                    release[key] = value.strip().strip("\"'")
            break
        except OSError:
            continue
    distribution = release.get("ID", "")
    lines.append("distribution=" + (distribution if distribution in DISTRIBUTIONS else "other"))
    version = release.get("VERSION_ID", "")
    if re.fullmatch(r"[0-9]+(?:[.][0-9]+)*", version):
        lines.append("distribution_version=" + version)
    session = os.environ.get("XDG_SESSION_TYPE", "")
    lines.append("session_type=" + (session if session in ("x11", "wayland", "tty") else "unknown"))
    backend = os.environ.get("GDK_BACKEND", "")
    lines.append("requested_gdk_backend=" + (backend if backend in ("x11", "wayland", "broadway", "macos", "win32") else "unspecified"))
    hints = re.split(r"[:;]", os.environ.get("XDG_CURRENT_DESKTOP", ""))
    desktops = sorted({DESKTOPS[value.lower()] for value in hints if value.lower() in DESKTOPS})
    lines.append("desktop_hint=" + (", ".join(desktops) if desktops else "unknown"))
    wm = command(["wmctrl", "-m"])
    match = re.search(r"^Name: (.+)$", wm, re.MULTILINE)
    managers = {name.lower(): name for name in ("Mutter", "GNOME Shell", "KWin", "KWin_x11", "i3", "awesome", "Xfwm4", "Openbox", "Metacity", "Marco", "bspwm", "herbstluftwm")}
    lines.append("window_manager_hint=" + managers.get(match.group(1).lower(), "unknown") if match else "window_manager_hint=unknown")
    return lines


def library_version(name, symbol, string=False):
    try:
        library = ctypes.CDLL(ctypes.util.find_library(name) or name)
        if string:
            function = getattr(library, symbol)
            function.restype = ctypes.c_char_p
            value = function().decode("ascii", errors="replace")
        else:
            value = ".".join(str(getattr(library, "gtk_get_" + part + "_version")())
                             for part in ("major", "minor", "micro"))
        return value if VERSION.fullmatch(value) else "unavailable"
    except (OSError, AttributeError, TypeError, UnicodeError):
        return "unavailable"


def library_information():
    # These are host libraries, not necessarily those bundled with Geeqie.
    lines = ["", "[Host libraries - may differ from packaged Geeqie]"]
    try:
        glib = ctypes.CDLL(ctypes.util.find_library("glib-2.0") or "libglib-2.0.so.0")
        version = ".".join(str(ctypes.c_uint.in_dll(glib, "glib_" + part + "_version").value)
                           for part in ("major", "minor", "micro"))
    except (OSError, ValueError):
        version = "unavailable"
    lines.append("GLib=" + version)
    lines.append("GTK=" + library_version("gtk-4", ""))
    lines.append("Pango=" + library_version("pango-1.0", "pango_version_string", True))
    lines.append("LibRaw=" + library_version("raw", "libraw_version", True))
    for package in ("glib-2.0", "exiv2", "gdk-pixbuf-2.0", "lcms2", "libheif"):
        version = command(["pkg-config", "--modversion", package]).strip()
        lines.append(package + "_pkgconfig=" + (version if VERSION.fullmatch(version) else "unavailable"))
    return lines


def safe_value(value):
    if value.lower() in ("true", "false"):
        return value.lower()
    if re.fullmatch(r"-?[0-9]{1,12}", value):
        return value
    return None


def read_config(path):
    try:
        with path.open("rb") as stream:
            data = stream.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024 or b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
            return None
        return ET.fromstring(data)
    except (OSError, ET.ParseError, ValueError):
        return None


def saved_settings(config):
    lines = ["", "[Saved settings]", "source=saved configuration; unsaved runtime changes are unavailable"]
    if config is None:
        return lines + ["status=configuration not found; use --config to select it"]
    roots = []
    root = read_config(config)
    if root is None:
        return lines + ["status=configuration unreadable or unsupported"]
    roots.append(root)
    # Modern Geeqie can store window settings separately. Never print filenames/IDs.
    layouts = config.parent / "layouts"
    if layouts.is_dir():
        for path in sorted(layouts.glob("*.xml"))[:64]:
            root = read_config(path)
            if root is not None:
                roots.append(root)
    window = 0
    for root in roots:
        for node in root.iter():
            if node.tag not in ("global", "layout"):
                continue
            if node.tag == "layout":
                window += 1
                lines.append("\n[Saved window %d]" % window)
                for field, labels in (("file_view_type", {"0": "list", "1": "icons"}),
                                      ("dir_view_type", {"0": "list", "1": "tree"})):
                    if field in node.attrib:
                        lines.append(field + "=" + labels.get(node.attrib[field], "unknown"))
            for field in (PREFERENCES if node.tag == "global" else LAYOUT_FIELDS):
                attribute = PREFERENCE_ALIASES.get(field, field)
                value = safe_value(node.attrib.get(attribute, ""))
                if value is not None:
                    lines.append(field + "=" + value)
    lines.append("saved_layouts=%d" % window)
    return lines



def installation_information(executable):
    """Classify the selected executable; never emit its path or raw markers."""
    candidates = set()
    if executable:
        selected = Path(executable)
        try:
            resolved = str(selected.resolve())
            if str(selected).startswith("/snap/") or resolved.startswith("/snap/"):
                candidates.add("snap")
            if resolved.startswith("/app/") and Path("/.flatpak-info").is_file():
                candidates.add("flatpak")
            with selected.open("rb") as stream:
                header = stream.read(11)
            if header[:4] == b"\x7fELF" and header[8:11] in (b"AI\x01", b"AI\x02"):
                candidates.add("appimage")
        except OSError:
            pass
        if str(selected).lower().endswith(".appimage"):
            candidates.add("appimage")
    # Inherited markers are only hints about the environment running this script.
    if os.environ.get("APPIMAGE"):
        candidates.add("appimage")
    if os.environ.get("SNAP") and os.environ.get("SNAP_NAME"):
        candidates.add("snap")
    if Path("/.flatpak-info").is_file() or os.environ.get("FLATPAK_ID"):
        candidates.add("flatpak")
    hint = (next(iter(candidates)) if len(candidates) == 1 else
            "unknown-conflicting-markers" if candidates else "native-or-unknown")
    return ["", "[Installation - best-effort hint; verify for the affected instance]",
            "installation_method_hint=" + hint,
            "Distribution packages and source builds cannot be distinguished reliably."]


def collect(config, executable):
    lines = ["Geeqie standalone diagnostic report, format 1", "Generated (UTC): " + datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "This report uses an explicit privacy allowlist. Nothing was uploaded.",
             "No filenames, folder paths, hostnames, usernames, history, bookmarks,",
             "image metadata, commands, or arbitrary environment values are included.",
             "Runtime view state, build flags, and bundled library versions cannot be determined reliably.", ""]
    if executable:
        output = command([executable, "--version"])
        match = re.search(r"\bGeeqie\s+(" + VERSION.pattern + r")(?:\s|$)", output)
        lines.append("Geeqie_version=" + (match.group(1) if match else "unavailable"))
    else:
        lines.append("Geeqie_version=executable not found; use --geeqie to select it")
    return "\n".join(lines + system_information() + installation_information(executable) + library_information() + saved_settings(config)) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", type=Path, help="Geeqie configuration file to read (path is not included in report)")
    parser.add_argument("--geeqie", help="Geeqie executable or AppImage to query with --version")
    parser.add_argument("--output", type=Path, help="New report file; existing files are never overwritten")
    args = parser.parse_args()
    config = args.config
    if config is None:
        base = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
        candidates = [base / "geeqie/geeqierc.xml", Path.home() / ".geeqie/geeqierc.xml"]
        config = next((path for path in candidates if path.is_file()), None)
    report = collect(config, args.geeqie or shutil.which("geeqie"))
    try:
        if args.output:
            fd = os.open(str(args.output), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            path = str(args.output)
        else:
            fd, path = tempfile.mkstemp(prefix="geeqie-diagnostic-", suffix=".txt")
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(report)
    except OSError:
        print("Unable to create report. Choose a new, writable --output filename.", file=sys.stderr)
        return 1
    print("Report saved to: " + path)
    print("Review this file before attaching it to a GitHub issue.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
