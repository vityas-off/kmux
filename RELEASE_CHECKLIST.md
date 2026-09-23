# Kmux release checklist

This document tracks the work required for the first public Kmux release.

Last assessment: 2026-09-23 (development-tree build, test, staged install, and
metadata validator evidence; the last clean-tree evidence is from 2026-07-18)

Checklist reconciled with `master` tip `d2657c299` on 2026-09-23.

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
- background projects restore their tabs lazily on first activation;
- agent status integration is implemented for Codex and Claude Code, and is
  shown on both project entries and terminal tabs;
- projects have persistent icons with bundled Material Symbols and Devicon
  sets, KDE theme icons, and custom files;
- side-by-side installation with Konsole is largely separated;
- workspace-specific behavior has dedicated regression tests.

The product code is ready for release-engineering work to proceed in parallel.

On 2026-09-23 the development build tree (`build/`, RelWithDebInfo, testing,
DBus, X11, and libssh enabled) was rebuilt at `d2657c299` and all 29 registered
CTest tests passed, including `PartTest`, `TerminalInterfaceTest`,
`ViewManagerTest`, and `appstreamtest`. A `DESTDIR` installation from that tree
produced 129 files: 35 application files plus 94 translation catalogs. This is
not a clean-tree result; a clean Release build must still be repeated, ideally
by CI.

The previous clean evidence follows. The `master` commit `1c0aca821` was
verified on 2026-07-18 with a new
clean Release build configured with testing, DBus, X11, and libssh enabled. The
complete build succeeded and all 27 registered CTest tests passed, including
`PartTest`, `TerminalInterfaceTest`, and the workspace regression tests. The
main executable reports `0.1.0` plus the source commit, and the version-reporting
helper tools report `0.1.0`.

A clean `DESTDIR` installation also succeeded and produced 28 installed files:
the application and helper executables, two internal libraries, two bundled
plugins, the KPart, desktop and AppStream metadata, KGlobalAccel and notification
metadata, the KIO service menu, icons, logging metadata, and zsh completion. No
staged file path collided with a file already present on the development host.
This is a useful preliminary check, but it does not replace comparing package
ownership with a distribution's Konsole package or testing installation and
removal in a clean VM.

Between 2026-07-21 and 2026-09-23 `master` advanced from `5043259d` to
`d2657c299`. Relevant changes in that range:

- an upstream Konsole merge (`236fbbd52`) switched the project to C++20, raised
  the minimum KF version to 6.6, and dropped the Zlib dependency;
- lazy restoration of background project tabs, restored-action binding to the
  owning project container, and keeping the window open while deferred projects
  remain;
- agent status on terminal tabs, many Claude Code lifecycle fixes, and sleep
  inhibition while agents run;
- persistent project icons, including bundled third-party icon sets with their
  license texts installed under `share/kmux/licenses`;
- plugin discovery from the configured plugin directory, and preservation of
  existing profile files when saving.

None of these commits changed release-gate items directly, but they touched
persistence and agent hooks. The full CTest suite and the staged installation
must be re-run on the final release commit, and the dogfooding window restarts
after the most recent persistence, IPC, or hook change.

The remaining alpha blockers are a short code/metadata/documentation cleanup
and release engineering rather than new product scope:

- complete clean-package, installed-runtime, side-by-side, and manual smoke
  testing before tagging;
- dogfood the release candidate;
- tag the release and set its date in AppStream and `CHANGELOG.md`.

CI, the version and maintainer decisions, attribution, AppStream metadata,
documentation, licensing, and the sanitizer run were completed on 2026-09-23.

Infrastructure preparation can start now. In particular, create or verify the
AUR maintainer account, add CI, prepare an Arch clean chroot, prepare one clean
Arch Plasma VM, and draft the `PKGBUILD`. Do not publish the stable AUR package
or finalize its source checksum until the GitHub prerelease tag exists. Native
Debian/RPM packaging remains a later channel and should not delay the first
GitHub/AUR alpha.

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

After the GitHub prerelease, publish an AUR package named `kmux-workspaces`
built from the immutable release tag and archive. The AUR already contains an
unrelated `kmux-git` package (`futpib/kmux`), so the plain `kmux` name would
suggest a relationship that does not exist. The package name does not need to
match the display name; the application remains Kmux. The package must declare
explicit `conflicts` for unrelated packages that install the same file paths.

The stable AUR package should not build from `master`. A development-snapshot
package, if added later, must use a distinct name such as
`kmux-workspaces-git` rather than `kmux-git`.

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
2. AUR `kmux-workspaces`;
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
- [ ] Re-run all registered tests and confirm they pass on the final release
      commit after the remaining metadata and documentation changes.
- [x] Reproduce and fix or conclusively explain the previously recorded
      `PartTest` failure.
- [x] Reproduce and fix or conclusively explain the previously recorded
      `TerminalInterfaceTest` failure.
