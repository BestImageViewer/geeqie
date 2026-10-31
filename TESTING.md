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
static destructors, and does not attempt an in-process backtrace: allocator,
logging, and stack-unwinding code can be unsafe after a crash.

For a backtrace, run a debug build under GDB and use `thread apply all bt` when
it stops at the fault, or inspect the core dump with a debugger. Signal-handler
regression tests run in child processes with core dumps disabled and verify
termination from both the main thread and a worker thread.
