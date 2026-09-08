# Runs when evidence targets build. Content-stable output avoids timestamp loops.
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE revision_result ERROR_QUIET)
execute_process(COMMAND git status --porcelain=v1 --untracked-files=normal WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE status RESULT_VARIABLE status_result ERROR_QUIET)
if(NOT revision_result EQUAL 0)
  set(revision "")
endif()
set(dirty -1)
set(status_hash "")
if(status_result EQUAL 0)
  string(SHA256 status_hash "${status}")
  if(status STREQUAL "")
    set(dirty 0)
  else()
    set(dirty 1)
  endif()
endif()
file(READ "${CONFIG_FILE}" configured)
# Include actual source/configuration content so edits to already-dirty files
# refresh capturedUtc, while a no-change build preserves the generated header.
file(GLOB_RECURSE evidence_inputs LIST_DIRECTORIES false
  "${SOURCE_DIR}/src/processing/*.cpp" "${SOURCE_DIR}/src/processing/*.hpp"
  "${SOURCE_DIR}/src/core/*.cpp" "${SOURCE_DIR}/src/core/*.hpp"
  "${SOURCE_DIR}/benchmarks/*.cpp" "${SOURCE_DIR}/benchmarks/*.hpp"
  "${SOURCE_DIR}/benchmarks/*.cmake" "${SOURCE_DIR}/benchmarks/CMakeLists.txt"
  "${SOURCE_DIR}/benchmarks/processing/CMakeLists.txt")
list(APPEND evidence_inputs "${SOURCE_DIR}/CMakeLists.txt" "${SOURCE_DIR}/src/CMakeLists.txt"
  "${SOURCE_DIR}/tests/support/AllocationTracker.cpp" "${SOURCE_DIR}/tests/support/AllocationTracker.hpp"
  "${SOURCE_DIR}/vcpkg.json")
list(SORT evidence_inputs)
set(source_content "")
foreach(input IN LISTS evidence_inputs)
  file(SHA256 "${input}" input_hash)
  string(APPEND source_content "${input}:${input_hash}\n")
endforeach()
set(state "${revision}|${dirty}|${status_hash}|${configured}|${source_content}")
string(SHA256 state_hash "${state}")
if(EXISTS "${OUTPUT_FILE}.state")
  file(READ "${OUTPUT_FILE}.state" old_state)
  if(old_state STREQUAL state_hash)
    return()
  endif()
endif()
string(TIMESTAMP captured "%Y-%m-%dT%H:%M:%S.000Z" UTC)
file(WRITE "${OUTPUT_FILE}.tmp" "#pragma once\n#define EVIDENCE_REVISION \"${revision}\"\n#define EVIDENCE_DIRTY ${dirty}\n#define EVIDENCE_STATUS_SHA \"${status_hash}\"\n#define EVIDENCE_CAPTURED \"${captured}\"\n#define EVIDENCE_CONFIG R\"evidence(${configured})evidence\"\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUTPUT_FILE}.tmp" "${OUTPUT_FILE}" COMMAND_ERROR_IS_FATAL ANY)
file(REMOVE "${OUTPUT_FILE}.tmp")
file(WRITE "${OUTPUT_FILE}.state" "${state_hash}")