- [x] Verify that tests find the built `kmuxpart` plugin in a clean build tree.
- [x] Run the AppStream validation test.
- [x] Run at least one ASan/UBSan build manually or as a scheduled CI job.

A clean Release build in `~/kde/build/kmux` passed all 27 registered tests on
2026-07-14, including `PartTest`, `TerminalInterfaceTest`, and both registered
AppStream tests. A second clean Release build from current `master` in
`/tmp/kmux-release-audit` passed all 27 tests on 2026-07-18. This confirms that
the built `kmuxpart` plugin is discovered without relying on stale build-tree
state. The first 2026-07-14 full run had one transient `TerminalInterfaceTest`
timeout while waiting for `currentDirectoryChanged`; the test then passed five
consecutive standalone runs, the complete rerun, and the 2026-07-18 clean run.
Keep tracking this shell-startup timing failure in CI until its cause is
conclusive or it has a sufficiently stable history. On 2026-09-23 it passed in
serial runs and five consecutive standalone runs. It failed in five of six
`ctest -j16` runs, always at `TerminalInterfaceTest.cpp:200`: no
`currentDirectoryChanged` arrived within the five one-second attempts. The
failure is not tied to one neighboring test; pairing it with `PartTest`,
`ViewManagerTest`, or `ProcessInfoTest` under `-j2` also failed occasionally.
Interactive `zsh` startup on the host takes about 30 ms, so slow shell startup
is not the cause. CI runs CTest serially for now.

Explained and fixed on 2026-09-23. The tests started the developer's login
shell with the real `HOME` and startup files. The KDE Linux default zsh
configuration sets `HISTFILE=~/.histfile`; zsh locks that file, and a shell
that a parallel test kills while it holds the lock leaves a fresh lock behind.
An interactive zsh then waits about ten seconds for the lock before running its
first command (measured: 11.2 s with a fresh lock, 0.3 s without), longer than
the test's five one-second attempts. With the real startup files, parallel
`ctest -j16` failed in 3 of 4 runs; with an empty `ZDOTDIR`, or with the real
files but without `HISTFILE`, 8 of 8 runs passed. CI was unaffected because
root's shell there has no startup files. `src/autotests/CMakeLists.txt` now
gives every test a separate `HOME` and `ZDOTDIR` in the build tree with an
empty `.zshrc`; afterwards the serial run and 6 of 6 `ctest -j16` runs passed.
This also stops the tests from writing to the developer's shell history and
`~/.qttest`.

On 2026-09-23 a Debug build with `-DECM_ENABLE_SANITIZERS='address;undefined'`
passed the full suite with no AddressSanitizer report. UndefinedBehaviorSanitizer
found one defect in Kmux code: `ViewManager::handleSessionDestroyed()` cast the
`QObject` passed by `destroyed()` to `Session` after `~Session()` had already
run. The handler now receives the `Session` pointer captured when connecting
and uses it only as a key. With leak detection enabled, only two test-side
leaks remain: `KeyboardTranslatorManager::deleteTranslator()` deliberately
keeps the removed translator alive, as upstream does, because running
sessions may still use it, and the upstream `ViewManagerTest` fixture never
frees its temporary directory. `tools/ci/linux-sanitizers.sh` runs the
sanitizer build in CI with leak detection off and `halt_on_error=1`, since
UBSan reports would otherwise not fail a test.
The full suite must still be rerun after the remaining release commits before
the tag is created.
The relevant tests are in:

- `src/autotests/PartTest.cpp`;
- `src/autotests/TerminalInterfaceTest.cpp`.

### 2. Minimal CI

- [x] Add a Linux Qt 6/KF6 CI workflow.
- [x] Configure and build a clean Release tree in CI.
- [x] Configure a test-enabled build in CI.
- [x] Run the complete CTest suite in CI.
- [x] Perform a staged installation using `DESTDIR` in CI.
- [x] Validate the main desktop file with `desktop-file-validate`.
- [x] Validate AppStream metadata with `appstreamcli validate --pedantic`.
- [x] Check that the expected files appear in the staged installation.
- [x] Check that no staged path uses Konsole's names.
- [x] Check REUSE licensing information (`tools/ci/reuse-check.py`).
- [x] Run the test suite under ASan and UBSan (`tools/ci/linux-sanitizers.sh`).
- [x] Confirm that the workflow passes on GitHub for commits to `master`
      (run 35851499644 on `db75238b0`, 2026-09-23).
- [ ] Confirm that the workflow also runs for the first pull request.
- [ ] Add release-archive and checksum automation for tags, if practical.

A single reliable Linux CI environment is sufficient for the first alpha. A
larger platform matrix can follow before beta.

`.github/workflows/ci.yml` runs in an `archlinux:latest` container and calls
`tools/ci/linux-build.sh`, which can also be run locally from the source root.
The script configures a clean Release build with testing, DBus, X11, and libssh
enabled. It runs CTest serially under `dbus-run-session` with the `offscreen`
platform and installs into a `DESTDIR` stage. It then checks the expected
installed paths and the absence of Konsole-named paths, validates the desktop
file, and fails on any AppStream issue, including pedantic hints. Finally it
checks that the newest AppStream release matches `kmux --version`.

