# Runs `nectar --scenario <name>` and asserts its exit code (the runner's
# negative paths: exit handler, no window or GL, unknown scenario, no data).
# Called by CTest as:
#   cmake -DGAME=<nectar> -DSCENARIO=<name> -DEXPECT=<code> [-DRATE=<30|60>]
#         [-DUNSET_ENV=<var>] [-DEXPECT_OUTPUT=<regex>] [-DFORBID_OUTPUT=<regex>]
#         [-DARGS=<extra arguments, ;-separated>] -P check_scenario_exit.cmake
# The test passes only when the exit code equals EXPECT (no WILL_FAIL), the
# output matches EXPECT_OUTPUT and does not match FORBID_OUTPUT.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME SCENARIO EXPECT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_exit.cmake: ${var} is not set")
    endif()
endforeach()
if(NOT DEFINED RATE)
    set(RATE 30)
endif()
if(DEFINED UNSET_ENV)
    unset(ENV{${UNSET_ENV}})
endif()

execute_process(
    COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}" ${ARGS}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out
    RESULT_VARIABLE rc)
message("${out}")
message("[check] scenario ${SCENARIO} at ${RATE}: exit '${rc}', expected '${EXPECT}'")

if(NOT rc STREQUAL "${EXPECT}")
    message(FATAL_ERROR "scenario ${SCENARIO}: exit '${rc}', expected '${EXPECT}'")
endif()
if(DEFINED EXPECT_OUTPUT AND NOT out MATCHES "${EXPECT_OUTPUT}")
    message(FATAL_ERROR "scenario ${SCENARIO}: output does not match '${EXPECT_OUTPUT}'")
endif()
if(DEFINED FORBID_OUTPUT AND out MATCHES "${FORBID_OUTPUT}")
    message(FATAL_ERROR "scenario ${SCENARIO}: output matches '${FORBID_OUTPUT}'")
endif()
