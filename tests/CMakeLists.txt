add_executable(
  lumora_ui_tests
  unit/ui/MainWindowSmokeTests.cpp
)

target_link_libraries(
  lumora_ui_tests
  PRIVATE
    lumora::ui
    Qt6::Widgets
    GTest::gtest
)

lumora_enable_warnings(lumora_ui_tests)

add_test(NAME MainWindowSmoke COMMAND lumora_ui_tests)
