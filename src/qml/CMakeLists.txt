# The proven C++ sink remains usable independently of QML composition.
add_library(lumora_quick_renderer STATIC QuickImageItem.cpp QuickImageItem.hpp)
target_include_directories(lumora_quick_renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lumora_quick_renderer PUBLIC lumora::presentation Qt6::Quick)
lumora_enable_warnings(lumora_quick_renderer)

set(lumora_qml_files
  qml/Main.qml qml/Theme.qml qml/CameraStartup.qml qml/CameraSettings.qml qml/ViewingToolbar.qml
  qml/InstallationSettings.qml qml/OrientationPreview.qml
  qml/PresetControls.qml qml/WindowLevelControls.qml qml/StatusStrip.qml
  qml/ExactProcessingField.qml qml/ToneControls.qml qml/ProcessingControls.qml
  qml/LocalContrastControls.qml qml/DenoiseControls.qml qml/SharpenControls.qml)
foreach(file IN LISTS lumora_qml_files)
  get_filename_component(name "${file}" NAME)
  set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(lumora_qml_ui STATIC)
set_target_properties(lumora_qml_ui PROPERTIES AUTOMOC ON)
qt_add_qml_module(lumora_qml_ui
  URI Lumora.Workstation VERSION 1.0 RESOURCE_PREFIX /qt/qml
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Lumora/Workstation"
  QML_FILES ${lumora_qml_files}
  SOURCES
    QmlWorkstation.hpp QmlWorkstation.cpp CameraAdapter.hpp CameraAdapter.cpp
    CameraSettingsAdapter.hpp CameraSettingsAdapter.cpp
    InstallationAdapter.hpp InstallationAdapter.cpp
    LayoutAdapter.hpp LayoutAdapter.cpp
    ViewerAdapter.hpp ViewerAdapter.cpp ViewerSurface.hpp ViewerSurface.cpp
    ProcessingAdapter.hpp ProcessingAdapter.cpp)
if(MSVC)
  # Qt 6.11.1 cachegen instantiates Qt templates that trigger C4702 under /W4.
  # Keep /WX and all warnings on handwritten sources; exempt only this warning
  # in the generated QML cache translation units.
  get_target_property(lumora_qml_sources lumora_qml_ui SOURCES)
  foreach(source IN LISTS lumora_qml_sources)
    if(source MATCHES "/[.]rcc/qmlcache/.*[.]cpp$")
      set_property(SOURCE "${source}" APPEND PROPERTY COMPILE_OPTIONS /wd4702)
    endif()
  endforeach()
endif()
target_include_directories(lumora_qml_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lumora_qml_ui PUBLIC
  lumora_quick_renderer lumora::presentation lumora::configuration
  Qt6::Qml Qt6::Quick Qt6::QuickControls2)
lumora_enable_warnings(lumora_qml_ui)

qt_add_executable(lumora_app MANUAL_FINALIZATION
  main.cpp ../app/SimulatorComposition.cpp ../app/SimulatorComposition.hpp)
target_include_directories(lumora_app PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(lumora_app PRIVATE
  lumora_qml_ui lumora_qml_uiplugin lumora::camera_simulator lumora::diagnostics)
lumora_enable_warnings(lumora_app)
qt_import_qml_plugins(lumora_app)
qt_finalize_executable(lumora_app)
# Keep the old build target as an alias for automation; no second executable.
add_custom_target(lumora_qml_app DEPENDS lumora_app)
include("${PROJECT_SOURCE_DIR}/cmake/QmlPilotDeployment.cmake")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_custom_target(run-lumora-qml
    COMMAND "${CMAKE_COMMAND}" -E env
      "QT_QPA_PLATFORM=xcb"
      "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>"
      $<TARGET_FILE:lumora_app>
    DEPENDS lumora_app USES_TERMINAL VERBATIM)
endif()
