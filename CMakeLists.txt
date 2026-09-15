cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Version)
project(
  Lumora
  VERSION ${LUMORA_VERSION}
  DESCRIPTION "Evaluation X-ray imaging workstation"
  LANGUAGES CXX
)

option(LUMORA_ENABLE_BASLER "Build the Basler pylon camera adapter" OFF)
option(LUMORA_BUILD_TESTS "Build automated tests" ON)
option(LUMORA_BUILD_BENCHMARKS "Build benchmark executables" OFF)
option(LUMORA_BUILD_QML_UI "Build the Qt Quick/QML workstation (required)" ON)
option(LUMORA_BUILD_LEGACY_WIDGETS_TESTS "Build opt-in legacy Widgets regression tests" OFF)
if(NOT LUMORA_BUILD_QML_UI)
  message(FATAL_ERROR "The workstation is Qt Quick/QML only; LUMORA_BUILD_QML_UI must be ON.")
endif()
if(LUMORA_BUILD_LEGACY_WIDGETS_TESTS AND NOT LUMORA_BUILD_TESTS)
  message(FATAL_ERROR "Legacy Widgets regression tests require LUMORA_BUILD_TESTS=ON.")
endif()
option(LUMORA_ENABLE_STRESS_TESTS "Register opt-in stress tests" OFF)
if(LUMORA_ENABLE_STRESS_TESTS AND NOT LUMORA_BUILD_LEGACY_WIDGETS_TESTS)
  message(FATAL_ERROR "The retained viewer stress test requires LUMORA_BUILD_LEGACY_WIDGETS_TESTS=ON.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(Dependencies)
include(Pylon)
include(Warnings)

lumora_find_dependencies()

if(LUMORA_BUILD_QML_UI)
  qt_policy(SET QTP0001 NEW)
  qt_policy(SET QTP0004 NEW)
  qt_policy(SET QTP0005 NEW)
endif()

lumora_configure_pylon()

add_subdirectory(src)

if(LUMORA_BUILD_TESTS OR LUMORA_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(LUMORA_BUILD_TESTS)
  add_subdirectory(tests)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/VerifyTargets.cmake")
lumora_verify_milestone_one_targets()
