add_executable(lumora_quick_image_tests
  qml/QuickImageItemTests.cpp qml/QuickRendererTestMain.cpp)
target_link_libraries(lumora_quick_image_tests PRIVATE
  lumora_quick_renderer Qt6::Test GTest::gtest)
lumora_enable_warnings(lumora_quick_image_tests)
add_test(NAME Qml.ImageRenderer COMMAND lumora_quick_image_tests)
set_tests_properties(Qml.ImageRenderer PROPERTIES TIMEOUT 120
  ENVIRONMENT "${lumora_qml_test_environment}")

add_executable(lumora_quick_renderer_benchmark
  "${PROJECT_SOURCE_DIR}/benchmarks/presentation/QuickRendererBenchmark.cpp")
target_include_directories(lumora_quick_renderer_benchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_link_libraries(lumora_quick_renderer_benchmark PRIVATE
  lumora_quick_renderer lumora::processing Qt6::Test)
lumora_enable_warnings(lumora_quick_renderer_benchmark)
