---
name: verify
description: Verify Kmux runtime changes through the narrowest user-facing surface.
---

# Verify Kmux changes

Use the existing `build` directory and exercise the changed runtime surface directly.

For `kmux-agent-hooks` changes:

1. Build with `cmake --build build --target kmux-agent-hooks -j2`.
2. Create temporary Claude and XDG data directories with `mktemp -d`.
3. Run `build/bin/kmux-agent-hooks --claude-home <temp>/claude-home install claude`.
4. Run the matching `status` command, inspect the generated `settings.json`, and execute the generated hook script with representative JSON on stdin.
5. Probe idempotency by installing again and confirming managed hook groups are not duplicated.

Keep all generated configuration under the temporary directory. Do not modify the user's real Claude or Codex configuration.
