set(lumora_generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${lumora_generated_include_dir}/lumora/core")
configure_file(
  core/include/lumora/core/AppVersion.hpp.in
  "${lumora_generated_include_dir}/lumora/core/AppVersion.hpp"
  @ONLY
)

add_library(
  lumora_core
  STATIC
  core/include/lumora/core/CheckedMath.hpp
  core/include/lumora/core/Error.hpp
  core/include/lumora/core/FrameMetadata.hpp
  core/include/lumora/core/ImageLayout.hpp
  core/include/lumora/core/PixelFormat.hpp
  core/include/lumora/core/Result.hpp
  core/src/Error.cpp
  core/src/ImageLayout.cpp
)
add_library(lumora::core ALIAS lumora_core)

target_include_directories(
  lumora_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
    $<BUILD_INTERFACE:${lumora_generated_include_dir}>
)
lumora_enable_warnings(lumora_core)

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
  lumora_configuration
  STATIC
  configuration/include/lumora/configuration/ApplicationConfiguration.hpp
  configuration/include/lumora/configuration/ConfigurationCodec.hpp
  configuration/include/lumora/configuration/ConfigurationStore.hpp
  configuration/src/ConfigurationCodec.cpp
  configuration/src/ConfigurationStore.cpp
)

add_library(lumora::configuration ALIAS lumora_configuration)

target_include_directories(
  lumora_configuration
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/configuration/include>
)

target_link_libraries(
  lumora_configuration
  PUBLIC
    lumora::core
    Qt6::Core
)
lumora_enable_warnings(lumora_configuration)

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
