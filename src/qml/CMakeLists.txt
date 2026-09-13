set_source_files_properties(
  qml/Main.qml
  PROPERTIES QT_RESOURCE_ALIAS Main.qml
)
set_source_files_properties(
  qml/Theme.qml
  PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS Theme.qml
)

qt_add_library(lumora_qml_ui STATIC)
set_target_properties(lumora_qml_ui PROPERTIES AUTOMOC ON)
qt_add_qml_module(
  lumora_qml_ui
  URI Lumora.Workstation
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Lumora/Workstation"
  QML_FILES
    qml/Main.qml
    qml/Theme.qml
)
target_link_libraries(
  lumora_qml_ui
  PUBLIC
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
lumora_enable_warnings(lumora_qml_ui)

qt_add_executable(
  lumora_qml_app
  MANUAL_FINALIZATION
  main.cpp
)
target_link_libraries(
  lumora_qml_app
  PRIVATE
    lumora_qml_ui
    lumora_qml_uiplugin
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
lumora_enable_warnings(lumora_qml_app)
qt_import_qml_plugins(lumora_qml_app)
qt_finalize_executable(lumora_qml_app)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_custom_target(
    run-lumora-qml
    COMMAND "${CMAKE_COMMAND}" -E env
      "QT_QPA_PLATFORM=xcb"
      "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>"
      $<TARGET_FILE:lumora_qml_app>
    DEPENDS lumora_qml_app
    USES_TERMINAL
    VERBATIM
  )
endif()

# C++ renderer experiment; no live image composition in the preview scene.
add_library(lumora_quick_renderer STATIC QuickImageItem.cpp QuickImageItem.hpp)
target_include_directories(lumora_quick_renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lumora_quick_renderer PUBLIC lumora::presentation Qt6::Quick)
lumora_enable_warnings(lumora_quick_renderer)
