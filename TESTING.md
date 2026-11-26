# Geeqie testing and validation patterns

Geeqie incorporates a number of forms of validation, including functional tests,
unit tests, and static analysis.  These tests are defined towards the end of the
root `meson.build` file (search the file for `test(`).

You can run all enabled suites using:

```text
meson test -C build
```

Four test suites exist: `functional`, `unit`, `analysis`, and `filedata`.  You
can pick out particular suites to execute with commands like:

```text
meson test -C build --suite functional
meson test -C build --suite analysis --suite unit
```

See the Unit tests section for how to enable unit tests.

## Deprecation warnings

Enable warnings for deprecated APIs with:

```sh
meson configure build -Ddeprecated_warnings=true
```

Intentionally retained calls with no suitable replacement or needed for the
minimum supported GTK version have narrow, documented warning suppressions.
Tests inspecting intentionally retained widgets may use the same suppression.
These suppressions must be conditional on `#ifndef SHOW_ALL_DEPRECATED_WARNINGS`
so that a complete audit can expose them with:

```sh
meson configure build -Dall_deprecated_warnings=true
```

Retained tree-view APIs and tests inspecting those widgets are also suppressed
in normal warning mode until their migration to GTK list models. They remain
visible with `all_deprecated_warnings=true`.
The same applies to retained GdkPixbuf animation APIs and GLib mount-table
APIs whose replacements require a newer GLib version.
Use a clean rebuild for a complete warning inventory; an incremental build
only reports warnings from files it recompiles.

## Previewing documentation changes

To preview documentation changes, install `yelp-tools`
(which provides `yelp-build`) and the Python `lxml` module, then run:

```sh
python3 tools/preview-doc-changes.py
```

With no commit argument, this compares uncommitted changes under `doc/` with
`HEAD`, including staged, unstaged, and non-ignored untracked files. Supply a commit argument
to compare that commit with its first parent instead.

Add `--open` to launch the preview in your default browser using `xdg-open`.
The preview path is always printed, and the generated files remain available
if the browser cannot be opened.

If the desktop launcher cannot open the preview, use `--browser firefox` to
invoke Firefox directly. `--browser` accepts an executable name or path and
opens the preview without requiring `--open`.

Short options are `-d` for `--dir`, `-o` for `--open`, and `-b` for
`--browser`. Use `-h` or `--help` for all options. For example:

```sh
python3 tools/preview-doc-changes.py HEAD -d ~/my-previews -b firefox
```

The command prints a local HTML link. Open it in a browser to see the changed
files and open a page to review its changes. Git-detected renames are
identified separately, including whether their content changed. Each page
highlights inserted text in green and deleted text in red with a
strikethrough. Removed pages are retained to show their deleted content.
Highlights compare rendered text; formatting-only changes may not be marked.
Changes to graphics are listed,
but do not have standalone manual page comparisons.

The preview reads committed files or copies the current documentation into a
temporary directory, leaving the working tree, index, and stashes alone.
Output defaults to `~/geeqie-doc-previews/`. A visible home directory avoids the
private `/tmp` directories used by some sandboxed browsers. To choose the
directory instead, use:

```sh
python3 tools/preview-doc-changes.py HEAD --dir ~/my-previews --open
```

The directory may already exist and is
created if needed. Each run creates a uniquely named HTML file inside it,
such as `preview-abc123456789-random.html`, and a matching folder containing
the supporting pages and images. Existing previews are preserved.

Keep the HTML file and matching folder together when sharing the preview. The
baseline version is built temporarily and discarded. An explicitly supplied
merge commit is compared with its first parent; an explicitly supplied root
commit cannot be previewed.

## Unit tests

Unit tests live under `./tests`.  Because they include a lot of extra code in
the Geeqie binary, they must be manually enabled with a command like:

```text
meson setup -D unit_tests=enabled build
```

After that point, they can be executed with:

