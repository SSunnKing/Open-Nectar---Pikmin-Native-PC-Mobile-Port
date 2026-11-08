# Input record then replay (coverage/edge-cases.md EC-62): run a scenario once with
# --input-record, then again with --scenario-replay, and compare state hash logs.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME SCENARIO RATE WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_input_replay.cmake: ${var} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(rec "${WORK_DIR}/${SCENARIO}_${RATE}.input")
set(log_a "${WORK_DIR}/${SCENARIO}_${RATE}.record.hash.log")
set(log_b "${WORK_DIR}/${SCENARIO}_${RATE}.replay.hash.log")
file(REMOVE "${rec}" "${log_a}" "${log_b}")

execute_process(
    COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}" --input-record "${rec}"
            --state-hash-log "${log_a}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out
    RESULT_VARIABLE rc)
if(NOT rc STREQUAL "0")
    message("${out}")
    message(FATAL_ERROR "record run of scenario ${SCENARIO} at ${RATE} exited with '${rc}'")
endif()

execute_process(
    COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}" --scenario-replay "${rec}"
            --state-hash-log "${log_b}"
    OUTPUT_VARIABLE out2
    ERROR_VARIABLE out2
    RESULT_VARIABLE rc2)
if(NOT rc2 STREQUAL "0")
    message("${out2}")
    message(FATAL_ERROR "replay run of scenario ${SCENARIO} at ${RATE} exited with '${rc2}'")
endif()
if(NOT out2 MATCHES "\\[netplay\\] input replay:")
    message("${out2}")
    message(FATAL_ERROR "replay run did not load the recording (no '[netplay] input replay:' line)")
endif()

file(READ "${log_a}" text_a)
file(READ "${log_b}" text_b)
if(NOT text_a STREQUAL text_b)
    message(FATAL_ERROR "input replay hash logs differ for ${SCENARIO} at ${RATE}")
endif()
