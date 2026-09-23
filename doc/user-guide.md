# Kmux User Guide

This guide covers what the [README](../README.md) leaves out: how workspace
restoration works and how to reset it, the agent status integration, the known
limitations of the alpha, and what to include in a bug report.

## Workspace Restoration

When Kmux starts, it restores the projects from its last run: their order,
titles, and icons, the active project, and the width of the project rail.
Every project gets back its tabs, split layouts, and active tab, and every
terminal its working directory, profile, tab title and colors, encoding, and
badge.

Restoration recreates terminals; it does not checkpoint running processes.
Every restored terminal starts a new process:

- A terminal that ran your shell starts a new shell in the saved directory.
  Programs you started from that shell, such as editors, `ssh`, builds, Claude
  Code, or Codex, are not started again, and scrollback is not restored. Resume
  an agent conversation yourself, for example with `claude --continue` or
  `codex resume`.
- A terminal that was opened with a command, for example `kmux -e htop` or a
  profile with a custom command, runs that command again, but only if it was
  still running when Kmux closed.
- A finished command is not saved, even if `--hold` kept its tab open, so it
  never runs again by accident.

Projects that were in the background start their terminals when you first open
them after a restart.

Kmux saves the workspace when its window closes normally and when the desktop
session ends. If Kmux crashes or is killed, the next start restores the
workspace from the last normal close.

### Resetting Workspace State

The workspace state is stored in `~/.local/state/kmuxstaterc`
(`$XDG_STATE_HOME/kmuxstaterc` if `XDG_STATE_HOME` is set). Kmux skips saved
tabs it cannot use and opens a new shell in a project that has none left. If
the restored workspace is still broken, reset it:

1. Quit Kmux. It writes the file again when its window closes.
2. Move the file aside rather than deleting it, so it can be attached to a bug
   report:

   ```sh
   mv ~/.local/state/kmuxstaterc ~/.local/state/kmuxstaterc.old
   ```

3. Start Kmux. It opens with a single new project.

This also resets the saved window size. Settings (`~/.config/kmuxrc`),
profiles, color schemes, and custom project icons (`~/.local/share/kmux`) are
kept.

## Agent Status Integration

Kmux can show when Codex or Claude Code is running, idle, or waiting for input.
Inside Kmux terminals, the regular `codex` and `claude` commands are
transparently routed through Kmux helpers. The helpers install or repair the
matching hooks on first launch, associate status with the agent process, and
then launch the original command from the rest of `PATH`.

Hooks can also be installed or inspected explicitly:

```sh
kmux-agent-hooks install codex
kmux-agent-hooks install claude
```

The commands update the respective agent configuration under `~/.codex` or
`~/.claude`. Use `status` or `uninstall` in place of `install` to inspect or
remove the integration. Set `KMUX_CODEX_HOOKS_DISABLED=1` or
`KMUX_CLAUDE_HOOKS_DISABLED=1` to launch the agent without installing or
updating its hooks. The legacy
`KONSOLE_CODEX_HOOKS_DISABLED=1` name remains supported for compatibility.

Agent hooks communicate with the Kmux session through the
`KMUX_DBUS_*` environment exported inside Kmux terminals. These helpers are
built when DBus support is enabled.

By default, Kmux also prevents automatic system sleep while at least one
integrated agent reports that it is running. Sleep is allowed again when all
agents become idle or wait for input. This behavior can be disabled in the
General settings and requires DBus support.

Other tools and scripts can set the status of the terminal they run in with
`kmux-project-status`, for example `kmux-project-status running` at the start
of a long job and `kmux-project-status needsInput` when it waits for you.
`kmux-project-status none` clears the status.

### Shift+Enter

Kmux intentionally changes Konsole's default `Shift+Enter` binding to send a
newline (`\n`). This enables multiline input in terminal coding agents such as
Claude Code. See [Configuring Shift+Enter for Claude Code in
Konsole](https://www.reddit.com/r/ClaudeAI/comments/1nuvtwv/configuring_shiftenter_for_claude_code_in_konsole/)
for background and manual configuration details. Users who need Konsole's
upstream behavior can select or customize another key binding in the active
profile's Keyboard settings.

## Known Limitations

Kmux is alpha software. It may still contain bugs that lose the saved workspace
or show a status, notification, or new tab in the wrong project.

- Kmux uses a single main window. Starting `kmux` while it is running opens a
  new tab in the active project. There is no New Window action, and tabs and
  split views cannot be detached into separate windows.
- Restoration recreates terminals but does not checkpoint running processes;
  see [Workspace Restoration](#workspace-restoration).
- The workspace is saved only when the window closes normally or the desktop
  session ends.
- The format of the saved workspace may change between alpha releases without
  migration. If an upgrade restores projects incorrectly,
  [reset the workspace state](#resetting-workspace-state).
- Agent status integration and sleep inhibition require a build with DBus
  support.
- The embeddable terminal part, `kmuxpart`, provides plain terminals without
  the project rail. Applications such as Dolphin and Kate continue to embed
  Konsole's own part.
- The project rail, project icons, and agent statuses are available in English
  only. The rest of the interface uses the translations inherited from Konsole.
- Kmux is tested mainly on Arch Linux and KDE Linux with Plasma on Wayland. The
  X11 integration is built but has had less testing.

## Reporting Problems

Report bugs with the bug report form in
[GitHub Issues](https://github.com/vityas-off/kmux/issues/new/choose); it asks
for the Kmux version, your system, and how Kmux was installed.

For a restore problem, a copy of `~/.local/state/kmuxstaterc` helps. Review it
before attaching it: it contains your project titles, working directories, and
the commands, arguments, and profile environment variables of your terminals.

To collect debug output, quit Kmux and start it from another terminal with
`QT_LOGGING_RULES='io.github.vityas_off.kmux*.debug=true' kmux`.
