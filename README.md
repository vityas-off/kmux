# Kmux

Kmux is a Konsole-based terminal emulator with project workspaces.

It keeps Konsole's terminal emulation foundation, profiles, color schemes,
shortcuts, split views, search, and KDE/Qt integration, while adding a
workspace model for project-oriented work:

- vertical project tabs for workspace-level switching;
- independent terminal tabs inside each project;
- per-project active tab and split/view state;
- cheap project switching without restarting terminal sessions.

## Why

Regular terminal tabs become hard to manage when work is organized by projects.
Kmux adds a project layer above normal terminal tabs so each project keeps its
own terminal context instead of sharing one global tab strip.

Kmux is not a terminal multiplexer server like tmux. It is a graphical terminal
emulator for local desktop workflows where projects, tabs, and split views
should stay visually separate.

## Relationship To Konsole

Kmux is a fork of KDE Konsole. The terminal emulator foundation remains Konsole;
the main product change is the project workspace UI.

Kmux is not an official KDE project unless explicitly accepted by KDE. KDE and
Konsole names remain the property of their respective owners. Kmux keeps the
upstream license and attribution.

## Side-By-Side Installation

Kmux is intended to install next to KDE Konsole without depending on the
distribution's `konsole` package.

The public install surface is renamed to avoid conflicts:

- binary: `kmux`;
- desktop/AppStream ID: `io.github.kmux_project.kmux`;
- config file: `kmuxrc`;
- data directory: `~/.local/share/kmux`;
- DBus environment variables: `KMUX_DBUS_*`;
- helper tools: `kmux-project-status`, `kmux-codex`, `kmux-agent-hooks`;
- plugin namespace: `kmuxplugins`.

The source still contains many internal `Konsole` class, namespace, and file
names. That is deliberate for now: it keeps the fork easier to rebase while the
installed application behaves as a standalone product.

## Build

Use the same dependency baseline as upstream Konsole: Qt 6, KDE Frameworks 6,
ECM, ICU, and the usual optional platform integrations.

```sh
cmake -S . -B build
cmake --build build
```

To run from the build tree:

```sh
./build/bin/kmux
```

For broader validation:

```sh
ctest --test-dir build --output-on-failure
```

## Packaging Notes

Kmux should be packaged as a standalone application. Do not add a runtime
dependency on the `konsole` package; depend on the needed Qt/KF6 libraries
instead.

For the first public releases, Flatpak or AppImage are good distribution
targets because they avoid distro-level file conflicts while the fork's package
metadata stabilizes.

## Source Layout

| Directory | Description |
| --- | --- |
| `src` | Application, terminal emulator integration, sessions, profiles, project workspaces, and plugins. |
| `desktop` | Desktop entry, AppStream metadata, notification config, and XMLGUI resources. |
| `data` | Bundled profiles, keyboard layouts, color schemes, and layouts. |
| `doc` | Upstream documentation sources retained for reference; Konsole handbook installation is disabled for side-by-side packaging. |
| `tests` / `src/autotests` | Upstream and fork tests. Some upstream tests still refer to Konsole names and need follow-up updates. |

## Status

The fork is being prepared for public release. Before publishing, verify that
the final project name and repository namespace are clear on major package
indexes, GitHub/GitLab, and trademark databases.
