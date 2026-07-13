
ki18n_wrap_ui(extra_sshplugin_SRCS sshwidget.ui)

ecm_qt_declare_logging_category(
    extra_sshplugin_SRCS
    HEADER sshmanagerplugindebug.h
    IDENTIFIER SshManagerPluginDebug
    CATEGORY_NAME io.github.vityas_off.kmux.plugin.sshmanager
    DESCRIPTION "Kmux plugin (SSH Manager)"
    EXPORT KONSOLE
)

kcoreaddons_add_plugin(kmux_sshmanagerplugin
SOURCES
    sshmanagerplugin.cpp
    sshmanagerpluginwidget.cpp
    sshmanagermodel.cpp
    sshmanagerfiltermodel.cpp
    sshtreeview.cpp
    ${extra_sshplugin_SRCS}
INSTALL_NAMESPACE
    "kmuxplugins"
)

configure_file(kmux_sshmanager.in.json kmux_sshmanager.json)
target_link_libraries(kmux_sshmanagerplugin
    konsoleprivate
    konsoleapp
)
