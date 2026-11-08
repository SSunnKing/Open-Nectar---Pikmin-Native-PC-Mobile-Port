# Proves a scenario run writes its memory card and shader cache into its
# scratch NECTAR_SAVE_DIR and nowhere else. Runs boot_smoke with
# PIKMIN_SCENARIO_KEEP_SAVE_DIR=1 (the runner then keeps the scratch directory),
# from an empty working directory. Called by CTest as:
#   cmake -DGAME=<nectar> -DCWD_DIR=<empty dir> [-DSCENARIO=<name>] [-DRATE=<30|60>] -P check_scenario_save_dir.cmake
# with PIKMIN_SCENARIO_DATA_DIR and PIKMIN_SCENARIO_STAGE in the environment.
# SCENARIO defaults to boot_smoke, RATE to 30.
# Checks:
#   - exit 0, and a `[scenario] save dir <path>` line;
#   - <path>/card0 exists (CARDInit), and the CARDInit line names it;
#   - the shader cache line names <path>/shader_cache and that directory exists,
#     unless the driver offers no program binary format (then logged, not required);
#   - none of save/, shader_cache/, card0/, card1/ appears that was not there
#     before in the working directory, the data directory or the executable's
#     directory, nor the per-user default save directory;
#   - every file under the data directory has the same size and time stamp after
#     the run, and none is added or removed.
cmake_minimum_required(VERSION 3.16)

