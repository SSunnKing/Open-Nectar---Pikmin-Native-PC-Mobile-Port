# Hand-off probe (issue #76): runs `nectar --scenario <name> --scenario-handoff`
# headless with PIKMIN_SCENARIO_HANDOFF_PROBE=1 and checks that the hand-off
# gives the game to the real input path and that nothing is written outside the
# run's scratch save directory. Called by CTest as:
#   cmake -DGAME=<nectar> -DSCENARIO=<name> -DRATE=<30|60> -DWORK_DIR=<dir> -P check_scenario_handoff.cmake
# with PIKMIN_SCENARIO_DATA_DIR (the stand-in tree) and PIKMIN_SCENARIO_STAGE in
# the environment.
#
# The probe mode (pc_scenario.cpp) keeps the window hidden, attaches a virtual
# SDL game controller with its left stick held up, and asserts inside the run:
# before the hand-off the scripted pad wins over that controller; after it the
# scripted input provider is never called again, deterministic and unthrottled
# mode are off, the pad the game reads is the controller's, the captain walks,
# and the ticks are paced in real time. It then prints a PASS line and exits 0.
#
# This script runs it with a private copy of the stand-in tree as the data
# directory, an empty working directory, and HOME, TMPDIR (TMP, TEMP),
# LOCALAPPDATA, APPDATA and XDG_* pointed into empty folders, with Mesa's shader
# disk cache off (a driver cache, not the game's). After the run:
#   - exit 0 and the probe's PASS line;
#   - the `[scenario] save dir` is under the private TMPDIR and holds card0;
#   - the data copy is unchanged: the same files with the same sizes and MD5;
#   - the working directory and the private HOME are empty;
#   - the private TMPDIR holds nothing but the scratch save directory;
#   - the executable's directory gained none of save/, shader_cache/, card0/,
#     card1/, pikmin_settings.conf or pikmin_achievements.txt.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME SCENARIO RATE WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_handoff.cmake: ${var} is not set")
    endif()
endforeach()
if("$ENV{PIKMIN_SCENARIO_DATA_DIR}" STREQUAL "")
    message(FATAL_ERROR "check_scenario_handoff.cmake: PIKMIN_SCENARIO_DATA_DIR is not set")
endif()
set(source_data "$ENV{PIKMIN_SCENARIO_DATA_DIR}")
get_filename_component(exe_dir "${GAME}" DIRECTORY)

file(REMOVE_RECURSE "${WORK_DIR}")
set(data_dir "${WORK_DIR}/data")
set(cwd_dir "${WORK_DIR}/cwd")
set(home_dir "${WORK_DIR}/home")
set(tmp_dir "${WORK_DIR}/tmp")
file(MAKE_DIRECTORY "${cwd_dir}" "${home_dir}" "${tmp_dir}")
file(COPY "${source_data}/" DESTINATION "${data_dir}")

# path|size|md5 for every file under a directory, sorted.
function(snapshot dir out)
    file(GLOB_RECURSE files RELATIVE "${dir}" "${dir}/*")
    list(SORT files)
    set(lines)
    foreach(f IN LISTS files)
        file(SIZE "${dir}/${f}" size)
        file(MD5 "${dir}/${f}" md5)
        list(APPEND lines "${f}|${size}|${md5}")
    endforeach()
    set(${out} "${lines}" PARENT_SCOPE)
endfunction()
snapshot("${data_dir}" before)
list(LENGTH before n_before)
if(n_before LESS 10)
    message(FATAL_ERROR "the data copy has only ${n_before} files; is ${source_data} the stand-in tree?")
endif()

set(exe_watch)
foreach(name save shader_cache card0 card1 pikmin_settings.conf pikmin_achievements.txt)
    if(NOT EXISTS "${exe_dir}/${name}")
        list(APPEND exe_watch "${exe_dir}/${name}")
    endif()
endforeach()

