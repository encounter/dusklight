# The game ABI surface shared by the main build and the mod SDK (sdk/CMakeLists.txt)
include_guard(GLOBAL)

get_filename_component(_DUSK_GAME_ABI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(DUSK_GAME_ABI_COMPILE_DEFS TARGET_PC WIDESCREEN_SUPPORT=1 AVOID_UB=1 VERSION=0 MTX_USE_PS=1)

# PARTIAL_DEBUG is the config-independent layout define: Dusklight builds always set
# it, so debug and release share one struct-layout ABI and a mod is compatible with either.
set(DUSK_GAME_ABI_LAYOUT_DEFS NDEBUG=1 NDEBUG_DEFINED=1 DEBUG_DEFINED=0 PARTIAL_DEBUG=1)

set(DUSK_GAME_ABI_INCLUDE_DIRS
        ${_DUSK_GAME_ABI_ROOT}/include
        ${_DUSK_GAME_ABI_ROOT}/src
        ${_DUSK_GAME_ABI_ROOT}/assets/GZ2E01 # TODO: make this dynamic if needed?
        ${_DUSK_GAME_ABI_ROOT}/libs/JSystem/include
        ${_DUSK_GAME_ABI_ROOT}/libs
        ${_DUSK_GAME_ABI_ROOT}/extern/aurora/include/dolphin
        ${_DUSK_GAME_ABI_ROOT}/extern/aurora/include
        ${_DUSK_GAME_ABI_ROOT}/extern)

# Interface target for mods and sub-projects to inherit game headers/defines.
function(dusk_add_game_headers_target version_header_dir)
    add_library(dusklight_game_headers INTERFACE)
    target_include_directories(dusklight_game_headers INTERFACE
            ${DUSK_GAME_ABI_INCLUDE_DIRS}
            "${version_header_dir}")
    target_compile_definitions(dusklight_game_headers INTERFACE
            TARGET_PC=1
            ${DUSK_GAME_ABI_COMPILE_DEFS}
            ${DUSK_GAME_ABI_LAYOUT_DEFS})
endfunction()
