# Geeqie testing and validation patterns

Geeqie incorporates a number of forms of validation, including functional tests,
unit tests, and static analysis.  These tests are defined towards the end of the
root `meson.build` file (search the file for `test(`).

You can run all enabled suites using:

```text
meson test -C build
```

Three test suites exist: `functional`, `unit`, and `analysis`.  You can pick out
particular suites to execute with commands like:

```text
meson test -C build --suite functional
meson test -C build --suite analysis --suite unit
```

See the Unit tests section for how to enable unit tests.

## Unit tests

Unit tests live under `src/tests`.  Because they include a lot of extra code in
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

### Lua tests

Verifies that Geeqie can successfully run lua scripts by opening a stock test
image and running a variety of lua operations on it.

See `build-aux/lua-test.sh` for more details.

## Static Analysis

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

### GTK4 migration regression checks

Checks that gtk functions for which there is a Geeqie GTK4 compatibility
function have a `gq_` prefix.

See `build-aux/gtk4-migration-regression-check.sh` for more details.

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
