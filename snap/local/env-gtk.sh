#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck disable=SC2154  # SNAP is set by snapd

case "$SNAP_ARCH" in
  amd64)  MA=x86_64-linux-gnu ;;
  arm64)  MA=aarch64-linux-gnu ;;
  armhf)  MA=arm-linux-gnueabihf ;;
  *)      MA="$SNAP_ARCH" ;;
esac

prepend_path() {
    var_name="$1"
    value="$2"
    eval "old_value=\${$var_name:-}"
    if [ -n "$old_value" ]; then
        eval "export $var_name=\"$value:\$old_value\""
    else
        eval "export $var_name=\"$value\""
    fi
}

prepend_path LD_LIBRARY_PATH "$SNAP/usr/lib/$MA"
prepend_path LD_LIBRARY_PATH "$SNAP/usr/lib"
prepend_path XDG_DATA_DIRS "$SNAP/usr/share"

if [ -f "$SNAP/usr/lib/$MA/geeqie/libgdk-pixbuf-png-shim.so" ]; then
    prepend_path LD_PRELOAD "$SNAP/usr/lib/$MA/geeqie/libgdk-pixbuf-png-shim.so"
fi

export GSETTINGS_SCHEMA_DIR="$SNAP/usr/share/glib-2.0/schemas"
export GIO_MODULE_DIR="$SNAP/usr/lib/$MA/gio/modules"
export GIO_EXTRA_MODULES="$SNAP/usr/lib/$MA/gio/modules"
export SSL_CERT_FILE="$SNAP/etc/ssl/certs/ca-certificates.crt"
export SSL_CERT_DIR="$SNAP/etc/ssl/certs"
export GTK_EXE_PREFIX="$SNAP/usr"
export GTK_DATA_PREFIX="$SNAP/usr"

exec "$@"
