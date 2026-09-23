#!/bin/sh
# SPDX-FileCopyrightText: 2026 Kmux contributors
# SPDX-License-Identifier: CC0-1.0
#
# Debug build with AddressSanitizer and UndefinedBehaviorSanitizer, and a full
# test run in which any sanitizer report fails the test. Used by CI; can also be
# run locally from the source root:
#
#   tools/ci/linux-sanitizers.sh [build-directory]
#
# The build directory must not exist yet, so that every run is a clean build.

set -eu

source_dir=$(pwd)
build_dir=${1:-"$source_dir/build-sanitizers"}

if [ -e "$build_dir" ]; then
    echo "error: $build_dir already exists; CI builds must start from an empty directory" >&2
    exit 1
fi

section() {
    printf '\n==> %s\n' "$1"
}

section "Configure"
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DUSE_DBUS=ON \
    -DWITH_X11=ON \
    -DWITH_LIBSSH=ON \
    -DWITH_KAPSULE=OFF \
    -DECM_ENABLE_SANITIZERS='address;undefined'

section "Build"
cmake --build "$build_dir"

section "Test"
# UBSan reports are recoverable by default and would not fail a test.
# Leak detection stays off: KeyboardTranslatorManager deliberately keeps deleted
# translators alive because running sessions may still use them, and an
# upstream test fixture never frees its temporary directory.
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
QT_QPA_PLATFORM=offscreen dbus-run-session -- \
    ctest --test-dir "$build_dir" --output-on-failure --timeout 600
