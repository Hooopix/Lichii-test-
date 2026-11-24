#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

# tools/../
ROOTDIR=$(CDPATH='' cd -- "$(dirname -- "$0")/../" && pwd)
SRC_DIR="$ROOTDIR"/src

die() {
    echo "-- $*" >&2
    exit 1
}

usage() {
    cat <<EOF
Usage: $0 <command>

Dumb script that serves as an ad-hoc cpp-linter

Commands:
    once    Check for #pragma once presence in header files
    osdef   Finds OS defines that are not recommended to use.
EOF
}

if [ "$#" -eq 0 ]; then
    usage >&2
    exit 1
fi

case "$1" in
    once)
        find "$SRC_DIR" -type f -name "*.h" -exec grep -L "#pragma once" {} +
        ;;
    osdef)
        # not recommended macros
        PATTERN="ANDROID\|_WIN64\|__linux\|__unix\|APPLE\|__APPLE"
        strings=("ANDROID" "_WIN64" "__linux" "__unix" "APPLE" "__APPLE" "linux" "unix")
        for item in "${strings[@]}"; do
            PATTERN="$PATTERN\|ifdef $item\|($item)"
        done
        # if statements for macros that shouldn't be if
        strings=("_WIN32" "_AIX" "__managarm__" "__unix__" "__linux__" "__FreeBSD__" "__NetBSD__" \
            "__OpenBSD__" "__DragonFly__" "__redox__" "__HAIKU__" "__OHOS__" "__FIREOS__")
        for item in "${strings[@]}"; do
            PATTERN="$PATTERN\|if $item"
        done
        find "$SRC_DIR" -type f -name "*.h" -exec grep -nw "$PATTERN" {} + || echo
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
