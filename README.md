<img src="logo.png" alt="Kmux logo" width="96" align="right">

# Kmux

Kmux is a terminal for KDE Plasma that groups your terminals by project. It is
a fork of [Konsole](https://apps.kde.org/konsole/) with a
[cmux](https://cmux.com/)-inspired project rail: every project keeps its own
tabs and split views, and switching projects never stops a running session.

![Kmux project workspace UI](screenshots/kmux-project-workspaces.png)

## Features

- a vertical project rail with renaming, drag-and-drop ordering, icons, and
  keyboard navigation;
- independent tabs and split views in every project; tabs can be moved between
  projects;
- activity indicators, notifications, and Claude Code and Codex statuses
  (running, idle, needs input) on projects and tabs;
- restoration of projects, tabs, splits, and working directories after a
  restart;
- everything else from Konsole: profiles, color schemes, shortcuts, search,
  plugins, and KDE integration;
- installs next to Konsole without conflicts.

## Status

Kmux is alpha software, currently 0.1.0-alpha.1. Expect bugs, including ones
that lose the saved workspace. Most notably:

- restoring a workspace starts new shells; it does not bring back the programs
  that were running in them;
- Kmux uses a single main window;
- the format of the saved workspace may change between alpha releases.

The [user guide](doc/user-guide.md#known-limitations) lists all known
limitations.

## Installation

There are no binary packages yet, so Kmux is built from source. It needs CMake
3.16, a C++20 compiler, Qt 6.5, KDE Frameworks 6.6, ICU, and, by default,
libssh; [`BUILD.md`](BUILD.md) lists the exact components and the matching Arch
Linux packages.

```sh
git clone https://github.com/vityas-off/kmux.git
cd kmux
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
sudo cmake --install build
```

Kmux installs next to Konsole. [`BUILD.md`](BUILD.md) also covers build
options, running the tests, and removing a manual installation.

On Arch Linux, you can build a package instead, so that pacman tracks the
installed files. An AUR package named `kmux-workspaces` is planned.

```sh
cd packaging/aur/kmux-workspaces
makepkg -si
```

The `PKGBUILD` builds the release tag it names, not your checkout.

## Getting Started

Add a project with **Add Project** on the toolbar, from the context menu of the
project rail, or with `Ctrl+Alt+P`. Double-click a project to rename it; its
context menu changes the icon or closes it. Tabs and split views work as in
Konsole, and **Move Tab to Project** in a tab's context menu moves it to
another project.

| Shortcut | Action |
| --- | --- |
| `Ctrl+Alt+P` | Add a project |
| `Ctrl+Alt+PgDown` / `Ctrl+Alt+PgUp` | Next / previous project |
| `Ctrl+Alt+1` … `Ctrl+Alt+9` | Switch to project 1–9 |
| `Ctrl+Alt+A` | Next project needing attention |

Kmux leaves `Ctrl+Alt+T` to Konsole; the
[user guide](doc/user-guide.md#global-shortcut) explains how to assign a global
shortcut to Kmux.

Inside Kmux, `claude` and `codex` report their status without any setup: Kmux
installs their hooks the first time you run them. The
[user guide](doc/user-guide.md) explains the agent integration, what
workspace restoration brings back, and how to reset a broken workspace.

## Alternatives

- [cmux](https://github.com/manaflow-ai/cmux), which inspired Kmux: a
  Ghostty-based terminal with vertical tabs, for macOS only.
- [herdr](https://herdr.dev/): an agent-aware terminal multiplexer for Linux
  and macOS that keeps agents running after you detach.
- [tmux](https://github.com/tmux/tmux) or [Zellij](https://zellij.dev/), if you
  mainly need persistent sessions.

## Support and Contributing

Report bugs in [GitHub Issues](https://github.com/vityas-off/kmux/issues) and
send changes as pull requests. Kmux is not supported by KDE, so please do not
report Kmux problems to KDE unless they also happen in Konsole. Report
security issues privately as described in [`SECURITY.md`](SECURITY.md).

## Acknowledgements and License

Kmux is built on [KDE Konsole](https://invent.kde.org/utilities/konsole) and
keeps its licenses; see [`COPYING`](COPYING), [`COPYING.LIB`](COPYING.LIB), and
[`COPYING.DOC`](COPYING.DOC). Its project model is inspired by cmux. Kmux is an
independent project, not affiliated with KDE or cmux, whose names belong to
their respective owners.
