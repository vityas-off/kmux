
ki18n_wrap_ui(EXTRA_QUICKCOMMANDSPLUGIN_SRCS qcwidget.ui)

kcoreaddons_add_plugin(kmux_quickcommandsplugin
SOURCES
    quickcommandsplugin.cpp
    quickcommandswidget.cpp
    quickcommandsmodel.cpp
    filtermodel.cpp
    ${EXTRA_QUICKCOMMANDSPLUGIN_SRCS}
INSTALL_NAMESPACE
    "kmuxplugins"
)

configure_file(kmux_quickcommands.in.json kmux_quickcommands.json)
target_link_libraries(kmux_quickcommandsplugin
    konsoleprivate
    konsoleapp
)