set(ENV{PIKMIN_SCENARIO_DATA_DIR} "${data_dir}")
set(ENV{PIKMIN_SCENARIO_HANDOFF_PROBE} 1)
set(ENV{PIKMIN_SCENARIO_KEEP_SAVE_DIR} 1)
set(ENV{HOME} "${home_dir}")
set(ENV{USERPROFILE} "${home_dir}")
set(ENV{LOCALAPPDATA} "${home_dir}/AppData/Local")
set(ENV{APPDATA} "${home_dir}/AppData/Roaming")
set(ENV{XDG_DATA_HOME} "${home_dir}/.local/share")
set(ENV{XDG_CONFIG_HOME} "${home_dir}/.config")
set(ENV{XDG_CACHE_HOME} "${home_dir}/.cache")
set(ENV{TMPDIR} "${tmp_dir}")
set(ENV{TMP} "${tmp_dir}")
set(ENV{TEMP} "${tmp_dir}")
set(ENV{MESA_SHADER_CACHE_DISABLE} true)

execute_process(
    COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}" --scenario-handoff
    WORKING_DIRECTORY "${cwd_dir}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out
    RESULT_VARIABLE rc)
message("${out}")

set(errors)
if(NOT rc STREQUAL "0")
    list(APPEND errors "the probe exited with '${rc}'")
endif()
if(NOT out MATCHES "\\[scenario-probe\\] PASS ${SCENARIO}")
    list(APPEND errors "no '[scenario-probe] PASS ${SCENARIO}' line")
endif()

string(REGEX MATCH "\\[scenario\\] save dir ([^\r\n]+)" m "${out}")
set(scratch "")
if(m)
    file(TO_CMAKE_PATH "${CMAKE_MATCH_1}" scratch)
    get_filename_component(scratch_parent "${scratch}" DIRECTORY)
    get_filename_component(scratch_parent "${scratch_parent}" REALPATH)
    get_filename_component(tmp_real "${tmp_dir}" REALPATH)
    message("[check] scratch save dir: ${scratch}")
    if(NOT scratch_parent STREQUAL tmp_real)
        list(APPEND errors "the scratch save dir ${scratch} is not in the private TMPDIR ${tmp_dir}")
    endif()
    if(NOT IS_DIRECTORY "${scratch}/card0")
        list(APPEND errors "no card0 in the scratch save dir")
    endif()
else()
    list(APPEND errors "no '[scenario] save dir' line")
endif()

snapshot("${data_dir}" after)
if(NOT before STREQUAL after)
    foreach(line IN LISTS after)
        list(FIND before "${line}" i)
        if(i EQUAL -1)
            list(APPEND errors "the data directory has a new or changed file: ${line}")
        endif()
    endforeach()
    foreach(line IN LISTS before)
        list(FIND after "${line}" i)
        if(i EQUAL -1)
            list(APPEND errors "a data directory file is gone or changed: ${line}")
        endif()
    endforeach()
else()
    list(LENGTH after n_after)
    message("[check] data copy unchanged: ${n_after} files, same sizes and MD5")
endif()

file(GLOB cwd_entries "${cwd_dir}/*" "${cwd_dir}/.*")
if(cwd_entries)
    list(APPEND errors "the working directory is not empty: ${cwd_entries}")
endif()
file(GLOB_RECURSE home_entries LIST_DIRECTORIES true "${home_dir}/*")
if(home_entries)
    list(APPEND errors "the run wrote into HOME: ${home_entries}")
endif()
file(GLOB tmp_entries LIST_DIRECTORIES true "${tmp_dir}/*" "${tmp_dir}/.*")
foreach(entry IN LISTS tmp_entries)
    file(TO_CMAKE_PATH "${entry}" entry)
    if(NOT entry STREQUAL scratch)
        list(APPEND errors "the run wrote into TMPDIR outside its scratch save dir: ${entry}")
    endif()
endforeach()
foreach(path IN LISTS exe_watch)
    if(EXISTS "${path}")
        list(APPEND errors "the run created ${path}")
    endif()
endforeach()

if(errors)
    string(REPLACE ";" "\n  " shown "${errors}")
    message(FATAL_ERROR "hand-off check failed:\n  ${shown}")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
message("[check] PASS: the hand-off reached the real input path and wrote only into ${scratch}")
