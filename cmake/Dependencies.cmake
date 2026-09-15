include_guard(GLOBAL)

macro(lumora_find_dependencies)
  if(LUMORA_BUILD_QML_UI)
    set(lumora_qt_version 6.11.1)
    set(lumora_qt_components Core Gui Qml Quick QuickControls2)
    if(LUMORA_BUILD_LEGACY_WIDGETS_TESTS)
      list(APPEND lumora_qt_components Widgets)
    endif()
    if(LUMORA_BUILD_TESTS)
      list(APPEND lumora_qt_components Test QuickTest)
    endif()

    find_package(
      Qt6 ${lumora_qt_version} EXACT REQUIRED
      COMPONENTS ${lumora_qt_components}
    )

    if(NOT Qt6Core_VERSION VERSION_EQUAL lumora_qt_version)
      message(FATAL_ERROR
        "LUMORA_BUILD_QML_UI requires Qt ${lumora_qt_version} exactly, "
        "but Qt Core ${Qt6Core_VERSION} was resolved.")
    endif()

    foreach(lumora_qt_component IN LISTS lumora_qt_components)
      set(lumora_qt_component_version_var "Qt6${lumora_qt_component}_VERSION")
      if(NOT DEFINED ${lumora_qt_component_version_var})
        message(FATAL_ERROR
          "Qt ${lumora_qt_component} did not report a version; refusing to "
          "combine it with pinned Qt Core ${Qt6Core_VERSION}.")
      endif()
      if(NOT "${${lumora_qt_component_version_var}}" VERSION_EQUAL Qt6Core_VERSION)
        message(FATAL_ERROR
          "Qt module version mismatch: Core is ${Qt6Core_VERSION}, but "
          "${lumora_qt_component} is ${${lumora_qt_component_version_var}}.")
      endif()
    endforeach()

    get_target_property(lumora_qt_gui_features Qt6::Gui QT_ENABLED_PUBLIC_FEATURES)
    if(NOT "imageformat_png" IN_LIST lumora_qt_gui_features)
      message(FATAL_ERROR
        "The Qt Quick workstation requires Qt Gui PNG decoding for Qt Quick Controls. "
        "Enable qtbase[png] with vcpkg, or use a Qt SDK with PNG support.")
    endif()

    message(STATUS
      "Lumora QML UI: resolved matching Qt ${Qt6Core_VERSION} modules: "
      "${lumora_qt_components}")
  endif()

  find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc imgcodecs)
  find_package(spdlog 1.15 REQUIRED CONFIG)

  if(LUMORA_BUILD_TESTS)
    find_package(GTest 1.15 REQUIRED CONFIG)
  endif()
endmacro()
