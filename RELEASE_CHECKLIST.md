<!--
SPDX-FileCopyrightText: 2026 Kmux contributors
SPDX-License-Identifier: CC0-1.0
-->

# Kmux release checklist

This document tracks the work required for the first public Kmux release.

Last assessment: 2026-07-14

Target release: `v0.1.0-alpha.1`

Recommended initial distribution: GitHub prerelease and AUR package

## Current assessment

Kmux is functionally close to a public alpha, but is not yet ready to be
presented as a beta or stable `0.1.0` release.

The product MVP is already substantial:

- project workspaces have independent terminal tabs;
- projects preserve active tabs, tab order, split state, profiles, and metadata;
- tabs can be moved between projects;
- project order, titles, tabs, splits, working directories, and active state are
  persisted;
- DBus and local IPC secondary-launch routing are implemented;
- agent status integration is implemented for Codex and Claude Code;
- side-by-side installation with Konsole is largely separated;
- workspace-specific behavior has dedicated regression tests.

The largest remaining gap is release engineering rather than product scope:
CI, a verified clean test run, stable application identity, release metadata,
tagging, staged installation validation, and packaging.

## Release strategy

### First public release

Publish `v0.1.0-alpha.1` as a GitHub prerelease rather than publishing an
ordinary `v0.1.0` release.

The release should provide:

- GitHub-generated source archives;
- release notes;
- documented known limitations;
- build and installation instructions;
- a dependency list;
- checksums for any manually generated artifacts;
- a link to the issue tracker.

Do not publish an archive containing only the executable and an arbitrary set
of shared libraries. Kmux installs internal libraries, Qt/KF plugins, a KPart,
desktop and AppStream metadata, notification metadata, icons, a KIO service
menu, helper programs, and shell completion. A loose binary archive is unlikely
to install or integrate these consistently.

### Initial package

After the GitHub prerelease, publish an AUR package named `kmux` built from the
immutable release tag and archive.

The stable AUR package should not build from `master`. A separate `kmux-git`
package may be added later for development snapshots.

Recommended initial CMake configuration for the AUR package:

```text
-DCMAKE_BUILD_TYPE=Release
-DBUILD_TESTING=OFF
-DWITH_KAPSULE=OFF
-DWITH_LIBSSH=ON
```

Validate the package using `makepkg`, `namcap`, and an Arch clean chroot.

### Later channels

Recommended order:

1. GitHub prerelease and source archive;
2. AUR `kmux`;
3. Flatpak/Flathub;
4. OBS/COPR or native Debian/RPM repositories if there is demand;
5. AppImage if there is demand for a portable download;
6. Snap only if a concrete need appears.

Flatpak should not be the first package because terminal emulators require broad
access to the host shell, PTYs, files, SSH configuration, and user tools. The
current development manifest also requires identity, metadata, source, and
permission work before it can be submitted to Flathub.

AppImage should not be the first package because bundling Qt/KF libraries,
plugins, KPart support, KIO integration, notifications, and global shortcut
integration requires significant cross-distribution testing.

## Alpha release gate

All items in this section should be completed before publishing
`v0.1.0-alpha.1`.

### 1. Build and test verification

- [x] Configure a clean Release build from an empty build directory.
- [x] Build the complete project successfully.
- [x] Run the complete CTest suite with `--output-on-failure`.
- [x] Confirm that all registered tests pass on the release commit.
- [x] Reproduce and fix or conclusively explain the previously recorded
      `PartTest` failure.
- [ ] Reproduce and fix or conclusively explain the previously recorded
      `TerminalInterfaceTest` failure.
- [x] Verify that tests find the built `kmuxpart` plugin in a clean build tree.
- [x] Run the AppStream validation test.
- [ ] Run at least one ASan/UBSan build manually or as a scheduled CI job.

A clean Release build in `~/kde/build/kmux` passed all 27 registered tests on
2026-07-14, including `PartTest`, `TerminalInterfaceTest`, and both registered
AppStream tests. This confirms that the built `kmuxpart` plugin is discovered
without relying on stale build-tree state. The first full run had one transient
`TerminalInterfaceTest` timeout while waiting for `currentDirectoryChanged`;
the test then passed five consecutive standalone runs and the complete rerun.
Keep tracking this shell-startup timing failure until its cause is conclusive.
The relevant tests are in:

