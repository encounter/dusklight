# The game ABI surface shared by the main build and the mod SDK (sdk/CMakeLists.txt)
include_guard(GLOBAL)

get_filename_component(_GAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(GAME_COMPILE_DEFS TARGET_PC WIDESCREEN_SUPPORT=1 AVOID_UB=1 VERSION=0 MTX_USE_PS=1)
if (ANDROID)
    list(APPEND GAME_COMPILE_DEFS TARGET_ANDROID=1)
endif ()

# PARTIAL_DEBUG is the config-independent layout define: Dusklight builds always set
# it, so debug and release share one struct-layout ABI and a mod is compatible with either.
set(GAME_LAYOUT_DEFS NDEBUG=1 NDEBUG_DEFINED=1 DEBUG_DEFINED=0 PARTIAL_DEBUG=1)

set(GAME_INCLUDE_DIRS
        ${_GAME_ROOT}/include
        ${_GAME_ROOT}/src
        ${_GAME_ROOT}/assets/GZ2E01 # TODO: make this dynamic if needed?
        ${_GAME_ROOT}/libs/JSystem/include
        ${_GAME_ROOT}/libs
        ${_GAME_ROOT}/extern/aurora/include/dolphin
        ${_GAME_ROOT}/extern/aurora/include
        ${_GAME_ROOT}/extern)

# Interface target for mods and sub-projects to inherit game headers/defines.
function(dusk_add_game_headers_target version_header_dir)
    add_library(dusklight_game_headers INTERFACE)
    target_include_directories(dusklight_game_headers INTERFACE
            ${GAME_INCLUDE_DIRS}
            "${version_header_dir}")
    target_compile_definitions(dusklight_game_headers INTERFACE
            TARGET_PC=1
            ${GAME_COMPILE_DEFS}
            ${GAME_LAYOUT_DEFS})
endfunction()
