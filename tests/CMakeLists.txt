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
  unit/core/ImageLayoutTests.cpp
  unit/core/FrameMetadataTests.cpp
  unit/core/BufferPoolTests.cpp
  unit/core/FrameTests.cpp
  unit/core/LatestValueSlotTests.cpp
  unit/core/BoundedQueueTests.cpp
  unit/core/ClockTests.cpp
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
add_test(
  NAME Core.ImageLayout
  COMMAND lumora_core_tests --gtest_filter=ImageLayout*
)
add_test(
  NAME Core.FrameMetadata
  COMMAND lumora_core_tests --gtest_filter=FrameMetadata*:SourcePixelFormat*
)
add_test(
  NAME Core.BufferPool
  COMMAND lumora_core_tests --gtest_filter=BufferPool*:SharedBuffer*
)
add_test(
  NAME Core.Frame
  COMMAND lumora_core_tests --gtest_filter=RawFrame*:ProcessedFrame*:DisplayFrame*:FrameBundle*
)
add_test(
  NAME Core.LatestValueSlot
  COMMAND lumora_core_tests --gtest_filter=LatestValueSlot*
)
add_test(
  NAME Core.BoundedQueue
  COMMAND lumora_core_tests --gtest_filter=BoundedQueue*
)
add_test(
  NAME Core.Clock
  COMMAND lumora_core_tests --gtest_filter=Clock*:ManualClock*
)

add_executable(
  lumora_camera_api_tests
  unit/camera/CameraConfigurationValidatorTests.cpp
  unit/camera/CameraContractTests.cpp
)

target_link_libraries(
  lumora_camera_api_tests
  PRIVATE
    lumora::camera_api
    GTest::gtest_main
)
lumora_enable_warnings(lumora_camera_api_tests)
add_test(NAME CameraConfiguration COMMAND lumora_camera_api_tests --gtest_filter=CameraConfigurationValidator.*)
add_test(NAME CameraContract COMMAND lumora_camera_api_tests --gtest_filter=CameraContract.*)

add_executable(
  lumora_camera_simulator_tests
  unit/camera/SimulatedCameraTests.cpp
)

target_link_libraries(
  lumora_camera_simulator_tests
  PRIVATE
    lumora::camera_simulator
    GTest::gtest_main
)
lumora_enable_warnings(lumora_camera_simulator_tests)
add_test(NAME SimulatedCamera COMMAND lumora_camera_simulator_tests)

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