On 2026-09-23 the same steps passed locally in a fresh `archlinux:latest`
container with podman (Qt 6.11.2, KF 6.30): 29 of 29 tests passed and the stage
contained 129 files. The first container runs exposed two problems that the
development host had hidden:

- `ProcessInfoTest` needs `which`, which the Arch base image lacks; the workflow
  now installs it.
- `ViewManagerTest` hung in `ProfileManager::sortProfiles()`. The inherited
  upstream comparator `profileNameLessThan` returned `true` when the built-in
  profile was compared with itself. That violates the strict weak ordering
  `std::sort` requires, and with more than 16 profiles the partition step ran
  past the range. Users with many profiles could hit the same undefined
  behavior. The comparator is fixed and covered by
  `ProfileTest::testProfileNameOrderingIsStrict`. The same bug exists in
  upstream Konsole.

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

- [x] Confirm `0.1.0-alpha.1` as the first public version.
- [x] Ensure all version-reporting installed executables report a consistent
      Kmux product version.
- [x] Replace the hard-coded `1.0` version in `kmux-project-status` with the
      product version where appropriate.
- [ ] Create a Kmux-specific `v0.1.0-alpha.1` tag.
- [x] Define the Kmux product version independently of inherited Konsole tags.
- [ ] Create a GitHub prerelease from the tag.
- [x] Add concise release notes or a changelog entry (`CHANGELOG.md`).
- [x] Include the source commit in the main executable version and About data.

Current version output from the 2026-07-18 clean build is:

```text
kmux 0.1.0 (1c0aca821009)
kmux-project-status 0.1.0
kmux-agent-hooks 0.1.0
```

The embedded commit tracks the build tree. Re-record this output from the final
release build.

Decided on 2026-09-23: the application displays the full prerelease version.
`KMUX_VERSION_PRERELEASE` in `CMakeLists.txt` is `alpha.1`, and
`KMUX_DISPLAY_VERSION` (`0.1.0-alpha.1`) is used by `kmux --version`, the About
data, the helper tools, and the macOS bundle long version. `KMUX_VERSION` stays
numeric (`0.1.0`) for library and plugin versioning. After the change the
development build reports:

```text
kmux 0.1.0-alpha.1 (d2657c299359)
kmux-project-status 0.1.0-alpha.1
kmux-agent-hooks 0.1.0-alpha.1
```

CI fails if the newest AppStream `<release>` differs from the version reported
by `kmux --version`, so both must be updated together.

`CHANGELOG.md` holds the `0.1.0-alpha.1` release notes and serves as the body
of the GitHub prerelease. At tag time replace "unreleased" with the tag date,
and update its Installation section if the AUR package is published by then.

Relevant version locations include:

- `CMakeLists.txt`;
- `src/config-konsole.h.cmake`;
- `src/main.cpp`;
- `src/konsole-project-status.cpp`.

### 5. Maintainer and support metadata

- [x] Identify the current Kmux maintainer in the About dialog.
- [x] Identify the current Kmux maintainer in AppStream metadata.
- [x] Update `Mainpage.dox` to distinguish Kmux maintenance from upstream
      Konsole maintenance.
- [x] Keep Konsole authors and maintainers as upstream attribution rather than
      implying that they support Kmux.
- [x] Add a bug tracker URL to AppStream metadata.
- [x] Send the About dialog's bug reports to GitHub Issues instead of the
      inherited KDE Bugzilla default.
- [x] Add a support or contact URL if available.
- [x] Decide how private security reports should be submitted.
- [ ] Enable GitHub private vulnerability reporting in the repository settings.

Relevant files:

- `src/main.cpp`;
- `Mainpage.dox`;
- `desktop/*.metainfo.xml`.

The Kmux maintainer is `vityas-off`
(`15840124+vityas-off@users.noreply.github.com`). The About data lists the
maintainer first, relabels the inherited Konsole authors as upstream Konsole
attribution, and reports bugs to GitHub Issues; `KAboutData` otherwise defaults
to `submit@bugs.kde.org`. AppStream names `vityas-off` as developer and update
contact, and links the issue tracker, repository, and pull requests. GitHub
Issues is the support channel. Private security reports go through GitHub
private vulnerability reporting, as described in `SECURITY.md`; the feature
still has to be enabled for the repository.

### 6. AppStream and desktop metadata

- [x] Rename AppStream metadata to `<app-id>.metainfo.xml` after finalizing the
      App ID.
