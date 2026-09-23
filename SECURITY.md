# Security Policy

## Supported Versions

Kmux is alpha software. Security fixes are made on `master` and included in the
next release; older releases are not patched separately.

## Reporting a Vulnerability

Please do not report security issues in public GitHub issues.

Report them privately through GitHub instead: open the repository's
[Security tab](https://github.com/vityas-off/kmux/security) and choose
**Report a vulnerability**. Include the affected version (`kmux --version`),
steps to reproduce, and the impact you observed.

You should receive a first response within a week. Once a fix is available,
the report will be published as a GitHub security advisory.

## Scope

Kmux is a fork of KDE Konsole. Vulnerabilities in terminal emulation or other
code that Kmux shares with upstream Konsole should also be reported to the KDE
security team as described at <https://kde.org/info/security/>. Report issues
specific to Kmux, such as project workspaces, workspace persistence, agent hooks,
and the `kmux-*` helper tools, here.
