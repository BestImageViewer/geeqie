#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

unset DISABLE_WAYLAND
export GDK_BACKEND=wayland
exec "$@"