- [x] Add a `<releases>` entry for `0.1.0-alpha.1`.
- [ ] Set the `<release>` date to the actual tag date.
- [x] Add a bug tracker URL.
- [x] Add at least one screenshot.
- [x] Reference screenshots by an immutable URL (the `v0.1.0-alpha.1` tag).
- [x] Retake the screenshot from the current UI before tagging.
- [ ] Resize or prepare the existing screenshot if needed for store guidelines.
- [x] Pass `appstreamcli validate --pedantic` with the release metadata.
- [x] Validate the main application desktop file.
- [ ] Treat `kmuxrun.desktop` as a KDE service-menu file rather than passing it
      blindly through the generic desktop-file validator.
- [x] Update notification metadata so the application is presented as Kmux,
      not Konsole.

The existing screenshot is:

- `screenshots/kmux-project-workspaces.png`.

AppStream references it as
`https://raw.githubusercontent.com/vityas-off/kmux/v0.1.0-alpha.1/screenshots/kmux-project-workspaces.png`,
which resolves only after the tag exists. The image was retaken on 2026-09-23
(2722×1728) from the current UI. It shows project icons, agent statuses in the
project rail and on tabs, and Claude Code running in the Kmux project; the
other projects are empty neutral demo directories.
`tools/screenshot-demo/create-demo.py` recreates that workspace in `~/kmux-demo`
and starts an isolated Kmux instance for future captures. The
`<release>` entry is marked `type="development"`, which software centers treat as
a prerelease.

The main desktop file passed `desktop-file-validate` on 2026-07-18 and again
on 2026-09-23. On 2026-09-23 `appstreamcli validate --pedantic --no-net`
reported only one pedantic issue, `releases-info-missing`. After the release
entry, maintainer, URLs, and screenshot were added the same day, the pedantic
validation reports no issues. CI fails on any AppStream issue, including
pedantic hints. URL reachability warnings seen in the restricted audit environment
were caused by unavailable network access; `releases-info-missing` is the real
metadata failure that must be fixed.

### 7. Dependency declarations

- [x] Resolve the Zlib dependency declaration. Upstream Konsole dropped the
      dependency entirely (`bd96482ac`, merged in `236fbbd52`), so Zlib is no
      longer a build or runtime dependency.
- [x] Add Qt XML explicitly to the main Qt component lookup rather than relying
      on a transitive dependency.
- [x] Clearly document that libssh is required when `WITH_LIBSSH=ON`.
- [x] Decide and document that the initial AUR package enables libssh.
- [x] Confirm that `WITH_X11` controls the existing X11-specific build paths.
- [x] Add the complete Qt/KF dependency set to build documentation.
      `BUILD.md` now lists the components required by `CMakeLists.txt`, the
      Arch package set verified by CI, the build options, testing, and
      installation; the unverified inherited KDE neon `apt` command was
      removed.
- [x] Verify a clean configure on a system that does not already have a Konsole
      development environment installed (fresh `archlinux:latest` container,
      2026-09-23).

Likely Arch runtime/build dependencies must be derived and verified from the
actual clean package build. They include Qt 6 (at least 6.5), KF6 components (at
least 6.6), ICU, libssh when enabled, and optionally xkbcommon and KDocTools.

The 2026-07-18 clean configure found Qt XML and libssh as direct dependencies
(Zlib has since been dropped upstream). `WITH_X11` is consumed by `WindowSystemInfo.cpp` and
`MainWindow.cpp`, so it is not currently a dead option. The complete package
dependency list still needs to be derived in an Arch clean chroot rather than
from the development host.

### 8. Staged installation and side-by-side validation

- [x] Install into a clean `DESTDIR` staging directory.
- [x] Inspect the complete install manifest.
- [x] Confirm that no file collides with an installed Konsole package.
- [x] Confirm that removing Kmux does not remove Konsole resources.
- [x] Launch the installed executable rather than the build-tree executable.
- [x] Verify installed internal libraries.
- [x] Verify both bundled plugins.
- [x] Verify the installed `kmuxpart` KPart.
- [x] Verify `kmux-project-status`.
- [x] Verify `kmux-codex` and `kmux-claude` wrappers.
- [x] Verify `kmux-agent-hooks` installation and removal.
- [ ] Verify `kmuxprofile`.
- [x] Verify zsh completion.
- [x] Verify desktop menu discovery and the installed icon.
- [x] Verify notifications and global shortcut metadata.
- [x] Verify that Kmux and Konsole can run side by side.

The current installation surface can be reviewed in:

- `src/CMakeLists.txt`;
- `desktop/CMakeLists.txt`;
- `tools/CMakeLists.txt`;
- `build/install_manifest.txt` for the existing local build.

The 2026-07-18 staged Release installation contained 28 files and had no path
collision with files already present on the development host. The 2026-09-23
staged installation from the development tree contained 35 application files
and 94 translation catalogs. The new application files are the agent shims in
`lib/libexec/kmux/agent-shims`, the installed `kmux.kcfg`, and the bundled icon
license texts in `share/kmux/licenses`.

