# Replay determinism (coverage/edge-cases.md EC-60): runs one scenario twice on
# the stand-in tree with --state-hash-log and fails when the two per-tick state
# hash logs differ. Needs no scenario change: the runner already forces netplay
# deterministic mode and the pad script is the scenario's own. Called by CTest as:
#   cmake -DGAME=<nectar> -DSCENARIO=<name> -DRATE=<30|60> -DWORK_DIR=<dir>
#         [-DSECOND_ENV=<VAR=value>] [-DSECOND_ARGS=<arguments>] [-DEXPECT_DIFFER=1]
#         [-DSECOND_MAY_FAIL=1] -P check_scenario_determinism.cmake
# SECOND_ARGS adds arguments to the second run only (--scenario-realtime: the
# real-time pacing must not change the per-tick state; --scenario-card-delay-ms: a
# slow memory card must not either). Separate several with $<SEMICOLON> in add_test.
# Both runs must exit 0 and write the same non-empty log. Negative control:
# SECOND_ENV sets one environment variable for the second run only, and
# EXPECT_DIFFER=1 inverts the comparison (passes only when the logs differ).
# The test uses PIKMIN_SCENARIO_STAGE=stages/sandbox_objects.ini to make run 2 a
# different world. (The runner clears PIKMIN_NETPLAY_TEST_PREROLL_RAND, so the
# RNG preroll cannot serve as the perturbation.) With EXPECT_DIFFER, SECOND_MAY_FAIL=1
# also counts a second run that fails one of the scenario's own checks (exit 1 and a
# "[scenario] FAIL" line) as a difference: the card-delay control's run can miss the
# scenario's time bound on a fast machine and exit late on a slow one.
# PIKMIN_SCENARIO_DATA_DIR and PIKMIN_SCENARIO_STAGE come from the test's environment.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME SCENARIO RATE WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_determinism.cmake: ${var} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(log_a "${WORK_DIR}/${SCENARIO}_${RATE}.run1.hash.log")
set(log_b "${WORK_DIR}/${SCENARIO}_${RATE}.run2.hash.log")
file(REMOVE "${log_a}" "${log_b}")

foreach(run 1 2)
    if(run EQUAL 2 AND DEFINED SECOND_ENV)
        string(REGEX MATCH "^([^=]+)=(.*)$" _m "${SECOND_ENV}")
        set(ENV{${CMAKE_MATCH_1}} "${CMAKE_MATCH_2}")
    endif()
    set(extra)
    if(run EQUAL 1)
        set(log "${log_a}")
    else()
        set(log "${log_b}")
        if(DEFINED SECOND_ARGS)
            set(extra ${SECOND_ARGS})
        endif()
    endif()
    execute_process(
        COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}" --state-hash-log "${log}" ${extra}
        OUTPUT_VARIABLE out
        ERROR_VARIABLE out
        RESULT_VARIABLE rc)
    if(run EQUAL 2 AND DEFINED EXPECT_DIFFER AND DEFINED SECOND_MAY_FAIL AND rc STREQUAL "1" AND out MATCHES "\\[scenario\\] FAIL [^\r\n]*")
        message("[determinism] ${SCENARIO} at ${RATE}: run 2 (${SECOND_ARGS}) failed a scenario check, which counts as a difference: ${CMAKE_MATCH_0}")
        return()
    endif()
    if(NOT rc STREQUAL "0")
        message("${out}")
        message(FATAL_ERROR "run ${run} of scenario ${SCENARIO} at ${RATE} exited with '${rc}'")
    endif()
endforeach()

file(READ "${log_a}" text_a)
file(READ "${log_b}" text_b)
string(REGEX MATCHALL "\n" nl_a "${text_a}")
list(LENGTH nl_a lines_a)
if(lines_a LESS 10)
    message(FATAL_ERROR "state hash log of run 1 has only ${lines_a} lines; nothing was compared")
endif()

if(text_a STREQUAL text_b)
    set(same TRUE)
else()
    set(same FALSE)
endif()
message("[determinism] ${SCENARIO} at ${RATE}: ${lines_a} hash lines per run, logs identical: ${same}")

if(DEFINED EXPECT_DIFFER)
    if(same)
        message(FATAL_ERROR "negative control: the logs are identical although run 2 had ${SECOND_ENV} ${SECOND_ARGS}; the comparison cannot detect divergence")
    endif()
    return()
endif()
if(NOT same)
    # First differing line, for the failure message.
    string(REPLACE "\n" ";" va "${text_a}")
    string(REPLACE "\n" ";" vb "${text_b}")
    list(LENGTH va na)
    list(LENGTH vb nb)
    set(first "(line counts differ: ${na} vs ${nb})")
    set(i 0)
    foreach(a IN LISTS va)
        if(i LESS nb)
            list(GET vb ${i} b)
            if(NOT a STREQUAL b)
                math(EXPR n "${i} + 1")
                set(first "line ${n}:\n  run1: ${a}\n  run2: ${b}")
                break()
            endif()
        endif()
        math(EXPR i "${i} + 1")
    endforeach()
    message(FATAL_ERROR "scenario ${SCENARIO} at ${RATE} is not deterministic; first difference ${first}\nlogs: ${log_a} ${log_b}")
endif()