```text
meson test -C build -v --suite unit
```

Or you can run them by hand by starting geeqie with the `--run-unit-tests`
argument:

```text
$ ninja -C build
...

$ ./build/src/geeqie --run-unit-tests
[==========] Running N tests from 1 test suite.
...
[==========] N tests from 1 test suite ran. (0 ms total)
[  PASSED  ] N tests.
```

### Adding or modifying unit tests

Geeqie uses the Googletest framework, which is well-documented: \
<http://google.github.io/googletest/>

To add a new testcase in an existing test file, just add it to the test file.
That testcase will be automatically picked up and executed.

To create a new test file, create the file under `src/tests/` with a name that
matches the file being tested.  **Then make sure to add the file to
`src/tests/meson.build` or it won't be seen or executed.**

### Keyboard input tests

The icon-pane keyboard tests use `xdotool` to send real X11 key events under
`xvfb-run`. Install both tools and run `meson test -C build --suite integration`
with unit tests enabled. The tests check focus, selection, range extension and
shrinking, paging, and the visible focus outline. Tests that call the navigation
handler directly cover the remaining modifier combinations. Real-input tests
are skipped when `xdotool` or an X11 display is unavailable.

## Functional tests

The Geeqie functional tests rely on `xvfb` in order to be able to start the app
in a standard way without requiring access to a real X server on the test
machine.  If `xvfb` is not present, these tests will not run.

### Basic test

This just ensures that Geeqie will start.  It uses the `--version` flag to keep
Geeqie from staying running.

### Image tests

The image tests are only enabled in unit_test mode.  You can set that with:

```text
meson setup -C build -D unit_test=enabled
```

This tests that Geeqie can successfully open and provide metadata info about a
library of images of different types.

See `build-aux/image-test.py` for more details.

### FileData refcount test

The refcount test is a variation of the image tests that also requires verbose
FD debugging to be enabled.  You can set that up with:

```text
meson setup -C build --debug -D unit_test=enabled -D fd_verbose_debug=enabled
```

Note that with `fd_verbose_debug` enabled, Geeqie will _always_ perform verbose
diagnostics at exit.  This is not appropriate for non-development-oriented
builds.

This test attempts some directory changing patterns that may trigger Geeqie
refcount bugs, and then runs verbose debugging on exit.  It does not currently
automatically detect leaked FileData objects.

See `build-aux/fd-refcount-test.py` for more details.

### Lua tests

Verifies that Geeqie can successfully run lua scripts by opening a stock test
image and running a variety of lua operations on it.

See `build-aux/lua-test.sh` for more details.

## Static Analysis

Source code checks exclude `tests/` and `src/third-party/`. Unit test sources
are still compiled and run when unit tests are enabled.

### Icon naming

Checks that C/C++ code uses the theme icon constants in `src/main-defines.h`.
Reports repeated hyphenated icon names, including names in tables and return
statements, and literal icon arguments to the APIs and wrappers listed in the
script. Comments, third-party code, and unit test sources are excluded.

Run with `meson test -C build 'Icon naming'` or
`python3 build-aux/icon-naming-check.py`. Diagnostics include the file and line.
When adding an icon wrapper, extend `ICON_ARGUMENTS` in the script.
This is a source check, not a C++ parser: it does not track names through
variables or validate XML UI files. It also verifies that used theme icon
constants have bundled fallback entries and source files in the icon resource
manifest. Use the fallback-aware `ui_*` icon helpers from `ui-misc.h` instead
of the corresponding GTK constructors and setters; the check enforces this.

The `IconFallback` unit tests use an isolated icon theme to check resource
lookup and decoding without system icons, selected-theme priority, theme
changes, and unknown plugin icons. They require a display; run them under
`xvfb-run` when testing without a desktop session.

### Code correctness

Runs `clang-tidy` code correctness checks for every source file in the project.
Note that this will only execute when running from a clone of the Geeqie git
project.