On 2026-09-23 the draft AUR package (129 files) was installed with `pacman -U`
next to Arch's `konsole 26.08.1-1` in a fresh `archlinux:latest` container.
pacman's file-conflict check passed. After `pacman -R kmux-workspaces`,
`pacman -Qkk konsole` reported no changes beyond the container's missing
`/usr/share/doc`, which was already missing before installation. The same day the clean-chroot package was tested in the clean Arch Plasma VM
(Plasma 6.7.5, Qt 6.11.2, KF 6.30, Wayland session). `kstart --application
io.github.vityas_off.kmux` found and launched Kmux through KService, and it
ran next to a running `konsole` with separate windows, taskbar icons, and
shell sessions. The Kmux process mapped `libkmuxapp`, `libkmuxprivate`, and
both `kmuxplugins` from `/usr/lib`, and no Konsole library. The helper tools
were checked only with `--version`; the KPart, agent wrappers, hooks,
`kmuxprofile`, zsh completion, notifications, and global shortcuts still need
functional checks.

Later on 2026-09-23 a package built from `950580b29` was installed in the clean
VM and checked over SSH, with keystrokes sent through QMP and screenshots taken
with Spectacle in the Plasma session:

- A test program found `kmuxpart` with `KPluginMetaData::findPluginById()`,
  loaded it, obtained `TerminalInterface`, and started a shell in `/tmp`.
- zsh maps `kmux` to `_kmux`, and `kmux --pro<Tab>` completes `--profile`.
- `kmux-agent-hooks install`, `status`, and `uninstall` for Claude Code and
  Codex left a pre-existing `settings.json` semantically unchanged and a
  pre-existing `config.toml` byte-identical, including a user `Stop` hook. The
  Codex `hooks.json` created by the installation remained as an empty
  `{"hooks": {}}` after uninstalling, and Claude settings without hooks gained
  an empty `"hooks"` object; uninstalling now removes both.
- Inside a Kmux terminal the `KMUX_DBUS_*` variables are set and the agent
  shims come first in `PATH`. With a stand-in `claude` later in `PATH`, the
  shim installed the hooks and passed its arguments through.
  `kmux-project-status needsInput` marked the tab, the project, and the
  taskbar entry. Without an agent, `kmux-claude` and `kmux-codex` exit with
  127 and "No such file or directory" outside Kmux. The shim inside Kmux
  reported "Too many levels of symbolic links" because it found only itself;
  it now reports "No such file or directory" as well.
- A bell in a background tab produced a notification from "Kmux" with the
  `kmux` icon and a "Show session" action.
- Projects, their order, the active project and tab, a split view, working
  directories, and the rail width survived a normal restart and a Plasma
  "log out and restart"; Plasma restarted Kmux once, without a duplicate
  restore. Background projects started their shells on first activation.

The checks found three more problems:

- With Breeze Light, the default Plasma color scheme, the selected project had
  dark text on a dark background. Fixed in `5bfb3ae72` and verified in the VM.
- Kmux declared `Ctrl+Alt+T` as its default global shortcut, the same as
  Konsole. Both components reported the key, and pressing it started only
  Konsole. Kmux no longer declares a default global shortcut; the user guide
  explains how to assign one. Not yet re-checked in the VM.
- Logging out with several terminals open shows Kmux's close confirmation and
  blocks the logout until Plasma forces it after two minutes, which lost the
  state since the last normal close. On Wayland, Qt does not report the logout
  as session saving, so the `isSavingSession()` path in `queryClose()` is not
  taken. Kmux now saves the workspace before asking; the confirmation itself
  remains and the user guide describes it.

`kmuxprofile` still needs a check.

### 9. Licensing and source archive checks

- [x] Run `reuse lint` and record the current failures.
- [x] Add or correct licensing annotations for new Kmux files as needed.
- [x] Check licensing for `screenshots/kmux-project-workspaces.png`.
- [x] Confirm that release archives contain all required license files.
- [x] Confirm that generated files and local build output are not included in
      the source release.

`reuse --no-multiprocessing lint` ran on 2026-07-18 and did not pass. It reported
copyright information for 460 of 662 files and licensing information for 385 of
662 files. On 2026-09-23 it still did not pass: copyright information for 753 of
966 files and licensing information for 686 of 966 files. It also reported
unused license texts `LGPL-2.1-only`, `LGPL-3.0-only`, and
`LicenseRef-KDE-Accepted-LGPL`. New failures include files under
`data/project-icons`. Many failures are inherited upstream assets and translations, but
new Kmux source files also lack explicit copyright lines and
`screenshots/kmux-project-workspaces.png` lacks a licensing annotation. Prefer
targeted SPDX fixes for new Kmux files plus maintainable `REUSE.toml`
annotations for accurately classified inherited files rather than manually
editing hundreds of imported files without provenance review.

Resolved on 2026-09-23 with a narrow release exception instead of a passing
`reuse lint`:

- Upstream Konsole at the last merged commit `c3cf096c4` does not pass
  `reuse lint` either: licensing information for 359 of 644 files, and the
  same three unused license texts.
