add_executable(lumora_application_tests
  unit/application/CameraSessionStateMachineTests.cpp
  unit/application/CameraCommandMailboxTests.cpp
  unit/application/AcquisitionWorkerTests.cpp
  unit/application/ProcessingWorkerTests.cpp
  unit/application/StartupPreferencesTests.cpp)
target_link_libraries(lumora_application_tests PRIVATE
  lumora::application lumora::camera_simulator GTest::gtest_main)
target_include_directories(lumora_application_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src/application/src")
lumora_enable_warnings(lumora_application_tests)
add_test(NAME Application.CameraSessionStateMachine
  COMMAND lumora_application_tests --gtest_filter=CameraSessionStateMachine.*)
add_test(NAME Application.CameraCommandMailbox
  COMMAND lumora_application_tests --gtest_filter=CameraCommandMailbox.*)
set_tests_properties(Application.CameraSessionStateMachine Application.CameraCommandMailbox
  PROPERTIES TIMEOUT 60)
add_test(NAME Application.AcquisitionWorker
  COMMAND lumora_application_tests --gtest_filter=AcquisitionWorker.*)
set_tests_properties(Application.AcquisitionWorker PROPERTIES TIMEOUT 60)
add_test(NAME Application.ProcessingWorker
  COMMAND lumora_application_tests --gtest_filter=ProcessingWorker.*)
set_tests_properties(Application.ProcessingWorker PROPERTIES TIMEOUT 60)
add_test(NAME Application.StartupPreferences
  COMMAND lumora_application_tests --gtest_filter=StartupPreferences.*)
set_tests_properties(Application.StartupPreferences PROPERTIES TIMEOUT 60)

add_executable(lumora_processing_tests
  unit/processing/Mono8PassThroughProcessorTests.cpp
  unit/processing/PipelineCompilerTests.cpp
  unit/processing/ImageViewTests.cpp
  unit/processing/NormalizeStageTests.cpp
  unit/processing/WindowLevelStageTests.cpp
  unit/processing/DisplayMapperTests.cpp)
target_link_libraries(lumora_processing_tests PRIVATE
  lumora::processing GTest::gtest_main)
lumora_enable_warnings(lumora_processing_tests)
add_test(NAME Processing.Mono8PassThrough
  COMMAND lumora_processing_tests --gtest_filter=Mono8PassThroughProcessor.*)
set_tests_properties(Processing.Mono8PassThrough PROPERTIES TIMEOUT 60)

add_executable(
  lumora_ui_tests
  support/QtTestMain.cpp
  unit/ui/CameraStartupPanelTests.cpp
  unit/ui/WorkstationControllerTests.cpp
  unit/ui/FramePresenterTests.cpp
  unit/ui/ImageViewportTests.cpp
  unit/ui/MainWindowSmokeTests.cpp
  unit/ui/ViewportTransformTests.cpp
  unit/ui/WorkstationViewTests.cpp
)

target_include_directories(
  lumora_ui_tests
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support"
)

target_link_libraries(
  lumora_ui_tests
  PRIVATE
    lumora::ui
    Qt6::Widgets
    GTest::gtest
)

lumora_enable_warnings(lumora_ui_tests)

add_test(
  NAME FramePresenter
  COMMAND lumora_ui_tests --gtest_filter=FramePresenter.*
)
add_test(
  NAME MainWindowSmoke
  COMMAND lumora_ui_tests --gtest_filter=MainWindowSmoke.*
)
add_test(
  NAME ViewportTransform
  COMMAND lumora_ui_tests --gtest_filter=ViewportTransform.*
)
add_test(
  NAME ImageViewport
  COMMAND lumora_ui_tests --gtest_filter=ImageViewport.*
)
add_test(
  NAME WorkstationView
  COMMAND lumora_ui_tests --gtest_filter=WorkstationView.*
)
add_test(
  NAME CameraStartupPanel
  COMMAND lumora_ui_tests --gtest_filter=CameraStartupPanel.*
)
add_test(NAME WorkstationController COMMAND lumora_ui_tests --gtest_filter=WorkstationController.*)
# Surface stalled Qt startup promptly on unattended CI runners.
# Resolve the matching Debug/Release plugin explicitly: Windows DLL staging
# does not make the installed Qt platform plugins discoverable by itself.
set_tests_properties(
  MainWindowSmoke ViewportTransform ImageViewport WorkstationView FramePresenter CameraStartupPanel WorkstationController
  PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>"
)

add_executable(
  lumora_integration_tests
  support/QtTestMain.cpp
  integration/SimulatedViewerTests.cpp
  integration/LivePipelineTests.cpp
  "${PROJECT_SOURCE_DIR}/src/app/SimulatorComposition.cpp"
)
target_include_directories(
  lumora_integration_tests
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
    "${PROJECT_SOURCE_DIR}/tools/viewer-harness"
    "${PROJECT_SOURCE_DIR}/src/app"
)
target_link_libraries(
  lumora_integration_tests
  PRIVATE lumora::ui lumora::configuration lumora::camera_simulator lumora_viewer_harness_support Qt6::Widgets GTest::gtest
)
lumora_enable_warnings(lumora_integration_tests)
add_test(NAME LivePipeline COMMAND lumora_integration_tests --gtest_filter=LivePipeline.*)
set_tests_properties(LivePipeline PROPERTIES TIMEOUT 180
  ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")

add_test(
  NAME SimulatedViewer
  COMMAND lumora_integration_tests --gtest_filter=SimulatedViewer.ResponsiveTenSeconds
)
set_tests_properties(SimulatedViewer PROPERTIES TIMEOUT 60
  ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")
if(LUMORA_ENABLE_STRESS_TESTS)
  add_test(
    NAME SimulatedViewer.Stress
    COMMAND lumora_integration_tests --gtest_filter=SimulatedViewer.StressTenMinutes
  )
  set_tests_properties(SimulatedViewer.Stress PROPERTIES LABELS stress TIMEOUT 750
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")
endif()

add_test(NAME CMake.StressEvidence
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StressEvidenceTests.cmake")
set_tests_properties(CMake.StressEvidence PROPERTIES TIMEOUT 30)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  option(LUMORA_TEST_LINUX_DESKTOP "Register the smoke test requiring an X11 display" OFF)
  if(LUMORA_TEST_LINUX_DESKTOP)
    add_executable(lumora_linux_desktop_tests unit/ui/LinuxDesktopSmokeTests.cpp)
    target_link_libraries(lumora_linux_desktop_tests PRIVATE lumora::ui GTest::gtest)
    lumora_enable_warnings(lumora_linux_desktop_tests)

    add_test(NAME LinuxDesktopSmoke COMMAND lumora_linux_desktop_tests)
    # All QPA plugins share this configuration-specific directory. Using the
    # minimal plugin's location lets this test detect a missing xcb plugin too.
    set_tests_properties(LinuxDesktopSmoke PROPERTIES
      LABELS desktop
      TIMEOUT 15
      ENVIRONMENT "QT_QPA_PLATFORM=xcb;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>"
    )
  endif()
endif()

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
  COMMAND lumora_core_tests --gtest_filter=Clock*:ManualClock*:SystemClock*
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
  unit/camera/SequenceSourceTests.cpp
  unit/camera/FaultScriptTests.cpp
)

target_link_libraries(
  lumora_camera_simulator_tests
  PRIVATE
    lumora::camera_simulator
    GTest::gtest_main
)
lumora_enable_warnings(lumora_camera_simulator_tests)
target_compile_definitions(lumora_camera_simulator PRIVATE LUMORA_SIMULATOR_TEST_HOOKS)
target_include_directories(lumora_camera_simulator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_include_directories(lumora_camera_simulator_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_compile_definitions(lumora_camera_simulator_tests PRIVATE
  LUMORA_SEQUENCE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sequences")
add_test(NAME SimulatedCamera COMMAND lumora_camera_simulator_tests --gtest_filter=SimulatedCamera.*)
add_test(NAME SequenceSource COMMAND lumora_camera_simulator_tests --gtest_filter=SequenceSource.*)
add_test(NAME FaultScript COMMAND lumora_camera_simulator_tests --gtest_filter=FaultScript.*)

add_executable(lumora_pattern_allocation_tests unit/camera/PatternAllocationTests.cpp)
target_link_libraries(lumora_pattern_allocation_tests PRIVATE lumora::camera_simulator)
lumora_enable_warnings(lumora_pattern_allocation_tests)
add_test(NAME SimulatedCamera.PatternAllocation COMMAND lumora_pattern_allocation_tests)

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
  unit/configuration/StartupPreferencesTests.cpp
)

target_link_libraries(
  lumora_configuration_tests
  PRIVATE
    lumora::configuration
    GTest::gtest_main
)
lumora_enable_warnings(lumora_configuration_tests)
add_test(NAME ConfigurationStore
  COMMAND lumora_configuration_tests --gtest_filter=ConfigurationStore.*:*/InvalidConfigurationTest.*)
add_test(NAME Configuration.StartupPreferences
  COMMAND lumora_configuration_tests --gtest_filter=StartupPreferencesTest.*:StartupPreferencesService.*)
set_tests_properties(Configuration.StartupPreferences PROPERTIES TIMEOUT 60)

add_test(NAME Processing.PipelineCompiler
  COMMAND lumora_processing_tests --gtest_filter=PipelineCompiler.*)
set_tests_properties(Processing.PipelineCompiler PROPERTIES TIMEOUT 60)

add_test(NAME Processing.ImageView
  COMMAND lumora_processing_tests --gtest_filter=ImageView.*)
set_tests_properties(Processing.ImageView PROPERTIES TIMEOUT 60)
add_test(NAME Processing.NormalizeStage
  COMMAND lumora_processing_tests --gtest_filter=NormalizeStage.*)
set_tests_properties(Processing.NormalizeStage PROPERTIES TIMEOUT 60)
add_test(NAME Processing.WindowLevelStage
  COMMAND lumora_processing_tests --gtest_filter=WindowLevelStage.*)
set_tests_properties(Processing.WindowLevelStage PROPERTIES TIMEOUT 60)
add_test(NAME Processing.DisplayMapper
  COMMAND lumora_processing_tests --gtest_filter=DisplayMapper.*)
set_tests_properties(Processing.DisplayMapper PROPERTIES TIMEOUT 60)
