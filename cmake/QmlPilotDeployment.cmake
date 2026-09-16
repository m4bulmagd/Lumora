include_guard(GLOBAL)

# Local Linux staging of the sole workstation. QmlPilot is retained as the
# install component name for existing automation; it is not release acceptance.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()

include(GNUInstallDirs)
set_target_properties(lumora_app PROPERTIES
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  INSTALL_RPATH_USE_LINK_PATH FALSE
)

install(TARGETS lumora_app
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  COMPONENT QmlPilot
)

# Include the C++ camera backend even when no QML file imports QtMultimedia.
# Qt's dependency scan also deploys the FFmpeg shared libraries used by it.
qt_generate_deploy_qml_app_script(
  TARGET lumora_app
  OUTPUT_SCRIPT lumora_qml_deploy_script
  INCLUDE_PLUGINS ffmpegmediaplugin
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
