cmake_minimum_required(VERSION 3.28)

# Exercise the command-line boundary with real CTest, not a mocked process.
# A missing label filter would execute UnrelatedFailure and fail this case.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef fixture_id)
set(fixture "${TEST_BINARY_DIR}/stress-evidence-${fixture_id}")
file(MAKE_DIRECTORY "${fixture}/build")
file(WRITE "${fixture}/CMakePresets.json" [=[
{
  "version": 6,
  "configurePresets": [{"name": "fixture", "binaryDir": "${sourceDir}/build"}],
  "testPresets": [{"name": "fixture", "configurePreset": "fixture", "configuration": "Release"}]
}
]=])
file(WRITE "${fixture}/build/CTestTestfile.cmake"
  "add_test(StressFixture \"${CMAKE_COMMAND}\" -E echo \"observed publications=3 completed paints=2\")\n"
  "set_tests_properties(StressFixture PROPERTIES LABELS stress)\n"
  "add_test(UnrelatedFailure \"${CMAKE_COMMAND}\" -E false)\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -DLUMORA_STRESS_PRESET=fixture
    "-DLUMORA_STRESS_OUTPUT_DIR=${fixture}/passed"
    -P "${PROJECT_SOURCE_DIR}/cmake/RunStress.cmake"
  WORKING_DIRECTORY "${fixture}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Passing stress test must succeed and exclude unrelated tests: ${output}${error}")
endif()
foreach(name IN ITEMS ctest.log results.xml metadata.txt)
  if(NOT EXISTS "${fixture}/passed/${name}")
    message(FATAL_ERROR "Missing downloadable evidence: ${name}")
  endif()
endforeach()
file(READ "${fixture}/passed/ctest.log" log)
if(NOT log MATCHES "observed publications=3 completed paints=2")
  message(FATAL_ERROR "Successful test output must be retained, including counts")
endif()
file(READ "${fixture}/passed/results.xml" junit)
if(NOT junit MATCHES "name=\"StressFixture\"" OR junit MATCHES "name=\"UnrelatedFailure\"")
  message(FATAL_ERROR "JUnit must identify the selected stress test only")
endif()

# A runner that swallows CTest's exit code must not turn a failed test green.
file(WRITE "${fixture}/build/CTestTestfile.cmake"
  "add_test(FailingStress \"${CMAKE_COMMAND}\" -E false)\n"
  "set_tests_properties(FailingStress PROPERTIES LABELS stress)\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -DLUMORA_STRESS_PRESET=fixture
    "-DLUMORA_STRESS_OUTPUT_DIR=${fixture}/failed"
    -P "${PROJECT_SOURCE_DIR}/cmake/RunStress.cmake"
  WORKING_DIRECTORY "${fixture}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(result STREQUAL "0")
  message(FATAL_ERROR "Failed stress test must fail the runner")
endif()
foreach(name IN ITEMS ctest.log results.xml metadata.txt)
  if(NOT EXISTS "${fixture}/failed/${name}")
    message(FATAL_ERROR "Failed tests must retain diagnostic evidence: ${name}")
  endif()
endforeach()
file(READ "${fixture}/failed/metadata.txt" metadata)
if(NOT metadata MATCHES "ctest_exit_code=[1-9][0-9]*")
  message(FATAL_ERROR "Metadata must record the failed CTest result")
endif()

# With stress registration OFF, CTest must fail rather than report a false pass.
file(WRITE "${fixture}/build/CTestTestfile.cmake"
  "add_test(UnrelatedSuccess \"${CMAKE_COMMAND}\" -E true)\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -DLUMORA_STRESS_PRESET=fixture
    "-DLUMORA_STRESS_OUTPUT_DIR=${fixture}/missing"
    -P "${PROJECT_SOURCE_DIR}/cmake/RunStress.cmake"
  WORKING_DIRECTORY "${fixture}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(result STREQUAL "0" OR NOT "${output}${error}" MATCHES "No tests were found")
  message(FATAL_ERROR "Missing stress registration must fail visibly: ${output}${error}")
endif()

# A second invocation must preserve the evidence from the first invocation.
file(READ "${fixture}/passed/metadata.txt" original_metadata)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -DLUMORA_STRESS_PRESET=fixture
    "-DLUMORA_STRESS_OUTPUT_DIR=${fixture}/passed"
    -P "${PROJECT_SOURCE_DIR}/cmake/RunStress.cmake"
  WORKING_DIRECTORY "${fixture}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output ERROR_VARIABLE error
)
file(READ "${fixture}/passed/metadata.txt" preserved_metadata)
if(result STREQUAL "0" OR NOT original_metadata STREQUAL preserved_metadata)
  message(FATAL_ERROR "Existing evidence must not be overwritten")
endif()