- Every file Kmux created or rewrote now has SPDX tags or a `REUSE.toml`
  annotation. New C++ files had a license but no copyright line; they, and
  three files that named "Kmux Authors" or "KDE Contributors", now use
  `2026 Kmux contributors`. Kmux documentation (README, BUILD.md, AGENTS.md,
  CHANGELOG.md, SECURITY.md, this checklist, the user guide, the bug report
  form, the project icon READMEs), the verify skill, the main desktop file, the service menu, and the AppStream
  metadata (whose `metadata_license` was already CC0-1.0) are CC0-1.0 through
  one `REUSE.toml` annotation, which keeps license headers out of those
  files. The screenshot is annotated there as CC0-1.0 like the logo, and
  `project-icons.qrc` like the inherited `kmux.qrc`.
- The remaining 269 files and 3 license texts are listed in
  `tools/ci/reuse-inherited.txt`. Each one was verified against upstream: it
  exists there under the same or a renamed path (for example
  `po/*/konsole.po`), and upstream reports the same missing information, so
  Kmux did not remove any existing annotation. Kmux does not assign licenses
  to them without a provenance review.
- `tools/ci/reuse-check.py` runs in CI and fails on any licensing problem not
  in that list, and on list entries that are no longer reported, so the
  exception can only shrink.

`reuse lint` now reports licensing information for 706 of 975 files. A
`git archive` of `HEAD`, which matches GitHub's tag archive, contains
`COPYING`, `COPYING.LIB`, `COPYING.DOC`, every text in `LICENSES/`, and the
Devicon `LICENSE`, and no build output or generated files.

### 10. User-visible branding cleanup

- [x] Replace “Start Konsole in fullscreen mode” with Kmux wording.
- [x] Replace “Konsole View Layout” where it describes a Kmux-facing dialog.
- [x] Update notification metadata still displayed as Konsole.
- [ ] Review other user-visible `Konsole` strings and distinguish intentional
      compatibility terminology from stale branding.
- [x] Make Codex and Claude wrapper environment-variable naming consistent.
- [x] If `KONSOLE_CODEX_HOOKS_DISABLED` is retained for compatibility, add and
      document a `KMUX_CODEX_HOOKS_DISABLED` alias.

Potential locations:

- `src/Application.cpp`;
- `src/ViewManager.cpp`;
- `src/konsole-codex.cpp`;
- `desktop/kmux.notifyrc`.

The targeted Kmux branding fixes are complete. The broader review remains open
because inherited settings and compatibility UI still contain user-visible
Konsole wording; each occurrence must be classified as intentional compatibility
terminology or stale fork branding rather than changed mechanically.

### 11. Known limitations and release documentation

Document all of the following in README or release notes:

- [x] The first release is an alpha and may contain data-loss or routing bugs.
- [x] Kmux currently uses one primary application window.
- [x] Detaching tabs or views is disabled.
- [x] Workspace UI is not provided in KPart mode.
- [x] Cold restore reconstructs sessions and may restart commands; it does not
      checkpoint arbitrary running processes.
- [x] Describe which one-shot commands are excluded from automatic restart.
- [x] Agent status integration requires a DBus-enabled build.
- [x] Localization is currently incomplete or English-only where applicable.
- [x] Workspace persistence compatibility may change during alpha releases.
- [x] Describe how users can reset corrupted workspace state.
- [x] Describe how users can uninstall agent hooks safely.

Written on 2026-09-23. `doc/user-guide.md` has "Workspace Restoration" (what
is restored, which commands run again, lazy background projects, when the
state is saved), "Resetting Workspace State", "Known Limitations", and
"Reporting Problems" (reviewing `kmuxstaterc` before attaching it, debug
logging). README keeps a short status section with the three most important
limitations and links to the guide; `CHANGELOG.md` links to it as well. The
GitHub bug report form (`.github/ISSUE_TEMPLATE/bug_report.yml`) asks for
`kmux --version`, `kinfo` output, and the installation method.

Writing the restore documentation exposed two defects, both fixed with
`ViewManagerTest` coverage:

- A finished command in a tab held open with `--hold` or `--noclose` was saved
  and ran again on the next start. Cold restore now skips every session whose
  process has exited.
- A saved terminal without `SessionRestoreId` was restored as an empty,
  non-interactive tab. Such objects now start a shell, splitters and tabs
  without terminals are dropped, the saved active tab is kept when earlier
  tabs are dropped, and a project without usable tabs gets a default session.
  Kmux built from the fixed tree also started without crashing on unparsable
  and structurally invalid `kmuxstaterc` files.

The state is saved only when the window closes normally or the desktop
session ends; a crash loses changes since the last normal close. This is
documented as an alpha limitation.

### 12. Manual alpha smoke test

