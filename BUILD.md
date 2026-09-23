# Building Kmux

Kmux builds with CMake against Qt 6 and KDE Frameworks 6. It installs next to
KDE Konsole and does not need the `konsole` package.

## Requirements

- CMake 3.16 and a C++20 compiler;
- Extra CMake Modules 6.6;
- Qt 6.5: Core, DBus, Multimedia, Network, PrintSupport, Widgets, and Xml;
- KDE Frameworks 6.6: Bookmarks, Config, ConfigWidgets, CoreAddons, Crash,
  DBusAddons, GlobalAccel, GuiAddons, I18n, IconThemes, KIO, NewStuff,
  Notifications, NotifyConfig, Parts, Pty, Service, TextWidgets,
  WidgetsAddons, WindowSystem, and XmlGui;
- ICU 61;
- libssh 0.9.8, unless libssh support is disabled (`-DWITH_LIBSSH=OFF`);
- optionally xkbcommon, found through pkg-config, for the win32-input-mode
  keyboard protocol.

### Arch Linux

This is the package set CI builds and tests with:

```sh
sudo pacman -S --needed git cmake ninja gcc pkgconf extra-cmake-modules \
    qt6-base qt6-multimedia kbookmarks kconfig kconfigwidgets kcoreaddons \
    kcrash kdbusaddons kglobalaccel kguiaddons ki18n kiconthemes kio \
    knewstuff knotifications knotifyconfig kparts kpty kservice \
    ktextwidgets kwidgetsaddons kwindowsystem kxmlgui icu libssh \
    libxkbcommon
```

To run the tests, also install `dbus` and `which`; with `appstream`
installed, CTest also validates the AppStream metadata.

To install Kmux as a package instead, build it from
`packaging/aur/kmux-workspaces` with `makepkg -si`. The `PKGBUILD` downloads
the source archive of the release tag it names.

### Other distributions

Install the development packages for the components listed above. On
Debian-based and RPM-based distributions they are usually named after the
component, for example `libkf6pty-dev` or `kf6-kpty-devel`.

## Build

```sh
git clone https://github.com/vityas-off/kmux.git
cd kmux
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
```

Build options:

| Option | Default | Description |
| --- | --- | --- |
| `WITH_LIBSSH` | `ON` on Linux and BSD | Resolve host aliases from SSH configuration in the titles of tabs running `ssh`. |
| `USE_DBUS` | `ON` on Linux and BSD | DBus interfaces, global shortcuts, agent status integration, and sleep inhibition. |
| `WITH_X11` | `ON` on Linux and BSD | X11-specific window integration. |
| `WITH_KAPSULE` | `OFF` | Kapsule container support; needs QCoro6 and Kapsule. |
| `BUILD_TESTING` | `ON` | Build the test suite. |

## Test

```sh
QT_QPA_PLATFORM=offscreen dbus-run-session -- \
    ctest --test-dir build --output-on-failure
```

Run the suite serially: `TerminalInterfaceTest` can time out when CTest runs
tests in parallel with `-j`.

## Run and install

To try the build without installing it, run `./build/bin/kmux`. Kmux runs as a
single instance: if another Kmux is already running, the new launch opens a tab
in the running instance instead of starting the build.

To install into `/usr`:

```sh
sudo cmake --install build
```

A manual installation bypasses the package manager. To remove it later,
delete the installed files:

```sh
xargs -d '\n' sudo rm -f -- < build/install_manifest.txt
```

When installing into another prefix, source `build/prefix.sh` before starting
Kmux so that Qt and KDE Frameworks find its plugins and data files.

## Packaging Notes

Kmux is designed to install next to KDE Konsole without depending on the
distribution's `konsole` package. Packagers should depend directly on the
required Qt and KDE Frameworks libraries.

The public install surface is renamed to avoid conflicts:

- binary: `kmux`;
- desktop/AppStream ID and DBus service: `io.github.vityas_off.kmux`;
- config file: `kmuxrc`; workspace state: `kmuxstaterc`;
- data directory: `~/.local/share/kmux`;
- DBus environment variables: `KMUX_DBUS_*`;
- helper tools: `kmux-project-status`, `kmux-codex`, `kmux-claude`,
  `kmux-agent-hooks`, and `kmuxprofile`;
- plugin namespace: `kmuxplugins`; terminal part: `kmuxpart`;
- translation domain: `kmux`.

The source still contains many internal `Konsole` class, namespace, and file
names. That is deliberate: it keeps the fork easier to rebase while the
installed application behaves as a standalone product.

## Source Layout

| Directory | Description |
| --- | --- |
| `src` | Application, terminal emulator integration, sessions, profiles, project workspaces, and plugins. |
| `desktop` | Desktop entry, AppStream metadata, notification config, and XMLGUI resources. |
| `data` | Bundled profiles, keyboard layouts, color schemes, layouts, and project icons. |
| `doc` | The Kmux user guide, and upstream Konsole documentation sources kept for reference; the Konsole handbook is not installed. |
| `po` | Translation catalogs inherited from Konsole. |
| `packaging` | Arch Linux `PKGBUILD` and a script to test it before a release tag exists. |
| `tools` | `kmuxprofile`, the CI scripts, and the screenshot demo workspace. |
| `tests` / `src/autotests` | Upstream and fork tests. Some upstream tests still refer to Konsole names and need follow-up updates. |
