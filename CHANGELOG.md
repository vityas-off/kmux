# Changelog

## 0.1.0-alpha.1 (unreleased)

The first public alpha of Kmux, a KDE Konsole fork with
[cmux](https://github.com/manaflow-ai/cmux)-inspired project workspaces.

### Highlights

- A vertical project rail. Every project owns its own terminal tabs and split
  views, and keeps its active tab when you switch projects.
- Projects can be renamed, reordered by drag and drop, and given icons from
  the bundled Material Symbols and Devicon sets, the KDE icon theme, or a
  custom file. Tabs can be moved between projects.
- Project entries show the working directory, tab count, activity, terminal
  notifications, and agent statuses.
- Workspace restoration: projects, tabs, split layouts, working directories,
  profiles, and tab appearance are restored after a restart. Background
  projects start their terminals when first opened.
- Status integration for Claude Code and Codex: the `claude` and `codex`
  commands inside Kmux install their hooks automatically and report running,
  idle, and needs-input states on the project and on the tab. System sleep is
  inhibited while an agent is running.
- `Shift+Enter` sends a newline by default, for multiline input in coding
  agents.
- Kmux installs next to Konsole: it uses its own application ID
  (`io.github.vityas_off.kmux`), executable, settings, data directory,
  plugin namespace, and translation domain.

### Fixes to inherited Konsole code

- Sorting more than 16 profiles could hang or crash. Konsole's comparator
  treated the built-in profile as less than itself, which `std::sort` does not
  allow.

### Installation

Kmux is distributed as source for this release; see [`BUILD.md`](BUILD.md).
Arch Linux users can build a package from `packaging/aur/kmux-workspaces` with
`makepkg -si`.

### Known limitations

Kmux is alpha software. Restoration recreates terminals rather than
checkpointing running processes, Kmux uses a single main window, and the saved
workspace format may change between alpha releases without migration. See
the [known limitations](doc/user-guide.md#known-limitations) for the complete
list, and [Resetting Workspace State](doc/user-guide.md#resetting-workspace-state)
if a restored workspace is broken.
