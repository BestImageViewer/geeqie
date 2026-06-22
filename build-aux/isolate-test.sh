#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Isolates the test from the rest of the environment.  The goal is to
##        make the test more reliable, and to avoid interrupting other processes
##        that might be running on the host.  Passes all args through and passes
##        the return code back.
##
## $1 Full path to dbus-session.sh
## $2 Path to test executable
## $3 Path to file to test
##

set -e

TEST_HOME=$(mktemp -d)

if [ -z "$TEST_HOME" ]; then
    echo "Failed to create temporary home directory." >&2
    exit 1
fi

if [ "$TEST_HOME" = "$HOME" ]; then
    # This both breaks isolation, and makes automatic cleanup extremely dangerous.
    echo "Temporary homedir ($TEST_HOME) is the same as the actual homedir ($HOME)" >&2
    exit 1
fi

# Automatically clean up the temporary home directory on exit.
teardown() {
    # echo "Cleaning up temporary homedir $TEST_HOME" >&2
    if ! rm -rf "$TEST_HOME"; then
        # Could be a race condition; try sleeping and repeating.
        echo >&2
        echo "First cleanup attempt failed; sleeping and retrying…" >&2
        sleep 2

        # Repeat with verbose listing
        rm -rfv "$TEST_HOME"
    fi
}
trap teardown EXIT

export HOME="$TEST_HOME"
export XDG_CONFIG_HOME="${HOME}/.config"
export XDG_RUNTIME_DIR="${HOME}/.runtime"
mkdir -p "$XDG_RUNTIME_DIR"
# Mode setting required by the spec.
# https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
chmod 0700 "$XDG_RUNTIME_DIR"

# Change to temporary homedir and ensure that XDG_CONFIG_HOME exists.
cd
mkdir -p "$XDG_CONFIG_HOME"

# This file must exist to prevent a modal dialog in main.cc being triggered.
mkdir -p "$XDG_CONFIG_HOME/geeqie"
touch "$XDG_CONFIG_HOME/geeqie/accels.ini"

# Debug setting
# export G_DEBUG="fatal-warnings"  # Causes persistent SIGTRAP currently.
export G_DEBUG="fatal-critical"

SANDBOX_PATH="${HOME}/bin"
mkdir -p "$SANDBOX_PATH"

# If `bwrap` is installed, symlink it into the sandbox PATH. Used by glycin GdkPixuf.
if command -v bwrap >/dev/null; then
    ln -sf "$(command -v bwrap)" "$SANDBOX_PATH/bwrap"
fi
# Take into account system binaries that may be used by glycin
export PATH="$SANDBOX_PATH:/usr/bin:/bin"

echo "Variables in isolated environment:" >&2
env -i PATH="$PATH" G_DEBUG="$G_DEBUG" HOME="$HOME" XDG_CONFIG_HOME="$XDG_CONFIG_HOME" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" dbus-run-session -- env >&2
echo >&2

env -i PATH="$PATH" G_DEBUG="$G_DEBUG" HOME="$HOME" XDG_CONFIG_HOME="$XDG_CONFIG_HOME" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" dbus-run-session -- "$@"
