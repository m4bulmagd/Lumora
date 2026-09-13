add_library(
  lumora_presentation
  STATIC
  include/lumora/presentation/CameraActionPolicy.hpp
  include/lumora/presentation/CameraSettingsModel.hpp
  include/lumora/presentation/DisplayMode.hpp
  include/lumora/presentation/ProcessingControlsModel.hpp
  include/lumora/presentation/ViewportTransform.hpp
  include/lumora/presentation/WorkstationCoordinator.hpp
  include/lumora/presentation/WorkstationState.hpp
  include/lumora/presentation/WorkstationStatus.hpp
  include/lumora/presentation/PresentationProtocol.hpp
  include/lumora/presentation/FramePresenter.hpp
  src/PresentationProtocol.cpp
  src/FramePresenter.cpp
  src/CameraActionPolicy.cpp
  src/ProcessingControlsModel.cpp
  src/ViewportTransform.cpp
  src/WorkstationCoordinator.cpp
)

add_library(lumora::presentation ALIAS lumora_presentation)

target_include_directories(
  lumora_presentation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  lumora_presentation
  PUBLIC
    lumora::application
    lumora::camera_api
    lumora::core
  PRIVATE
    lumora::configuration
)

lumora_enable_warnings(lumora_presentation)