- `src/autotests/PartTest.cpp`;
- `src/autotests/TerminalInterfaceTest.cpp`.

### 2. Minimal CI

- [ ] Add a Linux Qt 6/KF6 CI workflow.
- [ ] Configure and build a clean Release tree in CI.
- [ ] Configure a test-enabled build in CI.
- [ ] Run the complete CTest suite in CI.
- [ ] Perform a staged installation using `DESTDIR` in CI.
- [ ] Validate the main desktop file with `desktop-file-validate`.
- [ ] Validate AppStream metadata with `appstreamcli validate --pedantic`.
- [ ] Check that the expected files appear in the staged installation.
- [ ] Run CI for pull requests and commits to the main branch.
- [ ] Add release-archive and checksum automation for tags, if practical.

A single reliable Linux CI environment is sufficient for the first alpha. A
larger platform matrix can follow before beta.

### 3. Final application identity

- [x] Decide the final App ID before any public installation is distributed.
- [x] Ensure the App ID corresponds to a namespace controlled by the project.
- [x] Use `io.github.vityas_off.kmux`, matching the controlled
      `github.com/vityas-off/kmux` repository namespace.
- [x] Update the desktop filename.
- [x] Update the AppStream filename and component ID.
- [x] Update `ApplicationMetadata` constants.
- [x] Update DBus service and interface identifiers where applicable.
- [x] Update the KGlobalAccel desktop filename.
- [x] Update the Flatpak ID and manifest filename.
- [x] Update the macOS bundle identifier where applicable.
- [x] Verify that all identifiers agree after the rename.

The final App ID is `io.github.vityas_off.kmux`. It is used consistently for
the desktop and AppStream identity, DBus service and interfaces, Flatpak ID,
macOS bundle identifier, and Qt logging namespace. Changing identity after
users have installed the application would require migration of desktop
integration, settings, DBus names, and possibly sandboxed application data.

Identity locations include:

- `desktop/io.github.vityas_off.kmux.desktop`;
- `desktop/io.github.vityas_off.kmux.metainfo.xml`;
- `desktop/kmux.notifyrc`;
- `src/ApplicationMetadata.h`;
- `src/Application.h`;
- `src/ViewManager.h`;
- `src/session/Session.h`;
- `src/CMakeLists.txt`;
- `io.github.vityas_off.kmux.json`.

### 4. Release version and tag

- [ ] Confirm `0.1.0-alpha.1` as the first public version.
- [ ] Ensure all installed executables report a consistent Kmux version.
- [ ] Replace the hard-coded `1.0` version in `kmux-project-status` with the
      product version where appropriate.
- [ ] Create a Kmux-specific `v0.1.0-alpha.1` tag.
- [ ] Do not derive the Kmux package version from inherited Konsole tags.
- [ ] Create a GitHub prerelease from the tag.
- [ ] Add concise release notes or a changelog entry.
- [ ] Include the release tag or commit in useful bug-report information.

Relevant version locations include:

- `CMakeLists.txt`;
- `src/config-konsole.h.cmake`;
- `src/main.cpp`;
- `src/konsole-project-status.cpp`.

### 5. Maintainer and support metadata

- [ ] Identify the current Kmux maintainer in the About dialog.
- [ ] Identify the current Kmux maintainer in AppStream metadata.
- [ ] Update `Mainpage.dox` to distinguish Kmux maintenance from upstream
      Konsole maintenance.
- [ ] Keep Konsole authors and maintainers as upstream attribution rather than
      implying that they support Kmux.
- [ ] Add a bug tracker URL to AppStream metadata.
- [ ] Add a support or contact URL if available.
- [ ] Decide how private security reports should be submitted.

Relevant files:

- `src/main.cpp`;
- `Mainpage.dox`;
- `desktop/*.metainfo.xml`.

### 6. AppStream and desktop metadata

- [x] Rename AppStream metadata to `<app-id>.metainfo.xml` after finalizing the
      App ID.
