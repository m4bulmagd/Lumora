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

add_executable(
  lumora_core_tests
  unit/core/ResultSmokeTests.cpp
  unit/core/ResultTests.cpp
  unit/core/CheckedMathTests.cpp
)

target_link_libraries(
  lumora_core_tests
  PRIVATE
    lumora::core
    GTest::gtest_main
)
lumora_enable_warnings(lumora_core_tests)
add_test(
  NAME Core.Result
  COMMAND lumora_core_tests --gtest_filter=Result*:ResultSmoke*
)
add_test(
  NAME Core.CheckedMath
  COMMAND lumora_core_tests --gtest_filter=CheckedMath*
)

add_executable(
  lumora_diagnostics_tests
  unit/diagnostics/LoggingTests.cpp
)

target_link_libraries(
  lumora_diagnostics_tests
  PRIVATE
    lumora::diagnostics
    GTest::gtest_main
)
lumora_enable_warnings(lumora_diagnostics_tests)
add_test(NAME Logging COMMAND lumora_diagnostics_tests)

add_executable(
  lumora_configuration_tests
  unit/configuration/ConfigurationStoreTests.cpp
)

target_link_libraries(
  lumora_configuration_tests
  PRIVATE
    lumora::configuration
    GTest::gtest_main
)
lumora_enable_warnings(lumora_configuration_tests)
add_test(NAME ConfigurationStore COMMAND lumora_configuration_tests)
