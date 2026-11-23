#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi

# Strip any non-interactive early-return guard from ~/.bashrc so non-interactive
# shells pick up persisted PATH and Qt environment variables.
touch "$HOME/.bashrc"
sed -i '/case \$- in/,/esac/d' "$HOME/.bashrc" 2>/dev/null || true

# System build, Vulkan, shader, and Qt runtime/development dependencies.
# Keep libcpp-httplib-dev, libxbyak-dev, and catch2 uninstalled so CPM uses
# the pinned versions from cpmfile.json, matching .github/workflows/build-check.yml.
$SUDO DEBIAN_FRONTEND=noninteractive apt-get update
$SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    autoconf \
    ccache \
    clang-20 \
    g++-13 \
    gcc-13 \
    git \
    glslang-tools \
    libasound2-dev \
    libavcodec-dev \
    libavfilter-dev \
    libavutil-dev \
    libboost-context-dev \
    libboost-dev \
    libboost-fiber-dev \
    libclang-rt-20-dev \
    libegl1-mesa-dev \
    libenet-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libhidapi-dev \
    libopus-dev \
    libpulse-dev \
    libssl-dev \
    libswscale-dev \
    libtool \
    libudev-dev \
    libusb-1.0-0-dev \
    libva-dev \
    libvdpau-dev \
    libxcb-cursor0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-xinerama0 \
    libxcb-xkb1 \
    libxext-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-0 \
    lld-20 \
    llvm-20 \
    mesa-common-dev \
    mesa-vulkan-drivers \
    mold \
    nasm \
    ninja-build \
    nlohmann-json3-dev \
    pipx \
    pkg-config \
    python3-venv \
    python3-zstandard \
    qt6-base-private-dev \
    qt6-charts-dev \
    qt6-tools-dev \
    spirv-headers \
    spirv-tools \
    vulkan-validationlayers \
    zlib1g-dev

# Install CMake 4.4.4 (CMakeLists.txt requires >= 3.31; Ubuntu 24.04 ships 3.28.3)
# and aqtinstall under ~/.local so they persist across / resets.
export PIPX_HOME="$HOME/.local/pipx"
export PIPX_BIN_DIR="$HOME/.local/bin"
mkdir -p "$PIPX_HOME" "$PIPX_BIN_DIR" "$HOME/Qt"

if ! "$PIPX_BIN_DIR/cmake" --version 2>/dev/null | grep -q '4\.4\.4'; then
    pipx install --force --python /usr/bin/python3 cmake==4.4.4
fi

if [ ! -x "$PIPX_BIN_DIR/aqt" ]; then
    pipx install --force --python /usr/bin/python3 aqtinstall
fi

# Install Qt 6.8.3 (Ubuntu 24.04's Qt 6.4.2 uic rejects scoped enums in .ui files).
case "$(uname -m)" in
    x86_64)
        QT_HOST="linux"
        QT_ARCH="linux_gcc_64"
        QT_SUBDIR="gcc_64"
        ;;
    aarch64|arm64)
        QT_HOST="linux_arm64"
        QT_ARCH="linux_gcc_arm64"
        QT_SUBDIR="gcc_arm64"
        ;;
    *)
        echo "Unsupported architecture for Qt 6.8.3 binary install: $(uname -m)" >&2
        exit 1
        ;;
esac

QT_PREFIX="$HOME/Qt/6.8.3/$QT_SUBDIR"
if [ ! -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    (
        cd /tmp
        "$PIPX_BIN_DIR/aqt" install-qt "$QT_HOST" desktop 6.8.3 "$QT_ARCH" \
            -m qtcharts qtimageformats -O "$HOME/Qt"
        rm -f /tmp/aqtinstall.log
    )
fi

append_bashrc() {
    grep -qxF "$1" "$HOME/.bashrc" || echo "$1" >> "$HOME/.bashrc"
}

append_bashrc 'export PATH="$HOME/.local/bin:$PATH"'
append_bashrc 'export PATH="/usr/lib/llvm-20/bin:$PATH"'
append_bashrc "export QT_ROOT_DIR=\"\$HOME/Qt/6.8.3/$QT_SUBDIR\""
append_bashrc "export Qt6_DIR=\"\$HOME/Qt/6.8.3/$QT_SUBDIR/lib/cmake/Qt6\""
append_bashrc "export CMAKE_PREFIX_PATH=\"\$HOME/Qt/6.8.3/$QT_SUBDIR\""
