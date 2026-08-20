add_definitions(-DTRANSLATION_DOMAIN=\"kmux\")

### Handle DragonFlyBSD here instead of using __DragonFly__
if(${CMAKE_SYSTEM_NAME} MATCHES "DragonFly")
  set(HAVE_OS_DRAGONFLYBSD 1)
else()
  set(HAVE_OS_DRAGONFLYBSD 0)
endif()

include(ECMAddAppIcon)

configure_file(config-konsole.h.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/config-konsole.h)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XKBCOMMON QUIET xkbcommon)
    endif()
    if(XKBCOMMON_FOUND)
        message(STATUS "Found xkbcommon, enabling win32-input-mode support")
        set(KONSOLE_HAVE_XKBCOMMON TRUE)
    else()
        message(STATUS "xkbcommon not found — building without win32-input-mode support")
    endif()
else()
    message(STATUS "Not on Linux — building without win32-input-mode support")
endif()

### Tests
if(BUILD_TESTING)
        find_package(Qt6Test ${QT_MIN_VERSION} CONFIG REQUIRED)
        add_subdirectory(autotests)
        add_subdirectory(tests)
endif()

### Security concerns about reading arbitrary screen positions
option(ENABLE_DECRQCRA "Konsole: enable DEC request checksum rectangular area" OFF)

### Development tools
option(KONSOLE_BUILD_UNI2CHARACTERWIDTH "Konsole: build uni2characterwidth executable" OFF)

set(dbus_xml_srcs)
if(HAVE_DBUS)
    ### Konsole source files shared between embedded terminal and main application
    # qdbuscpp2xml -m  Session.h -o io.github.vityas_off.kmux.Session.xml
    # qdbuscpp2xml -M -s ViewManager.h -o io.github.vityas_off.kmux.Window.xml

    # Generate dbus .xml files; do not store .xml in source folder
    qt_generate_dbus_interface(ViewManager.h io.github.vityas_off.kmux.Window.xml OPTIONS -m)

    qt_add_dbus_adaptor(windowadaptors_SRCS
                        ${CMAKE_CURRENT_BINARY_DIR}/io.github.vityas_off.kmux.Window.xml
                        ViewManager.h
                        Konsole::ViewManager)

    # qdbuscpp2xml -m  Session.h -o io.github.vityas_off.kmux.Session.xml
    # Generate dbus .xml files; do not store .xml in source folder
    qt_generate_dbus_interface(session/Session.h io.github.vityas_off.kmux.Session.xml OPTIONS -m)

    qt_add_dbus_adaptor(
        sessionadaptors_SRCS
        ${CMAKE_CURRENT_BINARY_DIR}/io.github.vityas_off.kmux.Session.xml
        session/Session.h
        Konsole::Session
    )

    set(dbus_xml_srcs
        ${CMAKE_CURRENT_BINARY_DIR}/io.github.vityas_off.kmux.Session.xml
        ${CMAKE_CURRENT_BINARY_DIR}/io.github.vityas_off.kmux.Window.xml
    )
endif()

