# Runs boot_smoke with --scenario-screenshot and checks the PNG (issue #87).
cmake_minimum_required(VERSION 3.16)

foreach(var GAME WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_screenshot.cmake: ${var} is not set")
    endif()
endforeach()

set(capture_w 640)
set(capture_h 480)
set(pos_png "${WORK_DIR}/boot_smoke_t100.png")
set(neg_png "${WORK_DIR}/never_t99999.png")
file(MAKE_DIRECTORY "${WORK_DIR}")

if(DEFINED USE_FULL_WINDOW)
    set(scenario_env ${CMAKE_COMMAND} -E env PIKMIN_SCENARIO_WINDOW=640x480)
else()
    set(scenario_env ${CMAKE_COMMAND} -E env)
endif()

# USE_FULL_WINDOW: run at 640x480 for the whole scenario (CTest default path).
# Otherwise exercise the default 64x36 window with a capture-tick resize to 640x480.
execute_process(
    COMMAND ${scenario_env}
        "${GAME}" --scenario boot_smoke --scenario-tick-rate 30
        --scenario-screenshot "100:${pos_png}"
        --scenario-screenshot "99999:${neg_png}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
message("${out}${err}")

if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "boot_smoke with screenshot exited ${rc}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(verify "${CMAKE_CURRENT_LIST_DIR}/verify_scenario_png.py")

set(common_verify
    --expect-width ${capture_w}
    --expect-height ${capture_h}
    --require-non-blank
    --min-distinct-colors 80
    --min-variance 80.0)

# The per-OS golden comparison is scenario_screenshot_goldens (golden_shots.py).
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${pos_png}" ${common_verify}
    RESULT_VARIABLE pos_rc)
if(NOT pos_rc STREQUAL "0")
    message(FATAL_ERROR "positive PNG check failed")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${neg_png}"
        --expect-width ${capture_w} --expect-height ${capture_h} --require-missing
    RESULT_VARIABLE neg_rc)
if(NOT neg_rc STREQUAL "0")
    message(FATAL_ERROR "negative PNG check failed (file should be absent for unreachable tick)")
endif()

message("scenario screenshot checks passed")