foreach(var GAME CWD_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_scenario_save_dir.cmake: ${var} is not set")
    endif()
endforeach()
if("$ENV{PIKMIN_SCENARIO_DATA_DIR}" STREQUAL "")
    message(FATAL_ERROR "check_scenario_save_dir.cmake: PIKMIN_SCENARIO_DATA_DIR is not set")
endif()
set(data_dir "$ENV{PIKMIN_SCENARIO_DATA_DIR}")
get_filename_component(exe_dir "${GAME}" DIRECTORY)

file(REMOVE_RECURSE "${CWD_DIR}")
file(MAKE_DIRECTORY "${CWD_DIR}")

# Places the game would use without NECTAR_SAVE_DIR (pc_card_save_root.cpp,
# pc_gfx.cpp program_binary_init).
set(watched)
foreach(dir "${CWD_DIR}" "${data_dir}" "${exe_dir}")
    foreach(name save shader_cache card0 card1)
        list(APPEND watched "${dir}/${name}")
    endforeach()
endforeach()
if(WIN32)
    if(NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
        list(APPEND watched "$ENV{LOCALAPPDATA}/Nectar/save")
    endif()
else()
    if(NOT "$ENV{XDG_DATA_HOME}" STREQUAL "")
        list(APPEND watched "$ENV{XDG_DATA_HOME}/pikmin-native/save")
    endif()
    if(NOT "$ENV{HOME}" STREQUAL "")
        list(APPEND watched "$ENV{HOME}/.local/share/pikmin-native/save")
    endif()
endif()
set(preexisting)
foreach(path IN LISTS watched)
    if(EXISTS "${path}")
        list(APPEND preexisting "${path}")
        message("[check] already present before the run (not judged): ${path}")
    endif()
endforeach()

if(NOT DEFINED SCENARIO)
    set(SCENARIO boot_smoke)
endif()
if(NOT DEFINED RATE)
    set(RATE 30)
endif()

# The data directory as "path size time" lines (the generator writes it before
# any scenario runs; no scenario may write into it).
function(snapshot_tree dir out_var)
    file(GLOB_RECURSE files LIST_DIRECTORIES true RELATIVE "${dir}" "${dir}/*")
    list(SORT files)
    set(lines)
    foreach(f IN LISTS files)
        if(IS_DIRECTORY "${dir}/${f}")
            list(APPEND lines "${f}/")
        else()
            file(SIZE "${dir}/${f}" size)
            file(TIMESTAMP "${dir}/${f}" stamp "%Y-%m-%dT%H:%M:%S" UTC)
            list(APPEND lines "${f} ${size} ${stamp}")
        endif()
    endforeach()
    set(${out_var} "${lines}" PARENT_SCOPE)
endfunction()
snapshot_tree("${data_dir}" data_before)

set(ENV{PIKMIN_SCENARIO_KEEP_SAVE_DIR} 1)
execute_process(
    COMMAND "${GAME}" --scenario "${SCENARIO}" --scenario-tick-rate "${RATE}"
    WORKING_DIRECTORY "${CWD_DIR}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out
    RESULT_VARIABLE rc)
message("${out}")

set(errors)
if(NOT rc STREQUAL "0")
    list(APPEND errors "${SCENARIO} exited with '${rc}'")
endif()

string(REGEX MATCH "\\[scenario\\] save dir ([^\r\n]+)" m "${out}")
if(NOT m)
    message(FATAL_ERROR "no '[scenario] save dir' line in the output (exit '${rc}')")
endif()
set(scratch_raw "${CMAKE_MATCH_1}")
file(TO_CMAKE_PATH "${scratch_raw}" scratch)
message("[check] scratch save dir: ${scratch}")

if(IS_DIRECTORY "${scratch}/card0")
    message("[check] card0 in scratch: yes")
else()
    list(APPEND errors "no card0 directory in ${scratch}")
endif()
if(IS_DIRECTORY "${scratch}/card1")
    message("[check] card1 in scratch: yes")
else()
    message("[check] card1 in scratch: no (only probed channels get a directory)")
endif()
string(REGEX MATCH "CARDInit\\(\\) - persistent filesystem card: ([^\r\n]+)" m "${out}")
if(m)
    file(TO_CMAKE_PATH "${CMAKE_MATCH_1}" card_path)
    message("[check] CARDInit card: ${card_path}")
    if(NOT card_path STREQUAL "${scratch}/card0")
        list(APPEND errors "CARDInit used ${card_path}, not ${scratch}/card0")
    endif()
else()
    list(APPEND errors "no CARDInit line in the output")
endif()

string(REGEX MATCH "Shader binary cache: ([^\r\n(]+) \\(" m "${out}")
if(m)
    file(TO_CMAKE_PATH "${CMAKE_MATCH_1}" cache_path)
    message("[check] shader cache: ${cache_path}")
    if(NOT cache_path STREQUAL "${scratch}/shader_cache")
        list(APPEND errors "shader cache at ${cache_path}, not ${scratch}/shader_cache")
    elseif(NOT IS_DIRECTORY "${scratch}/shader_cache")
        list(APPEND errors "no shader_cache directory in ${scratch}")
    else()
        file(GLOB cache_files "${scratch}/shader_cache/*.bin")
        list(LENGTH cache_files n)
        message("[check] shader_cache in scratch: yes, ${n} program binaries")
    endif()
elseif(out MATCHES "Shader binary cache unavailable")
    message("[check] shader cache: unavailable on this driver (no program binary format), not judged")
else()
    list(APPEND errors "no shader binary cache line in the output")
endif()

foreach(path IN LISTS watched)
    list(FIND preexisting "${path}" i)
    if(i EQUAL -1 AND EXISTS "${path}")
        list(APPEND errors "the run created ${path}")
    endif()
endforeach()
snapshot_tree("${data_dir}" data_after)
if(NOT data_before STREQUAL data_after)
    list(LENGTH data_before n_before)
    list(LENGTH data_after n_after)
    list(APPEND errors "the data directory changed (${n_before} entries before, ${n_after} after)")
endif()
file(GLOB cwd_entries "${CWD_DIR}/*")
if(cwd_entries)
    list(APPEND errors "the working directory is not empty: ${cwd_entries}")
endif()

file(REMOVE_RECURSE "${scratch}")
if(errors)
    string(REPLACE ";" "\n  " shown "${errors}")
    message(FATAL_ERROR "save dir check failed:\n  ${shown}")
endif()
message("[check] PASS: ${SCENARIO}: card and shader cache in the scratch save dir only, the data directory unchanged")
