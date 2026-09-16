# Keep every software scene/renderer test on the same matching Qt platform.
set(lumora_qml_test_environment
  "QT_QPA_PLATFORM=offscreen"
  "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>"
  "QT_QUICK_BACKEND=software")

if(WIN32)
  # Offscreen uses the generic FreeType font database, including on Windows.
  # It does not discover the native Windows fonts directory automatically.
  if(DEFINED ENV{QT_QPA_FONTDIR} AND NOT "$ENV{QT_QPA_FONTDIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{QT_QPA_FONTDIR}" lumora_qml_font_directory)
  else()
    file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" lumora_qml_font_directory)
  endif()
  if(NOT IS_DIRECTORY "${lumora_qml_font_directory}")
    message(FATAL_ERROR
      "QML offscreen tests require an existing fonts directory. "
      "Set QT_QPA_FONTDIR, or ensure WINDIR/Fonts exists.")
  endif()
  list(APPEND lumora_qml_test_environment
    "QT_QPA_FONTDIR=${lumora_qml_font_directory}")
endif()
