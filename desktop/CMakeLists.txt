

########### install files ###############

install( PROGRAMS io.github.kmux_project.kmux.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
install( FILES io.github.kmux_project.kmux.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
install(FILES io.github.kmux_project.kmux.desktop DESTINATION ${KDE_INSTALL_DATADIR}/kglobalaccel)

install(FILES kmux.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install( PROGRAMS kmuxrun.desktop DESTINATION ${KDE_INSTALL_DATADIR}/kio/servicemenus )
