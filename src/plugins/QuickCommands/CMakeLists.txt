
kcoreaddons_add_plugin(kmux_quickcommandsplugin
SOURCES
    quickcommandsplugin.cpp
    quickcommandswidget.cpp
    quickcommandsmodel.cpp
    filtermodel.cpp
INSTALL_NAMESPACE
    "kmuxplugins"
)

ki18n_wrap_ui(kmux_quickcommandsplugin qcwidget.ui)

configure_file(kmux_quickcommands.in.json kmux_quickcommands.json)
target_link_libraries(kmux_quickcommandsplugin
    konsoleprivate
    konsoleapp
    konsolelogging
    KF6::CoreAddons
    KF6::I18n
)