- [x] First launch creates a project and a terminal session.
- [x] Create several projects.
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
- [x] Restart Kmux and verify project order and titles.
- [x] Verify restored tabs, splits, active project, and active tab.
- [ ] Verify restored profile, working directory, command, title, colors, and
      project rail width.
- [x] Verify secondary launch routing with DBus.
- [ ] Verify secondary launch routing in a build without DBus.
- [ ] Verify Codex agent status transitions.
- [ ] Verify Claude Code agent status transitions.
- [ ] Verify concurrent approval/input-required transitions.
- [ ] Verify stale agent state is cleared after the process exits.
- [x] Verify graceful behavior when agent executables are absent.
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

- [x] Build the package from a pre-tag archive in an Arch clean chroot.
- [ ] Build the tagged release in an Arch clean chroot.
- [x] Confirm that the package does not rely on undeclared host dependencies.
- [x] Run `namcap` on the `PKGBUILD` and built package.
- [x] Inspect package file ownership and installed paths.
- [ ] Keep clean-chroot results or CI logs for the release candidate.

The clean chroot runs inside the dev base of the local Arch Plasma VM
(`kmux-arch-vm package-build [REVISION]`), using `devtools`
`extra-x86_64-build`. On 2026-09-23 it built `bb11c4ae3` successfully.
`namcap` reported only the package's own versioned libraries. Results are kept
under `~/.local/share/kmux-arch-vm/packages/`.

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

- [x] Prepare an Arch Plasma VM with a reusable clean snapshot.
- [x] Test package installation without development packages already present.
- [x] Test first launch and desktop menu discovery.
- [ ] Test Wayland behavior and, if claimed, X11 behavior. (Launch under
      Wayland checked; interactive use and X11 not yet.)
- [ ] Test DBus, notifications, PTYs, shell startup, SSH, and agent integrations.
- [x] Test persistence across application restarts and a VM reboot.
- [ ] Test package upgrade when a second package version exists.
- [x] Test package removal and verify that system Konsole remains operational.

The local VM (`kmux-arch-vm`, outside the repository) was rebuilt on
2026-09-23 from the current official Arch cloud image with two read-only
bases. The `clean` base has Arch, Plasma, `konsole`, `base-devel`, and `git`,
and no Kmux build dependencies. The `dev` base is a layer on top of it with
the Kmux build dependencies, `devtools`, and `namcap`.
`kmux-arch-vm reset --base clean|dev` starts a fresh work disk on either base.
On the clean base `pacman -U` installed the package without pulling extra
dependencies. After `pacman -R`, no Kmux files remained outside the user's
home directory, `pacman -Qkk konsole` reported no altered files, and Konsole
started normally.

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

- [ ] Create or verify the AUR maintainer account and its SSH key before the
      release window. Blocked as of 2026-09-23: AUR registration is temporarily
      closed (HTTP 503) because of automated account creation; updates are
      announced on aur-general and the Arch news feed. This blocks only the
      AUR upload. If registration stays closed at release time, the release
      notes can point Arch users to `makepkg -si` in
      `packaging/aur/kmux-workspaces/`.
- [x] Choose `kmux-workspaces` as the AUR package name, distinct from the
      unrelated existing `kmux-git` package.
- [ ] Confirm that `kmux-workspaces` is still available immediately before
      publishing.
- [x] Draft the `PKGBUILD` in `packaging/aur/kmux-workspaces/`.
- [x] Declare `conflicts` for unrelated AUR packages that install
      `/usr/bin/kmux` or other overlapping paths (`conflicts=(kmux)`; the
      package deliberately does not `provides=kmux`).
- [ ] Create a tagged GitHub prerelease first.
- [ ] Use the tagged source archive, not `master`.
- [ ] Pin and verify the source checksum.
- [x] Declare the complete dependency list.
- [x] Decide whether `libssh` is enabled (yes, see section 7).
- [x] Declare `libssh` consistently in `depends`.
- [ ] Build in an Arch clean chroot.
- [x] Run `namcap` on the `PKGBUILD` and built package.
- [x] Install the package on a clean test system.
- [x] Launch the installed application.
- [x] Verify plugin discovery.
- [x] Verify KPart discovery.
- [x] Verify desktop integration and icons.
- [x] Verify side-by-side operation with Arch's `konsole` package.
- [x] Remove the package and check for unexpected system leftovers.
- [ ] Keep the AUR packaging history in an appropriate packaging repository.
- [ ] Optionally add a separate `kmux-workspaces-git` package after the stable
      package is established.

The draft `PKGBUILD` follows Arch's `konsole` package: the same runtime
dependencies, `extra-cmake-modules` and `ninja` as build dependencies, and
`-DBUILD_TESTING=OFF -DWITH_LIBSSH=ON -DWITH_KAPSULE=OFF`. `pkgver` is the tag
with dashes removed (`0.1.0alpha.1`), which `vercmp` orders before `0.1.0`,
`0.1.0alpha.2`, and `0.1.0beta.1`. `sha256sums` stays `SKIP` until the tag
archive exists.

