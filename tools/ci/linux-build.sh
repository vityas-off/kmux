#!/bin/sh
# SPDX-FileCopyrightText: 2026 Kmux contributors
# SPDX-License-Identifier: CC0-1.0
#
# Clean Release build, full test run, staged installation, and metadata
# validation. Used by CI; can also be run locally from the source root:
#
#   tools/ci/linux-build.sh [build-directory]
#
# The build directory must not exist yet, so that every run is a clean build.

set -eu

source_dir=$(pwd)
build_dir=${1:-"$source_dir/build-ci"}
stage_dir="$build_dir/stage"
prefix=/usr
app_id=io.github.vityas_off.kmux

if [ -e "$build_dir" ] || [ -e "$build_dir-notests" ]; then
    echo "error: $build_dir already exists; CI builds must start from an empty directory" >&2
    exit 1
fi

section() {
    printf '\n==> %s\n' "$1"
}

section "Configure without tests"
# Distribution packages build with BUILD_TESTING=OFF, which skips the test
# CMake modules; make sure the project still configures without them.
cmake -S "$source_dir" -B "$build_dir-notests" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DWITH_KAPSULE=OFF \
    -DWITH_LIBSSH=ON > /dev/null
rm -rf "$build_dir-notests"

section "Configure"
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DBUILD_TESTING=ON \
    -DUSE_DBUS=ON \
    -DWITH_X11=ON \
    -DWITH_LIBSSH=ON \
    -DWITH_KAPSULE=OFF

section "Build"
cmake --build "$build_dir"

section "Test"
# Widget tests need a platform plugin, and session/agent tests need a session bus.
QT_QPA_PLATFORM=offscreen dbus-run-session -- \
    ctest --test-dir "$build_dir" --output-on-failure --timeout 300

section "Staged installation"
DESTDIR="$stage_dir" cmake --install "$build_dir"

root="$stage_dir$prefix"
missing=0
# Paths that every Linux build with DBus enabled must install. Library, plugin,
# and translation directories depend on KDEInstallDirs, so match them by name.
for path in \
    bin/kmux \
    bin/kmux-agent-hooks \
    bin/kmux-claude \
    bin/kmux-codex \
    bin/kmux-project-status \
    bin/kmuxprofile \
    share/applications/$app_id.desktop \
    share/metainfo/$app_id.metainfo.xml \
    share/kglobalaccel/$app_id.desktop \
    share/knotifications6/kmux.notifyrc \
    share/kio/servicemenus/kmuxrun.desktop \
    share/config.kcfg/kmux.kcfg \
    share/qlogging-categories6/kmux.categories \
    share/icons/hicolor/scalable/apps/kmux.svg \
    share/zsh/site-functions/_kmux \
    share/kmux/licenses/Devicon-MIT.txt \
    share/kmux/licenses/MaterialSymbols-Apache-2.0.txt
do
    if [ ! -e "$root/$path" ]; then
        echo "missing: $prefix/$path" >&2
        missing=1
    fi
done
for name in \
    'libkmuxapp.so*' \
    'libkmuxprivate.so*' \
    kmuxpart.so \
    kmux_quickcommandsplugin.so \
    kmux_sshmanagerplugin.so \
    kmux.mo \
    claude \
    codex
do
    if [ -z "$(find "$root" -name "$name" -print -quit)" ]; then
        echo "missing: $name" >&2
        missing=1
    fi
done
# Kmux must install side by side with Konsole, so nothing may use its names.
konsole_paths=$(find "$root" -iname '*konsole*')
if [ -n "$konsole_paths" ]; then
    echo "installed paths that would collide with Konsole:" >&2
    echo "$konsole_paths" >&2
    missing=1
fi
if [ "$missing" -ne 0 ]; then
    exit 1
fi
find "$root" -type f | wc -l | xargs printf '%s files installed\n'

section "Validate metadata"
desktop-file-validate "$root/share/applications/$app_id.desktop"
metainfo="$root/share/metainfo/$app_id.metainfo.xml"
# --strict ignores pedantic hints, so fail on any reported issue explicitly.
appstream_report=$(appstreamcli validate --pedantic --no-net "$metainfo" || true)
echo "$appstream_report"
if echo "$appstream_report" | grep -q '^[EWIP]:'; then
    echo "error: AppStream metadata has validation issues" >&2
    exit 1
fi

section "Version"
# The staged binaries resolve the internal libraries from the staging tree.
library_dir=$(dirname "$(find "$root" -name 'libkmuxprivate.so*' -print -quit)")
export LD_LIBRARY_PATH="$library_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
application_version=$(QT_QPA_PLATFORM=offscreen "$root/bin/kmux" --version)
echo "$application_version"
"$root/bin/kmux-project-status" --version
"$root/bin/kmux-agent-hooks" --version

# The newest AppStream release must describe the version the build reports.
version=$(echo "$application_version" | awk '{ print $2 }')
latest_release=$(grep -o '<release version="[^"]*"' "$metainfo" | head -n 1 | cut -d '"' -f 2)
if [ "$version" != "$latest_release" ]; then
    echo "error: kmux reports $version, but the newest AppStream release is $latest_release" >&2
    exit 1
fi
