---
name: Bug
about: Report a bug
title: ''
labels: 'bug'
assignees: ''

---

**Setup (please complete the following information):**

Complete anything not covered by the attached diagnostic report. Correct any
automatically detected hints that do not describe the Geeqie instance involved.
For Snap or Flatpak, check that the operating system release describes your host
system rather than the package's runtime environment.

 - Operating system and release: [e.g. Ubuntu 24.04, Debian Sid, FreeBSD, macOS]
 - Geeqie version (`geeqie --version`):
 - Installation method, if missing or reported as native/unknown: [distribution package, AppImage, Snap, Flatpak, or source build]
 - Desktop environment / window manager: [e.g. GNOME, KDE Plasma, i3]
 - Display backend, if known: [X11, Wayland, or other]
 - Graphics hardware / driver, if relevant: [not collected automatically]

**Diagnostic report**

If available in your version, select **Help → Save diagnostic report…**, review
the preview, and attach the saved text file. It includes technical settings such
as list/icons mode without exporting your configuration file or personal paths.

For older versions, download [geeqie-diagnostic-report.py](https://raw.githubusercontent.com/BestImageViewer/geeqie/master/tools/geeqie-diagnostic-report.py)
and run it with Python 3:

```sh
python3 geeqie-diagnostic-report.py
```

Attach the text file whose location is printed. Use `--help` for options to select
a configuration file or Geeqie executable. The script reads saved settings and
host libraries; these may differ from unsaved settings or libraries bundled with
an AppImage, Snap, or Flatpak.

Review all attachments before sharing. Do not attach your entire configuration
directory, private images, or a core dump unless specifically requested.

**Targeted debug capture (when available)**

If the log window provides **Start capture**:

1. Open **Help → Log window**.
2. Choose **Subsystems** and **Verbosity**. Use **All** subsystems if unsure;
   level 2 records general decisions, level 3 adds individual image loads and
   other per-file details, and level 4 adds rendering internals.
3. Select **Start capture**, then **Copy path** to keep its location.
4. Reproduce the problem, then select **Stop capture** if Geeqie is responsive.
5. Review and attach the file at the copied path. Compress large captures if needed.

Starting capture selects verbosity 2 if logging was disabled. The capture is a
unique `/tmp/geeqie-debug-XXXXXX.txt` file (or the configured temporary directory)
with diagnostic context and logging settings in its header. It continues while
the log window is hidden. Each capture stops automatically at a 100 MiB limit;
the window and file explain when that happens. If the limit is reached too early,
repeat with fewer subsystems or lower verbosity.

Unlike the diagnostic-only report, capture records can contain filenames, paths,
metadata, and filter text. Review them before sharing. If Geeqie freezes or
crashes, attach the capture already written as well as the applicable hang or
crash report below; a capture does not replace a backtrace.

**Describe the bug**

Describe what actually happens. Is it a crash, an unresponsive window, a slow
operation that eventually finishes, or incorrect behavior?

**To reproduce**

Steps to reproduce the behavior:

1. ...
2. ...
3. ...

Include the actions immediately before the problem, especially view changes,
selection changes, menu commands, or entering/leaving fullscreen.
Recent built-in reports include up to 50 action names, but do not record every
mouse gesture or action parameter. Please still provide reproduction steps.

 - How often does it happen? [always, sometimes, or once]
 - File view: [list or icons]
 - Folder view: [list or tree]
 - Relevant settings: [thumbnails, sorting, filters, comparison method, etc.]
 - Does it reproduce with a separate temporary configuration, if tested?

**Expected behavior**

If relevant, describe what you expected to happen.

**Screenshots**

If applicable, add screenshots to help explain your problem.

**Image sources**

If applicable, attach a non-sensitive sample image that reproduces the problem
(gzip if needed), or describe its format and dimensions. Check embedded metadata
before sharing an image.

**Crashes, freezes, or slow operations**

Complete the applicable items:

 - Approximate number of files involved and number selected:
 - Storage: [local disk, removable drive, NFS, SMB, or other; no personal paths needed]
 - Operation and phase when the problem occurs: [loading, comparing, deleting, refreshing, etc.]
 - Approximate elapsed time; does the operation eventually finish?
 - Does the window redraw or respond to Escape? Is CPU usage high or low?
 - Does the same operation work in the other file view (list/icons), if tested?

After a crash, if Geeqie offers a report on the next launch, use **Save a copy…**
and attach it. Reports are stored in `/tmp/geeqie-crash-XXXXXX.txt` and may be
removed on reboot. Developer debug builds can optionally collect a best-effort
backtrace with `GQ_CRASH_BACKTRACE=1`; this is not available in ordinary release
builds. Review backtraces for executable/library paths before sharing.

A prebuilt `Geeqie-latest-x86_64-debug.AppImage` is available from the
[Continuous build release](https://github.com/BestImageViewer/geeqie/releases/tag/continuous).
To enable its optional crash backtrace, run:

```sh
GQ_CRASH_BACKTRACE=1 ./Geeqie-latest-x86_64-debug.AppImage
```

Make the downloaded file executable first. This debug build is larger and slower
than the normal AppImage; include its full version and the accompanying build
information file when reporting a problem. Library frames may still lack symbols.

For a freeze, a backtrace captured while Geeqie is unresponsive is particularly
useful. Download [geeqie-hang-report.py](https://raw.githubusercontent.com/BestImageViewer/geeqie/master/tools/geeqie-hang-report.py)
and, on Linux, close Geeqie and run:

```sh
python3 geeqie-hang-report.py --launch
```

Reproduce the freeze, return to the terminal, and press Enter. For a source build,
use `--launch ./build/src/geeqie`. Requires Python 3 and GDB. Attach the
printed `/tmp/geeqie-hang-*.txt` file after reviewing it for private paths.
See [hang report instructions](https://github.com/BestImageViewer/geeqie/blob/master/TESTING.md#collecting-hang-reports).
Attach any relevant terminal output or logs as text files (gzip if large).

**Additional context**

Add any other context about the problem here.
