---
name: verify
description: Verify a Kmux change through the narrowest surface that exercises it - targeted autotests, Kmux in a nested KWin session, an agent's real OSC 7501 output, or agent hook installation.
---

# Verify Kmux changes

Pick the narrowest check below that exercises the changed behavior, run it
against the existing `build` directory, and report what it showed. Build first
with `cmake --build build`.

Keep everything a check generates in a temporary directory (the session
scratchpad when there is one). Never touch the user's running Kmux, its
settings or workspace state, `~/.claude`, or `~/.codex`.

## Model, status, and emulation logic

The autotests drive a real `MainWindow`, `ViewManager`, and emulation without
a display, so they cover most logic changes:

| Changed area | Tests |
| --- | --- |
| `ViewManager`, the project rail, tab and project statuses | `ViewManagerTest` |
| `ProjectWorkspaceModel` | `ProjectWorkspaceModelTest` |
| OSC 7501 parsing and records | `ProgramStatusTest`, `Vt102EmulationTest` |
| Codex usage limits | `CodexUsageLimitTest` |
| `kmux-agent-hooks` | `AgentHooksTest` |
| Project icons | `ProjectIconTest`, `ProjectIconDialogTest` |

```sh
ctest --test-dir build -R 'ViewManagerTest|ProgramStatusTest' --output-on-failure
```

Before a commit, run the whole suite as well (about 15 seconds):
`ctest --test-dir build --output-on-failure -j4`.

## UI and runtime behavior: nested KWin

`nested-kmux.py` runs `build/bin/kmux` in a virtual KWin session with
temporary XDG directories. It can type a shell command into the first
terminal over D-Bus or print OSC 7501 reports there, saves screenshots at
given times, and writes the Kmux debug log, which includes every derived
program status. Run `nested-kmux.py --help` for all options.

```sh
tmp=$(mktemp -d)
.claude/skills/verify/nested-kmux.py "$tmp/run" \
    --report 0:state=working:app=claude-code \
    --report 4:state=working:app=claude-code:id=b1 \
    --report 4:state=done:app=claude-code \
    --shot root-done:6 --duration 7
grep -o 'Program status of session.*' "$tmp/run/kmux.log"
```

Kmux always logs every change of OSC 7501 records, hook statuses, and
derived statuses in the `io.github.vityas_off.kmux.programstatus` category at
info level, so a desktop Kmux keeps them in the journal. To find why a status
stuck in the user's Kmux, read
`journalctl --user QT_CATEGORY=io.github.vityas_off.kmux.programstatus --since …`.

Look at the screenshots (`$tmp/run/<name>.png`) with the Read tool: the rail
shows `run`, `idle`, or `!` next to the project.

- `--report` waits in `cat` after its reports, because a new shell prompt
  drops working and blocked records. End a `--command` that reports such
  states with `cat` too.
- The helper enables the security-sensitive D-Bus API (`runCommand`) only in
  its temporary `kmuxrc`.
- It sets `KMUX_CLAUDE_HOOKS_DISABLED=1`. Otherwise `claude` started inside
  would add hooks that point into the temporary data directory to the real
  `~/.claude/settings.json`.
- To run Codex inside, point `CODEX_HOME` at a temporary copy of `~/.codex`
  (`auth.json`, `config.toml`, `models_cache.json`) with
  `cli_auth_credentials_store = "file"`. Otherwise Codex asks the keyring.
  `codex -m kmux-nonexistent-model "say hi"` gives a failed turn without
  using any quota.

The helper starts one project with one terminal. For flows that need clicks,
such as several projects and tabs, dragging, or menus, install the build and
ask the user to check the matching items of the UI checklist in `AGENTS.md`.

## Agent integration: real OSC 7501 output

When a behavior depends on what an agent actually sends, for example after a
Claude Code update, capture it with `osc7501-capture.py`. It runs the agent in
a pty that answers the OSC 7501 query, types one prompt, and logs every
report with its time and decoded `title` and `msg`:

```sh
.claude/skills/verify/osc7501-capture.py "$tmp/capture.log" \
    'Use the Bash tool with run_in_background set to true to run "sleep 8". Do not poll it; wait to be notified when it completes, then reply with only the word DONE.' \
    --watch 30 -- --allowedTools 'Bash(sleep:*)'
```

- `--then PROMPT` types a later prompt after the next `--watch` period, for
  states that span several turns.
- Each run is a real model turn on the user's account. Keep prompts short,
  and allow the tools that a prompt needs with `--allowedTools`.
- The agent runs with a minimal environment and without the Kmux launchers
  on `PATH`, so it does not install hooks. Stopping it at the end also stops
  its background tasks.
- The script answers Claude Code's folder trust dialog. Codex sends reports
  without the query, but the script does not handle its startup screens.

## `kmux-agent-hooks`

The hook scripts go to `$XDG_DATA_HOME/kmux/hooks`, so the data directory has
to be temporary along with the Claude home:

1. `cmake --build build --target kmux-agent-hooks`
2. `t=$(mktemp -d)`, then
   `XDG_DATA_HOME=$t/data build/bin/kmux-agent-hooks --claude-home $t/claude-home install claude`.
3. Run the same command with `status`. It should report 2 hook groups (Stop
   and StopFailure) with executable scripts. Inspect
   `$t/claude-home/settings.json`.
4. Run each generated script with representative hook JSON on stdin. Outside
   Kmux, without the `KMUX_DBUS_*` variables, it must print `{}` and exit 0.
5. Install again and check that the hook groups are not duplicated.

For Codex only `uninstall` remains. Test it against a temporary
`--codex-home`.

## Installing for the user

Install only when the user asks:

```sh
cmake --install build --prefix /home/w/kde/usr
```

This is safe while Kmux runs: the install replaces files with new inodes, so
the running Kmux keeps its old libraries. The new build takes effect after
the user restarts Kmux. Agents that should report OSC 7501 to it must be
restarted too, because Claude Code queries the terminal only at startup.
