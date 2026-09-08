# Capture declared CMake options, not an exact compiler invocation. Raw values
# are serialized only after target generator expressions have been evaluated.
function(lumora_write_evidence_configuration configuration)
  set(raw_dir "${CMAKE_CURRENT_BINARY_DIR}/${configuration}/raw")
  string(TOUPPER "${configuration}" configuration_upper)
  file(WRITE "${raw_dir}/configuration.txt" "${configuration}")
  file(WRITE "${raw_dir}/compiler-id.txt" "${CMAKE_CXX_COMPILER_ID}")
  file(WRITE "${raw_dir}/compiler-version.txt" "${CMAKE_CXX_COMPILER_VERSION}")
  file(WRITE "${raw_dir}/global-flags.txt" "${CMAKE_CXX_FLAGS}")
  file(WRITE "${raw_dir}/configuration-flags.txt" "${CMAKE_CXX_FLAGS_${configuration_upper}}")
  file(WRITE "${raw_dir}/source-options.txt"
    "LUMORA_BUILD_TESTS=${LUMORA_BUILD_TESTS} LUMORA_BUILD_BENCHMARKS=${LUMORA_BUILD_BENCHMARKS} LUMORA_ENABLE_BASLER=${LUMORA_ENABLE_BASLER}")
endfunction()

function(lumora_configure_evidence_provenance)
  if(ARGC GREATER 0)
    set(source_root "${ARGV0}")
  else()
    set(source_root "${PROJECT_SOURCE_DIR}")
  endif()
  if(CMAKE_CONFIGURATION_TYPES)
    foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
      lumora_write_evidence_configuration("${configuration}")
    endforeach()
  else()
    lumora_write_evidence_configuration("${CMAKE_BUILD_TYPE}")
  endif()

  # COMPILE_OPTIONS can differ by language even for a C++-only target (including
  # transitive Qt options). Give every evaluation a distinct file and consume
  # only the intended CXX context; no two contexts compete for a JSON output.
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/raw/$<COMPILE_LANGUAGE>/processing-options.txt"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:lumora_processing,COMPILE_OPTIONS>, >"
    TARGET lumora_processing)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/raw/$<COMPILE_LANGUAGE>/evidence-options.txt"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:lumora_processing_evidence_support,COMPILE_OPTIONS>, >"
    TARGET lumora_processing_evidence_support)

  add_custom_target(lumora_evidence_provenance
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${source_root}"
      "-DINPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/raw"
      "-DCONFIG_FILE=${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/evidence-config.json"
      "-DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/EvidenceBuild.hpp"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CaptureProvenance.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/EvidenceBuild.hpp"
      "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/evidence-config.json")
  add_dependencies(lumora_processing_evidence_support lumora_evidence_provenance)
  target_include_directories(lumora_processing_evidence_support PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
endfunction()
