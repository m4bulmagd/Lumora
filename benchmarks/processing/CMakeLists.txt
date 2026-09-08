add_library(lumora_processing_evidence_support STATIC EvidencePrimitives.cpp EvidenceJson.cpp EvidenceValidation.cpp EvidenceWorkload.cpp EvidenceProvenance.cpp)
target_include_directories(lumora_processing_evidence_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lumora_processing_evidence_support PUBLIC lumora::processing PRIVATE Qt6::Core opencv_core)
lumora_enable_warnings(lumora_processing_evidence_support)

target_compile_definitions(lumora_processing_evidence_support PRIVATE LUMORA_REFERENCE_ROOT="${PROJECT_SOURCE_DIR}/tests/reference/processing")

target_include_directories(lumora_processing_evidence_support PRIVATE "${PROJECT_SOURCE_DIR}/src/processing/src")

include("${CMAKE_CURRENT_SOURCE_DIR}/EvidenceProvenance.cmake")
lumora_configure_evidence_provenance()
# Query the actual dependency installation, not the manifest's desired override.
get_filename_component(_opencv_prefix "${OpenCV_DIR}/../.." ABSOLUTE)
set(_opencv_port "")
if(EXISTS "${_opencv_prefix}/../vcpkg/status")
  file(READ "${_opencv_prefix}/../vcpkg/status" _installed_status)
  string(REGEX MATCH "Package: opencv4[^\n]*\n([^\n]+\n)*Port-Version: ([0-9]+)" _opencv_record "${_installed_status}")
  string(REGEX MATCH "Port-Version: ([0-9]+)" _opencv_port_match "${_opencv_record}")
  set(_opencv_port "${CMAKE_MATCH_1}")
endif()
file(READ "${PROJECT_SOURCE_DIR}/vcpkg.json" _manifest)
string(JSON _baseline GET "${_manifest}" builtin-baseline)
target_compile_definitions(lumora_processing_evidence_support PRIVATE
  LUMORA_OPENCV_PORT_VERSION="${_opencv_port}" LUMORA_VCPKG_BASELINE="${_baseline}")

if(LUMORA_BUILD_BENCHMARKS)
  add_executable(lumora_processing_benchmark ProcessingBenchmark.cpp EvidenceMeasurement.cpp "${PROJECT_SOURCE_DIR}/tests/support/AllocationTracker.cpp")
  add_executable(lumora_processing_reference_generator ProcessingReferenceGenerator.cpp)
  add_executable(lumora_processing_allocation_probe ProcessingAllocationProbe.cpp EvidenceMeasurement.cpp EvidenceHeap.cpp "${PROJECT_SOURCE_DIR}/tests/support/AllocationTracker.cpp")
  foreach(target IN ITEMS lumora_processing_benchmark lumora_processing_reference_generator lumora_processing_allocation_probe)
    target_link_libraries(${target} PRIVATE lumora_processing_evidence_support Qt6::Core)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
    lumora_enable_warnings(${target})
  endforeach()
  target_compile_definitions(lumora_processing_reference_generator PRIVATE LUMORA_REFERENCE_FIXTURES="${PROJECT_SOURCE_DIR}/tests/reference/processing/fixtures")
  if(WIN32)
    target_link_libraries(lumora_processing_benchmark PRIVATE psapi)
    target_link_libraries(lumora_processing_allocation_probe PRIVATE psapi)
  endif()
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("#include <features.h>\n#include <mcheck.h>\n#ifndef __GLIBC__\n#error glibc required\n#endif\nint main(){mtrace();muntrace();}" LUMORA_HAS_GLIBC_MTRACE)
  if(LUMORA_HAS_GLIBC_MTRACE)
    add_library(lumora_evidence_heap_shared_control SHARED HeapSharedControl.cpp)
    lumora_enable_warnings(lumora_evidence_heap_shared_control)
    target_compile_definitions(lumora_processing_allocation_probe PRIVATE LUMORA_GLIBC_MTRACE=1)
    target_link_libraries(lumora_processing_allocation_probe PRIVATE lumora_evidence_heap_shared_control ${CMAKE_DL_LIBS})
  endif()
  if(LUMORA_BUILD_TESTS)
    add_test(NAME Processing.EvidenceSmoke COMMAND "${CMAKE_COMMAND}"
      "-DBENCHMARK=$<TARGET_FILE:lumora_processing_benchmark>"
      "-DGENERATOR=$<TARGET_FILE:lumora_processing_reference_generator>"
      "-DALLOCATION=$<TARGET_FILE:lumora_processing_allocation_probe>"
      "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/processing-evidence/$<CONFIG>"
      "-DVALIDATOR=$<TARGET_FILE:lumora_evidence_tests>"
      -P "${PROJECT_SOURCE_DIR}/tests/cmake/ProcessingEvidenceSmoke.cmake")
    set_tests_properties(Processing.EvidenceSmoke PROPERTIES TIMEOUT 600 LABELS evidence)
  endif()
endif()
