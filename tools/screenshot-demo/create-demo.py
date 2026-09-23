#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Kmux contributors
# SPDX-License-Identifier: CC0-1.0
"""Create neutral demo projects and a Kmux workspace state for screenshots.

Usage: create-demo.py [demo-root]   (default: ~/kmux-demo)

Creates empty project directories plus a ~/kmux link to this checkout, an
isolated XDG state directory with a prepared kmuxstaterc, and a run-kmux.sh
launcher. The real ~/.local/state/kmuxstaterc is never touched.

The launcher uses the demo root as HOME so that paths read ~/api-gateway
instead of a long real path. Hidden symlinks to the real configuration keep
the user's profiles, theme, fonts, and shell prompt.
"""

import json
import shutil
import sys
from pathlib import Path

REAL_HOME = Path.home()
DEMO_ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else "~/kmux-demo").expanduser()
# Entries of the real home that the demo HOME links to, when they exist.
LINKED_HOME_ENTRIES = [".config", ".local", ".cache", ".zsh", ".zshenv", ".zprofile", ".bashrc", ".profile"]
KMUX_BIN_DIR = Path("~/kde/usr/bin").expanduser()
KMUX_SOURCE_DIR = Path(__file__).resolve().parents[2]

# Title, directory, bundled icon, working directories of the terminal tabs.
# The first project is the real Kmux checkout, linked as ~/kmux.
PROJECTS = [
    ("Kmux", "kmux", "devicon/powershell.svg", [".", "src", "build"]),
    ("API Gateway", "api-gateway", "devicon/go.svg", [".", "."]),
    ("Web Console", "web-console", "devicon/react.svg", [".", "."]),
    ("Data Pipeline", "data-pipeline", "devicon/python.svg", ["."]),
    ("Infra Platform", "infra-platform", "devicon/kubernetes.svg", [".", "."]),
    ("Design System", "design-system", "devicon/figma.svg", ["."]),
    ("Docs Site", "docs-site", "material/menu_book.svg", ["."]),
]
ACTIVE_PROJECT = 0


def main():
    if DEMO_ROOT.exists():
        sys.exit(f"{DEMO_ROOT} already exists; remove it first")
    DEMO_ROOT.mkdir(parents=True)

    projects = []
    for title, directory_name, icon, tab_directories in PROJECTS:
        directory = DEMO_ROOT / directory_name
        if directory_name == "kmux":
            directory.symlink_to(KMUX_SOURCE_DIR)
        else:
            directory.mkdir()
        tabs = [
            # SessionRestoreId marks the widget as a terminal; without it the
            # restore code creates an empty splitter with no session.
            {"Orientation": "Horizontal", "Widgets": [{"SessionRestoreId": 0, "WorkingDirectory": str(directory / tab_directory)}]}
            for tab_directory in tab_directories
        ]
        projects.append({
            "Title": title,
            "Icon": f":/project-icons/{icon}",
            "Tabs": tabs,
            "Active": 0,
            "LastDirectory": str(directory),
        })

    state_dir = DEMO_ROOT / ".kmux-state"
    state_dir.mkdir()
    compact = dict(separators=(",", ":"))
    # Kmux rewrites the state file when it exits, so the launcher restores it
    # from this template on every start.
    (DEMO_ROOT / ".kmuxstaterc.template").write_text(
        "[LastProjectWorkspaceState]\n"
        f"Active=0\nActiveProject={ACTIVE_PROJECT}\nProjectRailWidth=260\n"
        f"Projects={json.dumps(projects, **compact)}\n"
        f"Tabs={json.dumps(projects[ACTIVE_PROJECT]['Tabs'], **compact)}\n"
    )

    for entry in LINKED_HOME_ENTRIES:
        if (REAL_HOME / entry).exists():
            (DEMO_ROOT / entry).symlink_to(REAL_HOME / entry)
    # Report the logical directory with OSC 7. Otherwise Kmux reads the
    # resolved cwd from /proc and shows ~/kmux by its long real path.
    zshrc = f'_kmux_demo_report_directory() {{ printf "\\e]7;file://%s\\a" "$PWD" }}\nprecmd_functions+=(_kmux_demo_report_directory)\n'
    if (REAL_HOME / ".zshrc").exists():
        zshrc = f'source "{REAL_HOME}/.zshrc"\n' + zshrc
    (DEMO_ROOT / ".zshrc").write_text(zshrc)

    launcher = DEMO_ROOT / "run-kmux.sh"
    launcher.write_text(f"""#!/bin/sh
# Starts a separate Kmux instance with the demo workspace.
# A private session bus keeps it apart from a running Kmux; the demo state
# directory keeps the real workspace state untouched.
demo_root=$(cd "$(dirname "$0")" && pwd)
cp "$demo_root/.kmuxstaterc.template" "$demo_root/.kmux-state/kmuxstaterc"
export XDG_CONFIG_HOME="{REAL_HOME}/.config"
export XDG_DATA_HOME="{REAL_HOME}/.local/share"
export XDG_CACHE_HOME="{REAL_HOME}/.cache"
export XDG_STATE_HOME="$demo_root/.kmux-state"
export HOME="$demo_root"
cd "$HOME"
exec dbus-run-session -- "{KMUX_BIN_DIR}/kmux" "$@"
""")
    launcher.chmod(0o755)
    print(f"Demo created in {DEMO_ROOT}\nStart it with: {launcher}")


if __name__ == "__main__":
    main()
