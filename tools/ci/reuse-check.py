#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Kmux contributors
# SPDX-License-Identifier: CC0-1.0
#
# Runs `reuse lint` and fails on any licensing problem except the ones listed in
# tools/ci/reuse-inherited.txt: files inherited from upstream Konsole that lack
# licensing information there as well, and license texts upstream ships without
# using them. Kmux does not assign licenses to those files without a provenance
# review.
#
# The check also fails when a listed entry is no longer reported, so the list
# only shrinks as files are fixed here or upstream.

import json
import pathlib
import subprocess
import sys

source_dir = pathlib.Path(__file__).resolve().parents[2]
inherited_list = source_dir / "tools/ci/reuse-inherited.txt"


def read_inherited():
    entries = set()
    for line in inherited_list.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            entries.add(line)
    return entries


def main():
    # Run from the source root: reuse reports paths relative to it.
    lint = subprocess.run(["reuse", "lint", "--json"], cwd=source_dir, capture_output=True, text=True)
    try:
        report = json.loads(lint.stdout)
    except json.JSONDecodeError:
        sys.stderr.write(lint.stderr)
        print("error: reuse lint did not produce a JSON report", file=sys.stderr)
        return 1

    problems = report["non_compliant"]
    failed = False
    for category in ("bad_licenses", "deprecated_licenses", "licenses_without_extension", "missing_licenses", "read_errors"):
        if problems[category]:
            print(f"error: reuse reports {category}: {problems[category]}", file=sys.stderr)
            failed = True

    reported = set(problems["missing_copyright_info"]) | set(problems["missing_licensing_info"])
    reported |= {f"LICENSES/{license}.txt" for license in problems["unused_licenses"]}
    inherited = read_inherited()

    new = sorted(reported - inherited)
    if new:
        print("error: missing copyright or licensing information:", file=sys.stderr)
        for path in new:
            print(f"  {path}", file=sys.stderr)
        print(
            "Add SPDX-FileCopyrightText and SPDX-License-Identifier tags to these files, or annotate them in REUSE.toml.",
            file=sys.stderr,
        )
        failed = True

    fixed = sorted(inherited - reported)
    if fixed:
        print(f"error: no longer reported; remove them from {inherited_list.relative_to(source_dir)}:", file=sys.stderr)
        for path in fixed:
            print(f"  {path}", file=sys.stderr)
        failed = True

    if failed:
        return 1

    summary = report["summary"]
    print(
        f"REUSE: {summary['files_with_licensing_info']} of {summary['files_total']} files have licensing information; "
        f"tolerated {len(inherited)} entries inherited from upstream Konsole."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
