# Kmux project

# KDE Application Version, managed by release script
set(RELEASE_SERVICE_VERSION_MAJOR "26")
set(RELEASE_SERVICE_VERSION_MINOR "07")
set(RELEASE_SERVICE_VERSION_MICRO "70")
set(RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")

# Do not increase these requirements without a merge-request or/and
# approval from maintainer(s).
# minimal requirements

# Keep this conservative so the fork remains easy to build on stable distributions.
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(QT_MIN_VERSION "6.5.0")
set(KF6_DEP_VERSION "6.0.0")

# Release script will create bugzilla versions
project(kmux VERSION ${RELEASE_SERVICE_VERSION})

find_package(ECM ${KF6_DEP_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMOptionalAddSubdirectory)
include(ECMInstallIcons)
include(ECMSetupVersion)
include(ECMMarkNonGuiExecutable)
include(ECMGenerateHeaders)
include(GenerateExportHeader)
include(FeatureSummary)
include(ECMQtDeclareLoggingCategory)
include(KDEClangFormat)
include(CheckFunctionExists)
include(CheckIncludeFiles)

# Allows passing e.g. -DECM_ENABLE_SANITIZERS='address;undefined' to cmake.
include(ECMEnableSanitizers)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED
    Core
    Multimedia
    PrintSupport
    Widgets
)

find_package(KF6 ${KF6_DEP_VERSION} REQUIRED
    Bookmarks
    Config
    ConfigWidgets
    CoreAddons
    Crash
    GuiAddons
    I18n
    IconThemes
    KIO
    NewStuff
    Notifications
    NotifyConfig
    Parts
    Service
    TextWidgets
    WidgetsAddons
    WindowSystem
    XmlGui
)

if(NOT WIN32)
    find_package(KF6 ${KF6_DEP_VERSION} REQUIRED
        Pty
    )
endif()

# shall we use DBus?
# enabled per default on Linux & BSD systems
set(USE_DBUS_DEFAULT OFF)
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT HAIKU)
    set(USE_DBUS_DEFAULT ON)
endif()
option(USE_DBUS "Build components using DBus" ${USE_DBUS_DEFAULT})
if(USE_DBUS)
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED
        DBus
    )
    find_package(KF6 ${KF6_DEP_VERSION} REQUIRED
        DBusAddons
        GlobalAccel
    )
    set(HAVE_DBUS 1)
endif()

find_package(KF6DocTools ${KF6_DEP_VERSION})
set_package_properties(KF6DocTools PROPERTIES DESCRIPTION
    "Tools to generate documentation"
    TYPE OPTIONAL
)

find_package(ICU 61.0 COMPONENTS uc i18n REQUIRED)

option(WITH_KAPSULE "Build with Kapsule container support" OFF)
if(WITH_KAPSULE)
    find_package(QCoro6 REQUIRED COMPONENTS Core)
    find_package(Kapsule REQUIRED)
    set(HAVE_KAPSULE 1)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT HAIKU)
    option(WITH_X11 "Build with X11 integration" ON)

    option(WITH_LIBSSH "Build with libssh support for resolving SSH configuration" ON)
    if(WITH_LIBSSH)
        find_package(libssh 0.9.8 REQUIRED CONFIG)
        set_package_properties(libssh PROPERTIES DESCRIPTION "the SSH library"
                               URL "https://www.libssh.org/"
                               TYPE OPTIONAL
                               PURPOSE "Needed to resolve SSH configuration"
                              )

        set(HAVE_LIBSSH 1)
    endif()
endif()

# Check for function GETPWUID
check_symbol_exists(getpwuid "pwd.h" HAVE_GETPWUID)

check_function_exists(malloc_trim HAVE_MALLOC_TRIM)

# See above includes for defaults
add_definitions(
  -DQT_NO_FOREACH
  -DQT_STRICT_ITERATORS
  -DQT_NO_URL_CAST_FROM_STRING
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

file(GLOB ICONS_SRCS "data/icons/*.png")

option(ENABLE_PLUGIN_SSHMANAGER "Build the SSHManager plugin" ON)
option(ENABLE_PLUGIN_QUICKCOMMANDS "Build the Quick Commands plugin" ON)

add_subdirectory( src )
add_subdirectory( desktop )

# Kmux does not install Konsole's handbook to avoid colliding with the
# distribution-provided Konsole package.

add_subdirectory( tools )

# Conditionally install icons for Linux as they may not be provided by the user theme
option(INSTALL_ICONS "Install icons" OFF)
if(INSTALL_ICONS)
    include(ECMInstallIcons)
    ecm_install_icons( ICONS ${ICONS_SRCS} DESTINATION ${KDE_INSTALL_ICONDIR} )
endif()

ecm_qt_install_logging_categories(
    EXPORT KONSOLE
    FILE kmux.categories
    DESTINATION "${KDE_INSTALL_LOGGINGCATEGORIESDIR}"
)

# Do not install upstream Konsole translations/doc translations under their
# original catalog names; distro packages must be able to install side-by-side.

if(ECM_VERSION VERSION_GREATER_EQUAL 5.101.0)
  install(FILES completions/kmux.zsh RENAME _kmux DESTINATION ${KDE_INSTALL_ZSHAUTOCOMPLETEDIR})
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

# add clang-format target for all our real source files
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.c *.cpp *.h *.hpp)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