add_library(konsolelogging OBJECT)
set_target_properties(konsolelogging PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(konsolelogging PUBLIC Qt::Core)

ecm_qt_declare_logging_category(konsolelogging
    HEADER konsoledebug.h
    IDENTIFIER KonsoleDebug
    CATEGORY_NAME io.github.vityas_off.kmux
    DESCRIPTION "Kmux"
    EXPORT KONSOLE
)

add_subdirectory(colorscheme)
add_subdirectory(keyboardtranslator)
add_subdirectory(characters)
add_subdirectory(decoders)
add_subdirectory(containers)
add_subdirectory(plugins)

add_library(konsoleprivate)

target_sources(konsoleprivate PRIVATE
    ${windowadaptors_SRCS}
    BookmarkHandler.cpp
    BookmarkMenu.cpp
    CheckableSessionModel.cpp
    CopyInputDialog.cpp
    Emulation.cpp
    EscapeSequenceUrlExtractor.cpp
    FontDialog.cpp
    HistorySizeDialog.cpp
    KeyBindingEditor.cpp
    LabelsAligner.cpp
    NullProcessInfo.cpp
    PrintOptions.cpp
    ProcessInfo.cpp
    Pty.cpp
    RenameTabDialog.cpp
    SSHProcessInfo.cpp
    SaveHistoryTask.cpp
    SaveHistoryAutoTask.cpp
    Screen.cpp
    ScreenWindow.cpp
    ScrollState.cpp
    SearchHistoryTask.cpp
    ShellCommand.cpp
    ShouldApplyProperty.cpp
    UnixProcessInfo.cpp
    ViewManager.cpp
    ViewProperties.cpp
    Vt102Emulation.cpp
    KittyKeyMap.cpp
    WindowSystemInfo.cpp
    WinKeys.h
    ZModemDialog.cpp
    filterHotSpots/EscapeSequenceUrlFilter.cpp
    filterHotSpots/EscapeSequenceUrlFilterHotSpot.cpp
    filterHotSpots/FileFilter.cpp
    filterHotSpots/FileFilterHotspot.cpp
    filterHotSpots/Filter.cpp
    filterHotSpots/FilterChain.cpp
    filterHotSpots/HotSpot.cpp
    filterHotSpots/RegExpFilter.cpp
    filterHotSpots/RegExpFilterHotspot.cpp
    filterHotSpots/TerminalImageFilterChain.cpp
    filterHotSpots/UrlFilter.cpp
    filterHotSpots/UrlFilterHotspot.cpp
    filterHotSpots/ColorFilter.cpp
    filterHotSpots/ColorFilterHotSpot.cpp
    history/HistoryFile.cpp
    history/HistoryScroll.cpp
    history/HistoryScrollFile.cpp
    history/HistoryScrollNone.cpp
    history/HistoryType.cpp
    history/HistoryTypeFile.cpp
    history/HistoryTypeNone.cpp
    history/compact/CompactHistoryScroll.cpp
    history/compact/CompactHistoryType.cpp
    widgets/DetachableTabBar.cpp
    widgets/EditProfileDialog.cpp
    widgets/HistorySizeWidget.cpp
    widgets/IncrementalSearchBar.cpp
    widgets/IncrementalSearchBarSizeGrip.cpp
    widgets/ProjectWorkspaceContainer.cpp
    workspaces/AgentSleepInhibitor.cpp
    workspaces/ProjectWorkspaceModel.cpp
    widgets/RenameTabWidget.cpp
    widgets/TabTitleFormatButton.cpp

    searchtabs/SearchTabs.cpp
    searchtabs/SearchTabsModel.cpp

    terminalDisplay/extras/CompositeWidgetFocusWatcher.cpp
    terminalDisplay/extras/AutoScrollHandler.cpp
    terminalDisplay/extras/HighlightScrolledLines.cpp

    terminalDisplay/TerminalDisplay.cpp
    terminalDisplay/TerminalPainter.cpp
    terminalDisplay/TerminalScrollBar.cpp
    terminalDisplay/TerminalColor.cpp
    terminalDisplay/TerminalFonts.cpp
    terminalDisplay/TerminalBell.cpp

    profile/Profile.cpp
    profile/ProfileCommandParser.cpp
    profile/ProfileGroup.cpp
    profile/ProfileList.cpp
    profile/ProfileReader.cpp
    profile/ProfileWriter.cpp
    profile/ProfileManager.cpp
    profile/ProfileModel.cpp

    ${sessionadaptors_SRCS}
    session/Session.cpp
    session/SessionController.cpp
    session/SessionDisplayConnection.cpp
    session/SessionGroup.cpp
    session/SessionListModel.cpp
    session/SessionManager.cpp
    session/SessionTask.cpp

    widgets/TerminalDisplayAccessible.cpp
    widgets/TerminalHeaderBar.cpp
    widgets/ViewContainer.cpp
    widgets/ViewSplitter.cpp
    widgets/KonsolePrintManager.cpp

    ${dbus_xml_srcs}

    ../data/data.qrc
    ../desktop/kmux.qrc
)

if(WIN32)
    # windows conpty backend
    target_sources(konsoleprivate PRIVATE
        ptyqt/iptyprocess.cpp
        ptyqt/conptyprocess.cpp
    )
endif()

kconfig_add_kcfg_files(konsoleprivate settings/KonsoleSettings.kcfgc)

ki18n_wrap_ui(konsoleprivate
    CopyInputDialog.ui
    HistorySizeDialog.ui
    KeyBindingEditor.ui
    PrintOptions.ui       # Temporary to build session static library
    RenameTabDialog.ui
    settings/GeneralSettings.ui
    settings/MemorySettings.ui
    settings/PartInfo.ui
    settings/ProfileSettings.ui
    settings/TabBarSettings.ui
    settings/TemporaryFilesSettings.ui
    settings/ThumbnailsSettings.ui
    widgets/EditProfileAdvancedPage.ui
    widgets/EditProfileAppearancePage.ui
    widgets/EditProfileGeneralPage.ui
    widgets/EditProfileKeyboardPage.ui
    widgets/EditProfileMousePage.ui
    widgets/EditProfileScrollingPage.ui
    widgets/EditProfileTabsPage.ui
    widgets/HistorySizeWidget.ui
    widgets/RenameTabWidget.ui
)

generate_export_header(konsoleprivate)

if(KONSOLE_HAVE_XKBCOMMON)
    target_compile_definitions(konsoleprivate PUBLIC HAVE_XKBCOMMON)
    target_include_directories(konsoleprivate PUBLIC ${XKBCOMMON_INCLUDE_DIRS})
    target_link_libraries(konsoleprivate PRIVATE ${XKBCOMMON_LIBRARIES})
endif()

target_link_libraries(konsoleprivate
    PUBLIC
        KF6::ConfigCore
        KF6::XmlGui
    PRIVATE
        konsolecolorscheme
        keyboardtranslator
        konsolecharacters
        konsoledecoders
        konsolecontainers
        konsolelogging
        KF6::Bookmarks
        KF6::BookmarksWidgets
        KF6::GuiAddons
        KF6::I18n
        KF6::IconWidgets
        KF6::KIOWidgets
        KF6::NewStuffCore
        KF6::NewStuffWidgets
        KF6::Notifications
        KF6::TextWidgets
        KF6::WindowSystem
        Qt::Multimedia
        Qt::PrintSupport
        Qt::Xml
        ICU::uc
        ICU::i18n
)

if (HAVE_LIBSSH)
    target_link_libraries(konsoleprivate PRIVATE ssh)
endif()

if(NOT WIN32)
    target_link_libraries(konsoleprivate PRIVATE KF6::Pty)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    #kinfo_getfile() is in libutil
    target_link_libraries(konsoleprivate PRIVATE util)
endif()

if(HAIKU)
    target_link_libraries(konsoleprivate PRIVATE network) # for undefined reference to `inet_aton'
endif()

set_target_properties(konsoleprivate PROPERTIES
    OUTPUT_NAME kmuxprivate
    VERSION ${KMUX_VERSION}
)

install(TARGETS konsoleprivate ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

### Konsole Application

add_library(konsoleapp
    Application.cpp
    MainWindow.cpp
    settings/ConfigurationDialog.cpp
    settings/ConfigDialogButtonGroupManager.cpp
    settings/TemporaryFilesSettings.cpp
    settings/GeneralSettings.cpp
    settings/MemorySettings.cpp
    settings/ProfileSettings.cpp
    settings/TabBarSettings.cpp
    settings/ThumbnailsSettings.cpp
    pluginsystem/IKonsolePlugin.cpp
    pluginsystem/PluginManager.cpp
    delegates/ProfileShortcutDelegate.cpp
)

generate_export_header(konsoleapp)

target_link_libraries(konsoleapp
    PRIVATE
        konsoleprivate
        konsolelogging
        KF6::Bookmarks
        KF6::Crash
        KF6::I18n
        KF6::KIOWidgets
        KF6::NotifyConfig
        KF6::WindowSystem
)
if(HAVE_DBUS)
    target_link_libraries(konsoleapp
        PRIVATE
            KF6::GlobalAccel
    )
endif()

set_target_properties(konsoleapp PROPERTIES
    OUTPUT_NAME kmuxapp
    VERSION ${KMUX_VERSION}
)

install(TARGETS konsoleapp ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)


set(KMUX_GIT_REVISION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kmux-git-revision.h")
add_custom_target(kmux_git_revision_header
    COMMAND ${CMAKE_COMMAND}
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_FILE=${KMUX_GIT_REVISION_HEADER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/GenerateGitRevision.cmake"
    BYPRODUCTS "${KMUX_GIT_REVISION_HEADER}"
    VERBATIM
)
set_source_files_properties("${KMUX_GIT_REVISION_HEADER}" PROPERTIES GENERATED TRUE)

# Sets the icon on Windows and OSX
ecm_add_app_icon(ICONS_SOURCES ICONS ${KMUX_ICON_SVG} ${PLATFORM_ICONS_SRCS})

add_executable(kmux
    LocalActivationServer.cpp
    main.cpp
    ${ICONS_SOURCES}
    "${KMUX_GIT_REVISION_HEADER}"
)
add_dependencies(kmux kmux_git_revision_header)

target_link_libraries(kmux
    konsoleprivate
    konsoleapp
    KF6::CoreAddons
    KF6::Crash
    KF6::I18n
    KF6::IconThemes
    KF6::WindowSystem
    Qt::Network
)
if(HAVE_DBUS)
    target_link_libraries(kmux
        KF6::DBusAddons
    )
endif()

if(APPLE)
    set_target_properties(kmux PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.vityas_off.kmux"
        MACOSX_BUNDLE_BUNDLE_NAME "Kmux"
        MACOSX_BUNDLE_DISPLAY_NAME "Kmux"
        MACOSX_BUNDLE_INFO_STRING "Kmux, a project workspace terminal"
        MACOSX_BUNDLE_LONG_VERSION_STRING "Kmux ${KMUX_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${KMUX_VERSION_MAJOR}.${KMUX_VERSION_MINOR}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${KMUX_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "© 1997–2026 The Konsole Developers; © 2026 Kmux contributors")
endif()

install(TARGETS kmux ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES settings/konsole.kcfg
        DESTINATION ${KDE_INSTALL_KCFGDIR}
        RENAME kmux.kcfg)

if(HAVE_DBUS)
    add_executable(kmux-project-status konsole-project-status.cpp)
    ecm_mark_nongui_executable(kmux-project-status)
    target_link_libraries(kmux-project-status Qt::Core Qt::DBus)
    install(TARGETS kmux-project-status ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
    install(PROGRAMS "$<TARGET_FILE:kmux-project-status>"
            DESTINATION "${KDE_INSTALL_LIBEXECDIR}/kmux/agent-shims"
            RENAME kmux-project-status)

    add_executable(kmux-codex konsole-codex.cpp)
    ecm_mark_nongui_executable(kmux-codex)
    install(TARGETS kmux-codex ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
    install(PROGRAMS "$<TARGET_FILE:kmux-codex>"
            DESTINATION "${KDE_INSTALL_LIBEXECDIR}/kmux/agent-shims"
            RENAME codex)

    add_executable(kmux-claude konsole-codex.cpp)
    target_compile_definitions(
        kmux-claude
        PRIVATE
            KMUX_AGENT_NAME="claude"
            KMUX_AGENT_PID_ENV="KMUX_CLAUDE_PID"
            KMUX_HOOKS_DISABLED_ENV="KMUX_CLAUDE_HOOKS_DISABLED"
            KMUX_HOOKS_DISABLED_COMPAT_ENV=""
    )
    ecm_mark_nongui_executable(kmux-claude)
    install(TARGETS kmux-claude ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
    install(PROGRAMS "$<TARGET_FILE:kmux-claude>"
            DESTINATION "${KDE_INSTALL_LIBEXECDIR}/kmux/agent-shims"
            RENAME claude)

    add_executable(kmux-agent-hooks konsole-agent-hooks.cpp)
    ecm_mark_nongui_executable(kmux-agent-hooks)
    target_link_libraries(kmux-agent-hooks Qt::Core)
    install(TARGETS kmux-agent-hooks ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
    install(PROGRAMS "$<TARGET_FILE:kmux-agent-hooks>"
            DESTINATION "${KDE_INSTALL_LIBEXECDIR}/kmux/agent-shims"
            RENAME kmux-agent-hooks)

    if(BUILD_TESTING)
        add_dependencies(AgentHooksTest kmux-agent-hooks kmux-codex kmux-claude kmux-project-status)
        target_compile_definitions(
            AgentHooksTest
            PRIVATE
                KMUX_AGENT_HOOKS_EXECUTABLE="$<TARGET_FILE:kmux-agent-hooks>"
                KMUX_CODEX_EXECUTABLE="$<TARGET_FILE:kmux-codex>"
                KMUX_CLAUDE_EXECUTABLE="$<TARGET_FILE:kmux-claude>"
                KMUX_PROJECT_STATUS_EXECUTABLE="$<TARGET_FILE:kmux-project-status>"
        )
    endif()
endif()

### Embedded Konsole KPart

add_library(kmuxpart MODULE
    Part.cpp
    settings/PartInfo.cpp
    settings/ProfileSettings.cpp
    delegates/ProfileShortcutDelegate.cpp
)

target_link_libraries(kmuxpart
    konsoleprivate
    konsolelogging
    KF6::I18n
    KF6::Parts
)

install(TARGETS kmuxpart DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/parts)

# Add cmake-options -DBUILD_COVERAGE=ON -DCOVERAGE_GENERATE_HTML=ON
if(COVERAGE_GENERATE_HTML)
  # find required tools - no error checking
  find_program(LCOV lcov REQUIRED)
  find_program(GENHTML genhtml REQUIRED)

  add_custom_target(coverage
    # runs tests to get .gcda files
    COMMAND ctest --progress
    # generate data
    COMMAND ${LCOV} --directory . --capture --output-file coverage.info
    # generate report
    COMMAND ${GENHTML} --ignore-errors source --demangle-cpp -o coverage coverage.info
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
