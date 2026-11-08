# standin_showcase capture at tick 120 with composition guards (issue #115).
cmake_minimum_required(VERSION 3.16)

foreach(var GAME WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_showcase_screenshot.cmake: ${var} is not set")
    endif()
endforeach()

if(NOT DEFINED PIKMIN_STANDIN_DATA)
    message(FATAL_ERROR "check_scenario_showcase_screenshot.cmake: PIKMIN_STANDIN_DATA is not set")
endif()

set(capture_w 640)
set(capture_h 480)
set(pos_png "${WORK_DIR}/standin_showcase_t120.png")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PIKMIN_SCENARIO_DATA_DIR=${PIKMIN_STANDIN_DATA}"
        "${GAME}" --scenario standin_showcase --scenario-tick-rate 30
        --scenario-screenshot "120:${pos_png}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
message("${out}${err}")

if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "standin_showcase with screenshot exited ${rc}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(verify "${CMAKE_CURRENT_LIST_DIR}/verify_scenario_png.py")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${pos_png}"
        --expect-width ${capture_w}
        --expect-height ${capture_h}
        --require-non-blank
        --min-distinct-colors 120
        --min-variance 120.0
        --max-near-black-ratio 0.20
        --min-center-non-grass 800
        --min-center-distinct 10
        --min-saturated-red 350
        --min-saturated-yellow 350
        --min-saturated-blue 350
        --max-center-near-black-ratio 0.18
        # Procedural Onion at left, silver ship with blue glass and orange nose at right.
        # The camera follows the captain, whose spawn overlaps the pellet; the nose box
        # is set from the frame of the authored fixture sizes (#190).
        --onion-region 15,0,205,120 --min-onion-red 2200
        --region-colour 45,60,185,170:D7262C:24:3500
        --region-colour 70,170,200,270:5F4646:24:1400
        --region-colour 375,160,575,225:DEE4EE:20:3200
        --region-colour 485,140,555,170:8CC8FF:25:400
        --region-colour 595,167,635,217:F55F2D:25:300
        --max-edge-object-pixels 20
        # The lineup shows the captain's helmet and suit, then the three body
        # colours and their distinct leaf, bud and flower silhouettes.
        --region-colour 172,297,217,373:C8DCFF:20:260
        --region-colour 172,297,217,373:F0F0FA:20:220
        --region-colour 300,287,339,365:FF2D32:25:320
        --region-colour 373,287,415,370:FFD723:25:340
        --region-colour 433,287,474,370:375FFF:25:330
        --region-colour 300,287,339,315:3CC83C:25:25
        --region-colour 373,287,415,315:C878A0:25:30
        --region-colour 433,287,474,315:F0F078:25:60
    RESULT_VARIABLE pos_rc)
if(NOT pos_rc STREQUAL "0")
    message(FATAL_ERROR "standin_showcase PNG composition check failed")
endif()

message("standin_showcase screenshot checks passed")
