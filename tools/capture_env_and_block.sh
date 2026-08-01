#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

# Simple utility to capture the environment to a file and then block indefinitely.
# This is intended to be used with weston to allow other commands to connect to it
# (by copying the environment it creates for child processes).

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <env_out_file>" > /dev/stderr
    exit 1
fi

env_out_file="$1"

env > "$env_out_file"

# Now block forever.
sleep infinity
