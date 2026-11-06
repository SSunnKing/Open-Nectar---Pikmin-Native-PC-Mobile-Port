# map_menu captures of the Y menu's two pages with region guards (issue #99).
# One run at RATE (30 or 60) captures the map page 2 s in and the controller page 3.5 s in
# (scenario_map_menu.cpp: open at 1 s, pages at 2.5 s), then checks each against the stand-in
# layout (standin_layout.h kMenu*): a blank, black or wrong page fails.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME WORK_DIR RATE PIKMIN_STANDIN_DATA)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_map_menu_screenshot.cmake: ${var} is not set")
    endif()
endforeach()

math(EXPR info_tick "${RATE} * 2")
math(EXPR cont_tick "${RATE} * 7 / 2")
set(info_png "${WORK_DIR}/map_menu_info_${RATE}.png")
set(cont_png "${WORK_DIR}/map_menu_cont_${RATE}.png")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(REMOVE "${info_png}" "${cont_png}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PIKMIN_SCENARIO_DATA_DIR=${PIKMIN_STANDIN_DATA}"
        "PIKMIN_SCENARIO_STAGE=stages/practice.ini"
        "${GAME}" --scenario map_menu --scenario-tick-rate ${RATE}
        --scenario-screenshot "${info_tick}:${info_png}"
        --scenario-screenshot "${cont_tick}:${cont_png}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
message("${out}${err}")
if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "map_menu with screenshots exited ${rc}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(verify "${CMAKE_CURRENT_LIST_DIR}/verify_scenario_png.py")
set(common --expect-width 640 --expect-height 480 --require-non-blank
    --min-distinct-colors 60 --min-variance 100.0)

# Map page: gold stage title, the radar's field greens on the left, the dark-blue counts panel
# with its light-blue text on the right, the right arrow only.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${info_png}" ${common}
        --region-colour 160,30,480,62:FAB632:24:300
        --region-colour 40,80,370,390:234934:8:20000
        --region-colour 384,80,596,384:15253F:4:30000
        --region-colour 384,80,596,384:94CEFF:4:2000
        --region-colour 602,204,638,276:FFCE29:30:100
        --max-region-colour 2,204,38,276:FFCE29:30:0
    RESULT_VARIABLE info_rc)
if(NOT info_rc STREQUAL "0")
    message(FATAL_ERROR "map_menu map page (${info_png}) failed its region checks")
endif()

# Controller page: gold title, the purple pad in the centre, the left column of dark-blue
# label windows with light-blue text, the left arrow only, and no radar greens.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${cont_png}" ${common}
        --region-colour 230,30,410,62:FAB632:24:150
        --region-colour 232,150,404,258:5242A5:4:4000
        --region-colour 42,80,226,400:15253F:4:10000
        --region-colour 42,80,226,400:94CEFF:4:1000
        --region-colour 2,204,38,276:FFCE29:30:100
        --max-region-colour 602,204,638,276:FFCE29:30:0
        --max-region-colour 40,80,370,390:234934:8:500
    RESULT_VARIABLE cont_rc)
if(NOT cont_rc STREQUAL "0")
    message(FATAL_ERROR "map_menu controller page (${cont_png}) failed its region checks")
endif()

message("map_menu ${RATE} Hz screenshot checks passed")