- [ ] Add a `<releases>` entry for `0.1.0-alpha.1` with the release date.
- [ ] Add a bug tracker URL.
- [ ] Add at least one screenshot.
- [ ] Publish screenshots at a stable or immutable URL.
- [ ] Resize or prepare the existing screenshot if needed for store guidelines.
- [x] Run pedantic AppStream validation.
- [x] Validate the main application desktop file.
- [ ] Treat `kmuxrun.desktop` as a KDE service-menu file rather than passing it
      blindly through the generic desktop-file validator.
- [ ] Update notification metadata so the application is presented as Kmux,
      not Konsole.

The existing screenshot is:

- `screenshots/kmux-project-workspaces.png`.

### 7. Dependency declarations

- [ ] Make Zlib explicitly required in CMake, because `ZLIB::ZLIB` is linked
      unconditionally.
- [ ] Add Qt XML explicitly to the main Qt component lookup rather than relying
      on a transitive dependency.
- [ ] Clearly document that libssh is required when `WITH_LIBSSH=ON`.
- [ ] Decide and document whether release packages enable libssh.
- [ ] Review or remove the `WITH_X11` option if it does not actually control
      the build.
- [ ] Add Zlib and the complete Qt/KF dependency set to build documentation.
- [ ] Verify a clean configure on a system that does not already have a Konsole
      development environment installed.

Likely Arch runtime/build dependencies must be derived and verified from the
actual clean package build. They include Qt 6, KF6 components, ICU, Zlib, and
libssh when enabled.

### 8. Staged installation and side-by-side validation

- [x] Install into a clean `DESTDIR` staging directory.
- [ ] Inspect the complete install manifest.
- [ ] Confirm that no file collides with an installed Konsole package.
- [ ] Confirm that removing Kmux does not remove Konsole resources.
- [ ] Launch the installed executable rather than the build-tree executable.
- [ ] Verify installed internal libraries.
- [ ] Verify both bundled plugins.
- [ ] Verify the installed `kmuxpart` KPart.
- [ ] Verify `kmux-project-status`.
- [ ] Verify `kmux-codex` and `kmux-claude` wrappers.
- [ ] Verify `kmux-agent-hooks` installation and removal.
- [ ] Verify `kmuxprofile`.
- [ ] Verify zsh completion.
- [ ] Verify desktop menu discovery and the installed icon.
- [ ] Verify notifications and global shortcut metadata.
- [ ] Verify that Kmux and Konsole can run side by side.

The current installation surface can be reviewed in:

- `src/CMakeLists.txt`;
- `desktop/CMakeLists.txt`;
- `tools/CMakeLists.txt`;
- `build/install_manifest.txt` for the existing local build.

### 9. Licensing and source archive checks

- [ ] Run `reuse lint`.
- [ ] Add or correct licensing annotations for new Kmux files as needed.
- [ ] Check licensing for `screenshots/kmux-project-workspaces.png`.
- [ ] Confirm that release archives contain all required license files.
- [ ] Confirm that generated files and local build output are not included in
      the source release.

### 10. User-visible branding cleanup

- [ ] Replace “Start Konsole in fullscreen mode” with Kmux wording.
- [ ] Replace “Konsole View Layout” where it describes a Kmux-facing dialog.
- [ ] Update notification metadata still displayed as Konsole.
- [ ] Review other user-visible `Konsole` strings and distinguish intentional
      compatibility terminology from stale branding.
- [ ] Make Codex and Claude wrapper environment-variable naming consistent.
- [ ] If `KONSOLE_CODEX_HOOKS_DISABLED` is retained for compatibility, add and
      document a `KMUX_CODEX_HOOKS_DISABLED` alias.

Potential locations:

- `src/Application.cpp`;
- `src/ViewManager.cpp`;
- `src/konsole-codex.cpp`;
- `desktop/kmux.notifyrc`.

### 11. Known limitations and release documentation

Document all of the following in README or release notes:

- [ ] The first release is an alpha and may contain data-loss or routing bugs.
- [ ] Kmux currently uses one primary application window.
- [ ] Detaching tabs or views is disabled.
- [ ] Workspace UI is not provided in KPart mode.
- [ ] Cold restore reconstructs sessions and may restart commands; it does not
      checkpoint arbitrary running processes.
