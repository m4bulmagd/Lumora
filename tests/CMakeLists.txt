if(LUMORA_BUILD_QML_UI)
  include("${CMAKE_CURRENT_SOURCE_DIR}/qml/TestEnvironment.cmake")
  include("${CMAKE_CURRENT_SOURCE_DIR}/qml/RendererTests.cmake")
  qt_add_executable(
    lumora_qml_tests
    MANUAL_FINALIZATION
    integration/QmlWorkstationTests.cpp
    "${PROJECT_SOURCE_DIR}/src/app/SimulatorComposition.cpp"
  )
  set_target_properties(lumora_qml_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(
    lumora_qml_tests
    PRIVATE
      lumora_qml_ui
      lumora_qml_uiplugin
      lumora::camera_simulator
      Qt6::Qml
      Qt6::Quick
      Qt6::QuickControls2
      Qt6::QuickTest
      Qt6::Test
  )
  target_include_directories(lumora_qml_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app" "${PROJECT_SOURCE_DIR}/src/processing/src")
  lumora_enable_warnings(lumora_qml_tests)
  qt_import_qml_plugins(lumora_qml_tests)
  qt_finalize_executable(lumora_qml_tests)

  add_test(NAME Qml.Workstation COMMAND lumora_qml_tests)
  set_tests_properties(
    Qml.Workstation
    PROPERTIES
      LABELS qml
      TIMEOUT 120
      ENVIRONMENT "${lumora_qml_test_environment}"
  )
  foreach(adapter IN ITEMS Runtime ProcessingAdapter CameraSettingsAdapter InstallationAdapter LayoutAdapter)
    string(TOLOWER "${adapter}" adapter_lower)
    set(adapter_target "lumora_qml_${adapter_lower}_tests")
    qt_add_executable(${adapter_target} MANUAL_FINALIZATION
      "integration/Qml${adapter}Tests.cpp" qml/QuickRendererTestMain.cpp
      "${PROJECT_SOURCE_DIR}/src/app/SimulatorComposition.cpp")
    target_include_directories(${adapter_target} PRIVATE "${PROJECT_SOURCE_DIR}/src/app" "${PROJECT_SOURCE_DIR}/src/processing/src")
    target_link_libraries(${adapter_target} PRIVATE lumora_qml_ui lumora_qml_uiplugin
      lumora::camera_simulator Qt6::Test GTest::gtest)
    lumora_enable_warnings(${adapter_target})
    qt_import_qml_plugins(${adapter_target})
    qt_finalize_executable(${adapter_target})
    add_test(NAME Qml.${adapter} COMMAND ${adapter_target})
    set_tests_properties(Qml.${adapter} PROPERTIES TIMEOUT 120 LABELS qml
      ENVIRONMENT "${lumora_qml_test_environment}")
  endforeach()

endif()

add_executable(lumora_application_tests
  unit/application/CameraSessionStateMachineTests.cpp
  unit/application/CameraCommandMailboxTests.cpp
  unit/application/AcquisitionWorkerTests.cpp
  unit/application/ProcessingWorkerTests.cpp
  unit/application/CameraSettingsPolicyTests.cpp
  unit/application/StartupPreferencesTests.cpp
  unit/application/PresetRepositoryTests.cpp)
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
add_test(NAME Application.CameraSettingsPolicy
  COMMAND lumora_application_tests --gtest_filter=CameraSettingsPolicy.*)
set_tests_properties(Application.CameraSettingsPolicy PROPERTIES TIMEOUT 60)
add_test(NAME Application.StartupPreferences
  COMMAND lumora_application_tests --gtest_filter=StartupPreferences.*:CameraProfile.*)
set_tests_properties(Application.StartupPreferences PROPERTIES TIMEOUT 60)
add_test(NAME Application.PresetRepository
  COMMAND lumora_application_tests --gtest_filter=PresetRepository.*)
set_tests_properties(Application.PresetRepository PROPERTIES TIMEOUT 60)

add_executable(
  lumora_presentation_tests
  unit/presentation/PresentationProtocolTests.cpp
  unit/presentation/FramePresenterTests.cpp
  unit/presentation/CameraActionPolicyTests.cpp
  unit/presentation/CameraSettingsDraftTests.cpp
  unit/presentation/InstallationSettingsDraftTests.cpp
  unit/presentation/ProcessingControlsModelTests.cpp
  unit/presentation/ViewportTransformTests.cpp
  unit/presentation/WorkstationCoordinatorTests.cpp
)
target_link_libraries(
  lumora_presentation_tests
  PRIVATE
    lumora::presentation
    lumora::camera_simulator
    lumora::configuration
    GTest::gtest_main
)
target_include_directories(lumora_presentation_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_compile_definitions(lumora_presentation PRIVATE
  LUMORA_WORKSTATION_COORDINATOR_TEST_HOOKS)
target_include_directories(lumora_presentation PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/support")
lumora_enable_warnings(lumora_presentation_tests)
add_test(NAME SharedFramePresenter COMMAND lumora_presentation_tests --gtest_filter=SharedFramePresenter.*)
add_test(NAME PresentationProtocol COMMAND lumora_presentation_tests --gtest_filter=PresentationProtocol.*)
set_tests_properties(SharedFramePresenter PresentationProtocol PROPERTIES TIMEOUT 60)
add_test(NAME Presentation.CameraSettingsDraft
  COMMAND lumora_presentation_tests --gtest_filter=CameraSettingsDraft.*)
set_tests_properties(Presentation.CameraSettingsDraft PROPERTIES TIMEOUT 60)
add_test(NAME Presentation.InstallationSettingsDraft
  COMMAND lumora_presentation_tests --gtest_filter=InstallationSettingsDraft.*)
set_tests_properties(Presentation.InstallationSettingsDraft PROPERTIES TIMEOUT 60)
add_test(
  NAME ProcessingControls
  COMMAND lumora_presentation_tests --gtest_filter=Controls.*
)
add_test(
  NAME ViewportTransform
  COMMAND lumora_presentation_tests --gtest_filter=ViewportTransform.*
)
set_tests_properties(ProcessingControls ViewportTransform PROPERTIES TIMEOUT 60)
add_test(
  NAME CameraActionPolicy
  COMMAND lumora_presentation_tests --gtest_filter=CameraActionPolicy.*
)
add_test(
  NAME WorkstationCoordinator
  COMMAND lumora_presentation_tests --gtest_filter=WorkstationCoordinator.*
)
set_tests_properties(CameraActionPolicy WorkstationCoordinator PROPERTIES TIMEOUT 60)

add_executable(lumora_processing_tests
  unit/processing/ProcessingDefaultsTests.cpp
  unit/processing/Mono8PassThroughProcessorTests.cpp
  unit/processing/PipelineCompilerTests.cpp
  unit/processing/ImageViewTests.cpp
  unit/processing/NormalizeStageTests.cpp
  unit/processing/WindowLevelStageTests.cpp
  unit/processing/ClaheStageTests.cpp
  unit/processing/DetailStageBatchingTests.cpp
  unit/processing/DenoiseStageTests.cpp
  unit/processing/SharpenStageTests.cpp
  unit/processing/ToneStageTests.cpp
  unit/processing/DisplayMapperTests.cpp
  unit/processing/OrientationTransformTests.cpp
  unit/processing/FrameProcessingEngineTests.cpp
  unit/processing/PreparedRuntimeTests.cpp
  unit/processing/PreparedCpuExecutorTests.cpp)
target_include_directories(lumora_processing_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/processing/src" "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(lumora_processing_tests PRIVATE
  lumora::processing opencv_core opencv_imgproc GTest::gtest_main)
target_compile_definitions(lumora_processing_tests PRIVATE
  LUMORA_CLAHE_REFERENCE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/reference/processing/clahe-reference.json")
target_include_directories(lumora_processing_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src/processing/src")
lumora_enable_warnings(lumora_processing_tests)
add_test(NAME Processing.Mono8PassThrough
  COMMAND lumora_processing_tests --gtest_filter=Mono8PassThroughProcessor.*)
set_tests_properties(Processing.Mono8PassThrough PROPERTIES TIMEOUT 60)

# These integration suites use the real backend without any Qt application.
add_executable(lumora_backend_integration_tests
  integration/ProcessingConfigurationTests.cpp
  integration/InstallationPipelineTests.cpp
  integration/CameraReconfigurationTests.cpp)
target_link_libraries(lumora_backend_integration_tests PRIVATE
  lumora::application lumora::camera_simulator GTest::gtest_main)
lumora_enable_warnings(lumora_backend_integration_tests)
foreach(suite IN ITEMS ProcessingConfiguration InstallationPipeline CameraReconfiguration)
  add_test(NAME ${suite} COMMAND lumora_backend_integration_tests --gtest_filter=${suite}.*)
  set_tests_properties(${suite} PROPERTIES TIMEOUT 180 LABELS backend)
endforeach()

if(LUMORA_BUILD_LEGACY_WIDGETS_TESTS)
  add_executable(
    lumora_ui_tests
    support/QtTestMain.cpp
    unit/ui/CameraSettingsDialogTests.cpp
    unit/ui/InstallationSettingsDialogTests.cpp
    unit/ui/CameraStartupPanelTests.cpp
    unit/ui/CameraPanelLayoutTests.cpp
    unit/ui/ProcessingPanelTests.cpp
    unit/ui/WorkstationControllerTests.cpp
    unit/ui/FramePresenterTests.cpp
    unit/ui/ImageViewportTests.cpp
    unit/ui/ComparisonViewportTests.cpp
    unit/ui/CompareWorkstationTests.cpp
    unit/ui/MainWindowSmokeTests.cpp
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
      lumora::configuration
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
    NAME ImageViewport
    COMMAND lumora_ui_tests --gtest_filter=ImageViewport.*
  )
  add_test(NAME ComparisonViewport COMMAND lumora_ui_tests --gtest_filter=ComparisonViewport.*)
  add_test(NAME CompareWorkstation COMMAND lumora_ui_tests --gtest_filter=CompareWorkstation.*)
  add_test(
    NAME WorkstationView
    COMMAND lumora_ui_tests --gtest_filter=WorkstationView.*
  )
  add_test(
    NAME CameraStartupPanel
    COMMAND lumora_ui_tests --gtest_filter=CameraStartupPanel.*
  )
  add_test(NAME ProcessingPanel COMMAND lumora_ui_tests --gtest_filter=ProcessingPanel.*)
  add_test(NAME WorkstationController COMMAND lumora_ui_tests --gtest_filter=WorkstationController.*)
  # Surface stalled Qt startup promptly on unattended CI runners.
  add_test(NAME CameraSettingsDialog COMMAND lumora_ui_tests --gtest_filter=CameraSettingsDialog.*)
  add_test(NAME InstallationSettingsDialog COMMAND lumora_ui_tests --gtest_filter=InstallationSettingsDialog.*)
  add_test(NAME CameraPanelLayout COMMAND lumora_ui_tests --gtest_filter=CameraPanelLayout.*)

  # Resolve the matching Debug/Release plugin explicitly: Windows DLL staging
  # does not make the installed Qt platform plugins discoverable by itself.
  set_tests_properties(
    MainWindowSmoke ImageViewport ComparisonViewport CompareWorkstation WorkstationView FramePresenter CameraSettingsDialog InstallationSettingsDialog CameraStartupPanel CameraPanelLayout WorkstationController ProcessingPanel
    PROPERTIES
      TIMEOUT 60
      ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>"
  )

  add_executable(
    lumora_integration_tests
    support/QtTestMain.cpp
    integration/SimulatedViewerTests.cpp
    integration/LivePipelineTests.cpp
    integration/InstallationControllerTests.cpp
    integration/CameraControlsTests.cpp
    "${PROJECT_SOURCE_DIR}/src/app/SimulatorComposition.cpp"
  )
  target_include_directories(
    lumora_integration_tests
    PRIVATE
      "${PROJECT_SOURCE_DIR}/src/processing/src"
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
  add_test(NAME InstallationController COMMAND lumora_integration_tests --gtest_filter=InstallationController.*)
  add_test(NAME CameraControls COMMAND lumora_integration_tests --gtest_filter=CameraControls.*)
  set_tests_properties(CameraControls PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")
  set_tests_properties(InstallationController PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")
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

  set_tests_properties(
    MainWindowSmoke ImageViewport ComparisonViewport CompareWorkstation WorkstationView
    FramePresenter CameraSettingsDialog InstallationSettingsDialog CameraStartupPanel
    CameraPanelLayout WorkstationController ProcessingPanel LivePipeline
    InstallationController CameraControls SimulatedViewer PROPERTIES LABELS legacy-widgets)
endif()

add_test(NAME CMake.StressEvidence
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StressEvidenceTests.cmake")
set_tests_properties(CMake.StressEvidence PROPERTIES TIMEOUT 30)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  option(LUMORA_TEST_LINUX_DESKTOP "Register Qt Quick scene tests requiring X11" OFF)
  if(LUMORA_TEST_LINUX_DESKTOP)
    foreach(backend IN ITEMS software opengl)
      set(desktop_quick_backend "")
      if(backend STREQUAL "software")
        set(desktop_quick_backend software)
      endif()
      add_test(NAME Qml.Desktop.${backend} COMMAND lumora_qml_tests)
      set_tests_properties(Qml.Desktop.${backend} PROPERTIES
        LABELS "qml;desktop" TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=xcb;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>;QT_QUICK_BACKEND=${desktop_quick_backend};QSG_RHI_BACKEND=opengl;QSG_RENDER_LOOP=basic")
    endforeach()
    if(LUMORA_BUILD_LEGACY_WIDGETS_TESTS)
      add_executable(lumora_linux_desktop_tests unit/ui/LinuxDesktopSmokeTests.cpp)
      target_link_libraries(lumora_linux_desktop_tests PRIVATE lumora::ui GTest::gtest)
      lumora_enable_warnings(lumora_linux_desktop_tests)
      add_test(NAME LinuxDesktopSmoke COMMAND lumora_linux_desktop_tests)
      set_tests_properties(LinuxDesktopSmoke PROPERTIES LABELS "desktop;legacy-widgets" TIMEOUT 15
        ENVIRONMENT "QT_QPA_PLATFORM=xcb;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>")
    endif()
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
  unit/core/FrameObjectPoolTests.cpp
  unit/core/ProcessingTimingsTests.cpp
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
  unit/configuration/PresetCodecTests.cpp
  unit/configuration/StartupPreferencesTests.cpp
  unit/configuration/UiPreferencesTests.cpp
  unit/configuration/InstallationProfilesTests.cpp
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
  COMMAND lumora_configuration_tests --gtest_filter=StartupPreferencesTest.*:StartupPreferencesService.*:UiPreferences.*)
add_test(NAME Configuration.InstallationProfiles
  COMMAND lumora_configuration_tests --gtest_filter=InstallationProfiles*)
set_tests_properties(Configuration.InstallationProfiles PROPERTIES TIMEOUT 60)
add_test(NAME Configuration.PresetCodec
  COMMAND lumora_configuration_tests --gtest_filter=PresetCodec.*)
set_tests_properties(Configuration.PresetCodec PROPERTIES TIMEOUT 60)
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
add_test(NAME Processing.ClaheStage
  COMMAND lumora_processing_tests --gtest_filter=ClaheStage.*)
set_tests_properties(Processing.ClaheStage PROPERTIES TIMEOUT 60)
add_test(NAME Processing.DetailStages
  COMMAND lumora_processing_tests
    --gtest_filter=DetailStageBatching.*:DenoiseStage.*:SharpenStage.*)
set_tests_properties(Processing.DetailStages PROPERTIES TIMEOUT 60)

add_executable(lumora_detail_allocation_tests
  support/AllocationTracker.cpp
  unit/processing/DetailStageAllocationTests.cpp)
target_include_directories(lumora_detail_allocation_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(lumora_detail_allocation_tests PRIVATE lumora::processing)
lumora_enable_warnings(lumora_detail_allocation_tests)
add_test(NAME Processing.DetailStageAllocation
  COMMAND lumora_detail_allocation_tests)
set_tests_properties(Processing.DetailStageAllocation PROPERTIES TIMEOUT 60)
add_test(NAME Processing.ToneStages
  COMMAND lumora_processing_tests --gtest_filter=ToneStages.*)
set_tests_properties(Processing.ToneStages PROPERTIES TIMEOUT 60)
add_test(NAME Processing.DisplayMapper
  COMMAND lumora_processing_tests --gtest_filter=DisplayMapper.*)
set_tests_properties(Processing.DisplayMapper PROPERTIES TIMEOUT 60)
add_test(NAME Processing.OrientationTransform
  COMMAND lumora_processing_tests --gtest_filter=OrientationTransform.*)
set_tests_properties(Processing.OrientationTransform PROPERTIES TIMEOUT 60)

add_test(NAME Processing.FrameProcessingEngine
  COMMAND lumora_processing_tests --gtest_filter=FrameProcessingEngine.*)
set_tests_properties(Processing.FrameProcessingEngine PROPERTIES TIMEOUT 60)

add_executable(lumora_frame_engine_allocation_tests
  support/AllocationTracker.cpp
  unit/processing/FrameEngineAllocationTests.cpp)
target_include_directories(lumora_frame_engine_allocation_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/support" "${PROJECT_SOURCE_DIR}/src/core/src" "${PROJECT_SOURCE_DIR}/src/processing/src")
target_link_libraries(lumora_frame_engine_allocation_tests PRIVATE lumora::processing)
lumora_enable_warnings(lumora_frame_engine_allocation_tests)
add_test(NAME Processing.FrameEngineAllocation COMMAND lumora_frame_engine_allocation_tests)
set_tests_properties(Processing.FrameEngineAllocation PROPERTIES TIMEOUT 180)

target_compile_definitions(lumora_core PRIVATE LUMORA_FRAME_POOL_TESTS=1)
target_include_directories(lumora_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")

target_include_directories(lumora_core_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/support" "${PROJECT_SOURCE_DIR}/src/core/src")
add_test(NAME Core.FrameObjectPool COMMAND lumora_core_tests --gtest_filter=FrameObjectPool.*)
add_test(NAME Core.ProcessingTimings COMMAND lumora_core_tests --gtest_filter=ProcessingTimings.*)

add_test(NAME Processing.Defaults COMMAND lumora_processing_tests --gtest_filter=ProcessingDefaults.*)

add_executable(lumora_evidence_tests unit/processing/EvidenceArtifactTests.cpp reference/processing/PipelineReferenceTests.cpp)
target_link_libraries(lumora_evidence_tests PRIVATE lumora_processing_evidence_support Qt6::Core GTest::gtest_main)
lumora_enable_warnings(lumora_evidence_tests)
add_test(NAME Processing.EvidenceArtifacts COMMAND lumora_evidence_tests)
set_tests_properties(Processing.EvidenceArtifacts PROPERTIES TIMEOUT 120)

target_compile_definitions(lumora_evidence_tests PRIVATE LUMORA_EXACT_ORACLE_ROOT="${PROJECT_SOURCE_DIR}/tests/reference/processing/fixtures")

target_compile_definitions(lumora_evidence_tests PRIVATE LUMORA_WORKSTATION_TEMPLATE="${PROJECT_SOURCE_DIR}/benchmarks/processing/reference-workstation.json")

# The small provenance projects exercise all configurations without rebuilding Qt
# or running image workloads. Reuse the native generator on multi-config hosts.
if(CMAKE_CONFIGURATION_TYPES)
  set(evidence_test_generator "${CMAKE_GENERATOR}")
else()
  set(evidence_test_generator "Ninja Multi-Config")
endif()
foreach(evidence_test_case IN ITEMS language flags)
  add_test(NAME Processing.EvidenceProvenance.${evidence_test_case}
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/evidence-provenance-${evidence_test_case}"
      "-DTEST_CASE=${evidence_test_case}" "-DTEST_GENERATOR=${evidence_test_generator}"
      "-DTEST_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProcessingEvidenceProvenanceTests.cmake")
  set_tests_properties(Processing.EvidenceProvenance.${evidence_test_case}
    PROPERTIES TIMEOUT 120 LABELS "processing;evidence")
endforeach()

add_test(NAME Processing.PreparedCpuExecutor COMMAND lumora_processing_tests --gtest_filter=PreparedCpuExecutor.*)
set_tests_properties(Processing.PreparedCpuExecutor PROPERTIES TIMEOUT 60)

target_compile_definitions(lumora_evidence_tests PRIVATE LUMORA_LEGACY_EVIDENCE_ROOT="${PROJECT_SOURCE_DIR}/tests/reference/processing/evidence-v1")
