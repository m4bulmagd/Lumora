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
  core/include/lumora/core/BufferPool.hpp
  core/include/lumora/core/BoundedQueue.hpp
  core/include/lumora/core/Clock.hpp
  core/include/lumora/core/Error.hpp
  core/include/lumora/core/Frame.hpp
  core/include/lumora/core/FrameMetadata.hpp
  core/include/lumora/core/ImageLayout.hpp
  core/include/lumora/core/LatestValueSlot.hpp
  core/include/lumora/core/PixelFormat.hpp
  core/include/lumora/core/Result.hpp
  core/include/lumora/core/SharedBuffer.hpp
  core/src/BufferPool.cpp
  core/src/Clock.cpp
  core/src/CheckedMath.cpp
  core/src/Frame.cpp
  core/src/FrameMetadata.cpp
  core/src/ImageLayout.cpp
)
add_library(lumora::core ALIAS lumora_core)

add_library(
  lumora_processing
  STATIC
  processing/include/lumora/processing/IFrameProcessor.hpp
  processing/include/lumora/processing/Mono8PassThroughProcessor.hpp
  processing/include/lumora/processing/ProcessingConfiguration.hpp
  processing/include/lumora/processing/PipelineCompiler.hpp
  processing/include/lumora/processing/IProcessingStage.hpp
  processing/include/lumora/processing/ImageView.hpp
  processing/include/lumora/processing/NormalizeStage.hpp
  processing/include/lumora/processing/WindowLevelStage.hpp
  processing/include/lumora/processing/ClaheStage.hpp
  processing/include/lumora/processing/DenoiseStage.hpp
  processing/include/lumora/processing/SharpenStage.hpp
  processing/include/lumora/processing/ToneStages.hpp
  processing/include/lumora/processing/DisplayMapper.hpp
  processing/include/lumora/processing/OrientationTransform.hpp
  processing/include/lumora/processing/ProcessingPipeline.hpp
  processing/include/lumora/processing/ProcessingWorkspace.hpp
  processing/include/lumora/processing/FrameProcessingEngine.hpp
  processing/src/ProcessingPipeline.cpp
  processing/src/ProcessingWorkspace.cpp
  processing/src/FrameProcessingEngine.cpp
  processing/src/ImageView.cpp
  processing/src/NormalizeStage.cpp
  processing/src/WindowLevelStage.cpp
  processing/src/ClaheStage.cpp
  processing/src/DetailStageSupport.hpp
  processing/src/DetailStageSupport.cpp
  processing/src/DenoiseStage.cpp
  processing/src/SharpenStage.cpp
  processing/src/ToneStageSupport.hpp
  processing/src/BrightnessContrastStage.cpp
  processing/src/GammaStage.cpp
  processing/src/InvertStage.cpp
  processing/src/DisplayMapper.cpp
  processing/src/OrientationTransform.cpp
  processing/src/PipelineCompiler.cpp
  processing/src/Mono8PassThroughProcessor.cpp
)
add_library(lumora::processing ALIAS lumora_processing)
target_include_directories(lumora_processing PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/processing/include>)
target_link_libraries(lumora_processing
  PUBLIC lumora::core
  PRIVATE opencv_core opencv_imgproc)
lumora_enable_warnings(lumora_processing)

# The camera API deliberately has no Qt, SDK, or transport dependency.
add_library(
  lumora_camera_api
  STATIC
  camera/api/include/lumora/camera/CameraTypes.hpp
  camera/api/include/lumora/camera/CameraConfigurationValidator.hpp
  camera/api/include/lumora/camera/ICameraDevice.hpp
  camera/api/include/lumora/camera/ICameraProvider.hpp
  camera/api/src/CameraConfigurationValidator.cpp
)
add_library(lumora::camera_api ALIAS lumora_camera_api)

target_include_directories(
  lumora_camera_api
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/camera/api/include>
)

target_link_libraries(lumora_camera_api PUBLIC lumora::core)
lumora_enable_warnings(lumora_camera_api)

add_library(
  lumora_application
  STATIC
  application/include/lumora/application/ApplicationState.hpp
  application/include/lumora/application/CameraCommand.hpp
  application/include/lumora/application/CameraCommandMailbox.hpp
  application/include/lumora/application/CameraSessionStateMachine.hpp
  application/src/CameraCommandMailbox.cpp
  application/src/CameraSessionStateMachine.cpp
  application/include/lumora/application/AcquisitionWorker.hpp
  application/src/AcquisitionWorker.cpp
  application/include/lumora/application/ProcessingWorker.hpp
  application/src/ProcessingWorkerCounters.hpp
  application/src/ProcessingWorker.cpp
  application/include/lumora/application/StartupPreferences.hpp
  application/src/StartupPreferences.cpp
  application/include/lumora/application/LivePipeline.hpp
  application/src/LivePipeline.cpp
)
add_library(lumora::application ALIAS lumora_application)
target_include_directories(lumora_application PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/application/include>)
target_link_libraries(lumora_application PUBLIC
  lumora::core lumora::camera_api lumora::processing)
lumora_enable_warnings(lumora_application)

