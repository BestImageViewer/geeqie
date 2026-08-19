# AGENTS.md

Guidance for coding agents working in this Geeqie checkout.

## Project Overview

Geeqie is a GTK4 image viewer and organizer for Linux, BSD, and other Unix-like
systems. The project is built with Meson and Ninja, uses C and C++17, and is
licensed GPL-2.0-or-later.

Important project docs:

- `README.md`: user-facing overview, build prerequisites, packaging notes.
- `CODING.md`: required code, shell, documentation, and commit style.
- `TESTING.md`: testing patterns and Meson suites.
- `DEVELOPER-NOTES.md`: implementation notes for menus, icons, file operation
  overrides, and Doxygen.
- `meson_options.txt`: feature flags such as optional image libraries,
  `unit_tests` and `fd_verbose_debug`.

## Repository Layout

- `src/`: main application source.
- `tests/`: unit test source files compiled into the Geeqie test-enabled binary.
- `build-aux/`: Meson helper scripts and functional/static-analysis tests.
- `data/`: UI files, desktop/appstream metadata, icons, plugins, and resources.
- `po/`: translations.
- `doc/`: help and Doxygen configuration.
- `tools/`: developer, release, documentation, and install helper scripts.
- `packaging/`, `snap/`: packaging support.
- `subprojects/`: Meson wraps, including Googletest.

Generated and local build output belongs under `build/`; do not edit generated
files there as source.

## Build Commands

Common local build:

```sh
meson setup build
ninja -C build
```

If `build/` already exists, reconfigure with:

```sh
meson setup --reconfigure build
```

Useful development configurations:

```sh
meson setup --buildtype=debug build
meson setup -Dunit_tests=enabled build
meson setup -C build -Dunit_tests=enabled
meson setup -C build -Dfd_verbose_debug=enabled
```

Run the built application from:

```sh
./build/src/geeqie
```

## Test Commands

Run all configured tests:

```sh
meson test -C build
```

Run selected suites:

```sh
meson test -C build --suite functional
meson test -C build --suite analysis
meson test -C build --suite unit
meson test -C build --suite filedata
```

Unit tests require a test-enabled build:

```sh
meson setup -C build -Dunit_tests=enabled
ninja -C build
meson test -C build -v --suite unit
./build/src/geeqie --run-unit-tests
```

Functional GUI-oriented tests use `xvfb-run` when available. Image tests are
enabled only when `unit_tests` is enabled and may download the Geeqie test image
repository during Meson setup.

The broad project test helper is:

```sh
tools/test-all.sh
```

It removes and recreates `build/`, runs an all-options-disabled test pass, then
runs a debug pass with unit tests and glib type checks enabled.

## Coding Style

Follow `CODING.md` for authoritative style. Key points:

- New files need `/* SPDX-License-Identifier: GPL-2.0-or-later */` or the
  script equivalent.
- C/C++ indentation uses tabs at 4-space width.
- Variables and functions use `small_letters`; defines use `CAPITAL_LETTERS`.
- Prefer explicit names and avoid macros where practical.
- Use GLib helpers where appropriate, for example `g_ascii_isspace()`.
- Do not leave temporary `DEBUG_0()`, `DEBUG_BT()`, `DEBUG_FD()`, or `DEBUG_RU()`
  calls in committed source.
- In C++ pointer inference, use `auto *var = function();`.
- Header include ordering follows the Google C++ include order guidance.
- For shell scripts, use `/bin/sh`, keep them POSIX-compatible, prefer `printf`
  over `echo`, and use portable `mktemp` patterns.

The project style places braces on their own indented lines for conditionals and
loops. Match nearby code before introducing any new formatting.

## Tests And Source Changes

- Unit test sources currently live in `tests/` and are listed in
  `tests/meson.build`.
- Adding a unit test file requires adding it to `tests/meson.build`.
- Meson test declarations are in the root `meson.build`; search for `test(`.
- Static analysis checks include clang-tidy, debug-statement checks,
  temporary-comment checks, boolean comparison checks, untranslated text checks,
  ancillary file validation, bash completion tests, and optional glib type
  checks.
- UI definitions under `data/ui/` are validated by ancillary tests; keep IDs,
  actions, and menu definitions consistent with code in `src/`.

## Documentation

- Use American English in developer docs and UI-facing source text unless
  editing a translation.
- Prefer ISO dates, `YYYY-MM-DD`.
- Doxygen comments use `/** ... */` with `@brief`, `@param`, `@return`, and
  related tags where useful.
- Script files intended for Doxygen use `##` comments and include `@file`.
- Generate full Doxygen output with:

```sh
tools/doxygen.sh
```

## Working Tree Rules

- The checkout may contain user changes. Inspect before editing and do not
  revert unrelated modifications.
- Keep edits scoped to the requested behavior.
- Prefer `rg`/`rg --files` for repository searches.
- Use Meson/Ninja commands rather than ad hoc compiler invocations.
- Avoid changing packaging, generated files, translations, or broad UI metadata
  unless the task requires it.