- [ ] Describe which one-shot commands are excluded from automatic restart.
- [ ] Agent status integration requires a DBus-enabled build.
- [ ] Localization is currently incomplete or English-only where applicable.
- [ ] Workspace persistence compatibility may change during alpha releases.
- [ ] Describe how users can reset corrupted workspace state.
- [ ] Describe how users can uninstall agent hooks safely.

### 12. Manual alpha smoke test

- [ ] First launch creates a project and a terminal session.
- [ ] Create several projects.
- [ ] Rename projects.
- [ ] Reorder projects with drag-and-drop.
- [ ] Close foreground and background projects.
- [ ] Create several horizontal tabs in each project.
- [ ] Confirm each project preserves its active horizontal tab.
- [ ] Create and modify split layouts in multiple projects.
- [ ] Confirm project switching does not mix or reparent tabs or splits.
- [ ] Move a tab between projects.
- [ ] Confirm closing a project does not close sessions from another project.
- [ ] Confirm existing Konsole tab shortcuts act on the active project.
- [ ] Verify keyboard focus after project switching.
- [ ] Verify activity and notification badges.
- [ ] Verify that activating a notification selects the correct project and
      terminal tab.
- [ ] Restart Kmux and verify project order and titles.
- [ ] Verify restored tabs, splits, active project, and active tab.
- [ ] Verify restored profile, working directory, command, title, colors, and
      project rail width.
- [ ] Verify secondary launch routing with DBus.
- [ ] Verify secondary launch routing in a build without DBus.
- [ ] Verify Codex agent status transitions.
- [ ] Verify Claude Code agent status transitions.
- [ ] Verify concurrent approval/input-required transitions.
- [ ] Verify stale agent state is cleared after the process exits.
- [ ] Verify graceful behavior when agent executables are absent.
- [ ] Test on Wayland.
- [ ] Test on X11 if X11 is claimed as supported.
- [ ] Dogfood the release candidate for several days after the last
      persistence, IPC, or hook change.

## Development and package test environments

Use separate tools for source development, package construction, and installed
runtime testing. No single environment covers all three reliably.

### kde-builder

`kde-builder` is useful for updating, configuring, building, testing, and
installing KDE projects from Git. It does not create AUR, Flatpak, AppImage, or
other distribution packages, and it does not replace clean package builds or
runtime testing in a fresh operating system.

The current user configuration already follows the expected KDE development
layout:

```text
source-dir: ~/kde/src
build-dir: ~/kde/build
install-dir: ~/kde/usr
```

Recommended usage:

- use the existing manual CMake build for fast day-to-day iteration;
- use `kde-builder` periodically to test against a current KDE/KF6 stack;
- use its test and install stages as an additional source-build check;
- do not treat a successful `kde-builder` install as proof that the AUR package
  is complete or reproducible;
- be aware that `include-dependencies: true` may build a substantial part of the
  KDE dependency graph rather than using only distribution packages.

The checkout is located at `~/kde/src/kmux`, while KDE project metadata uses
the name `konsole` for the upstream repository. Before running
`kde-builder konsole`, define and verify a custom project named `kmux` that
points to the Kmux repository. Keep this separate checkout path so the fork is
not confused with upstream Konsole. Always inspect the proposed actions first:

```sh
kde-builder --pretend kmux
```

Checklist:

- [ ] Define a custom `kmux` project in the kde-builder configuration.
- [ ] Ensure its repository points to the Kmux fork, not upstream Konsole.
- [ ] Ensure the intended source and build directories do not overwrite the
      active checkout.
- [ ] Run `kde-builder --pretend kmux` and inspect all proposed repositories and
      install paths.
- [ ] Decide whether Kmux should use system KF6 dependencies for normal builds
      and source-built dependencies only for periodic compatibility checks.
- [ ] Perform at least one pre-release build against the current KDE/KF6 stack.

### KDE Linux development installation

On KDE Linux, development files installed under `~/kde/usr` can be exposed over
the immutable `/usr` tree through a `systemd-sysext` development overlay. The
official development workflow starts with:

```sh
set-up-system-development
```