See `.clang-tidy` and <https://clang.llvm.org/extra/clang-tidy/checks/list.html>
for more details.

### Single value enum checks

Checks for single-value enums.

See `build-aux/enum-check.sh` for more details.

### Debug statement checks

Checks for `DEBUG_0`, `DEBUG_BT`, or `DEBUG_FD` statements in the source tree.

See `build-aux/debug-check.sh` for more details.

### Temporary comment checks

Checks for comments starting with `//~` in the source tree.

See `build-aux/temporary-comments-check.sh` for more details.

### Boolean comparison checks

Checks for boolean comparisons in the style: `if (a == FALSE)`.

See <https://docs.gtk.org/glib/types.html#gboolean> and
`build-aux/boolean-comparison-check.awk`for more details.

### Untranslated text checks

Checks for strings that haven't been marked for translation (starting with `_(`)
in the source tree.

See `build-aux/untranslated-text.sh` for more details.

### Ancillary files checks

Performs validation of non-source files within the project.  This includes
linting of `appstream` files, `desktop` files, Markdown files, GTK UI builder
files and shell scripts, all options are in the bash completions file,
as well as ensuring that all relevant build options
are covered in the functional test configuration.

These checks also require `xvfb` for the GTK UI builder validator to run.

See `build-aux/test-ancillary-files.sh` for more details.

### Fatal-signal handling

Fatal signals print a short diagnostic to standard error, then terminate with
that signal. The operating system may record a core dump, depending on the
system configuration. The handler does not run normal exit callbacks or C++
static destructors. A private report is reserved at startup as
`/tmp/geeqie-crash-XXXXXX.txt`. It records a privacy-filtered settings snapshot
and fatal-signal diagnostics. Its location is printed
to standard error on a crash. Normal exit removes the reserved report; fatal
termination leaves it available for inspection. Report creation is best effort.
On the next graphical launch, a dialog lists unacknowledged crash reports and
offers Open report, Save a copy, and Dismiss. Reports remain in `/tmp` after
acknowledgment, but are not offered again. Files reserved by running instances,
symbolic links, and files owned by other users are ignored. Reports in `/tmp`
may be lost on reboot; save a copy if needed.

Help → Save diagnostic report… previews the same technical settings and offers
to save a local text file for attachment to an issue. The explicit allowlist
includes library/build information, distribution and display backend, numeric
and boolean preferences, and current window settings such as list/icons mode.
Paths, filenames, window IDs, history, bookmarks, image metadata, external
commands, and arbitrary environment contents are excluded. Custom kernel suffixes
and unrecognized distribution identifiers are omitted. Reports are not uploaded.

The settings snapshot is refreshed during layout synchronization and checked
once per second on the main thread. Each published snapshot is immutable and
timestamped. The fatal handler copies it using signal-safe file operations and
lock-free descriptor publication, without querying GTK or inspecting preferences.
Settings changed immediately before a crash can predate the snapshot. Optional
developer backtraces can contain executable/library paths and memory addresses;
review them before sharing a crash report.

Reports saved through Help also include the last 50 action requests. Crash
reports copy the current action history separately from the settings snapshot,
so the last action does not wait for the periodic settings refresh. Timestamps
are elapsed seconds since the first recorded action. The history records compiled
action identifiers before their callbacks run, never parameters, targets, paths,
search text, or external command arguments. An entry means the action was
requested, not that it completed. It covers application/window action tables and
widget context shortcuts; mouse gestures and operations bypassing these tables
are not recorded. The standalone script cannot collect another process's history.
The fixed ring uses lock-free atomic bytes and sequence validation, allowing
signal handlers to skip an interrupted entry without allocating or waiting on a
lock. Recording actions adds no file I/O. History is not retained across sessions
unless a report is saved or a crash occurs.