# The generated simulator remains hardware- and UI-independent.
add_library(
  lumora_camera_simulator
  STATIC
  camera/simulator/include/lumora/camera/sim/IPatternGenerator.hpp
  camera/simulator/include/lumora/camera/sim/SequenceSource.hpp
  camera/simulator/include/lumora/camera/sim/FaultScript.hpp
  camera/simulator/include/lumora/camera/sim/SimulatedCameraOptions.hpp
  camera/simulator/include/lumora/camera/sim/SimulatedCameraProvider.hpp
  camera/simulator/src/PatternGenerators.cpp
  camera/simulator/src/SequenceSource.cpp
  camera/simulator/src/FaultScript.cpp
  camera/simulator/src/FrameSource.hpp
  camera/simulator/src/RetrievalBudget.hpp
  camera/simulator/src/FrameSource.cpp
  camera/simulator/src/SimulatedCameraDevice.cpp
  camera/simulator/src/SimulatedCameraProvider.cpp
)
add_library(lumora::camera_simulator ALIAS lumora_camera_simulator)

target_include_directories(
  lumora_camera_simulator
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/camera/simulator/include>
)

target_link_libraries(lumora_camera_simulator PUBLIC lumora::camera_api)
lumora_enable_warnings(lumora_camera_simulator)

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
  configuration/include/lumora/configuration/StartupPreferencesService.hpp
  configuration/src/ConfigurationCodec.cpp
  configuration/src/ConfigurationStore.cpp
  configuration/src/StartupPreferencesService.cpp
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
    lumora::application
    lumora::core
    Qt6::Core
)
lumora_enable_warnings(lumora_configuration)

add_library(
  lumora_ui
  STATIC
  ui/include/lumora/ui/FramePresenter.hpp
  ui/include/lumora/ui/CameraStartupPanel.hpp
  ui/include/lumora/ui/ImageViewport.hpp
  ui/include/lumora/ui/MainWindow.hpp
  ui/include/lumora/ui/ViewportTransform.hpp
  ui/include/lumora/ui/WorkstationStatus.hpp
  ui/include/lumora/ui/WorkstationView.hpp
  ui/src/ImageViewport.cpp
  ui/src/CameraStartupPanel.cpp
  ui/include/lumora/ui/WorkstationController.hpp
  ui/src/WorkstationController.cpp
  ui/src/FramePresenter.cpp
  ui/src/MainWindow.cpp
  ui/src/ViewportTransform.cpp
  ui/src/WorkstationView.cpp
  ui/resources/lumora.qrc
)

add_library(lumora::ui ALIAS lumora_ui)

target_include_directories(
  lumora_ui
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ui/include>
)

target_link_libraries(lumora_ui PUBLIC lumora::application lumora::core Qt6::Widgets)
target_link_libraries(lumora_ui PRIVATE lumora::configuration)
target_compile_definitions(lumora_ui PUBLIC LUMORA_RELEASE_CLASS_EVALUATION=1)
set_target_properties(lumora_ui PROPERTIES AUTOMOC ON AUTORCC ON AUTOUIC ON)
lumora_enable_warnings(lumora_ui)

add_executable(lumora_app app/main.cpp app/SimulatorComposition.hpp app/SimulatorComposition.cpp)
target_link_libraries(
  lumora_app
  PRIVATE
    lumora::diagnostics
    lumora::ui
    lumora::application
    lumora::camera_simulator
    lumora::configuration
)
lumora_enable_warnings(lumora_app)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # QPA plugins share a directory; resolve the matching Debug/Release location
  # from Qt rather than depending on its build-time prefix or a global setting.
  add_custom_target(run-lumora
    COMMAND "${CMAKE_COMMAND}" -E env
      "QT_QPA_PLATFORM=xcb"
      "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QMinimalIntegrationPlugin>"
      $<TARGET_FILE:lumora_app>
    DEPENDS lumora_app
    USES_TERMINAL
    VERBATIM
  )
endif()

if(LUMORA_BUILD_TESTS)
  add_library(
    lumora_viewer_harness_support
    STATIC
    "${PROJECT_SOURCE_DIR}/tools/viewer-harness/SimulatorFeed.hpp"
    "${PROJECT_SOURCE_DIR}/tools/viewer-harness/SimulatorFeed.cpp"
  )
  target_include_directories(
    lumora_viewer_harness_support
    PUBLIC "${PROJECT_SOURCE_DIR}/tools/viewer-harness"
  )
  target_link_libraries(
    lumora_viewer_harness_support
    PUBLIC lumora::core lumora::camera_simulator
  )
  lumora_enable_warnings(lumora_viewer_harness_support)

  add_executable(
    lumora_viewer_harness
    "${PROJECT_SOURCE_DIR}/tools/viewer-harness/main.cpp"
    "${PROJECT_SOURCE_DIR}/tools/viewer-harness/ViewerHarnessUi.hpp"
  )
  target_link_libraries(
    lumora_viewer_harness
    PRIVATE lumora::ui lumora_viewer_harness_support
  )
  lumora_enable_warnings(lumora_viewer_harness)
endif()
