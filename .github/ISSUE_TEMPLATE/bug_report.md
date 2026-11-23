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

For a freeze, a backtrace captured while Geeqie is unresponsive is particularly
useful. See [debugger instructions](https://github.com/BestImageViewer/geeqie/blob/master/TESTING.md#fatal-signal-handling).
Attach any relevant terminal output or logs as text files (gzip if large).

**Additional context**

Add any other context about the problem here.