For older Geeqie installations, `tools/geeqie-diagnostic-report.py` is a
standalone Python 3 script requiring no additional Python packages. Run it with
`python3 geeqie-diagnostic-report.py`; it creates a private text file in the
temporary directory and prints its location. Use `--config` to select a saved
configuration (including one belonging to a Snap or Flatpak), `--geeqie` to
select an executable or AppImage, and `--output` to choose a new output file.
The script cannot inspect unsaved settings, live view state, or a packaged
application's build flags and bundled libraries. Host-library versions and saved
configuration values are labeled accordingly. No data is uploaded.

To create a configuration solely from reported settings, run the developer
helper from a source checkout:

```sh
python3 tools/geeqie-config-from-report.py /path/to/report.txt
```

The input must be an existing native or standalone Geeqie diagnostic settings
report, format 1. The tool constructs a new XML document from the supported
settings in that report. Generation does not read a user or system configuration
or populate missing settings. Only the checkout's diagnostic schema and
serialization definitions are consulted for field names, types, and units.
Settings not included in the report are omitted; Geeqie initializes its defaults
for them when loading the generated file. The result is loadable XML, not an
exact reconstruction of settings deliberately excluded from diagnostics.

For interactive reproduction, launch a separate instance with a private profile:

```sh
python3 tools/geeqie-config-from-report.py /path/to/report.txt --launch
python3 tools/geeqie-config-from-report.py /path/to/report.txt --launch --geeqie ./build/src/geeqie
```

The short options are `-l` and `-g`. Launching requires `dbus-run-session`.
The profile has its own HOME and XDG configuration, cache, data, and state
directories. The desktop display connection is retained for X11 and Wayland.
The generated XML is copied into the profile; the original generated file stays
unchanged when Geeqie saves settings. The profile and `geeqie-launch.log` remain
beside it after exit, and their paths are printed. Clipboard failure does not
prevent launching, but still returns an error status. Check the log if the
window fails to appear.

Use the user's Geeqie version when possible. Missing settings use application
defaults; a system-wide Geeqie configuration, if installed, can also affect
startup. Private paths, custom shortcuts, sidebar contents, and unsupported
runtime state cannot be reconstructed from the report. Loading the XML into an
existing instance with `--config-load` overlays its current settings, so use
`--launch` for reproduction without your normal per-user settings.

The configuration is saved as `/tmp/geeqie-config-from-report-*/geeqierc.xml`
with owner-only permissions inside a private directory. Settings are written
one per line. The tool copies the file's absolute path to the normal Ctrl+V
clipboard using `wl-copy` on Wayland, `xclip` or `xsel` on X11, or `pbcopy` on
macOS. It does not use the X11 PRIMARY selection. If no clipboard helper works,
it prints the saved path and returns an error while retaining the result.

Attribute aliases, booleans, view enums, and slideshow-delay units are converted
using the report's format and the checkout's serialization definitions. Each
reported window gets its own layout in report order, with a generated identifier.
No unreported window settings or sidebar entries are added. Unsupported runtime
state and unknown fields are listed as skipped. The input report is unchanged.

Run its regression tests with
`meson test -C build 'Configuration from diagnostic report'`.

Both reports classify AppImage, Snap, and Flatpak packaging markers without
including their values or paths. These are best-effort hints: inherited or
conflicting markers can be misleading, and a native distribution package cannot
be reliably distinguished from a source installation. Desktop identifiers are
filtered through an allowlist. On X11 the built-in report also queries the window
manager name and emits only recognized names; on Wayland this remains unknown.
The standalone script can query `wmctrl` when installed. Graphics hardware and
drivers are not collected. The issue template asks users to supply missing
information and correct misleading hints.
Inside a sandbox, distribution information can describe the runtime base rather
than the host operating system; users should supply the host release separately.

### Diagnostic schema consistency

