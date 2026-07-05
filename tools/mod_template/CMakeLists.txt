cmake_minimum_required(VERSION 3.25)
project(my_mod CXX)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to dusk source root")
    # TODO: make this handled internally by SDK entry point based on existence of implib availability
    # We should instead configure a "target Dusklight version here" or something
    option(DUSK_MOD_USE_FULL_TREE "Use full build instead of the minimal mod SDK" OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if (DUSK_MOD_USE_FULL_TREE)
        add_subdirectory("${DUSK_DIR}" dusk EXCLUDE_FROM_ALL)
    else ()
        add_subdirectory("${DUSK_DIR}/sdk" dusk-sdk EXCLUDE_FROM_ALL)
    endif ()
endif ()

# Output .dusk packages next to the build directory by default.
set(DUSK_MODS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/mods" CACHE PATH "Directory to write .dusk packages into")

add_dusk_mod(template_mod
    SOURCES  src/mod.cpp
    MOD_JSON mod.json
    RES_DIR  res
)
