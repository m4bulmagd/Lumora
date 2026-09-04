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
lumora_enable_warnings(lumora_ui)

add_executable(lumora_app app/main.cpp)
target_link_libraries(lumora_app PRIVATE lumora::ui)
lumora_enable_warnings(lumora_app)
