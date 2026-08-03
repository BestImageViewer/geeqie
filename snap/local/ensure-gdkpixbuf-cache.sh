#!/bin/sh
# shellcheck disable=SC2154
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

CACHE_DIR="${SNAP_USER_COMMON}/.cache"
CACHE_FILE="${CACHE_DIR}/gdk-pixbuf-loaders.cache"
mkdir -p "$CACHE_DIR"

# Find the gdk-pixbuf loaders directory (…/gdk-pixbuf-2.0/<ver>/loaders)
discover_module_dir() {
    find "$SNAP/usr/lib" \
         -type d \
         -path "*/gdk-pixbuf-2.0/*/loaders" \
         2>/dev/null |
    sort |
    while IFS= read -r d; do
        [ -d "$d" ] && { echo "$d"; return 0; }
        break
    done

    find "$SNAP/gnome-platform/usr/lib" \
         -type d \
         -path "*/gdk-pixbuf-2.0/*/loaders" \
         2>/dev/null |
    sort |
    while IFS= read -r d; do
        [ -d "$d" ] && { echo "$d"; return 0; }
        break
    done

    return 1
}

# Find the gdk-pixbuf-query-loaders binary
discover_query_tool() {
    if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
        command -v gdk-pixbuf-query-loaders
        return 0
    fi

    find "$SNAP" "$SNAP/gnome-platform" \
         -type f \
         -name "gdk-pixbuf-query-loaders" \
         -perm -111 \
         2>/dev/null |
    sort |
    while IFS= read -r p; do
        [ -x "$p" ] && { echo "$p"; return 0; }
        break
    done

    return 1
}

MODULEDIR="$(discover_module_dir 2>/dev/null || true)"
QUERY="$(discover_query_tool 2>/dev/null || true)"

# Export env for the launched app
if [ -n "${MODULEDIR:-}" ]; then
    export GDK_PIXBUF_MODULEDIR="$MODULEDIR"
fi
export GDK_PIXBUF_MODULE_FILE="$CACHE_FILE"

# Rebuild cache if we found both pieces
if [ -n "${QUERY:-}" ] && [ -n "${MODULEDIR:-}" ]; then
    GDK_PIXBUF_MODULEDIR="$MODULEDIR" "$QUERY" >"$CACHE_FILE" 2>/dev/null || true

    # GdkPixbuf built with glycin also registers a format named "svg".  Glycin
    # tries to start a nested bubblewrap sandbox, which is not permitted inside
    # a strictly confined snap.  Give librsvg's in-process loader a distinct
    # name so Geeqie can select it explicitly.
    sed -i 's/^"svg" 6 /"geeqie-svg" 6 /' "$CACHE_FILE"
else
    echo "Note: could not regenerate GDK pixbuf cache (query tool or module dir missing)." >&2
fi

exec "$@"
