#!/bin/sh
#**********************************************************************
# Copyright (C) 2024 - The Geeqie Team
#
# Author: Colin Clark
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#**********************************************************************

set -e

# This will automatically pass the command name and args in the expected order.
# And `set -e` (above) means that we'll automatically exit with the same return
# code as our sub-command.
# Start with a clean environment containing only these variables.
#
# G_DEBUG="fatal-warnings" will force an abort if a warning or
# critical error is encountered.
# https://docs.gtk.org/glib/running.html#environment-variables

# Inhibit shellcheck warning SC2154 - var is referenced but not assigned
echo "${DBUS_SESSION_BUS_ADDRESS:-}"
echo "${XDG_CONFIG_HOME:-}"
echo "${XDG_RUNTIME_DIR:-}"

env -i DBUS_SESSION_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"  HOME="$HOME" XDG_CONFIG_HOME="$XDG_CONFIG_HOME" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" G_DEBUG="fatal-warnings" "$@"
