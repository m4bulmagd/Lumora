cmake_minimum_required(VERSION 3.28)

# Run from the source root after configuring/building a Release stress preset.
foreach(required IN ITEMS LUMORA_STRESS_PRESET LUMORA_STRESS_OUTPUT_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
get_filename_component(evidence_dir "${LUMORA_STRESS_OUTPUT_DIR}" ABSOLUTE)
if(EXISTS "${evidence_dir}")
  message(FATAL_ERROR "Use a new evidence directory; refusing to overwrite ${evidence_dir}")
endif()
file(MAKE_DIRECTORY "${evidence_dir}")

find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
  OUTPUT_VARIABLE source_sha OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
cmake_host_system_information(RESULT host_os QUERY OS_NAME OS_RELEASE OS_VERSION)
string(TIMESTAMP started_utc "%Y-%m-%dT%H:%M:%SZ" UTC)
file(WRITE "${evidence_dir}/metadata.txt"
  "source_sha=${source_sha}\npreset=${LUMORA_STRESS_PRESET}\n"
  "host_os=${host_os}\ncmake_version=${CMAKE_VERSION}\n"
  "runner_image=$ENV{ImageOS}\nrunner_image_version=$ENV{ImageVersion}\n"
  "github_run_id=$ENV{GITHUB_RUN_ID}\ngithub_run_attempt=$ENV{GITHUB_RUN_ATTEMPT}\n"
  "started_utc=${started_utc}\n")

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --preset "${LUMORA_STRESS_PRESET}"
    --output-on-failure --no-tests=error -L stress --verbose
    --output-log "${evidence_dir}/ctest.log"
    --output-junit "${evidence_dir}/results.xml"
  RESULT_VARIABLE test_result
)
string(TIMESTAMP finished_utc "%Y-%m-%dT%H:%M:%SZ" UTC)
file(APPEND "${evidence_dir}/metadata.txt"
  "finished_utc=${finished_utc}\nctest_exit_code=${test_result}\n")
if(NOT test_result STREQUAL "0")
  message(FATAL_ERROR "Stress verification failed (${test_result}); evidence: ${evidence_dir}")
endif()