After installing Kmux into `/home/w/kde/usr`, refresh the overlay with:

```sh
run0 systemd-sysext refresh --always-refresh=yes
```

Temporarily disable it with:

```sh
run0 systemd-sysext unmerge
```

This is the preferred environment for daily dogfooding and KDE Linux integration
checks, including desktop discovery, icons, DBus, notifications, plugins,
KPart, global shortcuts, Wayland, and side-by-side operation with Konsole.

The overlay exposes the complete `~/kde/usr` prefix, not only Kmux. A release
smoke test performed with unrelated source-built Frameworks or Plasma components
in that prefix is not equivalent to testing against a clean KDE Linux image.
Record or clean the prefix before claiming a clean integration result.

Checklist:

- [ ] Run `set-up-system-development` if the KDE Linux development environment
      has not already been initialized.
- [ ] Confirm that Kmux installs only into `/home/w/kde/usr`, never directly
      into the immutable `/usr` prefix.
- [ ] Inspect the contents of `~/kde/usr` before a release smoke test.
- [ ] Refresh the sysext overlay after installing a release candidate.
- [ ] Verify that the installed Kmux desktop entry, icon, plugins, and KPart are
      visible through the overlay.
- [ ] Verify that unmerging the overlay removes the development installation
      without affecting the base image.
- [ ] Verify that system Konsole still works both with and without the overlay.

### Distrobox build environment

Use an Arch Linux Distrobox as a reproducible dependency and source-build
check on immutable hosts. It is useful for discovering dependencies that happen
to be installed on the main development system and for running `kde-builder`
without modifying the host image.

Distrobox is not a fully independent desktop runtime environment. It commonly
shares the host display, DBus integration, and parts of the user's home, themes,
and desktop services. Therefore it supplements but does not replace a VM for
installed-package UI testing.

Checklist:

- [ ] Create a dedicated Arch Distrobox with a separate home directory.
- [ ] Install only documented build dependencies in the container.
- [ ] Configure the custom `kmux` kde-builder project inside the container if
      source-built KDE dependencies need to be tested.
- [ ] Perform a clean source build and full CTest run in the container.
- [ ] Confirm that no undeclared host development dependency is required.
- [ ] Do not use successful GUI startup from Distrobox as the only release
      runtime test.

### Arch clean chroot

The AUR package must be built in an Arch clean chroot. This is the authoritative
check for package dependencies and reproducibility, and is more important for
AUR packaging than building the package in a general-purpose VM.

Use Arch packaging tools such as `makepkg`, `namcap`, and the appropriate
`devtools` clean-chroot command (for example `extra-x86_64-build`).

The clean chroot verifies build dependencies, package contents, ELF linkage, and
installation paths. It does not provide a complete Plasma user session and does
not replace GUI/runtime testing.

Checklist:

- [ ] Build the tagged release in an Arch clean chroot.
- [ ] Confirm that the package does not rely on undeclared host dependencies.
- [ ] Run `namcap` on the `PKGBUILD` and built package.
- [ ] Inspect package file ownership and installed paths.
- [ ] Keep clean-chroot results or CI logs for the release candidate.

### Arch Plasma virtual machine

Use a clean Arch Plasma VM with snapshots for the final AUR user journey. A VM
provides independent systemd, user DBus, Plasma, Wayland/X11, desktop databases,
KService, notifications, PTYs, shell configuration, and package state.

Recommended VM flow:

1. restore a snapshot taken before Kmux installation;
2. install the built package and its declared dependencies;
3. launch Kmux from the desktop menu and from a shell;
4. run the manual alpha smoke test;
5. verify side-by-side operation with the distribution's Konsole package;
6. upgrade from the previous Kmux package when applicable;
7. remove Kmux and inspect system leftovers;
8. verify that Konsole and the Plasma session continue to work.

Checklist:

- [ ] Prepare an Arch Plasma VM with a reusable clean snapshot.
- [ ] Test package installation without development packages already present.
- [ ] Test first launch and desktop menu discovery.
- [ ] Test Wayland behavior and, if claimed, X11 behavior.
- [ ] Test DBus, notifications, PTYs, shell startup, SSH, and agent integrations.
- [ ] Test persistence across application restarts and a VM reboot.
- [ ] Test package upgrade when a second package version exists.
- [ ] Test package removal and verify that system Konsole remains operational.

