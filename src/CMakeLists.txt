set(lumora_generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${lumora_generated_include_dir}/lumora/core")
configure_file(
  core/include/lumora/core/AppVersion.hpp.in
  "${lumora_generated_include_dir}/lumora/core/AppVersion.hpp"
  @ONLY
)

add_library(lumora_core INTERFACE)
add_library(lumora::core ALIAS lumora_core)

target_include_directories(
  lumora_core
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
    $<BUILD_INTERFACE:${lumora_generated_include_dir}>
)

add_library(
  lumora_diagnostics
  STATIC
  diagnostics/include/lumora/diagnostics/Logging.hpp
  diagnostics/src/Logging.cpp
)

add_library(lumora::diagnostics ALIAS lumora_diagnostics)

target_include_directories(
  lumora_diagnostics
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/include>
)

target_link_libraries(
  lumora_diagnostics
  PUBLIC lumora::core
  PRIVATE spdlog::spdlog
)
lumora_enable_warnings(lumora_diagnostics)

add_library(
  lumora_ui
  STATIC
  ui/include/lumora/ui/MainWindow.hpp
  ui/src/MainWindow.cpp
  ui/resources/lumora.qrc
)

add_library(lumora::ui ALIAS lumora_ui)

target_include_directories(
  lumora_ui
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ui/include>
)

target_link_libraries(lumora_ui PUBLIC Qt6::Widgets)
target_compile_definitions(lumora_ui PUBLIC LUMORA_RELEASE_CLASS_EVALUATION=1)
set_target_properties(lumora_ui PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)
lumora_enable_warnings(lumora_ui)

add_executable(lumora_app app/main.cpp)
target_link_libraries(
  lumora_app
  PRIVATE
    lumora::diagnostics
    lumora::ui
)
lumora_enable_warnings(lumora_app)