`packaging/aur/prepare-local-build.sh` builds the package before the tag
exists. It places the PKGBUILD next to a `git archive` of a committed revision,
laid out and named like GitHub's tag archive.

On 2026-09-23 the package was built with `makepkg` in a fresh
`archlinux:latest` container that had only `base-devel` and the declared
dependencies installed. That build found and fixed two defects:

- The project did not configure with `BUILD_TESTING=OFF`. An upstream cleanup
  removed `include(ECMMarkNonGuiExecutable)`, which `kmux-project-status`
  still needs. CI now also configures without tests.
- A source archive unpacked inside another Git repository, such as an AUR
  clone, embedded that repository's commit in `kmux --version`. The revision is
  now taken only from a repository whose top level is the source directory.

`namcap PKGBUILD` reported nothing. `namcap` on the package reported only the
same items it reports for Arch's `konsole`: the package's own versioned
libraries (not yet installed when `namcap` runs) and `sh` for the
`kmuxprofile` script. The installed `kmux --version` reports `0.1.0-alpha.1`
without a commit, as expected for an archive build.
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

- [x] Decide whether to use a dedicated `kmux` translation domain.
- [x] Stop accidentally depending on installed Konsole translation catalogs.
- [x] Install Kmux translation catalogs.
- [ ] Make new project-workspace strings translatable and available to
      translators.

The dedicated `kmux` translation domain is in place: `src/CMakeLists.txt` sets
`-DTRANSLATION_DOMAIN="kmux"`, `src/main.cpp` calls
`KLocalizedString::setApplicationDomain("kmux")`, every `po/*/konsole.po`
catalog was renamed to `kmux.po`, and `ki18n_install(po)` installs the Kmux
catalogs. What remains is confirming that new project-workspace strings are
extracted into a `kmux` message template and exposed to translators.

Current translation-domain locations include:

- `src/main.cpp`;
- `src/CMakeLists.txt`;
- `CMakeLists.txt` (`ki18n_install(po)`).

### Project documentation and policy

- [ ] Add `CONTRIBUTING.md`.
- [x] Add `SECURITY.md`.
- [x] Add a changelog or documented release-notes process (`CHANGELOG.md`).
- [ ] Document supported platforms and versions.
- [x] Document workspace restoration and troubleshooting.
- [ ] Document profile fallback behavior.
- [ ] Document DBus and agent-hook troubleshooting.
- [x] Document persistence reset and recovery.
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
- `REUSE.toml`;
- `tools/ci/reuse-check.py` and `tools/ci/reuse-inherited.txt`.

## Suggested immediate execution order

1. ~~Add minimal Linux CI for clean Release/test builds, CTest, staged install,
   desktop/AppStream validation, and install-manifest checks.~~ Done
   2026-09-23; green on GitHub.
2. Create or verify the AUR maintainer account, ~~prepare an Arch clean chroot and
   one clean Arch Plasma VM, and draft the `PKGBUILD` without publishing it.~~
   Done 2026-09-23 except the AUR account; AUR registration is temporarily
   closed, which blocks only step 11.
3. ~~Confirm how `0.1.0-alpha.1` is presented by the application and identify the
   current Kmux maintainer, support contact, and private-security-report path.~~
   Done 2026-09-23; enable GitHub private vulnerability reporting.
4. ~~Correct About/Doxygen attribution and finish AppStream release and screenshot
   metadata so the pedantic validator passes.~~ Done 2026-09-23; retake the
   screenshot and set the release date at tag time.
5. ~~Document alpha limitations, persistence reset/recovery, one-shot command
   restore behavior, support information, and release notes.~~ Done
   2026-09-23; also fixed restoring held finished commands and incomplete
   terminal state.
6. ~~Correct SPDX coverage for new Kmux files and the screenshot, add accurate
   annotations for inherited files, and make `reuse lint` pass or document a
   narrowly justified release exception.~~ Done 2026-09-23 with a documented
   exception for inherited files, enforced in CI.
7. ~~Use CI to monitor the transient `TerminalInterfaceTest` shell-startup timeout
   and harden the test if it reproduces; run at least one ASan/UBSan build.~~
   Done 2026-09-23: tests now use a separate home directory, and CI runs an
   ASan/UBSan build.
8. Build the package in the Arch clean chroot, inspect dependency and file
   ownership results, then run installed-runtime, removal, side-by-side, and
   manual alpha smoke tests in the clean Plasma VM.
9. Dogfood the release candidate for several days after the last persistence,
   IPC, or agent-hook change.
10. Tag and publish `v0.1.0-alpha.1` as a GitHub prerelease with release notes
    and checksums.
11. Build and publish the AUR `kmux-workspaces` package from that immutable tag and verified
    source checksum.
12. Collect feedback before beginning Flatpak/Flathub work. Add native DEB/RPM
    packaging later if there is demand; it is not a first-alpha blocker.