### Minimal release test matrix

For the first public alpha, use the following minimum matrix:

| Frequency | Environment | Purpose |
| --- | --- | --- |
| Every pull request | CI or clean build container | Release build, CTest, staged install, metadata validation |
| Periodically | kde-builder | Compatibility with a current KDE/KF6 source stack |
| Before packaging | Arch clean chroot | AUR dependencies, reproducibility, and package contents |
| Before publishing | Clean Arch Plasma VM | Install, runtime, integration, upgrade, and uninstall |
| Daily dogfooding | KDE Linux with user prefix/sysext | Real workspace, Wayland, DBus, restore, and agent workflows |

Do not create a large VM matrix before there are packages for those systems.
Add Fedora, openSUSE, Debian/Ubuntu, or other VMs only when Kmux claims support
for a corresponding native package or when a reproducible distribution-specific
bug must be investigated. Flatpak should be tested on KDE Linux and optionally
an additional immutable desktop once Flatpak becomes an advertised channel.

## AUR checklist

- [ ] Confirm that the `kmux` AUR package name is available immediately before
      publishing.
- [ ] Create a tagged GitHub prerelease first.
- [ ] Use the tagged source archive, not `master`.
- [ ] Pin and verify the source checksum.
- [ ] Declare the complete dependency list.
- [ ] Decide whether `libssh` is enabled and declare it consistently.
- [ ] Build in an Arch clean chroot.
- [ ] Run `namcap` on the `PKGBUILD` and built package.
- [ ] Install the package on a clean test system.
- [ ] Launch the installed application.
- [ ] Verify KPart and plugin discovery.
- [ ] Verify desktop integration and icons.
- [ ] Verify side-by-side operation with Arch's `konsole` package.
- [ ] Remove the package and check for unexpected system leftovers.
- [ ] Keep the AUR packaging history in an appropriate packaging repository.
- [ ] Optionally add a separate `kmux-git` package after the stable package is
      established.

## Flatpak/Flathub checklist

These tasks are not required for the first alpha unless Flatpak is advertised
as an initial distribution method.

- [x] Finalize the App ID.
- [x] Rename the manifest to `<app-id>.json`.
- [ ] Replace the local `dir` source with a tagged archive or fixed Git commit.
- [ ] Add a source checksum where applicable.
- [ ] Remove the unused `INSTALL_ICONS` CMake argument or implement the option.
- [x] Rename AppStream metadata to `<app-id>.metainfo.xml`.
- [ ] Add AppStream release history.
- [ ] Add screenshots and store metadata.
- [ ] Review `--device=all` and reduce it to the narrowest required permission.
- [ ] Document and justify access to `org.freedesktop.Flatpak`.
- [ ] Verify host shell and host command launching.
- [ ] Verify PTY behavior.
- [ ] Verify SSH configuration and agent access.
- [ ] Verify user shell startup files.
- [ ] Verify agent hooks and wrappers inside the Flatpak model.
- [ ] Add a Flatpak build to CI.
- [ ] Run Flathub linter checks.
- [ ] Compare permissions and behavior with the official Konsole Flatpak.
- [ ] Prepare a Flathub submission only after the above checks pass.

Current development manifest:

- `io.github.vityas_off.kmux.json`.

## Beta release gate

The following work may be deferred until after the first public alpha, but
should be completed before calling Kmux a beta.

### Platform and configuration coverage

- [ ] Test at least two Linux environments or Qt/KF version combinations.
- [ ] Verify the real minimum supported Qt and KF versions.
- [ ] Test `WITH_LIBSSH=ON` and `WITH_LIBSSH=OFF`.
- [ ] Test `USE_DBUS=ON` and `USE_DBUS=OFF`.
- [ ] Add an installable artifact beyond AUR, such as Flatpak or a maintained
      native package repository.
- [ ] Define supported distributions and display servers.

### Persistence and reliability

