# Screenshot capture must not change per-tick state hashes (issue #87 P0).
cmake_minimum_required(VERSION 3.16)

foreach(var GAME SCENARIO RATE WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_screenshot_hash.cmake: ${var} is not set")
    endif()
endforeach()

set(base_log "${WORK_DIR}/base.hash.log")
set(shot_log "${WORK_DIR}/with_screenshot.hash.log")
set(png "${WORK_DIR}/frame.png")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
    COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}" --state-hash-log "${base_log}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
message("${out}${err}")
if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "baseline run exited ${rc}")
endif()

execute_process(
    COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}"
        --state-hash-log "${shot_log}" --scenario-screenshot "50:${png}"
    OUTPUT_VARIABLE out2
    ERROR_VARIABLE err2
    RESULT_VARIABLE rc2)
message("${out2}${err2}")
if(NOT rc2 STREQUAL "0")
    message(FATAL_ERROR "screenshot run exited ${rc2}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files "${base_log}" "${shot_log}"
    RESULT_VARIABLE same)
if(NOT same STREQUAL "0")
    message(FATAL_ERROR "state-hash log differs with --scenario-screenshot")
endif()
message("state-hash logs match with and without screenshot")
