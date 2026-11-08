# world_map_day_end captures of the stand-in world map with region guards (issue #99).
# One run at RATE (30 or 60) captures the map idle 2.33 s after it opens (before the A at
# 2.5 s) and with the "Land here?" confirm window open 4 s in (scenario_world_map.cpp), then
# checks each against the stand-in map (nectar_assets standin world map). A blank or navy-only
# map, a missing planet, pin, side panel or confirm window fails.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME WORK_DIR RATE PIKMIN_STANDIN_DATA)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_world_map_screenshot.cmake: ${var} is not set")
    endif()
endforeach()

math(EXPR idle_tick "${RATE} * 7 / 3")
math(EXPR confirm_tick "${RATE} * 4")
set(idle_png "${WORK_DIR}/world_map_idle_${RATE}.png")
set(confirm_png "${WORK_DIR}/world_map_confirm_${RATE}.png")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(REMOVE "${idle_png}" "${confirm_png}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PIKMIN_SCENARIO_DATA_DIR=${PIKMIN_STANDIN_DATA}"
        "PIKMIN_SCENARIO_STAGE=stages/sandbox_objects.ini"
        "${GAME}" --scenario world_map_day_end --scenario-tick-rate ${RATE}
        --scenario-screenshot "between:${idle_tick}:${idle_png}"
        --scenario-screenshot "between:${confirm_tick}:${confirm_png}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
message("${out}${err}")
if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "world_map_day_end with screenshots exited ${rc}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(verify "${CMAKE_CURRENT_LIST_DIR}/verify_scenario_png.py")
set(common --expect-width 640 --expect-height 480 --require-non-blank
    --min-distinct-colors 60 --min-variance 100.0)

# Idle map: the planet's greens, the gold landing pin at the Impact Site, the dark-blue side
# panel with its light-blue frame, and no confirm window (its panel blue or gold text) yet.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${idle_png}" ${common}
        --region-colour 40,60,480,460:52A539:10:6000
        --region-colour 280,320,340,390:FFDE52:20:150
        --region-colour 500,60,630,460:101F44:6:15000
        --region-colour 500,60,630,460:94CEFF:40:800
        --max-region-colour 200,180,450,300:162D54:12:200
        --max-region-colour 200,180,450,300:FFC000:60:50
    RESULT_VARIABLE idle_rc)
if(NOT idle_rc STREQUAL "0")
    message(FATAL_ERROR "world map idle (${idle_png}) failed its region checks")
endif()

# Confirm open: the gold "The Impact Site" title, the blue "Land here?" window with its gold
# prompt and cursor arrows over the planet, plus the planet, pin and side panel.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${confirm_png}" ${common}
        --region-colour 90,30,360,62:FFC000:60:80
        --region-colour 200,180,450,300:162D54:12:2000
        --region-colour 200,180,450,300:FFC000:60:300
        --region-colour 40,60,480,460:52A539:10:6000
        --region-colour 280,320,340,390:FFDE52:20:150
        --region-colour 500,60,630,460:101F44:6:15000
    RESULT_VARIABLE confirm_rc)
if(NOT confirm_rc STREQUAL "0")
    message(FATAL_ERROR "world map confirm (${confirm_png}) failed its region checks")
endif()

message("world map ${RATE} Hz screenshot checks passed")