Run `python3 build-aux/check-diagnostic-schema.py` or
`meson test -C build 'Diagnostic schema consistency'` to check field names,
without reading user configuration files or comparing values. The check scans
the configuration writers in `src/`, including nested writers and optional
features, and compares them with `data/diagnostic-report-schema.json` and both
diagnostic implementations.

Every serialized field must be explicitly classified as reported or excluded
with a documented reason. New fields, removed fields, missing diagnostic fields,
and stale standalone mappings fail the check. Runtime-only diagnostics are
classified separately. Renaming a writer function or moving it to another source
file also requires updating its manifest entries. Use `--list-excluded` to review
the exclusions. The manifest is a review record, not an automatically regenerated
baseline: decide whether each new setting belongs in diagnostics before updating
it. Excluded fields must not be added to report output merely to satisfy the check.

The extractor recognizes the project's `WRITE_*` attribute macros and direct
`rc.write_*_option()` calls. A new serialization mechanism requires updating the
extractor; this is a source-level schema check, not a C++ AST parser. It checks
serialized names and mappings, not runtime values or the correctness of getters.
`meson test -C build 'Diagnostic schema checker tests'` exercises additions,
removals, exclusions, nested writers, and attribute aliases using synthetic source.

Debug builds with execinfo support can optionally attempt an in-process trace:

```sh
GQ_CRASH_BACKTRACE=1 ./build/src/geeqie
```

This writes stack addresses and available symbol names to standard error and the
report. It is a developer aid, not a signal-safe crash reporter: unwinding can
deadlock or fail, even after resolving the unwinder at startup. It is disabled
by default and unavailable in release builds. Neither mode collects all thread
stacks or recent GTK warnings.

For a backtrace, run a debug build under GDB and use `thread apply all bt` when
it stops at the fault, or inspect the core dump with a debugger. Signal-handler
regression tests run in child processes with core dumps disabled and verify
termination from both the main thread and a worker thread.

