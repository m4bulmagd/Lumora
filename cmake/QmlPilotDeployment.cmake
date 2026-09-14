include_guard(GLOBAL)

# Included after lumora_qml_app is finalized. This is a local Linux pilot,
# separate from future installer components and platform acceptance.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()

include(GNUInstallDirs)
set_target_properties(lumora_qml_app PROPERTIES
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  INSTALL_RPATH_USE_LINK_PATH FALSE
)

install(TARGETS lumora_qml_app
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT QmlPilot
)

# Includes the scanned QML plugins, their runtime dependencies, and qt.conf.
qt_generate_deploy_qml_app_script(
  TARGET lumora_qml_app
  OUTPUT_SCRIPT lumora_qml_deploy_script
)
install(SCRIPT "${lumora_qml_deploy_script}" COMPONENT QmlPilot)

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/NOTICE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/lumora/licenses"
  COMPONENT QmlPilot
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/THIRD-PARTY-LICENSES/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/lumora/licenses/THIRD-PARTY-LICENSES"
  COMPONENT QmlPilot
)
