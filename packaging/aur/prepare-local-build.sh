#!/bin/sh
# SPDX-FileCopyrightText: 2026 Kmux contributors
# SPDX-License-Identifier: CC0-1.0
#
# Prepares an AUR build directory from the committed state of this checkout,
# so the PKGBUILD can be tested before the release tag exists:
#
#   packaging/aur/prepare-local-build.sh OUTPUT-DIRECTORY [REVISION]
#
# The output directory receives the PKGBUILD and a source archive laid out like
# GitHub's tag archive, under the file name the PKGBUILD downloads. makepkg and
# extra-x86_64-build use an existing source file instead of downloading it.
# Uncommitted changes are not included.

set -eu

output_dir=${1:?usage: prepare-local-build.sh OUTPUT-DIRECTORY [REVISION]}
revision=${2:-HEAD}
source_dir=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
package_dir="$source_dir/packaging/aur/kmux-workspaces"
tag=$(sed -n 's/^_tag=//p' "$package_dir/PKGBUILD")

if [ -e "$output_dir" ] && [ -n "$(ls -A "$output_dir")" ]; then
    echo "error: $output_dir is not empty" >&2
    exit 1
fi
mkdir -p "$output_dir"

cp "$package_dir/PKGBUILD" "$output_dir/"
git -C "$source_dir" archive --format=tar.gz --prefix="kmux-$tag/" \
    --output="$output_dir/kmux-$tag.tar.gz" "$revision"

echo "Prepared $output_dir from $(git -C "$source_dir" rev-parse --short=12 "$revision")"