- [ ] Add an explicit version to the workspace persistence format.
- [ ] Add migration tests between alpha persistence formats.
- [ ] Test corrupted and partially written workspace state.
- [ ] Test crash recovery.
- [ ] Test upgrades from the previous alpha release.
- [ ] Test tens of projects and a large number of tabs.
- [ ] Test closing projects containing shells, foreground processes, SSH
      sessions, splits, and shared session views.
- [ ] Register and run the existing DBus integration test if it remains useful.
- [ ] Complete at least a week of dogfooding without data-loss or cross-project
      session-routing defects.

### Localization

- [ ] Decide whether to use a dedicated `kmux` translation domain.
- [ ] Stop accidentally depending on installed Konsole translation catalogs.
- [ ] Install Kmux translation catalogs.
- [ ] Make new project-workspace strings translatable and available to
      translators.

Current translation-domain locations include:

- `src/main.cpp`;
- `src/CMakeLists.txt`.

### Project documentation and policy

- [ ] Add `CONTRIBUTING.md`.
- [ ] Add `SECURITY.md`.
- [ ] Add a changelog or documented release-notes process.
- [ ] Document supported platforms and versions.
- [ ] Document workspace restoration and troubleshooting.
- [ ] Document profile fallback behavior.
- [ ] Document DBus and agent-hook troubleshooting.
- [ ] Document persistence reset and recovery.
- [ ] Define an alpha/beta compatibility policy.

### Release quality

- [ ] Sign the release tag if a signing workflow is adopted.
- [ ] Publish checksums for release artifacts.
- [ ] Verify desktop entry, MIME/service menu, notifications, and global
      shortcuts in installed packages.
- [ ] Verify that agent-hook uninstall only removes Kmux-owned configuration
      blocks.
- [ ] Maintain a complete changelog between public releases.

## Items that should not block the first alpha

The following improvements are valuable but should not delay the alpha if all
mandatory release-gate items pass:

- full localization;
- Flatpak/Flathub publication;
- AppImage publication;
- Snap publication;
- native Debian and RPM packaging;
- broad architectural refactoring of inherited Konsole code;
- unrelated inherited TODO/FIXME cleanup;
- support for multiple top-level windows;
- restoration of arbitrary running process state.

## Useful code locations

Workspace model and UI:

- `src/workspaces/ProjectWorkspaceModel.h`;
- `src/workspaces/ProjectWorkspaceModel.cpp`;
- `src/widgets/ProjectWorkspaceContainer.h`;
- `src/widgets/ProjectWorkspaceContainer.cpp`.

Workspace integration and persistence:

- `src/ViewManager.cpp`;
- `src/Application.cpp`;
- `src/main.cpp`.

Tests:

- `src/autotests/ProjectWorkspaceModelTest.cpp`;
- `src/autotests/ViewManagerTest.h`;
- `src/autotests/ViewManagerTest.cpp`;
- `src/autotests/ApplicationTest.cpp`;
- `src/autotests/AgentHooksTest.cpp`;
- `src/autotests/LocalActivationServerTest.cpp`;
- `src/autotests/PartTest.cpp`;
- `src/autotests/TerminalInterfaceTest.cpp`.

Packaging and metadata:

- `CMakeLists.txt`;
- `src/CMakeLists.txt`;
- `desktop/CMakeLists.txt`;
- `desktop/io.github.vityas_off.kmux.desktop`;
- `desktop/io.github.vityas_off.kmux.metainfo.xml`;
- `desktop/kmux.notifyrc`;
- `desktop/kmuxrun.desktop`;
- `io.github.vityas_off.kmux.json`;
- `REUSE.toml`.

## Suggested immediate execution order

1. Finalize the App ID.
2. Run a fresh clean build and complete CTest suite.
3. Fix any reproducible failures, especially KPart loading tests.
4. Add minimal Linux CI.
5. Correct CMake dependency declarations.
6. Update maintainer, AppStream, notification, and branding metadata.
7. Document alpha limitations.
8. Run staged-install and side-by-side smoke tests.
9. Run licensing validation.
10. Dogfood the release candidate for several days.
11. Tag and publish `v0.1.0-alpha.1` as a GitHub prerelease.
12. Build and publish the AUR `kmux` package from that tag.
13. Collect feedback before beginning Flatpak/Flathub work.
