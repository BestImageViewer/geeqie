#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

# Prefer Wayland if available & supported; otherwise fall back to X11.
unset DISABLE_WAYLAND
if [ -n "$WAYLAND_DISPLAY" ]
then
  export GDK_BACKEND=wayland,x11
else
  export GDK_BACKEND=x11
fi
exec "$@"
