#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

export DISABLE_WAYLAND=1
export GDK_BACKEND=x11
exec "$@"
