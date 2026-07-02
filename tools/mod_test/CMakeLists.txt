cmake_minimum_required(VERSION 3.25)
project(mod_test CXX)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to dusk source root")
    add_subdirectory("${DUSK_DIR}" dusk EXCLUDE_FROM_ALL)
endif ()

set(DUSK_MODS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/mods" CACHE PATH "Directory to write .dusk packages into")

add_dusk_mod(mod_test
    SOURCES     src/mod.cpp
    MOD_JSON    mod.json
    RES_DIR     res
    OVERLAY_DIR overlay
)
target_include_directories(mod_test PRIVATE ../mod_test_dep/include)
