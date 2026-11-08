# title_boot captures of the stand-in title, main menu and file select with region guards
# (issue #99). The boot path counts boot ticks the same way at 30 and 60 Hz (both runs reach
# the stage after 577 boot ticks and the captures are pixel-identical), so the ticks are fixed:
# 314 is the Press START prompt at the top of its blink, 350 the main menu fully open (at 340
# it is still scaling in) and 528 the file select with its three cards, faded in (at 520 it is
# still fading). A black screen or the wrong screen fails.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME WORK_DIR RATE PIKMIN_STANDIN_DATA)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_title_screens_screenshot.cmake: ${var} is not set")
    endif()
endforeach()

set(press_png "${WORK_DIR}/title_press_start_${RATE}.png")
set(menu_png "${WORK_DIR}/title_menu_${RATE}.png")
set(files_png "${WORK_DIR}/file_select_${RATE}.png")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(REMOVE "${press_png}" "${menu_png}" "${files_png}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PIKMIN_SCENARIO_DATA_DIR=${PIKMIN_STANDIN_DATA}"
        "PIKMIN_SCENARIO_STAGE=stages/sandbox.ini"
        "${GAME}" --scenario title_boot --scenario-tick-rate ${RATE}
        --scenario-screenshot "boot:314:${press_png}"
        --scenario-screenshot "boot:350:${menu_png}"
        --scenario-screenshot "boot:528:${files_png}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
message("${out}${err}")
if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "title_boot with screenshots exited ${rc}")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(verify "${CMAKE_CURRENT_LIST_DIR}/verify_scenario_png.py")
set(common --expect-width 640 --expect-height 480 --require-non-blank --min-distinct-colors 60)

# Press START: the pale-gold prompt low in the centre on black, and no menu panel anywhere.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${press_png}" ${common} --min-variance 50.0
        --region-colour 180,340,460,380:CCBF8F:40:200
        --max-region-colour 0,0,640,480:0F1D3A:8:20
    RESULT_VARIABLE press_rc)
if(NOT press_rc STREQUAL "0")
    message(FATAL_ERROR "title Press START (${press_png}) failed its region checks")
endif()

# Main menu: the dark-blue panel with its light-blue frame in the centre, the gold "Pikmin"
# heading, the selected row and its two gold cursors, white rows, the panel reaching below the
# last row (Exit), and nothing in the file select's top plate or footer.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${menu_png}" ${common} --min-variance 100.0
        --region-colour 110,140,530,390:0F1D3A:8:50000
        --region-colour 110,140,530,390:94CEFF:30:4000
        --region-colour 250,150,500,240:FFC000:60:250
        --region-colour 150,200,500,240:FFCE29:30:100
        --region-colour 110,140,530,390:E5E5E5:30:200
        --region-colour 130,352,510,372:0F1D3A:8:4000
        --max-region-colour 140,10,500,70:0D1A34:6:50
        --max-region-colour 0,390,640,480:0D1A34:8:50
    RESULT_VARIABLE menu_rc)
if(NOT menu_rc STREQUAL "0")
    message(FATAL_ERROR "title main menu (${menu_png}) failed its region checks")
endif()

# File select: the "Select a file" plate (dark blue, gold text) at the top, the file panel at
# the bottom, the cards' light-blue frames and white text.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${verify}" "${files_png}" ${common} --min-variance 100.0
        --region-colour 140,10,500,70:0D1A34:6:6000
        --region-colour 140,10,500,70:FFC000:60:120
        --region-colour 0,380,640,480:0D1A34:8:15000
        --region-colour 0,0,640,480:85B9E5:30:5000
        --region-colour 0,0,640,480:E5E5E5:30:800
    RESULT_VARIABLE files_rc)
if(NOT files_rc STREQUAL "0")
    message(FATAL_ERROR "file select (${files_png}) failed its region checks")
endif()

message("title screens ${RATE} Hz screenshot checks passed")
