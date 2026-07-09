# Agent Instructions

This repository is a new fork of KDE Konsole. The product direction is a
Konsole-based terminal with a cmux-style workspace model:

- A vertical side tab bar represents project-level workspaces.
- Each project workspace owns its own independent set of regular horizontal
  terminal tabs.
- Switching a side tab must switch the active project context without mixing or
  reparenting that project's horizontal tabs into another project.

Treat upstream Konsole as the foundation. Keep terminal emulation, session
handling, profiles, plugins, keyboard behavior, and KDE integration compatible
unless a change is explicitly part of the fork's workspace UI.

## Product Model

- Use "project" for the side-tab level and "tab" for the horizontal terminal
  level. Avoid ambiguous names such as "group" unless the surrounding Konsole
  code already requires them.
- A project is a UI/workspace container, not a terminal session. Terminal
  sessions remain owned by regular tabs.
- Each project must preserve its own active tab, tab order, split/view state,
  current profile choices, and persisted metadata when persistence is added.
- Project switching should feel cheap: keep model changes separate from terminal
  process lifecycle changes.
- Prefer predictable, dense terminal UI over marketing-style or decorative
  screens.

## Architecture Guidelines

- Follow existing Konsole and KDE Frameworks patterns before adding new
  abstractions.
- Keep workspace/project state in a model layer that can be tested without
  rendering widgets.
- `ProjectWorkspaceModel` owns project IDs, order, titles, summaries, statuses,
  notifications, and default numbering. `ProjectWorkspaceContainer` only maps
  those IDs to tab containers and renders/binds the project rail.
- Keep UI widgets thin: they should bind actions and display state, not own core
  project/tab behavior.
- Do not duplicate terminal session management for projects. Reuse existing tab,
  view, session, and profile machinery where possible.
- Preserve DBus, plugin, and embedding behavior unless the fork has a deliberate
  replacement.
- When adding persistence, prefer explicit structured state over parsing UI text
  or relying on object names.
- Avoid broad refactors while implementing workspace features. Make small,
  reviewable changes that keep upstream merges plausible.

## Coding Style

- Use C++17 and Qt/KF6 idioms already present in the touched files.
- Match local formatting and naming. Existing private members commonly use a
  leading underscore.
- Use descriptive names; avoid short abbreviations for project/tab/workspace
  state.
- Prefer enums, constants, helper methods, and typed data structures over macros
  or magic literals.
- Add comments only when they explain non-obvious behavior or invariants.
- Keep user-facing strings translatable with the existing KDE i18n patterns.

## Validation

Use the narrowest useful verification for the change. Typical commands are:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

If the local build directory uses a different name, adapt the path rather than
creating unrelated build metadata in the source tree.

For UI changes, manually verify at least:

- creating multiple project side tabs;
- creating multiple horizontal tabs inside each project;
- switching between projects preserves each project's active horizontal tab;
- closing a project does not leak or close sessions from another project;
- existing Konsole tab shortcuts and actions still operate on the active
  project's horizontal tabs.

## Repository Hygiene

- Do not overwrite unrelated local changes.
- Do not introduce generated files or build output into the source tree.
- Keep changes scoped to the requested feature or fix.
- Update this file when the fork's terminology, architecture, or validation
  workflow becomes more concrete.
