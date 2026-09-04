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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(Dependencies)
include(Pylon)
include(Warnings)

lumora_find_dependencies()
lumora_configure_pylon()

add_subdirectory(src)

if(LUMORA_BUILD_TESTS)
  add_subdirectory(tests)
endif()
