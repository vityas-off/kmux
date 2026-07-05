# Kmux

Kmux is a Konsole-based terminal for KDE with cmux-style project workspaces.


## HOWTO Build

1. Install dependencies. On neon:
```
apt install git cmake make g++ extra-cmake-modules libkf6config-dev libkf6auth-dev libkf6package-dev libkf6declarative-dev libkf6coreaddons-dev libkf6kcmutils-dev libkf6i18n-dev libkf6crash-dev libkf6newstuff-dev libkf6textwidgets-dev libkf6iconthemes-dev libkf6dbusaddons-dev libkf6notifyconfig-dev libkf6pty-dev libkf6notifications-dev libkf6parts-dev qt6-base-dev libqt6core6t64 libqt6widgets6 libqt6gui6 libqt6qml6 qt6-multimedia-dev libicu-dev
```
2. Clone with `git clone https://github.com/kmux-project/kmux.git`
3. Make _build_ directory: `mkdir kmux/build`
4. Change into _build_ directory: `cd kmux/build`
5. Configure: `cmake ..` (or `cmake .. -DCMAKE_INSTALL_PREFIX=/where/your/want/to/install`)
6. Build: `make`
7. Install: `make install`