The x86_64 AppImage workflow also publishes
`Geeqie-latest-x86_64-debug.AppImage` in the
[Continuous build release](https://github.com/BestImageViewer/geeqie/releases/tag/continuous).
It uses Meson's `debug` build type and retains Geeqie's DWARF symbols through
installation and deployment. Its update information points to the debug variant.
An accompanying `.AppImage.build-info.txt` records the source commit and AppImage
checksum; the commit record is also included under `usr/share/geeqie/` inside
the image. The normal release AppImage remains the default download.

```sh
chmod +x Geeqie-latest-x86_64-debug.AppImage
GQ_CRASH_BACKTRACE=1 ./Geeqie-latest-x86_64-debug.AppImage
```

The debug image is larger and slower than the release image. The environment
variable enables the same best-effort backtrace described above; it is off by
default. Bundled libraries generally lack full debug symbols, so matching library
symbols can still be needed. The debug variant uses the same base system as the
release variant and does not extend its host compatibility.

Before publishing the debug variant, CI runs
`build-aux/check-debug-appimage.py` against the finished AppImage. It extracts
the image without FUSE, verifies `.debug_info`, `.debug_line`, and backtrace
symbols in its executable, then exercises `AppRun` under Xvfb and a separate
D-Bus session. The checker uses a private profile, disables core dumps, and sends
SIGABRT only to its own child after the crash handler is ready. It requires a
report containing multiple backtrace frames, which also checks that the deployed
runtime can unwind. This is a packaging test, not a crash menu item in Geeqie.
Extraction, launch, and crash-report logs are retained as workflow artifacts.

### Collecting hang reports

On Linux, the simplest method is to install Python 3 and GDB, close existing
Geeqie windows, then run:

```sh
python3 tools/geeqie-hang-report.py --launch
```

For a source build, use `--launch ./build/src/geeqie` instead. Reproduce the
freeze in the newly opened window, return to the terminal, and press Enter.
The helper collects the report automatically; no debugger commands, process IDs,
or administrator permissions are needed. The launched process grants tracing
permission specifically to this helper and its debugger children. This works
with Linux's usual parent-only tracing restriction without changing system
settings or the installed executable. Stricter sandbox or administrator policies
can still prevent tracing. Normal application output is suppressed in launch
mode; it is not collected in the report. Canceling the helper leaves Geeqie open.

For an already running Geeqie process, identify its PID with `pgrep -a geeqie`
and run:

```sh
python3 tools/geeqie-hang-report.py PID
```

Replace `PID` with that process ID. The standalone script also works with older
Geeqie versions. It writes a private, uniquely named `/tmp/geeqie-hang-*.txt`
file and prints its location. It takes three all-thread backtraces, two seconds
apart, with up to 40 frames per thread. Each attachment temporarily pauses Geeqie;
between samples the debugger detaches so processing can continue. Comparing
samples can help distinguish slow progress from a blocked thread.

Argument values and local variables are omitted. Debugger initialization files,
automatic script loading, and automatic debug-symbol downloads are disabled.
Backtraces can still contain executable, library, and source paths; review the
file before sharing. Matching debug symbols produce more useful traces. This
report does not collect Geeqie's settings or recent action buffer; attach a
normal diagnostic report separately if available.

Run as the same ordinary user as Geeqie. If attachment is blocked by ptrace
restrictions or sandboxing, the report retains the debugger's error. Close
Geeqie and retry in launch mode rather than changing system protections.
Each capture has a
30-second timeout, followed by up to ten seconds for debugger cleanup. If a
debugger times out, check whether Geeqie resumed. The script does not send signals
to Geeqie or terminate it. Remove the report when it is no longer needed.

Run the helper's regression tests with
`meson test -C build 'Hang report helper'`.

### Image preloading

Run `DirectionalReadAhead.*`, `FileCacheTest.*`, and `DiagnosticReport.*` in a
test-enabled build under `xvfb-run`. Read-ahead tests cover bounded loading,
loading more than 32 images until the cache budget is reached, yielding between
batches of cache hits, selection/collection order, direction changes, cancellation,
configuration persistence, and image-view transfers. Cache tests exercise LRU
eviction, invalidation, and reentrant notification callbacks with indexed lookups.

For a manual check, enable **Preferences → General → Image loading and caching →
Preload until cache is full**, choose an appropriate decoded image cache size,
and navigate forward from the first image. The status bar preload icon should
change from refresh arrows to a check mark on completion, a pause symbol at the
cache limit, or a warning if some images could not be loaded. Its tooltip should
show the detailed progress message and counts, with no preload text appended to
the image dimensions. The icon should disappear when preloading is inactive.
Reverse direction or change
folders while preloading to verify that obsolete work stops. Disabling the new
option restores the saved image-count limit; its default remains four.

### OSD text wrapping

Run `OsdWrapping.*` under `xvfb-run` in a test-enabled build. The tests exercise
long descriptions, unbroken text, Unicode, Pango markup, explicit newlines,
compact short text, left/right positioning, resizing, stereo viewport changes,
and template length limits. They check that description metadata is unchanged.
Run `meson test -C build 'HiDPI OSD wrapping tests'` to repeat the suite with
`GDK_SCALE=2`.

For a manual check, include `%description%` in the OSD template and view an image
with a long description. Resize the image pane, change split layout, and enter
or leave fullscreen. Long lines should reflow to the pane's width; short lines
should remain compact. A template such as `%description:80%` still deliberately
truncates the description, and descriptions taller than the pane can still be
clipped vertically.

### Pan View indexing

With unit tests enabled, run the `PanIndexing.*` tests under `xvfb-run`.
They create a temporary tree containing 6,001 images and build every Pan View
layout while a main-loop heartbeat checks responsiveness. The tests also compare
spatial-index queries against a full item scan, check sidecar grouping and hidden
file rules, and cancel, restart, and close windows with indexing pending.
Temporary trees are removed by the fixtures.
